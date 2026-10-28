// NS_HerdGPU, Simulation Stage 2 "Boids" (Iteration Source = Particles)
// Custom HLSL node inside a Scratch Pad module.
// Three boids rules (separation / alignment / cohesion) + flee from the dog,
// then pen check + Export Particle Data (GPU -> C++ readback for the score).
//
// Input pins (name : type)              wire from (Map Get)
//   Grid        : Neighbor Grid 3D       Emitter.Grid
//   ParticleReader : Particle Attribute Reader   Emitter.ParticleReader
//   Exporter    : Export Particle Data   Emitter.Exporter
//   Position    : Position               Particles.Position
//   Velocity    : Vector                 Particles.Velocity
//   SimulationPosition  : Position               Engine.Emitter.SimulationPosition
//   DogPosition : Vector                 User.DogPosition
//   DogRadius   : float                  User.DogRadius
//   PenPosition : Vector                 User.PenPosition
//   PenRadius   : float                  User.PenRadius
//   PenIndex    : int                    User.PenIndex
//   DeltaTime   : float                  Engine.DeltaTime
// Output pins
//   OutPosition : Position   -> Map Set Particles.Position
//   OutVelocity : Vector     -> Map Set Particles.Velocity
//   OutPenned   : bool       -> Map Set Particles.Penned

// ---- tuning (edit these numbers, then Apply / Compile) ----
const float PerceptionRadius = 150.0;   // keep at or below CellSize so the 3x3 cells cover it
const float SeparationRadius = 50.0;
const float GrazeSpeed       = 40.0;
const float FleeSpeed        = 900.0;
const float WSeparation      = 1.5;
const float WAlignment       = 0.6;     // also carries panic through the herd (fast neighbours speed you up)
const float WCohesion        = 0.3;
const float Responsiveness   = 3.0;

const float CellSize         = 156.25;  // must match GPU_PopulateGrid.hlsl

// ---- find my cell ----
int NX; int NY; int NZ;
Grid.GetNumCells(NX, NY, NZ);
float3 BBox = float3(NX * CellSize, NY * CellSize, 100000.0);
float3 Rel = Position - SimulationPosition;
float3 Unit = Rel / BBox + 0.5;

int CX; int CY; int CZ;
Grid.UnitToIndex(Unit, CX, CY, CZ);
int MaxPerCell;
Grid.MaxNeighborsPerCell(MaxPerCell);

int Self = ExecIndex();

// ---- neighbour loop: the 3x3 cells around me (the grid is one cell tall) ----
float3 Sep = float3(0, 0, 0);
float3 AlignSum = float3(0, 0, 0);
float3 CohSum = float3(0, 0, 0);
int Count = 0;

for (int DY = -1; DY <= 1; ++DY)
{
	for (int DX = -1; DX <= 1; ++DX)
	{
		int X = CX + DX;
		int Y = CY + DY;
		if (X < 0 || Y < 0 || X >= NX || Y >= NY)
		{
			continue;
		}

		int CellLinear;
		Grid.IndexToLinear(X, Y, CZ, CellLinear);
		int InCell;
		Grid.GetParticleNeighborCount(CellLinear, InCell);
		InCell = min(InCell, MaxPerCell);

		for (int K = 0; K < InCell; ++K)
		{
			int Slot;
			Grid.NeighborGridIndexToLinear(X, Y, CZ, K, Slot);
			int Other;
			Grid.GetParticleNeighbor(Slot, Other);
			if (Other < 0 || Other == Self)
			{
				continue;
			}

			bool ValidP; float3 OtherPos;
			ParticleReader.GetPositionByIndex<Attribute="Position">(Other, ValidP, OtherPos);
			bool ValidV; float3 OtherVel;
			ParticleReader.GetVectorByIndex<Attribute="Velocity">(Other, ValidV, OtherVel);

			float3 D = OtherPos - Position;
			D.z = 0;
			float Dist = length(D);
			if (ValidP && Dist > 0.01 && Dist < PerceptionRadius)
			{
				if (Dist < SeparationRadius)
				{
					Sep -= D / Dist * (1.0 - Dist / SeparationRadius);
				}
				AlignSum += OtherVel;
				CohSum += D;
				Count += 1;
			}
		}
	}
}

// ---- steering ----
float3 ToDog = Position - DogPosition;
ToDog.z = 0;
float DogDist = length(ToDog);
float Fear = saturate(1.0 - DogDist / max(DogRadius, 1.0));   // 1 next to the dog, 0 outside DogRadius

float CurSpeed = length(Velocity);
// Standing still (first frame): pick a random heading from the particle index.
float RandAngle = frac(sin(Self * 12.9898) * 43758.5453) * 6.2831853;
float3 Fwd = CurSpeed > 1.0 ? Velocity / CurSpeed : float3(cos(RandAngle), sin(RandAngle), 0);
float Speed = lerp(GrazeSpeed, FleeSpeed, Fear);

float3 Desired = Fwd * GrazeSpeed;
if (DogDist > 0.01)
{
	Desired += ToDog / DogDist * FleeSpeed * Fear * 1.5;          // flee from the dog
}
Desired += Sep * max(Speed, 200.0) * WSeparation;
if (Count > 0)
{
	Desired += (AlignSum / Count - Velocity) * WAlignment;
	float3 Coh = CohSum / Count;
	if (length(Coh) > 0.01)
	{
		Desired += normalize(Coh) * Speed * WCohesion;
	}
}

// Soft wall: push back when leaving the middle 90% of the grid box.
float2 Edge = abs(Rel.xy) - BBox.xy * 0.45;
if (Edge.x > 0) { Desired.x -= sign(Rel.x) * Edge.x * 2.0; }
if (Edge.y > 0) { Desired.y -= sign(Rel.y) * Edge.y * 2.0; }

float3 NewVel = Velocity + (Desired - Velocity) * saturate(Responsiveness * DeltaTime);
NewVel.z = 0;
float NewSpeed = length(NewVel);
float MaxSpeed = FleeSpeed * 1.2;
if (NewSpeed > MaxSpeed)
{
	NewVel *= MaxSpeed / NewSpeed;
}

OutVelocity = NewVel;
OutPosition = Position + NewVel * DeltaTime;

// ---- pen: simulation -> gameplay ----
float3 ToPen = OutPosition - PenPosition;
ToPen.z = 0;
bool Enter = length(ToPen) < PenRadius;

// Size carries the pen index so C++ can ignore late readbacks from an earlier pen.
bool Exported;
Exporter.ExportParticleData(Enter, OutPosition, (float)PenIndex, NewVel, Exported);

// Only leave the herd if the export really happened (the export buffer has a fixed size per frame),
// otherwise try again next frame. Penned particles are killed in Particle Update.
OutPenned = Enter && Exported;
