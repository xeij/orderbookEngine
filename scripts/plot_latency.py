#!/usr/bin/env python3
"""Render latency CDF + histogram plots from the replay harness output.

Inputs are the latency_*.csv files written by lob_replay (two columns:
nanoseconds, count). Produces one PNG per operation showing the CDF on a log-x
axis with p50/p99/p99.9 markers, plus a summary table printed to stdout.

Usage:
    scripts/plot_latency.py path/to/out_dir
"""

import argparse
import csv
import pathlib
import sys

try:
    import matplotlib.pyplot as plt
except ImportError:
    print("matplotlib not installed: pip install matplotlib", file=sys.stderr)
    sys.exit(1)


def load(path: pathlib.Path):
    nanos, counts = [], []
    with path.open() as f:
        reader = csv.reader(f)
        next(reader, None)  # header
        for row in reader:
            if len(row) != 2:
                continue
            nanos.append(int(row[0]))
            counts.append(int(row[1]))
    return nanos, counts


def percentile(nanos, counts, p):
    total = sum(counts)
    if total == 0:
        return 0
    target = total * p
    acc = 0
    for n, c in zip(nanos, counts):
        acc += c
        if acc >= target:
            return n
    return nanos[-1]


def plot(path: pathlib.Path, out_png: pathlib.Path, title: str):
    nanos, counts = load(path)
    if not nanos:
        print(f"{path}: empty")
        return

    total = sum(counts)
    cdf = []
    acc = 0
    for c in counts:
        acc += c
        cdf.append(acc / total)

    p50 = percentile(nanos, counts, 0.50)
    p99 = percentile(nanos, counts, 0.99)
    p999 = percentile(nanos, counts, 0.999)

    fig, ax = plt.subplots(figsize=(9, 5))
    ax.plot(nanos, cdf, drawstyle="steps-post")
    ax.set_xscale("log")
    ax.set_xlabel("latency (ns, log)")
    ax.set_ylabel("cumulative fraction")
    ax.set_title(f"{title}  n={total:,}")
    ax.grid(True, alpha=0.3)
    for q, val, label in [(0.50, p50, "p50"), (0.99, p99, "p99"), (0.999, p999, "p99.9")]:
        ax.axvline(val, color="red", linestyle="--", alpha=0.5)
        ax.text(val, q, f" {label}={val}ns", color="red", fontsize=8, va="bottom")
    fig.tight_layout()
    fig.savefig(out_png, dpi=120)
    plt.close(fig)
    print(f"{title}: n={total:,} p50={p50}ns p99={p99}ns p99.9={p999}ns -> {out_png}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir", type=pathlib.Path, help="directory containing latency_*.csv")
    args = ap.parse_args()

    for op in ("submit", "cancel", "reduce"):
        csv_path = args.out_dir / f"latency_{op}.csv"
        if not csv_path.exists():
            print(f"skip {csv_path} (not found)")
            continue
        plot(csv_path, args.out_dir / f"latency_{op}.png", op.title())


if __name__ == "__main__":
    main()
