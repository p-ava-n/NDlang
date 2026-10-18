# NDLang

A small, statically typed **numeric / vector DSL** compiled to **LLVM IR** and
on to native code.

NDLang's one special feature is first-class fixed-size vectors. `vec<N>` lowers
directly to LLVM's native `<N x float>` type, so vector arithmetic becomes real
SIMD instructions and LLVM's own optimizer vectorizes it for free. That gives a
concrete before/after IR story: *what does an optimizer actually do?*

```
func dot(a: vec<3>, b: vec<3>) -> float {
    return sum(a * b);
}

func main() -> int {
    let v1: vec<3> = [1.0, 2.0, 3.0];
    let v2: vec<3> = [4.0, 5.0, 6.0];
    print(dot(v1, v2));
    return 0;
}
```

```
$ ndlangc examples/02_dot_product.ndlang -o dot
$ ./dot
32.000000
```

## The pipeline

```
Source -> Lexer -> Parser -> Semantic -> LLVM IR -> Optimizer -> Backend -> executable
          Flex     recursive  scopes,     IRBuilder  mem2reg,     TargetMachine
                   descent    types,                 SLP/loop    + system linker
                              dim checks             vectorizer
```

Stages 1-3 are the front end (understand and check the program); stages 4-7
are the back end (lower it to fast native code). See
[docs/architecture.md](docs/architecture.md).

## Repository layout

| Path | Contents |
|------|----------|
| `src/lexer.l`, `src/Lexer.cpp` | Flex scanner and its C++ wrapper |
| `src/Parser.cpp` | hand-written recursive-descent parser |
| `include/ndlang/AST.h` | AST node classes with LLVM-style RTTI (`classof`) |
| `src/Sema.cpp` | scoped symbol table, two-pass resolution, type and dimension checking |
| `src/CodeGen.cpp` | AST to LLVM IR, optimization pipeline, object emission |
| `src/Interpreter.cpp` | reference evaluator, used for tests and `--interpret` |
| `src/main.cpp` | the `ndlangc` driver |
| `tools/bison_expr/` | Bison/LR parser for the expression grammar (parsing comparison) |
| `labs/exp7_flex_bison/` | Experiment 7 warm-up: one calculator, `;` vs newline statement terminator |
| `web/` | browser explorer: step through the pipeline stage by stage (Node, no dependencies) |
| `examples/` | example programs with expected output |
| `tests/` | unit tests, end-to-end tests, programs that must fail to compile |
| `benchmarks/`, `scripts/` | benchmark kernel and runner |
| `docs/` | **[project report](docs/report.md)**, language reference, grammar (EBNF), architecture, parser comparison, optimization demo with real IR (`docs/ir/`) |

## Building

Requirements: a C++17 compiler, CMake 3.16+, Flex and Bison, and (for the
backend) **LLVM 17 or newer** with its CMake package, plus `clang` or `gcc` on
`PATH` for linking.

```bash
cmake -S . -B build -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm
cmake --build build
ctest --test-dir build --output-on-failure
```

Without LLVM, CMake still builds the front end, the interpreter, the tests and
the Bison tool; only code generation is unavailable:

```bash
cmake -S . -B build -DNDLANG_WITH_LLVM=OFF
```

### On Windows: use WSL

The easiest setup on Windows is WSL with Ubuntu 24.04, the same environment
as CI. Inside the WSL shell:

```bash
sudo apt-get install -y cmake ninja-build flex bison g++ python3 clang llvm-18-dev libzstd-dev zlib1g-dev
cd "/mnt/c/path/to/ndlang"
cmake -S . -B ~/ndlang-build -G Ninja -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build ~/ndlang-build
ctest --test-dir ~/ndlang-build --output-on-failure
~/ndlang-build/ndlangc examples/02_dot_product.ndlang -o /tmp/dot && /tmp/dot
```

Putting the build directory in the Linux home (`~/ndlang-build`) rather than
under `/mnt/c` keeps builds fast. For `--target` cross-compilation, configure
a second build with `-DNDLANG_ALL_TARGETS=ON`.

A native Windows build also needs LLVM built for the same compiler ABI (for
example from MSYS2's `mingw-w64-ucrt-x86_64-llvm`). Flex and Bison are
available as `winget install WinFlexBison.win_flex_bison`, and CMake detects
`win_flex` / `win_bison`. The old GnuWin32 Flex 2.5.4 is too old for
`src/lexer.l`.

## Using the compiler

```
ndlangc <input.ndlang> [options]

  -o <file>         output file
  --emit-tokens     print the token stream
  --emit-parse      print the AST straight from the parser (no types)
  --emit-ast        print the type-checked AST
  --emit-ir         print LLVM IR
  --emit-asm        print target assembly
  --emit-obj        write an object file, do not link
  --interpret       run with the reference interpreter (no LLVM needed)
  -O0 | -O1 | -O2   none | slide pipeline (default) | LLVM -O2
  --target <triple> cross-compile (build with -DNDLANG_ALL_TARGETS=ON)
```

See before/after IR with `--emit-ir -O0` versus `--emit-ir -O1`
([docs/optimization-demo.md](docs/optimization-demo.md)).

## Explorer (browser front end)

`web/` holds a small black-and-white terminal-style web app that runs a
program through every stage of `ndlangc`. It shows which stage is running and
each stage's input and output side by side, including an aligned
before/after diff for the optimizer. Every stage of the latest compile is
kept, so you can go back and review earlier steps. With `ndlangc` built and
Node 18+ installed:

```bash
node web/server.js        # then open http://localhost:8090
```

See [web/README.md](web/README.md).

## Language at a glance

Types: `int` (i32), `float`, `bool`, `vec<N>`; `int` widens to `float`
implicitly and never narrows. Functions, `let`, assignment, `if` / `else`,
`for i in a..b`, `return`, recursion, elementwise vector operators, scalar
broadcast, indexing, and the builtins `sum`, `dot` and `print`. Full reference:
[docs/language.md](docs/language.md). Grammar: [docs/grammar.ebnf](docs/grammar.ebnf).

## Status

| Feature | State |
|---------|-------|
| Lexer, recursive-descent parser, AST, semantic analysis | done, covered by unit and end-to-end tests |
| LLVM IR generation, optimization, object emission, linking | done; the compiled-example tests check native output against the interpreter's |
| Optimization before/after IR and benchmark | done, real output in [docs/optimization-demo.md](docs/optimization-demo.md) |
| Bison/LR expression parser and comparison writeup | done |
| Experiment 7 Flex/Bison warm-up | done, in [labs/exp7_flex_bison/](labs/exp7_flex_bison/) |
| Project report | done: [docs/report.md](docs/report.md) |
| Second target architecture (stretch) | done: `--target aarch64-linux-gnu` with `-DNDLANG_ALL_TARGETS=ON`, checked in CI |
| `mat<R,C>`, matmul, custom constant-folding pass, ORC JIT REPL | stretch goals, not implemented |

## Team

Aryaman Bajaj, Ashika Rathore, Pavan Rangarajan, Ujjayanta Saha.

## License

MIT, see [LICENSE](LICENSE).
