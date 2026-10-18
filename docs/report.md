# NDLang: A Numeric/Vector DSL Compiled to LLVM IR

**Course:** BCSE307P Compiler Design Lab
**Team:** Aryaman Bajaj (24BCE0444), Ashika Rathore (24BCE2398),
Pavan Rangarajan (24BCE2836), Ujjayanta Saha (24BCT0245)
**Repository:** <https://github.com/p-ava-n/NDlang>

Every listing in this report is real output from `ndlangc`, built against
LLVM 18.1.3 on Ubuntu 24.04 (WSL2 on an AMD Ryzen 5 7530U, and the GitHub
Actions `ubuntu-24.04` runner). None of it is hand-written or "illustrative".

---

## 1. Scope and rationale

NDLang is a small, statically typed language with **first-class fixed-size
vectors**, compiled end to end: lexer, parser, AST, semantic analysis, LLVM
IR, optimization, object code, and finally a linked executable.

The scope was chosen for a 1-credit lab (30 lab hours):

* **Fixed-size vectors** (`vec<N>`, with `N` known at compile time) need no
  dynamic memory, generics or garbage collection.
* `vec<N>` lowers directly to LLVM's native `<N x float>` type, so vector
  arithmetic becomes real SIMD instructions. That gives the project a
  concrete optimization story instead of a toy calculator.
* Every phase of a compiler is present and testable on its own, which is
  what the lab experiments assess.

## 2. Language design

### 2.1 Types

| Type | Meaning | LLVM type |
|------|---------|-----------|
| `int` | 32-bit signed integer | `i32` |
| `float` | 32-bit float | `float` |
| `bool` | truth value | `i1` |
| `vec<N>` | `N` floats, `N` fixed at compile time | `<N x float>` |

`vec<3>` and `vec<4>` are different types. **Conversion rule** (decided once,
applied everywhere): `int` widens to `float` implicitly, in assignments,
arguments, return values, mixed arithmetic and vector-literal elements.
Nothing ever narrows, so `let x: int = 3.5;` is a compile-time error.

### 2.2 Example program

```
func dot(a: vec<3>, b: vec<3>) -> float {
    return sum(a * b);
}

func main() -> int {
    let v1: vec<3> = [1.0, 2.0, 3.0];
    let v2: vec<3> = [4.0, 5.0, 6.0];
    print(dot(v1, v2));        // 32.000000
    return 0;
}
```

Builtins: `sum(v)` (lowers to `llvm.vector.reduce.fadd`), `dot(a, b)`, and
`print(x)` for `int`, `float`, `bool` and `vec`, a thin wrapper over libc
`printf` and the language's only runtime. A program may define its own `dot`,
as the example above does. That definition then takes precedence over the
builtin.

### 2.3 Grammar

The final grammar is in [`grammar.ebnf`](grammar.ebnf). The recursive-descent
parser has one function per rule. The expression part, from lowest to highest
precedence:

```
expr     ::= orExpr ;
orExpr   ::= andExpr { "||" andExpr } ;
andExpr  ::= eqExpr  { "&&" eqExpr } ;
eqExpr   ::= relExpr { ( "==" | "!=" ) relExpr } ;
relExpr  ::= addExpr { ( "<" | ">" | "<=" | ">=" ) addExpr } ;
addExpr  ::= term    { ( "+" | "-" ) term } ;
term     ::= unary   { ( "*" | "/" ) unary } ;
unary    ::= ( "-" | "!" ) unary | postfix ;
postfix  ::= primary { "[" expr "]" } ;
primary  ::= INT | FLOAT | "true" | "false"
           | IDENT [ "(" [ expr { "," expr } ] ")" ]
           | "(" expr ")" | vecLit ;
vecLit   ::= "[" expr { "," expr } "]" ;
```

**Changes from the proposal grammar**, each made on purpose:

| Proposal | Final | Why |
|----------|-------|-----|
| `if "(" expr ")" block` | `if expr block` | The braces already delimit the condition. `if (x > 0) { ... }` still parses, because `(x > 0)` is a parenthesised expression. |
| `for "(" IDENT in a..b ")"` | `for IDENT in a..b block` | Same reason |
| `assignExpr ::= IDENT "=" assignExpr` | assignment is a statement | Avoids `if x = 1` mistakes and keeps every expression side-effect free, apart from calls |
| `letStmt` always typed | `let x = 1.5;` is allowed too | The type is inferred from the initialiser |
| (none) | `v[i]` indexing, `true`/`false`, `else if`, nested `{ }` blocks, `void` functions | Needed for realistic example programs |
| `mat<R,C>` | not implemented | Stretch goal (§13) |

## 3. Architecture and the lab experiments

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

The driver can stop after any stage: `--emit-tokens`, `--emit-parse`
(untyped AST), `--emit-ast` (typed AST), `--emit-ir`, `--emit-asm`,
`--emit-obj`, or a full build with `-o`.

| # | Lab experiment | Where in NDLang |
|---|----------------|-----------------|
| 1 | Lexer with LLVM | `src/lexer.l`, `src/Lexer.cpp` (§4) |
| 2 | Handwritten parser with LLVM | `src/Parser.cpp` (§5) |
| 3 | Codegen with LLVM backend | `src/CodeGen.cpp` (§8) |
| 4 | Defining a real language | §2, [`language.md`](language.md), [`grammar.ebnf`](grammar.ebnf) |
| 5 | Recursive descent parser for a CFG | `src/Parser.cpp`, one function per grammar rule (§5) |
| 6 | LR parser for the CFG | `tools/bison_expr/expr.y` (§10) |
| 7 | Flex & Bison intro, scanner/parser modification | `labs/exp7_flex_bison/` (§11) |
| 8 | LLVM-style RTTI for the AST, IR from the AST | `include/ndlang/AST.h` (§6), `src/CodeGen.cpp` |
| 9 | AST types to LLVM types | `CodeGen::Impl::lower` (§8.1) |
| 10 | Emitting object code | `CodeGen::Impl::emitObject` (§9) |

**Built here, versus reused.** We wrote the language design, the parser, the
AST and RTTI, the symbol table and type checker, the AST-to-IR translation,
the Bison grammars and the test suite. We reused LLVM (the IR, the optimization
passes, instruction selection, register allocation, object emission), Flex
and Bison (the generated scanner and parse tables), and the system linker.

## 4. Lexical analysis (Flex)

`src/lexer.l` describes the tokens: 13 keywords, identifiers, integer and
float literals, `//` comments, and 25 operators and punctuation marks,
including multi-character ones (`->`, `..`, `<=`, `>=`, `==`, `!=`, `&&`,
`||`). A `YY_USER_ACTION` macro records each token's line and column for
diagnostics. `src/Lexer.cpp` wraps the Flex scanner in a function returning
a `std::vector<Token>`.

`ndlangc examples/02_dot_product.ndlang --emit-tokens` (excerpt). Line 7 of
the program becomes 16 tokens:

```
7:5   KW_LET     let
7:9   IDENT      v1
7:11  COLON      :
7:13  KW_VEC     vec
7:16  LT         <
7:17  INT_LIT    3
7:18  GT         >
7:20  ASSIGN     =
7:22  LBRACKET   [
7:23  FLOAT_LIT  1.0
7:26  COMMA      ,
7:28  FLOAT_LIT  2.0
7:31  COMMA      ,
7:33  FLOAT_LIT  3.0
7:36  RBRACKET   ]
7:37  SEMI       ;
```

One lexical subtlety: `>=` is a token, so `vec<3>=` scans as `vec < 3 >=`.
The language reference tells users to write `vec<3> =` with a space. The
same problem exists in C++ with `>>`.

## 5. Syntax analysis (recursive descent)

`src/Parser.cpp` is a hand-written recursive-descent parser with one function
per grammar rule (`parseProgram`, `parseFunc`, `parseType`, `parseBlock`,
`parseStmt`, `parseLet`, `parseIf`, `parseFor`, `parseOr` ... `parseTerm`,
`parseUnary`, `parsePostfix`, `parsePrimary`, `parseVecLit`).

* **Precedence is implicit.** `parseAdd` calls `parseTerm`, which calls
  `parseUnary`, so `*` binds tighter than `+` without any table.
  Left-associativity comes from the `{ ... }` loops.
* **Lookahead.** One token is enough everywhere except one place: telling
  `x = e;` apart from an expression statement that starts with `x` needs a
  second token, `peekAt(1)`.
* **Errors** are specific to the rule that failed, and stop at the first one:

```
tests/errors/syntax.ndlang:4:5: error: expected ';', found 'return'
```

## 6. AST design and LLVM-style RTTI

Every node derives from `ASTNode`, which carries a `NodeKind` tag and a
source position. Each concrete node defines `static bool classof(const
ASTNode *)`. The templates `isa<T>` and `dyn_cast<T>` in `AST.h` give checked
downcasts the way LLVM does, without C++ RTTI:

```cpp
enum class NodeKind {
  // expressions
  NumberLit, BoolLit, VecLit, VarRef, UnaryOp, BinaryOp, CallExpr, IndexExpr,
  CastExpr,
  // statements
  LetStmt, AssignStmt, IfStmt, ForStmt, ReturnStmt, ExprStmt, BlockStmt,
  // declarations
  FuncDecl, Program
};

struct Expr : ASTNode {
  Type type;                       // filled in by semantic analysis
  static bool classof(const ASTNode *n) {
    return n->kind >= NodeKind::NumberLit && n->kind <= NodeKind::CastExpr;
  }
};

struct BinaryOp : Expr {
  BinOp op;
  ExprPtr lhs, rhs;
  static bool classof(const ASTNode *n) { return n->kind == NodeKind::BinaryOp; }
};
```

The enum is ordered so that the abstract classes `Expr` and `Stmt` can test
membership with a single range check. Every `Expr` holds a `Type`, which is
empty after parsing and resolved by semantic analysis. Because a `Type`
includes the vector dimension, `vec<3> + vec<4>` is caught at compile time.

The typed AST for the dot-product program (`--emit-ast`):

```
Program
  FuncDecl: dot(a: vec<3>, b: vec<3>) -> float
    Block
      ReturnStmt
        CallExpr: sum [builtin] : float
          BinaryOp: * : vec<3>
            VarRef: a : vec<3>
            VarRef: b : vec<3>
  FuncDecl: main() -> int
    Block
      LetStmt: v1 : vec<3>
        VecLit : vec<3>
          NumberLit: 1.000000 : float
          ...
      ExprStmt
        CallExpr: print [builtin] : void
          CallExpr: dot : float
            VarRef: v1 : vec<3>
            VarRef: v2 : vec<3>
      ReturnStmt
        NumberLit: 0 : int
```

## 7. Semantic analysis and the symbol table

`src/Sema.cpp` walks the AST and annotates it.

* **Two passes.** Pass 1 collects every function signature. Pass 2 checks the
  bodies. Calls can therefore come before definitions, and mutual recursion
  works (`is_even` and `is_odd` in `examples/03_control_flow.ndlang`).
* **Scoped symbol table:** `std::vector<std::unordered_map<std::string, Type>>`,
  pushed on block entry and popped on exit. Shadowing in an inner block is
  allowed. Redeclaring a name in the same block is an error.
* **Widening is made explicit.** Where an `int` meets a `float`, the checker
  wraps the `int` in a `CastExpr`. Code generation never has to work out a
  conversion. It just emits `sitofp` for a `CastExpr`:

```
LetStmt: b : vec<4>
  VecLit : vec<4>
    CastExpr (int -> float) : float
      NumberLit: 10 : int
```

**Checks implemented**, each with a test in `tests/errors/` that must fail
with exactly this message:

| Check | Diagnostic (real output) |
|-------|--------------------------|
| vector dimension in binary ops | `5:13: error: vector dimension mismatch in '+': vec<3> and vec<4>` |
| argument types, including dimensions | `6:16: error: argument 1 of 'norm': expected vec<3>, got vec<4>` |
| argument count | `6:11: error: function 'add' expects 2 argument(s), got 1` |
| no narrowing | `3:18: error: initialiser of 'x': expected int, got float (float -> int would narrow; conversions never narrow)` |
| condition types | `3:8: error: 'if' condition must be bool, got int` |
| constant index bounds | `4:12: error: index 3 is out of range for vec<3>` |
| every path returns | `2:1: error: function 'f' may reach its end without returning a value of type int` |
| scoping | `6:12: error: use of undeclared variable 'inner'` |
| undeclared names | `3:11: error: use of undeclared variable 'x'` |
| entry point | `1:1: error: program has no 'main' function` |

Unlike syntax errors, semantic errors do not stop at the first one: all of
them are collected and reported. The checker also rejects redefining a
builtin, invalid operand types (`vec + float` is rejected, while `vec * float`
broadcasts the scalar), integer literals wider than 32 bits, and printing a `void`
value or initialising a variable with one.

## 8. LLVM IR generation

`src/CodeGen.cpp` translates the typed AST with `IRBuilder`.

### 8.1 Type lowering

```cpp
llvm::Type *lower(const ndlang::Type &t) {
  switch (t.kind) {
  case ndlang::Type::Int:   return b.getInt32Ty();
  case ndlang::Type::Float: return b.getFloatTy();
  case ndlang::Type::Bool:  return b.getInt1Ty();
  case ndlang::Type::Vec:   return FixedVectorType::get(b.getFloatTy(), t.dim);
  default:                  return b.getVoidTy();
  }
}
```

### 8.2 How each construct is lowered

| NDLang | LLVM IR |
|--------|---------|
| variable, parameter | `alloca` in the entry block, then `store`/`load` (promoted to SSA by `mem2reg`) |
| `vec + vec`, `-`, `*`, `/` | one `fadd`/`fsub`/`fmul`/`fdiv` on `<N x float>` |
| `vec * scalar` | the scalar is splatted (`insertelement` + `shufflevector`), then one vector op |
| `[a, b, c]` | a chain of `insertelement` |
| `v[i]` | `extractelement` |
| `sum(v)` | `call reassoc float @llvm.vector.reduce.fadd` |
| `dot(a, b)` | `fmul <N x float>` + `llvm.vector.reduce.fadd` |
| `&&`, `||` | short-circuit branches merged with a `phi` |
| `if`/`else`, `for` | basic blocks (`if.then`/`if.else`/`if.end`, `for.cond`/`for.body`/`for.inc`/`for.end`) |
| `print(x)` | `printf` with a format chosen from the static type (floats `fpext` to `double` for varargs) |
| implicit widening | `sitofp` (from the `CastExpr` Sema inserted) |

`reassoc` on the reduction tells LLVM it may add the lanes in any order. The
backend can then use a tree reduction instead of a strictly sequential one.

`lerp` (`examples/05_lerp.ndlang`) at `-O0`. The source line is
`return a + (b - a) * t;`, a vector expression with a broadcast scalar:

```llvm
define <3 x float> @lerp(<3 x float> %a, <3 x float> %b, float %t) {
entry:
  %t.addr = alloca float, align 4
  %b.addr = alloca <3 x float>, align 16
  %a.addr = alloca <3 x float>, align 16
  store <3 x float> %a, ptr %a.addr, align 16
  store <3 x float> %b, ptr %b.addr, align 16
  store float %t, ptr %t.addr, align 4
  %a1 = load <3 x float>, ptr %a.addr, align 16
  %b2 = load <3 x float>, ptr %b.addr, align 16
  %a3 = load <3 x float>, ptr %a.addr, align 16
  %sub = fsub <3 x float> %b2, %a3
  %t4 = load float, ptr %t.addr, align 4
  %splat.splatinsert = insertelement <3 x float> poison, float %t4, i64 0
  %splat.splat = shufflevector <3 x float> %splat.splatinsert, <3 x float> poison, <3 x i32> zeroinitializer
  %mul = fmul <3 x float> %sub, %splat.splat
  %add = fadd <3 x float> %a1, %mul
  ret <3 x float> %add

after.ret:                                        ; No predecessors!
  unreachable
}
```

The code generator opens a fresh block after every `return`, so any statements
that follow still have somewhere to be emitted. That block is unreachable and
`simplifycfg` deletes it. The module is checked with `verifyModule` after
generation and again after optimization.

## 9. Optimization, object code and linking

### 9.1 Pass pipelines

| Flag | Pipeline |
|------|----------|
| `-O0` | none (raw `IRBuilder` output) |
| `-O1` (default) | `mem2reg`, `instcombine`, `simplifycfg`, `SLPVectorizer`, `instcombine`, spelled out in `CodeGen::Impl::optimize` |
| `-O2` | LLVM's default `-O2` pipeline (`PassBuilder::buildPerModuleDefaultPipeline`) |

### 9.2 Before and after

`sum_to` from `examples/03_control_flow.ndlang` shows `mem2reg` most clearly.
At `-O0`, every variable lives in memory:

```llvm
define i32 @sum_to(i32 %n) {
entry:
  %i = alloca i32, align 4
  %total = alloca i32, align 4
  %n.addr = alloca i32, align 4
  store i32 %n, ptr %n.addr, align 4
  store i32 0, ptr %total, align 4
  %n1 = load i32, ptr %n.addr, align 4
  %add = add i32 %n1, 1
  store i32 1, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %i.cur = load i32, ptr %i, align 4
  %for.cmp = icmp slt i32 %i.cur, %add
  br i1 %for.cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %total2 = load i32, ptr %total, align 4
  %i3 = load i32, ptr %i, align 4
  %add4 = add i32 %total2, %i3
  store i32 %add4, ptr %total, align 4
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %i.old = load i32, ptr %i, align 4
  %i.next = add nsw i32 %i.old, 1
  store i32 %i.next, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %total5 = load i32, ptr %total, align 4
  ret i32 %total5

after.ret:                                        ; No predecessors!
  unreachable
}
```

At `-O1`, the stack slots are gone. `total` and `i` became SSA values merged by
`phi` nodes, the empty `for.body` block was folded into `for.inc`, and the
unreachable block was removed:

```llvm
define i32 @sum_to(i32 %n) {
entry:
  %add = add i32 %n, 1
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %total.0 = phi i32 [ 0, %entry ], [ %add4, %for.inc ]
  %i.0 = phi i32 [ 1, %entry ], [ %i.next, %for.inc ]
  %for.cmp = icmp slt i32 %i.0, %add
  br i1 %for.cmp, label %for.inc, label %for.end

for.inc:                                          ; preds = %for.cond
  %add4 = add i32 %total.0, %i.0
  %i.next = add nuw nsw i32 %i.0, 1
  br label %for.cond

for.end:                                          ; preds = %for.cond
  ret i32 %total.0
}
```

The dot product goes from 10 instructions to 3, and `-O2` additionally
inlines `dot` into `main` and constant-folds `a * b`. The full listings, the
`-O2` loop-unrolling analysis of the benchmark kernel, and an explanation of
why the loop vectorizer does *not* fire on it (the scalar accumulator has no
`reassoc` flag) are in [`optimization-demo.md`](optimization-demo.md). The
raw IR files are in [`ir/`](ir/).

### 9.3 Benchmark

`scripts/benchmark.py` compiles `benchmarks/bench_dot.ndlang` (200 million
`vec<4>` multiply-reduce iterations) at `-O0` and `-O2` and reports the median
of 5 runs:

| Machine | `-O0` | `-O2` | Speed-up |
|---------|------:|------:|---------:|
| AMD Ryzen 5 7530U, WSL2 Ubuntu 24.04 (team laptop) | 0.563 s | 0.241 s | **2.34×** |
| AMD EPYC 9V74, GitHub Actions `ubuntu-24.04` | 0.656 s | 0.275 s | **2.38×** |

Both builds print the same result (`8589934592.000000`), so `-O2` preserved
the program's floating-point behaviour.

### 9.4 Object code

`CodeGen::Impl::emitObject` creates a `TargetMachine` for the host triple and
CPU and runs `addPassesToEmitFile(..., CodeGenFileType::ObjectFile)`.
`main.cpp` then links the `.o` with `clang`, `gcc` or `cc`, whichever is
found first, to get an executable:

```
$ ndlangc examples/02_dot_product.ndlang -o dot
$ ./dot
32.000000
$ file dot
dot: ELF 64-bit LSB pie executable, x86-64, ... dynamically linked
```

The disassembly of `dot` at `-O2` (`ndlangc --emit-obj -O2`, then
`objdump -d`) on the Ryzen laptop:

```
0000000000000000 <dot>:
   0:  vmulps    %xmm1,%xmm0,%xmm0        ; a * b, all lanes in one instruction
   4:  vmovshdup %xmm0,%xmm1              ; \
   8:  vaddss    %xmm1,%xmm0,%xmm1        ;  | horizontal reduction of the
   c:  vshufpd   $0x1,%xmm0,%xmm0,%xmm0   ;  | three products (reassoc lets
  11:  vaddss    %xmm0,%xmm1,%xmm0        ; /  LLVM pair them up as a tree)
  15:  vxorps    %xmm1,%xmm1,%xmm1        ; \  + 0.0: the reduction's start
  19:  vaddss    %xmm1,%xmm0,%xmm0        ; /  value
  1d:  ret
```

The multiply is one SIMD instruction (`vmulps`, AVX encoding because the
Ryzen supports it), and the reduction is done in registers without a loop.
The last two instructions add the reduction's start value of `+0.0`. LLVM
cannot drop that add, because `-0.0 + 0.0 = +0.0`: dropping it would change
the sign of a negative-zero result. A start value of `-0.0`, which is an exact
identity for addition, or the `nsz` fast-math flag would remove it. This is a
one-line improvement we found while writing this report (§13).

### 9.5 A second architecture (stretch goal, done)

Built with `-DNDLANG_ALL_TARGETS=ON`, the same IR can be lowered through an
AArch64 `TargetMachine`:

```
$ ndlangc examples/02_dot_product.ndlang --target aarch64-linux-gnu -O2 -o dot_arm.o
$ llvm-objdump -d dot_arm.o
0000000000000000 <dot>:
       0: movi    v2.2s, #0x80, lsl #24     ; -0.0, used to pad lane 3
       4: fmul    v0.4s, v0.4s, v1.4s       ; NEON: a * b
       8: movi    d1, #0000000000000000     ; the +0.0 start value
       c: mov     v0.s[3], v2.s[0]          ; vec<3> occupies a 4-lane register
      10: faddp   v0.4s, v0.4s, v0.4s       ; pairwise add
      14: faddp   s0, v0.2s                 ; pairwise add
      18: fadd    s0, s0, s1                ; + 0.0
      1c: ret
```

The front end, the IR and the optimizer are identical for both targets. Only
the `TargetMachine` changes. The AArch64 code shows how `<3 x float>` is
handled on a 4-lane machine: lane 3 is filled with `-0.0` so that the
pairwise adds are exact.

While checking this we found and fixed a build bug. `CMakeLists.txt` asked for
the LLVM component `all-targets`, a name `llvm-config` understands but CMake's
`llvm_map_components_to_libnames` does not. The `NDLANG_ALL_TARGETS=ON` build
therefore failed to link. It now requests `AllTargetsCodeGens`,
`AllTargetsDescs` and `AllTargetsInfos`, and CI builds this configuration
and cross-compiles on every push.

## 10. LR parsing with Bison (Experiment 6)

As planned, Bison covers only the **expression subgrammar**: numbers, vector
literals, `+ - * /`, unary minus and parentheses. `tools/bison_expr/expr.y`
builds an evaluator, `ndlang-bison-expr`:

```
$ ndlang-bison-expr "[1,2,3]*2+[4,5,6]"
[6.000000, 9.000000, 12.000000]
$ ndlang-bison-expr "[1,2,3]+[1,2]"
error: vector dimension mismatch
```

| | Recursive descent | Bison LALR(1) |
|---|---|---|
| Precedence | implicit in the call structure | `%left '+' '-'`, `%left '*' '/'`, `%precedence UMINUS` |
| Grammar rules / states | none (the code is the parser) | 12 rules, 24 states, `YYLAST` 28 |
| Conflicts with precedence declarations | n/a | 0 |
| Conflicts without the `%left` lines | n/a | 20 shift/reduce |
| Adding `[a, b, c]` | one `case LBRACKET:` in `parsePrimary` | two new rules, tables regenerated, no new conflicts |
| Error messages | tailored per rule | generic unless `parse.error verbose` |

The 20 conflicts are the classic `expr '+' expr . '*'` ambiguities: at that
point the parser cannot tell whether to reduce or shift. Sixteen come from the
four binary rules and four from unary minus. Recursive descent
avoids them by building precedence into the shape of the functions, while
LR makes you declare it. Full write-up:
[`parsing-comparison.md`](parsing-comparison.md).

## 11. Experiment 7: Flex & Bison warm-up

`labs/exp7_flex_bison/` holds a separate statement calculator built twice,
once with statements ending in `;` (`calc_semi`) and once ending in a newline
(`calc_nl`). The change touches exactly two places:

* **Scanner:** `\n` is removed from the whitespace rule and becomes an `EOL`
  token.
* **Parser:** `EOL` replaces `';'` in both statement rules, and a new
  `stmt : EOL` rule allows blank lines.

The expression grammar and its precedence declarations are untouched. The
terminator is a statement-level concern, so the change stays at statement
level. The visible effect on the language is that `calc_semi` allows a
statement to span lines and several statements per line, while `calc_nl`
allows neither. A ctest (`exp7_terminator`) checks both behaviours. Details:
[`labs/exp7_flex_bison/README.md`](../labs/exp7_flex_bison/README.md).

## 12. Testing

`ctest` runs five suites, both locally and in GitHub Actions CI:

| Test | What it checks |
|------|----------------|
| `unit` | lexer, parser and semantic analysis in isolation (`tests/unit_tests.cpp`) |
| `examples_interpreted` | the 5 example programs, run by the reference interpreter, match their `.expected` output; the 11 programs in `tests/errors/` fail with the expected message |
| `examples_compiled` | the same 5 programs compiled to native executables through LLVM, run, and compared with the same `.expected` files |
| `bison_expr` | the Bison expression evaluator |
| `exp7_terminator` | both Experiment 7 calculators |

The **reference interpreter** (`src/Interpreter.cpp`) evaluates the typed AST
directly. It is not part of the compiler. It gives an independent second
implementation of the language's semantics, so a code-generation bug shows up
as a mismatch between the interpreted and the natively compiled output. It
also lets the front end be tested on machines without LLVM: CI has a separate
job that builds with `-DNDLANG_WITH_LLVM=OFF`.

The example programs:

| Program | Demonstrates |
|---------|--------------|
| `01_scalars` | `int`/`float`, implicit widening, integer division, recursion (`fact`), `&&` |
| `02_dot_product` | the slide example: `vec<3>`, a user-defined `dot`, `sum` |
| `03_control_flow` | `if` / `else if` / `else`, `for`, mutual recursion (`is_even`/`is_odd`) |
| `04_vector_ops` | elementwise `+ - * /`, scalar broadcast, unary minus, indexing, `sum`, builtin `dot` |
| `05_lerp` | a vector-valued function called in a loop |

Final local result:

```
1/5 Test #1: unit .............................   Passed
2/5 Test #2: examples_interpreted .............   Passed
3/5 Test #3: examples_compiled ................   Passed
4/5 Test #4: bison_expr .......................   Passed
5/5 Test #5: exp7_terminator ..................   Passed
100% tests passed, 0 tests failed out of 5
```

## 13. Results, limitations and future work

**Deliverables (spec §10):**

| Deliverable | Status |
|-------------|--------|
| Grammar (final EBNF) | done: [`grammar.ebnf`](grammar.ebnf) |
| Lexer and recursive-descent parser | done |
| AST with RTTI-style tagging | done |
| Symbol table and type checker | done |
| LLVM IR generator | done |
| Bison LR parser for the expression subgrammar, with comparison | done |
| Object file emission and linked executable | done |
| Optimization before/after IR diff and benchmark | done: §9, [`optimization-demo.md`](optimization-demo.md) |
| 3–5 example programs | done: 5 examples, plus 11 programs that must fail to compile |
| Report | this document |

**Stretch goals:**

| Goal | Status |
|------|--------|
| Second backend target | done (§9.5) |
| `mat<R,C>` and `matmul` | not implemented |
| A hand-written constant-folding pass | not implemented. `instcombine` already folds constants, as the `-O2` output of `main` shows |
| ORC JIT REPL | not implemented. `--interpret` gives a no-LLVM way to run programs |

**Known limitations:**

* Vectors hold only `float`. There is no `vec<N>` of `int`.
* Index bounds are checked only for constant indices. A variable index out
  of range is undefined behaviour, as `extractelement` is in LLVM.
* Vector equality (`a == b`) is not defined.
* The reduction's `+0.0` start value costs one instruction (§9.4). Starting
  from `-0.0` would remove it without changing any result.
* The benchmark's accumulator saturates at 2³³ in single precision (explained
  in `optimization-demo.md`), so the benchmark measures throughput, not a
  meaningful sum.

## 14. Building and running

Requirements: a C++17 compiler, CMake 3.16+, Flex, Bison, LLVM 17+ with its
CMake package, and `clang` or `gcc` for linking. On Windows, use WSL with
Ubuntu 24.04:

```bash
sudo apt-get install -y cmake ninja-build flex bison g++ python3 clang llvm-18-dev libzstd-dev zlib1g-dev
cmake -S . -B build -G Ninja -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build build
ctest --test-dir build --output-on-failure
./build/ndlangc examples/02_dot_product.ndlang -o dot && ./dot
```

Before/after IR: `ndlangc <file> --emit-ir -O0` versus `-O1` or `-O2`.

**Explorer.** `node web/server.js` serves a browser front end at
<http://localhost:8090> ([`web/README.md`](../web/README.md)). It runs a
program, typed in or loaded from `examples/`, through the seven stages one
`ndlangc` call at a time, showing which stage is running. Each stage is shown
as an input window next to an output window: the optimizer stage shows `-O0`
beside `-O1` or `-O2` with aligned rows and a diff. Every run is kept, so
earlier stages and earlier programs can be reviewed again.
