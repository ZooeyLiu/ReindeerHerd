# Scaling sweep results (2026-10-28)

Standalone (`UnrealEditor.exe ReindeerHerd.uproject /Game/HerdLevel -game -windowed -ResX=1920 -ResY=1080 -HerdSweep
-ExecCmds="r.SetRes 1920x1080w,herd.Mode <0|1>,herd.SpatialHash <0|1>,r.VSync 0,t.MaxFPS 0"`), Development build,
RTX 5070 Ti Laptop / Ryzen 9 8940HX / 16 GB. Warmup 3 s + sample 5 s per count. Budget 16.6 ms.
Raw CSVs: `Saved/Profiling/HerdSweep/`.

Note: the laptop sometimes clamps the CPU to 22.7 % of base clock (firmware/power throttle). Runs made in that state
are invalid (game thread ~14 ms even when empty) and were moved to `_invalid_cpu_throttled/`. Check
`Get-Counter '\Processor Information(_Total)\% Processor Performance'` (should be >100) before measuring.

## System 1 — GPU sprites (HerdSweep_GPU_20261028_123611.csv)

| Count | Frame ms | GT ms | RT ms | GPU ms | FPS | ≤16.6 ms |
|---:|---:|---:|---:|---:|---:|:-:|
| 25,000 | 11.1 | 2.1 | 11.1 | 8.6 | 90 | yes |
| 50,000 | 14.0 | 2.2 | 14.0 | 11.3 | 72 | yes |
| 100,000 | 18.9 | 2.1 | 18.9 | 16.5 | 53 | **no** |
| 200,000 | 28.0 | 2.0 | 28.0 | 26.4 | 36 | no |
| 400,000 | 44.4 | 2.1 | 44.4 | 43.2 | 23 | no |
| 800,000 | 80.8 | 3.3 | 80.8 | 78.9 | 12 | no |
| 1,600,000 | 151.3 | 2.3 | 151.3 | 149.8 | 7 | no |

GPU-bound. Scene without herd costs ~6 ms GPU, so the herd adds ~0.1 ms GPU per 1,000 particles (linear).
RT tracks frame time because the render thread waits on the GPU. Budget breaks between 50k and 100k.

## System 2 — CPU meshes, spatial hash ON (HerdSweep_CPU_20261028_123741.csv)

| Count | Frame ms | GT ms | Agent sim ms | GPU ms | FPS | ≤16.6 ms |
|---:|---:|---:|---:|---:|---:|:-:|
| 250 | 8.2 | 2.4 | 0.15 | 6.1 | 122 | yes |
| 500 | 9.1 | 2.8 | 0.39 | 6.1 | 110 | yes |
| 1,000 | 9.1 | 3.5 | 1.04 | 6.1 | 110 | yes |
| 2,000 | 9.7 | 5.0 | 2.48 | 6.2 | 103 | yes |
| 5,000 | 11.2 | 10.6 | 7.83 | 6.4 | 89 | yes |
| 10,000 | 13.6 | 13.5 | 11.15 | 6.8 | 74 | yes |
| 20,000 | 24.3 | 24.3 | 21.35 | 8.1 | 41 | **no** |

## System 2 — CPU meshes, brute force (HerdSweep_CPU_20261028_123912.csv)

| Count | Frame ms | GT ms | Agent sim ms | FPS | ≤16.6 ms |
|---:|---:|---:|---:|---:|:-:|
| 250 | 8.1 | 2.4 | 0.19 | 124 | yes |
| 500 | 9.0 | 3.2 | 0.78 | 111 | yes |
| 1,000 | 9.3 | 5.7 | 3.30 | 108 | yes |
| 2,000 | 9.8 | 9.8 | 7.39 | 102 | yes |
| 5,000 | 16.8 | 16.8 | 14.64 | 60 | **no** |
| 10,000 | 27.6 | 27.6 | 25.25 | 36 | no |
| 20,000 | 51.0 | 51.0 | 48.12 | 20 | no |

## Optimization: spatial hash vs brute force (agent sim ms)

| Count | Brute | Hash | Speed-up |
|---:|---:|---:|---:|
| 1,000 | 3.30 | 1.04 | 3.2x |
| 2,000 | 7.39 | 2.48 | 3.0x |
| 5,000 | 14.64 | 7.83 | 1.9x |
| 10,000 | 25.25 | 11.15 | 2.3x |
| 20,000 | 48.12 | 21.35 | 2.3x |

Budget limit moves from ~4,000 agents (brute) to ~15,000 agents (hash).
Brute force grows roughly linearly, not quadratically, because each agent stops after MaxNeighbors (24) hits:
with the herd spread over a 4,000-unit disc and a 600-unit perception radius, ~1,000 agents are scanned before
24 are found. Without that cap brute force would be O(N²).

## Where the GPU herd's time goes at 100k (Docs/Figures/Fig3, Fig4)

Captured with `-trace=default,gpu,bookmark -statnamedevents` and `stat Niagara` (CPU was throttled during both,
so CPU-side numbers are upper bounds).
- `stat Niagara`: ~0.4 ms game thread + ~1.1 ms render thread (GPU Dispatch Setup 0.55, GPU Emitter Dispatch 0.39,
  Get Dynamic Mesh Elements 0.18 ms) — within the 4 ms Niagara budget.
- Insights GPU timers over a 24 s steady-state range: `NiagaraGpuComputeDispatch` (both boids sim stages) ≈ 1 ms/frame;
  `RayTracingDynamicGeometry` ≈ 4 ms/frame (sprites rebuilt into Lumen's ray-tracing scene every frame).
- `stat gpu` showed no overlay in this 5.7 standalone build; Insights GPU timers are used instead.

## Second optimization: sprites excluded from ray tracing (GPU)

`r.RayTracing.Geometry.NiagaraSprites 0`, measured back to back with an unthrottled CPU (147–174 % perf during both runs).
CSVs: `Docs/Data/HerdSweep_GPU_RTon_20261028_152456.csv`, `Docs/Data/HerdSweep_GPU_RToff_20261028_152625.csv`.

| Sprites | GPU on (ms) | GPU off (ms) | Frame on (ms) | Frame off (ms) | GPU saving |
|---:|---:|---:|---:|---:|---:|
| 25,000 | 9.0 | 8.3 | 10.8 | 10.7 | 8 % |
| 50,000 | 11.0 | 10.5 | 13.7 | 13.4 | 5 % |
| 100,000 | 16.3 | 14.6 | 18.5 | 17.4 | 11 % |
| 200,000 | 26.1 | 23.4 | 27.8 | 26.0 | 10 % |
| 400,000 | 44.9 | 39.7 | 46.1 | 41.8 | 12 % |
| 800,000 | 80.6 | 72.3 | 81.9 | 73.9 | 10 % |
| 1,600,000 | 154.0 | 138.0 | 155.2 | 139.1 | 10 % |

About 10 % GPU saving from 100k up; 100k still misses the budget (17.4 ms). The earlier throttled test overstated it.

## Niagara CPU time per level vs the 4 ms line (final runs, unthrottled)

`herd.Sweep` also writes a per-frame CSV-profiler capture (`HerdSweepFrames_<GPU|CPU>_*.csv`) with
`SampleBegin N` / `SampleEnd N` events. `python Tools/sweep_niagara.py <frames csv>` averages Niagara's exclusive
time (`Exclusive/*/Effects` and `/Niagara` columns) per step. CPU clock 143–180 % during both runs.
Data: `Docs/Data/HerdSweep*_final_*.csv`, `Docs/Data/HerdSweepNiagara_<GPU|CPU>_final.csv`.

| GPU sprites | Frame (ms) | Niagara GT+RT (ms) | CPU agents | Frame (ms) | Niagara GT+RT (ms) | C++ agent sim (ms) |
|---:|---:|---:|---:|---:|---:|---:|
| 25,000 | 11.0 | 0.18 | 250 | 7.6 | 0.09 | 0.11 |
| 50,000 | 14.1 | 0.20 | 500 | 8.7 | 0.11 | 0.33 |
| 100,000 | 18.8 | 0.22 | 1,000 | 8.1 | 0.10 | 0.77 |
| 200,000 | 27.6 | 0.24 | 2,000 | 8.3 | 0.10 | 1.74 |
| 400,000 | 45.2 | 0.34 | 5,000 | 8.6 | 0.10 | 4.67 |
| 800,000 | 81.8 | 0.72 | 10,000 | 12.2 | 0.18 | 9.71 |
| 1,600,000 | 155.5 | 0.18* | 20,000 | 24.9 | 0.53 | 21.73 |

Niagara itself stays far inside 4 ms; worker threads add 0.1–0.8 ms. *At 1.6M the render-thread value is unreliable
(GPU-bound at 6 fps). The GPU frame times reproduce the first sweep (100k: 18.8 vs 18.9 ms).
For System 2 the C++ behaviour sim that feeds Niagara is the real cost and passes 4 ms between 3,000 and 4,500 agents.

Niagara Debugger (`NiagaraDebugHud 3` / `5`, Docs/Figures/Fig5a–c, captured while throttled so CPU values are upper
bounds): NS_HerdGPU at 100k GT 1.0 / RT 0.17 / GPU 2.0 ms, of which the Boids stage is 1.93 ms and PopulateGrid
0.01 ms; NS_HerdCPU at 2,000 GT 0.89 / RT 0.38 ms.

## Open items
- 100k GPU herd is 0.8 ms over budget. Next: fewer neighbour reads in the Boids stage, cheaper sprite material.
