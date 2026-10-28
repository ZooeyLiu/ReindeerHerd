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

## Open items
- `stat gpu` breakdown of the GPU herd at 100k (sim stages vs sprite drawing) — not captured yet (CPU throttled again).
- GPU herd misses the budget at 100k (18.9 ms); candidate GPU optimization: lower MaxNeighbors per cell / fewer
  neighbour reads, or cheaper sprite material / smaller sprites (overdraw).
- Unreal Insights capture (`-trace=default,gpu`) for the report figure.
