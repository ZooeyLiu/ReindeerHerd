"""Average Niagara CPU time per sweep step from the per-frame CSV written by herd.Sweep.

herd.Sweep records a CSV-profiler capture (HerdSweepFrames_<GPU|CPU>_<stamp>.csv) and marks each step's
sample window with "SampleBegin <count>" / "SampleEnd <count>" events. Niagara's CPU work is reported in the
Exclusive/<thread>/Effects columns. This prints one row per step:
  count, frames, Niagara game thread ms, Niagara render thread ms, Niagara other threads ms, GT+RT ms

Usage: python Tools/sweep_niagara.py Saved/Profiling/HerdSweep/HerdSweepFrames_GPU_<stamp>.csv [--csv out.csv]
"""
import csv
import re
import sys


def main(path, out_path=None):
    with open(path, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.reader(f))

    header = rows[0]
    lower = [name.lower() for name in header]
    events_col = lower.index("events") if "events" in lower else None
    # Niagara's exclusive time is split between an "Effects" and a "Niagara" category depending on the thread.
    effects = [i for i, name in enumerate(header)
               if name.startswith("Exclusive/") and (name.endswith("/Effects") or name.endswith("/Niagara"))]
    if events_col is None or not effects:
        sys.exit("No events column or no Exclusive/*/Effects columns found; columns: %s" % ", ".join(header[:40]))

    def thread_of(i):
        return header[i].split("/")[1]

    steps, current = [], None
    for row in rows[1:]:
        if not row or row[0].startswith("[") or row == header:
            break  # repeated header / metadata rows at the end of the file
        events = row[events_col] if events_col < len(row) else ""
        begin = re.search(r"SampleBegin (\d+)", events)
        if begin:
            current = {"count": int(begin.group(1)), "frames": 0, "sums": {}}
        if current is not None:
            current["frames"] += 1
            for i in effects:
                if i >= len(row):
                    continue
                try:
                    value = float(row[i])
                except ValueError:
                    continue
                current["sums"][thread_of(i)] = current["sums"].get(thread_of(i), 0.0) + value
        if re.search(r"SampleEnd (\d+)", events) and current is not None:
            steps.append(current)
            current = None

    lines = ["Count,Frames,NiagaraGameThreadMs,NiagaraRenderThreadMs,NiagaraOtherThreadsMs,NiagaraGTplusRTMs"]
    for s in steps:
        n = max(1, s["frames"])
        gt = s["sums"].get("GameThread", 0.0) / n
        rt = s["sums"].get("RenderThread", 0.0) / n
        other = sum(v for k, v in s["sums"].items() if k not in ("GameThread", "RenderThread")) / n
        lines.append("%d,%d,%.3f,%.3f,%.3f,%.3f" % (s["count"], s["frames"], gt, rt, other, gt + rt))

    print("\n".join(lines))
    if out_path:
        with open(out_path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[sys.argv.index("--csv") + 1] if "--csv" in sys.argv else None
    main(sys.argv[1], out)
