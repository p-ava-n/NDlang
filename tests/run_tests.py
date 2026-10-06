#!/usr/bin/env python3
"""NDLang end-to-end tests.

  run_tests.py <ndlangc> <repo-root> [--mode interpret|compile]

* examples/*.ndlang  -> output must equal the sibling .expected file.
  `interpret` mode uses the reference interpreter (no LLVM needed);
  `compile` mode builds a native executable through LLVM and runs it.
* tests/errors/*.ndlang -> compilation must fail and stderr must contain the
  text after `// expect-error:` on the file's first line.
"""
import os
import subprocess
import sys
import tempfile


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    mode = "interpret"
    if "--mode" in sys.argv:
        mode = sys.argv[sys.argv.index("--mode") + 1]
        args = [a for a in args if a != mode]
    compiler, root = os.path.abspath(args[0]), os.path.abspath(args[1])
    failures = 0

    ex_dir = os.path.join(root, "examples")
    tmp = tempfile.mkdtemp(prefix="ndlang-test-")
    for name in sorted(os.listdir(ex_dir)):
        if not name.endswith(".ndlang"):
            continue
        src = os.path.join(ex_dir, name)
        expected = open(src[:-7] + ".expected", newline="").read().replace("\r\n", "\n")
        if mode == "compile":
            exe = os.path.join(tmp, name[:-7] + (".exe" if os.name == "nt" else ""))
            r = run([compiler, src, "-o", exe])
            if r.returncode != 0:
                print(f"FAIL {name}: compile error\n{r.stderr}")
                failures += 1
                continue
            r = run([exe])
        else:
            r = run([compiler, src, "--interpret"])
        got = r.stdout.replace("\r\n", "\n")
        if r.returncode != 0 or got != expected:
            print(f"FAIL {name} ({mode})\n--- expected\n{expected}--- got (exit {r.returncode})\n{got}{r.stderr}")
            failures += 1
        else:
            print(f"ok   {name} ({mode})")

    err_dir = os.path.join(root, "tests", "errors")
    for name in sorted(os.listdir(err_dir)):
        if not name.endswith(".ndlang"):
            continue
        src = os.path.join(err_dir, name)
        first = open(src).readline()
        marker = "// expect-error:"
        want = first.split(marker, 1)[1].strip() if marker in first else ""
        r = run([compiler, src, "--emit-ast"])  # stops after semantic analysis
        if r.returncode == 0 or want not in r.stderr:
            print(f"FAIL {name}: expected error containing {want!r}\n{r.stderr}")
            failures += 1
        else:
            print(f"ok   errors/{name}")

    print("FAILED" if failures else "all tests passed", f"({failures} failure(s))" if failures else "")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
