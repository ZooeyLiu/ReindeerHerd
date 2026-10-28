# ReindeerHerd — Niagara at Scale (Track B)

A herding game on a snowfield, built in **Unreal Engine 5.7**. You play a dog; the reindeer herd flees from you.
Drive a share of the herd into each pen before its timer runs out.

The same herd runs on two Niagara configurations that you can switch between at runtime:

| | System 1 — `NS_HerdGPU` (simple & many) | System 2 — `NS_HerdCPU` (complex & few) |
| --- | --- | --- |
| Simulation | GPU, velocity-aligned sprites | CPU, mesh renderer (facing velocity) |
| Scale | 100,000 by default, aerial camera | 2,000 by default, close third-person camera |
| Behaviour | Boids (separation, alignment, cohesion) with Neighbor Grid 3D + Particle Attribute Reader in two simulation stages; flee from the dog | State machine (graze → spooked → follow leader → penned), perception cone, panic contagion, leaders, obstacle avoidance (C++ `FHerdCpuSim`, pushed to Niagara Array data interfaces) |
| Sim → gameplay | Export Particle Data (GPU readback) reports pen arrivals | Arrivals counted in C++; state shown as particle colour |
| Gameplay → sim | User parameters: DogPosition, DogRadius, PenPosition, PenRadius, PenIndex | Same, plus per-agent position / velocity / state arrays |

Demo video: 88 s, with the stat overlay (submitted separately).
Performance report: see [Docs/PerfResults.md](Docs/PerfResults.md) for the full tables.

## Requirements

- Unreal Engine **5.7** (installed from the Epic Games Launcher or built from source)
- Visual Studio 2022 with the "Game development with C++" workload (MSVC v143, Windows 10/11 SDK)
- Windows 10/11, a DirectX 12 GPU. Tested on an RTX 5070 Ti Laptop GPU, Ryzen 9 8940HX, 16 GB RAM.

## Build and run

1. Clone the repository:
   ```
   git clone https://github.com/ZooeyLiu/ReindeerHerd.git
   ```
2. Right-click `ReindeerHerd.uproject` → **Generate Visual Studio project files**.
3. Build **either** way:
   - Open `ReindeerHerd.sln`, choose **Development Editor / Win64**, and build the `ReindeerHerd` project; **or**
   - Run from a command prompt (adjust the engine path):
     ```
     "<UE_5.7>\Engine\Build\BatchFiles\Build.bat" ReindeerHerdEditor Win64 Development -Project="<repo>\ReindeerHerd.uproject" -WaitMutex
     ```
4. Open `ReindeerHerd.uproject`. The level `Content/HerdLevel` opens by default; press **Play**.
   The first launch compiles shaders and takes several minutes.

Standalone (more representative performance than Play-in-Editor):
```
"<UE_5.7>\Engine\Binaries\Win64\UnrealEditor.exe" "<repo>\ReindeerHerd.uproject" /Game/HerdLevel -game -windowed -ResX=1920 -ResY=1080 -ExecCmds="r.SetRes 1920x1080w,stat fps,stat unit"
```
`Tools/RunDemo.bat` does the same (edit the engine path inside it); `Tools/RunDemo.bat 1` starts in CPU mode.

## Controls

| Input | Action |
| --- | --- |
| WASD / mouse | Move the dog / camera |
| **M** | Switch between System 1 (GPU) and System 2 (CPU) |
| **]** / **[** | Double / halve the herd size of the current system |
| **`** | Open the console |

## Console variables and commands

| Name | Default | Meaning |
| --- | --- | --- |
| `herd.Mode` | `0` | `0` = System 1 (GPU sprites), `1` = System 2 (CPU meshes) |
| `herd.SimpleCount` | `100000` | Particle count of the GPU system |
| `herd.ComplexCount` | `2000` | Agent count of the CPU system |
| `herd.SpatialHash` | `1` | CPU neighbour search: `1` = spatial hash, `0` = brute force (the measured optimization) |
| `herd.Sweep` | — | Runs the scaling experiment for the current mode and writes a CSV |
| `stat Herd` | — | Shows CPU agent simulation and Niagara push time |

Useful engine stats: `stat unit`, `stat fps`, `stat gpu`, `stat Niagara`.

## Reproducing the scaling sweep

`herd.Sweep` (or the `-HerdSweep` command-line flag) respawns the herd at each count in
`SimpleSweepCounts` / `ComplexSweepCounts` (set on the `HerdManager` actor), warms up for 3 s, then averages frame,
game-thread, render-thread and GPU time over 5 s. An Unreal Insights bookmark marks each step.
Results go to `Saved/Profiling/HerdSweep/HerdSweep_<GPU|CPU>_<timestamp>.csv`.

```
UnrealEditor.exe ReindeerHerd.uproject /Game/HerdLevel -game -windowed -ResX=1920 -ResY=1080 -HerdSweep -ExecCmds="r.SetRes 1920x1080w,herd.Mode 1,herd.SpatialHash 0,r.VSync 0,t.MaxFPS 0"
```

The CSVs used in the report are in [Docs/Data](Docs/Data).

## Results summary (1080p, Standalone, budget 16.6 ms)

- **System 1 (GPU):** within budget up to 50,000 sprites (14.0 ms); 100,000 takes 18.9 ms. GPU-bound.
- **System 2 (CPU):** within budget up to 10,000 agents (13.6 ms); fails at 20,000 (24.3 ms). Game-thread-bound.
- **Optimization:** the spatial hash makes the CPU agent simulation 1.9–3.2× faster than brute force.
  The largest herd within budget grows from about 4,000 to about 15,000 agents.

## Repository layout

```
Source/ReindeerHerd/HerdManager.*   Gameplay actor: owns both Niagara systems, cvars, pens, scoring, sweep
Source/ReindeerHerd/HerdCpuSim.*    CPU agent simulation for System 2 (state machine, spatial hash)
Content/FX/                          NS_HerdGPU, NS_HerdCPU, materials
Content/HerdLevel.umap               Test level (snowfield, three pens)
Docs/HLSL/                           Custom HLSL used inside the Niagara scratch-pad modules
Docs/NiagaraSetup.md                 Step-by-step build notes for both Niagara systems (Chinese)
Docs/PerfResults.md, Docs/Data/      Sweep results and raw CSVs
Tools/RunDemo.bat                    Standalone launcher with stat overlay
```

## Known issues

- The CPU herd's behaviour is simulated in C++ and copied into Niagara through Array data interfaces, because the
  CPU VM cannot loop over neighbours and Neighbor Grid 3D is GPU-only.
- System 1 misses the 16.6 ms budget at its 100,000 target on the test laptop; see the report for next steps.
- The test laptop sometimes throttles its CPU to about 23 % of base clock. Check that
  `\Processor Information(_Total)\% Processor Performance` is above 100 before measuring.
