// HerdCpuSim.cpp

#include "HerdCpuSim.h"

void FHerdCpuSim::Reset(int32 Count, const FVector& Center, float SpawnRadius, int32 Seed)
{
	Rng.Initialize(Seed);
	GroundZ = Center.Z;

	Agents.SetNum(Count);
	for (int32 I = 0; I < Count; ++I)
	{
		FAgent& A = Agents[I];
		// Uniform point in a disc.
		const float R = SpawnRadius * FMath::Sqrt(Rng.FRand());
		const float Angle = Rng.FRandRange(0.f, 2.f * PI);
		A.Pos = FVector2f(Center.X + R * FMath::Cos(Angle), Center.Y + R * FMath::Sin(Angle));
		A.WanderAngle = Rng.FRandRange(0.f, 2.f * PI);
		A.Vel = FVector2f(FMath::Cos(A.WanderAngle), FMath::Sin(A.WanderAngle)) * Settings.GrazeSpeed;
		A.State = EReindeerState::Graze;
		A.StateTimer = 0.f;
		A.Leader = INDEX_NONE;
		A.bIsLeader = (I % FMath::Max(1, Settings.LeaderEvery)) == 0;
	}

	WriteOutputs();
}

// ---------------------------------------------------------------------------
// Spatial hash (the optimization measured in the report: herd.SpatialHash 0 vs 1)
// ---------------------------------------------------------------------------
FIntPoint FHerdCpuSim::CellOf(const FVector2f& P) const
{
	return FIntPoint(FMath::FloorToInt(P.X / CellSize), FMath::FloorToInt(P.Y / CellSize));
}

int32 FHerdCpuSim::HashCell(int32 X, int32 Y) const
{
	const uint32 H = (uint32(X) * 73856093u) ^ (uint32(Y) * 19349663u);
	return int32(H & (HashSize - 1));
}

void FHerdCpuSim::BuildHash()
{
	CellSize = FMath::Max(1.f, Settings.PerceptionRadius);

	const int32 N = Agents.Num();
	CellCount.Init(0, HashSize); // SetNumZeroed would keep last frame's counts once the size matches
	CellStart.SetNumUninitialized(HashSize);
	AgentCell.SetNumUninitialized(N);
	SortedAgents.SetNumUninitialized(N);

	for (int32 I = 0; I < N; ++I)
	{
		if (Agents[I].State == EReindeerState::Penned)
		{
			AgentCell[I] = INDEX_NONE;
			continue;
		}
		const FIntPoint C = CellOf(Agents[I].Pos);
		AgentCell[I] = HashCell(C.X, C.Y);
		++CellCount[AgentCell[I]];
	}

	// Prefix sum, then counting sort into SortedAgents.
	int32 Running = 0;
	for (int32 H = 0; H < HashSize; ++H)
	{
		CellStart[H] = Running;
		Running += CellCount[H];
		CellCount[H] = 0; // reused as a fill cursor below, ends equal to the count again
	}
	for (int32 I = 0; I < N; ++I)
	{
		const int32 H = AgentCell[I];
		if (H != INDEX_NONE)
		{
			SortedAgents[CellStart[H] + CellCount[H]++] = I;
		}
	}
}

template <typename FVisit>
void FHerdCpuSim::ForEachNeighbor(int32 I, bool bUseSpatialHash, FVisit&& Visit) const
{
	const FVector2f P = Agents[I].Pos;
	const float RadiusSq = FMath::Square(Settings.PerceptionRadius);
	int32 Found = 0;

	auto Consider = [&](int32 J) -> bool
	{
		if (J == I || Agents[J].State == EReindeerState::Penned)
		{
			return true;
		}
		const FVector2f Offset = Agents[J].Pos - P;
		const float DistSq = Offset.SizeSquared();
		if (DistSq < RadiusSq)
		{
			Visit(J, Offset, DistSq);
			if (++Found >= Settings.MaxNeighbors)
			{
				return false; // stop
			}
		}
		return true;
	};

	if (!bUseSpatialHash)
	{
		// Before the optimization: every agent tests every other agent.
		for (int32 J = 0; J < Agents.Num(); ++J)
		{
			if (!Consider(J)) { return; }
		}
		return;
	}

	const FIntPoint C = CellOf(P);
	for (int32 DY = -1; DY <= 1; ++DY)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			const int32 H = HashCell(C.X + DX, C.Y + DY);
			const int32 Start = CellStart[H];
			const int32 End = Start + CellCount[H];
			for (int32 K = Start; K < End; ++K)
			{
				if (!Consider(SortedAgents[K])) { return; }
			}
		}
	}
}

void FHerdCpuSim::FindLeader(int32 I, bool bUseSpatialHash)
{
	FAgent& A = Agents[I];
	if (A.bIsLeader)
	{
		A.Leader = I; // leaders lead themselves
		return;
	}

	A.Leader = INDEX_NONE;
	float BestDistSq = TNumericLimits<float>::Max();
	ForEachNeighbor(I, bUseSpatialHash, [&](int32 J, const FVector2f&, float DistSq)
	{
		if (Agents[J].bIsLeader && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			A.Leader = J;
		}
	});
}

// ---------------------------------------------------------------------------
// Simulation step
// ---------------------------------------------------------------------------
void FHerdCpuSim::Tick(float DeltaSeconds, const FVector& DogPosition, float DogRadius,
	const TArray<FSphere>& Obstacles, bool bUseSpatialHash)
{
	const int32 N = Agents.Num();
	if (N == 0)
	{
		return;
	}

	const float Dt = FMath::Min(DeltaSeconds, 1.f / 20.f); // stay stable on hitches
	const FHerdCpuSettings& S = Settings;
	const FVector2f Dog(DogPosition.X, DogPosition.Y);

	if (bUseSpatialHash)
	{
		BuildHash();
	}

	// New velocities go to a separate array so the update order does not matter.
	TArray<FVector2f> NewVel;
	NewVel.SetNumUninitialized(N);

	for (int32 I = 0; I < N; ++I)
	{
		FAgent& A = Agents[I];
		if (A.State == EReindeerState::Penned)
		{
			NewVel[I] = FVector2f::ZeroVector;
			continue;
		}

		const float Speed = A.Vel.Size();
		const FVector2f Forward = Speed > 1.f ? A.Vel / Speed
			: FVector2f(FMath::Cos(A.WanderAngle), FMath::Sin(A.WanderAngle));

		// --- Perception: dog (cone, or heard when close) ---
		const FVector2f ToDog = Dog - A.Pos;
		const float DogDist = ToDog.Size();
		const FVector2f ToDogDir = DogDist > 1.f ? ToDog / DogDist : Forward;
		const bool bSeesDog = DogDist < DogRadius &&
			(DogDist < DogRadius * S.HearRadiusFraction || FVector2f::DotProduct(Forward, ToDogDir) > S.ViewCos);

		// --- Perception: neighbours ---
		FVector2f Separation = FVector2f::ZeroVector;
		FVector2f AlignSum = FVector2f::ZeroVector;
		FVector2f CohesionSum = FVector2f::ZeroVector;
		int32 Seen = 0;
		bool bPanicNearby = false;

		ForEachNeighbor(I, bUseSpatialHash, [&](int32 J, const FVector2f& Offset, float DistSq)
		{
			const float Dist = FMath::Sqrt(FMath::Max(DistSq, 1.f));
			const FVector2f Dir = Offset / Dist;

			// Separation works all around (you feel a body bumping into you).
			if (Dist < S.SeparationRadius)
			{
				Separation -= Dir * (1.f - Dist / S.SeparationRadius);
			}

			// Everything else needs the neighbour inside the view cone.
			if (FVector2f::DotProduct(Forward, Dir) < S.ViewCos)
			{
				return;
			}
			AlignSum += Agents[J].Vel;
			CohesionSum += Offset;
			++Seen;
			bPanicNearby |= Agents[J].State == EReindeerState::Spooked;
		});

		// --- State machine ---
		if (bSeesDog)
		{
			A.State = EReindeerState::Spooked;
			A.StateTimer = S.SpookedTime;
		}
		else if (A.State != EReindeerState::Spooked && bPanicNearby && Rng.FRand() < 4.f * Dt)
		{
			// Panic spreads through the herd (about a quarter second per "hop").
			A.State = EReindeerState::Spooked;
			A.StateTimer = S.SpookedTime * 0.6f;
		}
		else
		{
			A.StateTimer -= Dt;
			if (A.StateTimer <= 0.f)
			{
				if (A.State == EReindeerState::Spooked)
				{
					FindLeader(I, bUseSpatialHash);
					A.State = A.Leader != INDEX_NONE ? EReindeerState::Follow : EReindeerState::Graze;
					A.StateTimer = S.FollowTime;
				}
				else if (A.State == EReindeerState::Follow)
				{
					A.State = EReindeerState::Graze;
					A.Leader = INDEX_NONE;
				}
			}
		}

		// --- Steering: build a desired velocity, then turn toward it ---
		float StateSpeed = S.GrazeSpeed;
		FVector2f Desired = FVector2f::ZeroVector;

		switch (A.State)
		{
		case EReindeerState::Graze:
		{
			A.WanderAngle += Rng.FRandRange(-1.f, 1.f) * 1.5f * Dt;
			Desired = FVector2f(FMath::Cos(A.WanderAngle), FMath::Sin(A.WanderAngle)) * S.GrazeSpeed;
			break;
		}
		case EReindeerState::Spooked:
		{
			StateSpeed = S.FleeSpeed;
			// Run straight away from the dog while it is near, otherwise keep running the same way.
			Desired = (DogDist < DogRadius * 1.5f ? -ToDogDir : Forward) * S.FleeSpeed;
			break;
		}
		case EReindeerState::Follow:
		{
			StateSpeed = S.FollowSpeed;
			const bool bLeaderValid = Agents.IsValidIndex(A.Leader) && A.Leader != I &&
				Agents[A.Leader].State != EReindeerState::Penned;
			if (bLeaderValid)
			{
				const FVector2f ToLeader = Agents[A.Leader].Pos - A.Pos;
				const float LeaderDist = ToLeader.Size();
				// Close to the leader: match its velocity instead of crowding it.
				Desired = LeaderDist > S.SeparationRadius * 2.f
					? ToLeader / LeaderDist * S.FollowSpeed
					: Agents[A.Leader].Vel;
			}
			else
			{
				Desired = Forward * S.FollowSpeed * 0.8f; // leader (or lost leader): keep going
			}
			break;
		}
		default:
			break;
		}

		Desired += Separation * StateSpeed * S.WeightSeparation;
		if (Seen > 0)
		{
			Desired += (AlignSum / Seen - A.Vel) * S.WeightAlignment;
			Desired += (CohesionSum / Seen).GetSafeNormal() * StateSpeed * S.WeightCohesion;
		}

		// --- Obstacle avoidance: look ahead, push sideways out of any sphere in the way ---
		const FVector2f Ahead = A.Pos + Forward * FMath::Max(200.f, Speed * 0.6f);
		for (const FSphere& Obstacle : Obstacles)
		{
			const FVector2f Centre(Obstacle.Center.X, Obstacle.Center.Y);
			const float Reach = Obstacle.W + S.SeparationRadius;
			const FVector2f Away = Ahead - Centre;
			const float AwayDist = Away.Size();
			if (AwayDist < Reach && AwayDist > 1.f)
			{
				Desired += Away / AwayDist * (1.f - AwayDist / Reach) * FMath::Max(StateSpeed, S.FollowSpeed) * S.WeightAvoid;
			}
		}

		// Turn toward the desired velocity with limited acceleration.
		FVector2f Accel = (Desired - A.Vel) * S.Responsiveness;
		const float AccelSize = Accel.Size();
		if (AccelSize > S.MaxAccel)
		{
			Accel *= S.MaxAccel / AccelSize;
		}
		FVector2f V = A.Vel + Accel * Dt;
		const float MaxSpeed = FMath::Max(StateSpeed * 1.2f, S.GrazeSpeed * 3.f);
		const float VSize = V.Size();
		if (VSize > MaxSpeed)
		{
			V *= MaxSpeed / VSize;
		}
		NewVel[I] = V;
	}

	for (int32 I = 0; I < N; ++I)
	{
		Agents[I].Vel = NewVel[I];
		Agents[I].Pos += NewVel[I] * Dt;
	}

	WriteOutputs();
}

int32 FHerdCpuSim::CollectArrivals(const FVector& PenPosition, float PenRadius)
{
	const FVector2f Pen(PenPosition.X, PenPosition.Y);
	const float RadiusSq = PenRadius * PenRadius;
	int32 Arrived = 0;

	for (int32 I = 0; I < Agents.Num(); ++I)
	{
		FAgent& A = Agents[I];
		if (A.State != EReindeerState::Penned && FVector2f::DistSquared(A.Pos, Pen) < RadiusSq)
		{
			A.State = EReindeerState::Penned;
			A.Vel = FVector2f::ZeroVector;
			States[I] = int32(EReindeerState::Penned);
			++Arrived;
		}
	}
	return Arrived;
}

void FHerdCpuSim::WriteOutputs()
{
	const int32 N = Agents.Num();
	Positions.SetNumUninitialized(N);
	Velocities.SetNumUninitialized(N);
	States.SetNumUninitialized(N);

	for (int32 I = 0; I < N; ++I)
	{
		const FAgent& A = Agents[I];
		Positions[I] = FVector(A.Pos.X, A.Pos.Y, GroundZ);
		Velocities[I] = FVector(A.Vel.X, A.Vel.Y, 0.f);
		States[I] = int32(A.State);
	}
}
