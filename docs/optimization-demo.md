# Optimization demo: before / after IR

`dot` from `examples/02_dot_product.ndlang`:

```
func dot(a: vec<3>, b: vec<3>) -> float { return sum(a * b); }
```

Generate both versions:

```bash
ndlangc examples/02_dot_product.ndlang --emit-ir -O0 -o before.ll
ndlangc examples/02_dot_product.ndlang --emit-ir -O1 -o after.ll
diff before.ll after.ll
```

## Before (`-O0`: raw IRBuilder output)

```llvm
define float @dot(<3 x float> %a, <3 x float> %b) {
entry:
  %a.addr = alloca <3 x float>
  %b.addr = alloca <3 x float>
  store <3 x float> %a, ptr %a.addr
  store <3 x float> %b, ptr %b.addr
  %a1 = load <3 x float>, ptr %a.addr
  %b2 = load <3 x float>, ptr %b.addr
  %m = fmul <3 x float> %a1, %b2
  %0 = call reassoc float @llvm.vector.reduce.fadd.v3f32(float 0.0, <3 x float> %m)
  ret float %0
}
```

## After (`-O1`: mem2reg, instcombine, simplifycfg, SLP vectorizer)

```llvm
define float @dot(<3 x float> %a, <3 x float> %b) {
entry:
  %m = fmul <3 x float> %a, %b
  %0 = call reassoc float @llvm.vector.reduce.fadd.v3f32(float 0.0, <3 x float> %m)
  ret float %0
}
```

Nine instructions become three, and the source did not change. These listings
show the expected shape; exact register names depend on the LLVM version, so
regenerate them with the commands above.

## Pass levels

| Level | Pipeline |
|-------|----------|
| `-O0` | none |
| `-O1` | `mem2reg` (SSA promotion), `instcombine`, `simplifycfg`, `SLPVectorizer`, `instcombine` |
| `-O2` | LLVM's full default `-O2` pipeline (adds the loop vectorizer, inlining and more) |

## Benchmark

`scripts/benchmark.py` compiles `benchmarks/bench_dot.ndlang` at `-O0` and `-O2`
and times the executables:

```bash
python scripts/benchmark.py build/ndlangc
```

## Second architecture (stretch goal)

Configure with `-DNDLANG_ALL_TARGETS=ON`, then lower the same IR through a
different `TargetMachine`:

```bash
ndlangc examples/02_dot_product.ndlang --target aarch64-linux-gnu -o dot_arm.o
```
