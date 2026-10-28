// NS_HerdGPU, Simulation Stage 1 "PopulateGrid" (Iteration Source = Particles)
// Custom HLSL node inside the Scratch Pad module "PopGrid".
//
// Input pins (name : type)                 wire from (Map Get)
//   Grid               : Neighbor Grid 3D   Emitter.Grid
//   Position           : Position           Particles.Position
//   SimulationPosition : Position           Engine.Emitter.SimulationPosition (grid centre)
// Output pins
//   InGrid             : bool               -> Map Set  Particles.InGrid

// In 5.7 the grid's World BBox Size is hidden in the editor, so the box size is computed here:
// NumCells (set once by SetNumCells in Emitter Spawn) times a fixed cell size.
const float CellSize = 156.25;   // 256 cells * 156.25 = 40000 units (400 m) square
int NX; int NY; int NZ;
Grid.GetNumCells(NX, NY, NZ);
float3 BBox = float3(NX * CellSize, NY * CellSize, 100000.0);

// World position -> 0..1 inside the grid box (box is centred on the system).
float3 Unit = (Position - SimulationPosition) / BBox + 0.5;

int IX; int IY; int IZ;
Grid.UnitToIndex(Unit, IX, IY, IZ);

bool Added;
Grid.AddParticle(IX, IY, IZ, ExecIndex(), Added);
InGrid = Added;
