// HerdManager.cpp

#include "HerdManager.h"

#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("Herd"), STATGROUP_Herd, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("CPU agent sim"), STAT_HerdCpuSim, STATGROUP_Herd);
DECLARE_CYCLE_STAT(TEXT("CPU agent push to Niagara"), STAT_HerdCpuPush, STATGROUP_Herd);

// ---------------------------------------------------------------------------
// Console variables (runtime count control required by the brief)
// ---------------------------------------------------------------------------
static int32 GHerdMode = 0;
static FAutoConsoleVariableRef CVarHerdMode(
	TEXT("herd.Mode"), GHerdMode,
	TEXT("0 = GPU sprites (simple & many), 1 = CPU meshes (complex & few)."));

static int32 GHerdSimpleCount = 100000;
static FAutoConsoleVariableRef CVarHerdSimpleCount(
	TEXT("herd.SimpleCount"), GHerdSimpleCount,
	TEXT("Particle count for the GPU sprite herd (System 1)."));

static int32 GHerdComplexCount = 2000;
static FAutoConsoleVariableRef CVarHerdComplexCount(
	TEXT("herd.ComplexCount"), GHerdComplexCount,
	TEXT("Particle count for the CPU mesh herd (System 2)."));

static int32 GHerdSpatialHash = 1;
static FAutoConsoleVariableRef CVarHerdSpatialHash(
	TEXT("herd.SpatialHash"), GHerdSpatialHash,
	TEXT("CPU herd neighbour queries: 1 = spatial hash, 0 = brute force O(n^2). Used for the before/after measurement."));

static void RunHerdSweep(UWorld* World)
{
	if (!World) { return; }
	for (TActorIterator<AHerdManager> It(World); It; ++It)
	{
		It->StartSweep();
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("herd.Sweep: no HerdManager in this level."));
}

static FAutoConsoleCommandWithWorld CmdHerdSweep(
	TEXT("herd.Sweep"),
	TEXT("Run the scaling experiment for the current herd.Mode and write a CSV to Saved/Profiling/HerdSweep/."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&RunHerdSweep));

// Niagara User Parameter names. Create these in BOTH Niagara systems with exactly these names.
namespace HerdParams
{
	static const FName SpawnCount(TEXT("SpawnCount"));            // int
	static const FName DogPosition(TEXT("DogPosition"));          // Vector (herd flees from it)
	static const FName DogRadius(TEXT("DogRadius"));              // float
	static const FName PenPosition(TEXT("PenPosition"));          // Vector
	static const FName PenRadius(TEXT("PenRadius"));              // float
	static const FName PenIndex(TEXT("PenIndex"));                // int
	static const FName CallbackHandler(TEXT("CallbackHandler"));  // Object (used by Export Particle Data)

	// CPU system only: Niagara Array data interfaces, one element per particle (index = Particles.UniqueID).
	static const FName AgentPositions(TEXT("AgentPositions"));    // Array Position
	static const FName AgentVelocities(TEXT("AgentVelocities"));  // Array Vector
	static const FName AgentStates(TEXT("AgentStates"));          // Array Int32 (0 graze, 1 spooked, 2 follow, 3 penned)
}

AHerdManager::AHerdManager()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	SimpleComp = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SimpleHerd"));
	SimpleComp->SetupAttachment(RootComponent);
	SimpleComp->bAutoActivate = false;

	ComplexComp = CreateDefaultSubobject<UNiagaraComponent>(TEXT("ComplexHerd"));
	ComplexComp->SetupAttachment(RootComponent);
	ComplexComp->bAutoActivate = false;
}

void AHerdManager::BeginPlay()
{
	Super::BeginPlay();

	SimpleComp->SetAsset(SimpleSystem);
	ComplexComp->SetAsset(ComplexSystem);

	for (UNiagaraComponent* Comp : { SimpleComp.Get(), ComplexComp.Get() })
	{
		Comp->SetVariableObject(HerdParams::CallbackHandler, this);
		Comp->SetVariableFloat(HerdParams::PenRadius, PenRadius);
	}

	CurrentPen = 0;
	TotalArrived = 0;
	bGameOver = false;
	bWon = false;

	if (Obstacles.Num() == 0)
	{
		SpawnDefaultObstacles();
	}

	AppliedMode = -1; // force the first apply
	ApplyCvarsIfChanged();

	// Reproducible benchmark from the command line: UnrealEditor.exe ReindeerHerd -game -HerdSweep
	if (FParse::Param(FCommandLine::Get(), TEXT("HerdSweep")))
	{
		bAutoSweepPending = true;
		AutoSweepDelay = 2.f; // let the level finish loading
	}
}

void AHerdManager::SpawnDefaultObstacles()
{
	UStaticMesh* RockMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* RockMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!RockMesh)
	{
		return;
	}

	// The engine cylinder is 100 units wide and tall with its pivot in the middle.
	const float Height = 300.f;
	const FVector Scale(DefaultObstacleRadius / 50.f, DefaultObstacleRadius / 50.f, Height / 100.f);

	for (const FVector& Offset : DefaultObstacleOffsets)
	{
		const FVector Location = GetActorLocation() + Offset + FVector(0, 0, Height * 0.5f);
		AStaticMeshActor* Rock = GetWorld()->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
		if (!Rock)
		{
			continue;
		}
		UStaticMeshComponent* Mesh = Rock->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(RockMesh);
		Rock->SetActorScale3D(Scale);
		if (RockMaterial)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(RockMaterial, Rock);
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.12f, 0.13f, 0.15f));
			Mesh->SetMaterial(0, Mid);
		}
		Obstacles.Add(Rock);
	}
}

UNiagaraComponent* AHerdManager::GetActiveComp() const
{
	return AppliedMode == 1 ? ComplexComp.Get() : SimpleComp.Get();
}

void AHerdManager::SetMode(int32 NewMode)
{
	GHerdMode = FMath::Clamp(NewMode, 0, 1);
}

void AHerdManager::SetParticleCount(int32 NewCount)
{
	if (GHerdMode == 0) { GHerdSimpleCount = FMath::Max(1, NewCount); }
	else                { GHerdComplexCount = FMath::Max(1, NewCount); }
}

void AHerdManager::ApplyCvarsIfChanged()
{
	const int32 WantedMode = FMath::Clamp(GHerdMode, 0, 1);
	const int32 WantedCount = FMath::Max(1, WantedMode == 0 ? GHerdSimpleCount : GHerdComplexCount);

	if (WantedMode == AppliedMode && WantedCount == AppliedCount)
	{
		return;
	}

	const bool bModeChanged = WantedMode != AppliedMode;

	UNiagaraComponent* On  = WantedMode == 0 ? SimpleComp.Get() : ComplexComp.Get();
	UNiagaraComponent* Off = WantedMode == 0 ? ComplexComp.Get() : SimpleComp.Get();

	Off->DeactivateImmediate();
	Off->SetVisibility(false);

	AppliedMode = WantedMode;
	AppliedCount = WantedCount;

	if (WantedMode == 1)
	{
		// Fixed seed so every run (and every sweep step) starts from the same herd.
		CpuSim.Reset(WantedCount, GetActorLocation(), ComplexSpawnRadius, /*Seed=*/1234);
		PushCpuHerdToNiagara(); // arrays must be filled before the particles spawn
	}

	On->SetVisibility(true);
	On->SetVariableInt(HerdParams::SpawnCount, WantedCount);
	On->SetVariableFloat(HerdParams::DogRadius, WantedMode == 0 ? SimpleDogRadius : ComplexDogRadius);
	On->Activate(/*bReset=*/true); // restart so the new count is spawned

	// Fresh herd: nobody has been penned yet.
	ArrivedSinceSpawn = 0;

	// Restart the current pen so arrival counts stay fair after a switch.
	ResetPenState();

	// Only the CPU herd steers around obstacles, so the rocks exist only in mode 1.
	for (AActor* Obstacle : Obstacles)
	{
		if (Obstacle)
		{
			Obstacle->SetActorHiddenInGame(WantedMode == 0);
			Obstacle->SetActorEnableCollision(WantedMode == 1);
		}
	}

	if (bModeChanged)
	{
		UpdateCamera();
	}

	UE_LOG(LogTemp, Log, TEXT("Herd: mode=%d count=%d"), AppliedMode, AppliedCount);
}

void AHerdManager::UpdateCamera()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}

	AActor* Target = (AppliedMode == 0 && AerialCamera) ? AerialCamera.Get() : PC->GetPawn();
	if (Target)
	{
		PC->SetViewTargetWithBlend(Target, 0.75f, VTBlend_Cubic);
	}
}

// The GPU herd is ~120 m across, so mode 0 lifts the dog's camera into a fixed top-down view and
// makes the dog faster; mode 1 keeps the template's close third-person camera.
// Applied every tick (cheap) so it also works when the pawn spawns after this actor's BeginPlay.
void AHerdManager::ApplyDogView(ACharacter* Dog) const
{
	if (!Dog || (AppliedMode == 0 && AerialCamera))
	{
		return; // a placed AerialCamera actor takes over the view instead
	}

	const bool bAerial = AppliedMode == 0;

	if (USpringArmComponent* Boom = Dog->FindComponentByClass<USpringArmComponent>())
	{
		Boom->TargetArmLength = bAerial ? 9000.f : 400.f;
		Boom->bDoCollisionTest = !bAerial;
		Boom->bUsePawnControlRotation = !bAerial;
		Boom->SetUsingAbsoluteRotation(bAerial);
		if (bAerial)
		{
			Boom->SetWorldRotation(FRotator(-60.f, 0.f, 0.f));
		}
	}

	if (UCharacterMovementComponent* Move = Dog->GetCharacterMovement())
	{
		Move->MaxWalkSpeed = bAerial ? 1500.f : 900.f; // faster than the CPU herd's FleeSpeed
	}

	// Fixed camera: keep "forward" = +X so WASD matches the screen.
	if (bAerial)
	{
		if (AController* Controller = Dog->GetController())
		{
			Controller->SetControlRotation(FRotator::ZeroRotator);
		}
	}
}

void AHerdManager::HandleDebugKeys()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC || SweepPhase != ESweepPhase::None)
	{
		return;
	}

	if (PC->WasInputKeyJustPressed(EKeys::M))
	{
		SetMode(1 - FMath::Clamp(GHerdMode, 0, 1));
	}
	if (PC->WasInputKeyJustPressed(EKeys::RightBracket))
	{
		SetParticleCount(AppliedCount * 2);
	}
	if (PC->WasInputKeyJustPressed(EKeys::LeftBracket))
	{
		SetParticleCount(FMath::Max(1, AppliedCount / 2));
	}
}

void AHerdManager::ResetPenState()
{
	ArrivedThisPen = 0;
	PenTimeLeft = TimePerPen;
	RemainingAtPenStart = FMath::Max(1, AppliedCount - ArrivedSinceSpawn);
}

int32 AHerdManager::RequiredToPass() const
{
	return FMath::Max(1, FMath::CeilToInt(PassFraction * RemainingAtPenStart));
}

void AHerdManager::AddArrivals(int32 Count)
{
	if (Count <= 0 || bGameOver)
	{
		return;
	}

	ArrivedThisPen += Count;
	ArrivedSinceSpawn += Count;
	TotalArrived += Count;

	if (ArrivedThisPen >= RequiredToPass())
	{
		++CurrentPen;
		ResetPenState();
	}
}

void AHerdManager::TickCpuHerd(float DeltaSeconds)
{
	const double StartTime = FPlatformTime::Seconds();
	{
		SCOPE_CYCLE_COUNTER(STAT_HerdCpuSim);

		TArray<FSphere> ObstacleSpheres;
		for (const AActor* Obstacle : Obstacles)
		{
			if (Obstacle)
			{
				FVector Origin, Extent;
				Obstacle->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
				ObstacleSpheres.Emplace(Origin, FMath::Max(Extent.X, Extent.Y));
			}
		}

		FVector DogPos = FVector(1e9); // no dog: far away
		if (const APawn* Dog = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			DogPos = Dog->GetActorLocation();
		}

		CpuSim.Tick(DeltaSeconds, DogPos, ComplexDogRadius, ObstacleSpheres, GHerdSpatialHash != 0);

		// Simulation -> game state. The CPU herd knows its agents, so no GPU readback is needed here.
		if (!bGameOver && Pens.IsValidIndex(CurrentPen) && Pens[CurrentPen])
		{
			AddArrivals(CpuSim.CollectArrivals(Pens[CurrentPen]->GetActorLocation(), PenRadius));
		}
	}
	AgentSimMs = float((FPlatformTime::Seconds() - StartTime) * 1000.0);

	PushCpuHerdToNiagara();
}

void AHerdManager::PushCpuHerdToNiagara()
{
	SCOPE_CYCLE_COUNTER(STAT_HerdCpuPush);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(ComplexComp, HerdParams::AgentPositions, CpuSim.Positions);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(ComplexComp, HerdParams::AgentVelocities, CpuSim.Velocities);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(ComplexComp, HerdParams::AgentStates, CpuSim.States);
}

void AHerdManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bAutoSweepPending)
	{
		AutoSweepDelay -= DeltaSeconds;
		if (AutoSweepDelay <= 0.f)
		{
			bAutoSweepPending = false;
			StartSweep();
		}
	}

	HandleDebugKeys();
	TickSweep(DeltaSeconds);
	ApplyCvarsIfChanged();

	UNiagaraComponent* Active = GetActiveComp();
	if (!Active)
	{
		return;
	}

	if (AppliedMode == 1)
	{
		TickCpuHerd(DeltaSeconds);
	}

	// Player -> simulation: the herd flees from the dog.
	if (APawn* Dog = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Active->SetVariableVec3(HerdParams::DogPosition, Dog->GetActorLocation());
		ApplyDogView(Cast<ACharacter>(Dog));
	}

	if (!bGameOver)
	{
		if (!Pens.IsValidIndex(CurrentPen) || !Pens[CurrentPen])
		{
			// No pens placed yet: keep running so the sweep and tests still work.
			if (Pens.Num() > 0)
			{
				bGameOver = true;
				bWon = true;
			}
		}
		else
		{
			const FVector PenPos = Pens[CurrentPen]->GetActorLocation();
			Active->SetVariableVec3(HerdParams::PenPosition, PenPos);
			Active->SetVariableInt(HerdParams::PenIndex, CurrentPen);

			// The timer is paused during a sweep so a benchmark never ends the game.
			if (SweepPhase == ESweepPhase::None)
			{
				PenTimeLeft -= DeltaSeconds;
				if (PenTimeLeft <= 0.f)
				{
					bGameOver = true;
					bWon = false;
				}
			}

#if ENABLE_DRAW_DEBUG
			DrawDebugCylinder(GetWorld(), PenPos - FVector(0, 0, 100), PenPos + FVector(0, 0, 300),
				PenRadius, 32, FColor::Cyan);
#endif
		}
	}

	ShowStatus();
}

void AHerdManager::ReceiveParticleData_Implementation(const TArray<FBasicParticleData>& Data,
	UNiagaraSystem* NiagaraSystem, const FVector& SimulationPositionOffset)
{
	// Simulation -> game state.
	if (bGameOver)
	{
		return;
	}

	// Only the GPU herd reports through Export Particle Data; the CPU herd is counted in TickCpuHerd.
	if (AppliedMode != 0 || NiagaraSystem != SimpleComp->GetAsset())
	{
		return;
	}

	int32 Counted = 0;
	for (const FBasicParticleData& P : Data)
	{
		// Niagara writes the pen index into Size; skip stale readbacks from a previous pen.
		if (FMath::RoundToInt(P.Size) == CurrentPen)
		{
			++Counted;
		}
	}

	AddArrivals(Counted);
}

// ---------------------------------------------------------------------------
// Scaling sweep: step through counts, warm up, average frame / GT / RT / GPU times,
// write one CSV row per count. Unreal Insights bookmarks mark each step.
// ---------------------------------------------------------------------------
void AHerdManager::StartSweep()
{
	if (SweepPhase != ESweepPhase::None)
	{
		return;
	}

	SweepMode = FMath::Clamp(GHerdMode, 0, 1);
	const TArray<int32>& Counts = SweepMode == 0 ? SimpleSweepCounts : ComplexSweepCounts;
	if (Counts.Num() == 0)
	{
		return;
	}

	// Same view for every run.
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (BenchmarkViewpoint)
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				Pawn->SetActorLocation(BenchmarkViewpoint->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
			}
			PC->SetControlRotation(BenchmarkViewpoint->GetActorRotation());
		}
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}

	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	SweepFilePath = FPaths::ProfilingDir() / TEXT("HerdSweep") /
		FString::Printf(TEXT("HerdSweep_%s_%s.csv"), SweepMode == 0 ? TEXT("GPU") : TEXT("CPU"), *Stamp);
	SweepCsv = TEXT("Mode,Count,SpatialHash,AvgFrameMs,MaxFrameMs,AvgGameThreadMs,AvgRenderThreadMs,AvgGpuMs,AvgAgentSimMs,AvgFPS,Within16_6ms\n");

#if CSV_PROFILER
	// Per-frame CSV next to the summary: its Exclusive/*/Effects columns are Niagara's CPU time per thread.
	// Tools/sweep_niagara.py averages them per sweep step using the Sample events below.
	FCsvProfiler::Get()->BeginCapture(-1, FPaths::ProfilingDir() / TEXT("HerdSweep"),
		FString::Printf(TEXT("HerdSweepFrames_%s_%s.csv"), SweepMode == 0 ? TEXT("GPU") : TEXT("CPU"), *Stamp));
#endif

	SweepIndex = 0;
	BeginSweepStep();
	UE_LOG(LogTemp, Log, TEXT("herd.Sweep started (mode %d), output: %s"), SweepMode, *SweepFilePath);
}

void AHerdManager::BeginSweepStep()
{
	const TArray<int32>& Counts = SweepMode == 0 ? SimpleSweepCounts : ComplexSweepCounts;
	const int32 Count = Counts[SweepIndex];

	GHerdMode = SweepMode;
	SetParticleCount(Count); // respawned by ApplyCvarsIfChanged this frame

	SweepPhase = ESweepPhase::Warmup;
	SweepTimer = SweepWarmupSeconds;
	SweepSamples = 0;
	SumFrameMs = MaxFrameMs = SumGameMs = SumRenderMs = SumGpuMs = SumAgentSimMs = 0;

	CSV_EVENT_GLOBAL(TEXT("HerdSweep mode %d count %d"), SweepMode, Count); // also an Insights bookmark
}

void AHerdManager::TickSweep(float DeltaSeconds)
{
	if (SweepPhase == ESweepPhase::None)
	{
		return;
	}

	SweepTimer -= DeltaSeconds;

	if (SweepPhase == ESweepPhase::Warmup)
	{
		if (SweepTimer <= 0.f)
		{
			SweepPhase = ESweepPhase::Sample;
			SweepTimer = SweepSampleSeconds;
			const TArray<int32>& Counts = SweepMode == 0 ? SimpleSweepCounts : ComplexSweepCounts;
			CSV_EVENT_GLOBAL(TEXT("SampleBegin %d"), Counts[SweepIndex]);
		}
		return;
	}

	// Sample phase. These are the same numbers "stat unit" shows (previous frame's values).
	const double FrameMs  = FApp::GetDeltaTime() * 1000.0;
	const double GameMs   = FPlatformTime::ToMilliseconds(GGameThreadTime);
	const double RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	const double GpuMs    = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));

	SumFrameMs  += FrameMs;
	SumGameMs   += GameMs;
	SumRenderMs += RenderMs;
	SumGpuMs    += GpuMs;
	SumAgentSimMs += SweepMode == 1 ? AgentSimMs : 0.f;
	MaxFrameMs = FMath::Max(MaxFrameMs, FrameMs);
	++SweepSamples;

	if (SweepTimer <= 0.f)
	{
		FinishSweepStep();
	}
}

void AHerdManager::FinishSweepStep()
{
	const TArray<int32>& Counts = SweepMode == 0 ? SimpleSweepCounts : ComplexSweepCounts;
	const double N = FMath::Max(1, SweepSamples);
	CSV_EVENT_GLOBAL(TEXT("SampleEnd %d"), Counts[SweepIndex]);
	const double AvgFrame = SumFrameMs / N;

	SweepCsv += FString::Printf(TEXT("%s,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%s\n"),
		SweepMode == 0 ? TEXT("GPU") : TEXT("CPU"),
		Counts[SweepIndex],
		GHerdSpatialHash != 0 ? 1 : 0,
		AvgFrame, MaxFrameMs, SumGameMs / N, SumRenderMs / N, SumGpuMs / N, SumAgentSimMs / N,
		AvgFrame > 0 ? 1000.0 / AvgFrame : 0.0,
		AvgFrame <= 16.6 ? TEXT("yes") : TEXT("no"));

	// Save after every step so a crash at a huge count still keeps the earlier rows.
	FFileHelper::SaveStringToFile(SweepCsv, *SweepFilePath);

	++SweepIndex;
	if (SweepIndex < Counts.Num())
	{
		BeginSweepStep();
	}
	else
	{
		EndSweep();
	}
}

void AHerdManager::EndSweep()
{
	SweepPhase = ESweepPhase::None;

#if CSV_PROFILER
	FCsvProfiler::Get()->EndCapture();
#endif

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->ResetIgnoreMoveInput();
		PC->ResetIgnoreLookInput();
	}

	const FString FullPath = FPaths::ConvertRelativePathToFull(SweepFilePath);
	UE_LOG(LogTemp, Log, TEXT("herd.Sweep finished: %s"), *FullPath);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(4244, 15.f, FColor::Green, FString::Printf(TEXT("Sweep saved: %s"), *FullPath));
	}
}

void AHerdManager::ShowStatus() const
{
	if (!GEngine)
	{
		return;
	}

	FString Msg;
	if (SweepPhase != ESweepPhase::None)
	{
		const TArray<int32>& Counts = SweepMode == 0 ? SimpleSweepCounts : ComplexSweepCounts;
		Msg = FString::Printf(TEXT("SWEEP %d/%d  |  count %d  |  %s %.1fs"),
			SweepIndex + 1, Counts.Num(), Counts[SweepIndex],
			SweepPhase == ESweepPhase::Warmup ? TEXT("warming up") : TEXT("sampling"), FMath::Max(0.f, SweepTimer));
	}
	else if (bGameOver)
	{
		Msg = bWon ? FString::Printf(TEXT("YOU WIN  |  reindeer brought in: %d"), TotalArrived)
		           : FString::Printf(TEXT("TIME UP  |  pen %d  |  reindeer brought in: %d"), CurrentPen + 1, TotalArrived);
	}
	else
	{
		Msg = FString::Printf(TEXT("Mode %s  |  Pen %d/%d  |  %d / %d reindeer  |  %.1fs"),
			AppliedMode == 0 ? TEXT("GPU sprites") : TEXT("CPU meshes"),
			CurrentPen + 1, Pens.Num(), ArrivedThisPen, RequiredToPass(), PenTimeLeft);
	}

	GEngine->AddOnScreenDebugMessage(/*Key=*/4242, 0.f, FColor::Yellow, Msg);
	FString Info = FString::Printf(TEXT("Count %d   [M] switch mode   [ ] halve / double count   console: herd.Sweep"), AppliedCount);
	if (AppliedMode == 1)
	{
		Info += FString::Printf(TEXT("   |  agent sim %.2f ms (%s)"), AgentSimMs,
			GHerdSpatialHash != 0 ? TEXT("spatial hash") : TEXT("brute force"));

		// Check on obstacle avoidance: should stay near 0 once the herd has settled.
		int32 InsideRocks = 0;
		for (const AActor* Obstacle : Obstacles)
		{
			if (!Obstacle)
			{
				continue;
			}
			FVector Origin, Extent;
			Obstacle->GetActorBounds(false, Origin, Extent);
			const float RadiusSq = FMath::Square(FMath::Max(Extent.X, Extent.Y));
			for (const FVector& P : CpuSim.Positions)
			{
				InsideRocks += FVector::DistSquared2D(P, Origin) < RadiusSq ? 1 : 0;
			}
		}
		Info += FString::Printf(TEXT("   |  inside rocks: %d"), InsideRocks);
	}
	GEngine->AddOnScreenDebugMessage(/*Key=*/4243, 0.f, FColor::Cyan, Info);
}
