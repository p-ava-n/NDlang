#!/usr/bin/env python3
"""Experiment 7 tests: the same calculator before and after the terminator change.

  run_exp7_tests.py <calc_semi> <calc_nl>

calc_semi ends statements with ';', calc_nl with a newline. Each case feeds a
program on stdin and checks the printed values, or that it is rejected.
"""
import subprocess
import sys

# (program, expected stdout, or None if the parser must reject it)
SEMI_CASES = [
    ("x = 3;\ny = x * 2 + 1;\ny;\n(x + y) / 2;\n", "7\n5\n"),
    ("x = 3; y = 4; x * y;", "12\n"),        # several statements on one line
    ("x =\n  4;\nx\n;\n", "4\n"),             # one statement over several lines
    ("-2 * (3 + 4);", "-14\n"),
    ("x = 3\ny = 4\n", None),                 # newlines do not end statements
]

NL_CASES = [
    ("x = 3\ny = x * 2 + 1\ny\n(x + y) / 2\n", "7\n5\n"),
    ("\nx = 3\n\nx * x\n", "9\n"),            # blank lines are allowed
    ("-2 * (3 + 4)\n", "-14\n"),
    ("x = 3;\n", None),                       # ';' is no longer a terminator
    ("x =\n  4\n", None),                     # a newline ends the statement
]


def check(exe, cases):
    failures = 0
    for src, want in cases:
        r = subprocess.run([exe], input=src, capture_output=True, text=True)
        got = r.stdout.replace("\r\n", "\n")
        if want is None:
            ok = r.returncode != 0 and "error" in r.stderr
        else:
            ok = r.returncode == 0 and got == want
        label = repr(src)
        if ok:
            print(f"ok   {exe.split('/')[-1]}: {label}")
        else:
            print(f"FAIL {exe}: {label}\n  expected {want!r}\n  got {got!r} (exit {r.returncode}) {r.stderr}")
            failures += 1
    return failures


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    failures = check(sys.argv[1], SEMI_CASES) + check(sys.argv[2], NL_CASES)
    print("FAILED" if failures else "all tests passed", f"({failures} failure(s))" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
