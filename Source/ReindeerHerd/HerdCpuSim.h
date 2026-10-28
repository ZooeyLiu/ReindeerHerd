// HerdCpuSim.h
// System 2 ("complex & few"): CPU reindeer agents. Plain C++, no UObjects.
// AHerdManager ticks it and pushes the results into a CPU Niagara system (mesh renderer)
// through Niagara Array data interfaces, one array element per particle (indexed by UniqueID).
//
// Per agent:
//   - state machine: Graze -> Spooked -> Follow (leader) -> Graze, Penned when it reaches the pen
//   - perception cone: neighbours behind the agent are ignored, the dog is only seen inside the cone
//     (or heard when it is very close)
//   - panic spreads: a grazing agent that sees a spooked neighbour becomes spooked too
//   - obstacle avoidance against spheres (level actors listed in AHerdManager::Obstacles)
//   - neighbour queries either brute force O(n^2) or through a spatial hash (herd.SpatialHash)

#pragma once

#include "CoreMinimal.h"

enum class EReindeerState : uint8
{
	Graze   = 0,
	Spooked = 1,
	Follow  = 2,
	Penned  = 3,   // reached a pen, removed from the herd
};

struct FHerdCpuSettings
{
	float PerceptionRadius = 600.f;    // neighbours closer than this are considered
	float SeparationRadius = 160.f;    // personal space
	float ViewCos = -0.5f;             // perception cone: cos(120 deg) = 240 deg field of view
	float HearRadiusFraction = 0.35f;  // dog inside DogRadius * this is noticed even from behind
	int32 MaxNeighbors = 24;           // cap per agent so cost stays bounded in dense spots

	float GrazeSpeed = 60.f;
	float FleeSpeed = 650.f;            // slower than the dog (900) so the player can catch up and steer
	float FollowSpeed = 450.f;
	float MaxAccel = 1800.f;

	float SpookedTime = 2.5f;          // seconds of panic after the last scare
	float FollowTime = 5.f;            // seconds following a leader before calming down
	int32 LeaderEvery = 40;            // every Nth agent is a leader

	// Each steering term is a desired velocity scaled by the state's speed times its weight.
	float WeightSeparation = 1.5f;
	float WeightAlignment = 0.5f;
	float WeightCohesion = 0.3f;
	float WeightAvoid = 2.0f;
	float Responsiveness = 4.f;        // how fast velocity turns toward the desired velocity (1/s)
};

class FHerdCpuSim
{
public:
	FHerdCpuSettings Settings;

	// Respawns Count agents in a disc around Center.
	void Reset(int32 Count, const FVector& Center, float SpawnRadius, int32 Seed);

	// Advance the simulation. Obstacles are spheres (XY used, Z ignored).
	void Tick(float DeltaSeconds, const FVector& DogPosition, float DogRadius,
		const TArray<FSphere>& Obstacles, bool bUseSpatialHash);

	// Agents inside the pen switch to Penned. Returns how many arrived this call.
	int32 CollectArrivals(const FVector& PenPosition, float PenRadius);

	int32 Num() const { return Positions.Num(); }

	// Read by AHerdManager and copied into Niagara arrays every frame.
	TArray<FVector> Positions;
	TArray<FVector> Velocities;
	TArray<int32> States;              // EReindeerState as int (Niagara colours / kills by this)

private:
	struct FAgent
	{
		FVector2f Pos;
		FVector2f Vel;
		float StateTimer = 0.f;
		float WanderAngle = 0.f;
		int32 Leader = INDEX_NONE;
		EReindeerState State = EReindeerState::Graze;
		bool bIsLeader = false;
	};

	TArray<FAgent> Agents;
	float GroundZ = 0.f;
	FRandomStream Rng;

	// Spatial hash: agents sorted by cell, CellStart/CellCount index into SortedAgents.
	static constexpr int32 HashSize = 1 << 14;
	TArray<int32> CellStart;
	TArray<int32> CellCount;
	TArray<int32> SortedAgents;
	TArray<int32> AgentCell;
	float CellSize = 600.f;

	void BuildHash();
	int32 HashCell(int32 X, int32 Y) const;
	FIntPoint CellOf(const FVector2f& P) const;

	// Calls Visit(OtherIndex, Offset, DistSq) for agents within PerceptionRadius of agent I.
	template <typename FVisit>
	void ForEachNeighbor(int32 I, bool bUseSpatialHash, FVisit&& Visit) const;

	void FindLeader(int32 I, bool bUseSpatialHash);
	void WriteOutputs();
};
