# Architecture

```
 .ndlang source
      |
 1. Lexer     src/lexer.l (Flex) + src/Lexer.cpp                  -> tokens
 2. Parser    src/Parser.cpp (hand-written recursive descent)     -> AST
 3. Semantic  src/Sema.cpp                                        -> typed AST
 ---- front end above, back end below -----------------------------------
 4. IR gen    src/CodeGen.cpp (LLVM IRBuilder)                    -> LLVM IR
 5. Optimizer src/CodeGen.cpp (LLVM new pass manager)             -> optimized IR
 6. Backend   src/CodeGen.cpp (TargetMachine)                     -> .o file
 7. Link      src/main.cpp (clang / gcc / cc)                     -> executable
```

Every stage is independently testable: `ndlangc --emit-tokens` and `--emit-ast`
stop after stages 1 and 3, `--emit-ir` after 5, `--emit-obj` after 6.

## What we build vs. what we reuse

Built in this repository:

* the language design (`docs/grammar.ebnf` and the type rules);
* the hand-written recursive-descent parser;
* AST node classes with LLVM-style RTTI (`NodeKind` + `classof()`);
* the scoped symbol table and the semantic analyser / type checker, including
  the vector-dimension checks (no library does this for you);
* the AST to LLVM IR translation (which LLVM calls to emit, and when);
* the Bison grammar file for the expression subgrammar;
* the example `.ndlang` programs.

Reused, not reimplemented:

* **LLVM**: IR, optimization passes (mem2reg, instcombine, SLP and loop
  vectorizers), instruction selection, register allocation and object emission
  via `TargetMachine`; the native `<N x float>` type and the
  `llvm.vector.reduce.fadd` intrinsic; the `IRBuilder` API;
* **Flex** generates the scanner; **Bison** generates the LR parser tables;
* the system linker (via clang or gcc); libc `printf` behind `print`.

## AST and types

`ASTNode` carries a `NodeKind` tag. Each concrete node is its own struct that
defines `classof()`, and `isa<>` / `dyn_cast<>` (in `AST.h`) give LLVM-style
checked downcasts without C++ RTTI. Every `Expr` has a `Type`, empty after
parsing and filled in by semantic analysis. Because `Type` includes the vector
dimension, `vec<3> + vec<4>` fails to type-check.

## Semantic analysis

* **Two-pass resolution.** Pass 1 records every function signature, pass 2
  checks bodies. Forward references and mutual recursion therefore work.
* **Scoped symbol table.** A `vector<unordered_map<string, Type>>`, pushed and
  popped per block.
* **Widening.** Where an `int` meets a `float`, the checker wraps the int
  expression in a `CastExpr`, so code generation never has to guess.

## Code generation

* **Type lowering:** `int` to `i32`, `float` to `float`, `bool` to `i1`,
  `vec<N>` to `<N x float>`.
* **Vector ops are native:** `a * b` is one `fmul <N x float>`; a scalar
  operand is splatted first.
* **Reductions via intrinsic:** `sum(v)` is `llvm.vector.reduce.fadd`, not a loop.
* **SSA via mem2reg:** every variable starts as an `alloca` with loads and
  stores, and `mem2reg` promotes them to registers.

## Reference interpreter

`src/Interpreter.cpp` evaluates the typed AST directly. It is not part of the
compilation pipeline. It exists so the front end can be tested on machines
without LLVM, and so native output can be compared against an independent
implementation (`tests/run_tests.py --mode compile`).
