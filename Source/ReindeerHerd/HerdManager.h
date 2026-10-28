// HerdManager.h
// Reindeer herding game: owns both Niagara systems (GPU sprites / CPU meshes),
// drives them from gameplay, receives particle data back for scoring, and runs the
// scaling sweep for the report.
//
// Console variables / commands:
//   herd.Mode          0 = GPU sprites (simple & many), 1 = CPU meshes (complex & few)
//   herd.SimpleCount   particle count for the GPU system   (default 100000)
//   herd.ComplexCount  particle count for the CPU system   (default 2000)
//   herd.SpatialHash   1 = CPU agents use a spatial hash for neighbour queries, 0 = brute force
//                      (the measured optimization)
//   herd.Sweep         run the scaling experiment for the current mode, writes a CSV
//                      to Saved/Profiling/HerdSweep/
//   stat Herd          shows the CPU agent simulation time
// Keys in play:  M = switch mode,  ] = double count,  [ = halve count
// Command line:  -HerdSweep  starts the sweep automatically (combine with -ExecCmds="herd.Mode 1")

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NiagaraDataInterfaceExport.h"
#include "HerdCpuSim.h"
#include "HerdManager.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

UCLASS()
class REINDEERHERD_API AHerdManager : public AActor, public INiagaraParticleCallbackHandler
{
	GENERATED_BODY()

public:
	AHerdManager();

	virtual void Tick(float DeltaSeconds) override;

	// Called by the Niagara "Export Particle Data" data interface when reindeer reach the current pen.
	// Each entry is one reindeer. Niagara writes the pen index into Size so late GPU readbacks can be filtered.
	virtual void ReceiveParticleData_Implementation(const TArray<FBasicParticleData>& Data,
		UNiagaraSystem* NiagaraSystem, const FVector& SimulationPositionOffset) override;

	// For a debug UI slider / buttons. Same effect as typing the console variables.
	UFUNCTION(BlueprintCallable, Category = "Herd")
	void SetMode(int32 NewMode);

	UFUNCTION(BlueprintCallable, Category = "Herd")
	void SetParticleCount(int32 NewCount);

	UFUNCTION(BlueprintCallable, Category = "Herd")
	int32 GetParticleCount() const { return AppliedCount; }

	// Scaling experiment for the current mode (same as the herd.Sweep console command).
	UFUNCTION(BlueprintCallable, Category = "Herd|Benchmark")
	void StartSweep();

	// --- Setup (assign in the editor) ---
	UPROPERTY(EditAnywhere, Category = "Herd|Setup")
	TObjectPtr<UNiagaraSystem> SimpleSystem;   // System 1: GPU sim, sprite renderer, migration-scale herd

	UPROPERTY(EditAnywhere, Category = "Herd|Setup")
	TObjectPtr<UNiagaraSystem> ComplexSystem;  // System 2: CPU sim, mesh renderer, close-up herd with states

	// Optional: a CameraActor placed high above the snowfield. Used as the view in mode 0.
	UPROPERTY(EditAnywhere, Category = "Herd|Setup")
	TObjectPtr<AActor> AerialCamera;

	// --- Gameplay tuning ---
	UPROPERTY(EditAnywhere, Category = "Herd|Gameplay")
	TArray<TObjectPtr<AActor>> Pens;           // Camps/checkpoints: any actors placed in the level, in order

	UPROPERTY(EditAnywhere, Category = "Herd|Gameplay")
	float PenRadius = 1500.f;

	// Share of the herd still out on the snow (when the pen opens) that must reach it to open the next one.
	// Reindeer that reach a pen leave the herd, so later pens ask for a share of fewer animals.
	UPROPERTY(EditAnywhere, Category = "Herd|Gameplay", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float PassFraction = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Herd|Gameplay")
	float TimePerPen = 60.f;

	// How far the dog scares reindeer. The aerial herd is much bigger, so the dog needs a bigger reach.
	UPROPERTY(EditAnywhere, Category = "Herd|Gameplay")
	float SimpleDogRadius = 4000.f;

	UPROPERTY(EditAnywhere, Category = "Herd|Gameplay")
	float ComplexDogRadius = 1000.f;

	// CPU herd (mode 1) spawns in a disc of this radius around the HerdManager. The GPU herd's
	// spawn area is set inside its Niagara system.
	UPROPERTY(EditAnywhere, Category = "Herd|Gameplay")
	float ComplexSpawnRadius = 4000.f;

	// Rocks / trees the CPU reindeer steer around. Each actor's bounds become a sphere.
	UPROPERTY(EditAnywhere, Category = "Herd|Gameplay")
	TArray<TObjectPtr<AActor>> Obstacles;

	// When Obstacles is empty, BeginPlay spawns rocks at these offsets from the HerdManager (between the
	// CPU herd and the first pen). They are hidden in mode 0 because the GPU boids do not avoid obstacles.
	UPROPERTY(EditAnywhere, Category = "Herd|Gameplay")
	TArray<FVector> DefaultObstacleOffsets = { {5500, 1200, 0}, {6200, -1800, 0}, {3500, -2600, 0}, {2500, 2800, 0}, {6000, 3600, 0} };

	UPROPERTY(EditAnywhere, Category = "Herd|Gameplay")
	float DefaultObstacleRadius = 400.f;

	// --- Benchmark (scaling experiment) ---
	// Optional: the pawn is moved here and frozen during a sweep so every run uses the same view.
	UPROPERTY(EditAnywhere, Category = "Herd|Benchmark")
	TObjectPtr<AActor> BenchmarkViewpoint;

	UPROPERTY(EditAnywhere, Category = "Herd|Benchmark")
	TArray<int32> SimpleSweepCounts = { 25000, 50000, 100000, 200000, 400000, 800000, 1600000 };

	UPROPERTY(EditAnywhere, Category = "Herd|Benchmark")
	TArray<int32> ComplexSweepCounts = { 250, 500, 1000, 2000, 5000, 10000, 20000 };

	UPROPERTY(EditAnywhere, Category = "Herd|Benchmark")
	float SweepWarmupSeconds = 3.f;            // let the simulation settle after a respawn

	UPROPERTY(EditAnywhere, Category = "Herd|Benchmark")
	float SweepSampleSeconds = 5.f;            // average over this long

	// --- Read-only state for UI ---
	UPROPERTY(BlueprintReadOnly, Category = "Herd|State")
	int32 CurrentPen = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Herd|State")
	int32 ArrivedThisPen = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Herd|State")
	int32 TotalArrived = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Herd|State")
	float PenTimeLeft = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Herd|State")
	bool bGameOver = false;

	UPROPERTY(BlueprintReadOnly, Category = "Herd|State")
	bool bWon = false;

	// Milliseconds the CPU agent simulation took last frame (mode 1 only).
	UPROPERTY(BlueprintReadOnly, Category = "Herd|State")
	float AgentSimMs = 0.f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> SimpleComp;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> ComplexComp;

	// System 2's agents live here; their positions are copied into ComplexComp every frame.
	FHerdCpuSim CpuSim;

	UNiagaraComponent* GetActiveComp() const;
	void ApplyCvarsIfChanged();
	void TickCpuHerd(float DeltaSeconds);
	void PushCpuHerdToNiagara();
	void AddArrivals(int32 Count);
	void UpdateCamera();
	void ApplyDogView(class ACharacter* Dog) const;
	void SpawnDefaultObstacles();
	void HandleDebugKeys();
	void ResetPenState();
	int32 RequiredToPass() const;
	void ShowStatus() const;

	// Sweep helpers
	void TickSweep(float DeltaSeconds);
	void BeginSweepStep();
	void FinishSweepStep();
	void EndSweep();

	int32 AppliedMode = -1;
	int32 AppliedCount = 0;
	int32 ArrivedSinceSpawn = 0;      // reindeer removed from the current herd (it respawns on mode/count change)
	int32 RemainingAtPenStart = 0;

	enum class ESweepPhase : uint8 { None, Warmup, Sample };
	ESweepPhase SweepPhase = ESweepPhase::None;
	int32 SweepMode = 0;
	int32 SweepIndex = 0;
	float SweepTimer = 0.f;
	int32 SweepSamples = 0;
	double SumFrameMs = 0, MaxFrameMs = 0, SumGameMs = 0, SumRenderMs = 0, SumGpuMs = 0, SumAgentSimMs = 0;
	FString SweepCsv;
	FString SweepFilePath;
	bool bAutoSweepPending = false;
	float AutoSweepDelay = 0.f;
};
