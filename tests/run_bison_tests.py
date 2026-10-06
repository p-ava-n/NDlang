#!/usr/bin/env python3
"""Checks the Bison/LR expression parser against known results."""
import subprocess
import sys

CASES = [
    ("1+2*3", "7.000000"),
    ("(1+2)*3", "9.000000"),
    ("10-3-2", "5.000000"),
    ("-(2+3)*4", "-20.000000"),
    ("[1,2,3]*2+[4,5,6]", "[6.000000, 9.000000, 12.000000]"),
    ("[2,4]/2", "[1.000000, 2.000000]"),
]


def main():
    exe = sys.argv[1]
    bad = 0
    for expr, want in CASES:
        r = subprocess.run([exe, expr], capture_output=True, text=True)
        got = r.stdout.strip()
        if r.returncode != 0 or got != want:
            print(f"FAIL {expr!r}: want {want!r}, got {got!r}")
            bad += 1
        else:
            print(f"ok   {expr}")
    r = subprocess.run([exe, "[1,2,3]+[1,2]"], capture_output=True, text=True)
    if r.returncode == 0 or "dimension mismatch" not in r.stderr:
        print("FAIL dimension mismatch not rejected")
        bad += 1
    else:
        print("ok   [1,2,3]+[1,2] rejected")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
