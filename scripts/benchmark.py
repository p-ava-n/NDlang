#!/usr/bin/env python3
"""Compile benchmarks/bench_dot.ndlang at -O0 and -O2 and time the executables.

  python scripts/benchmark.py <path-to-ndlangc> [--runs N]

Requires an ndlangc built with the LLVM backend.
"""
import os
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "benchmarks", "bench_dot.ndlang")


def build(compiler, level, out_dir):
    exe = os.path.join(out_dir, f"bench_O{level}" + (".exe" if os.name == "nt" else ""))
    r = subprocess.run([compiler, SOURCE, f"-O{level}", "-o", exe], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"compilation at -O{level} failed:\n{r.stderr}")
    return exe


def time_run(exe, runs):
    samples = []
    output = ""
    for _ in range(runs):
        start = time.perf_counter()
        r = subprocess.run([exe], capture_output=True, text=True)
        samples.append(time.perf_counter() - start)
        output = r.stdout.strip()
    return statistics.median(samples), output


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    compiler = os.path.abspath(sys.argv[1])
    runs = int(sys.argv[sys.argv.index("--runs") + 1]) if "--runs" in sys.argv else 3

    with tempfile.TemporaryDirectory() as tmp:
        results = {}
        for level in (0, 2):
            exe = build(compiler, level, tmp)
            results[level] = time_run(exe, runs)
            print(f"-O{level}: {results[level][0]:.3f}s (median of {runs})  result = {results[level][1]}")
    t0, t2 = results[0][0], results[2][0]
    if t2 > 0:
        print(f"speed-up -O0 -> -O2: {t0 / t2:.2f}x")


if __name__ == "__main__":
    main()
