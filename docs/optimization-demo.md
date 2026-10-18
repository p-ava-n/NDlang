# Optimization demo: before / after IR

Every listing and timing on this page is real `ndlangc` output, captured by the
`llvm` job in `.github/workflows/ci.yml` (ubuntu-24.04, LLVM 18.1.3) in
[CI run 39102429702](https://github.com/p-ava-n/NDlang/actions/runs/39102429702).
The full files are in [`docs/ir/`](ir/), and every CI run uploads a fresh copy
as the `optimization-demo` artifact.

To reproduce:

```bash
ndlangc examples/02_dot_product.ndlang --emit-ir -O0 -o dot_O0.ll
ndlangc examples/02_dot_product.ndlang --emit-ir -O1 -o dot_O1.ll
ndlangc examples/02_dot_product.ndlang --emit-ir -O2 -o dot_O2.ll
diff -u dot_O0.ll dot_O1.ll
```

## `dot` from `examples/02_dot_product.ndlang`

```
func dot(a: vec<3>, b: vec<3>) -> float { return sum(a * b); }
```

### Before (`-O0`: raw IRBuilder output)

```llvm
define float @dot(<3 x float> %a, <3 x float> %b) {
entry:
  %b.addr = alloca <3 x float>, align 16
  %a.addr = alloca <3 x float>, align 16
  store <3 x float> %a, ptr %a.addr, align 16
  store <3 x float> %b, ptr %b.addr, align 16
  %a1 = load <3 x float>, ptr %a.addr, align 16
  %b2 = load <3 x float>, ptr %b.addr, align 16
  %mul = fmul <3 x float> %a1, %b2
  %0 = call reassoc float @llvm.vector.reduce.fadd.v3f32(float 0.000000e+00, <3 x float> %mul)
  ret float %0

after.ret:                                        ; No predecessors!
  unreachable
}
```

The code generator gives every parameter a stack slot (`alloca` + `store`) and
reloads it at each use. It also opens a new block after every `return`, so
anything after it still has somewhere to go. Here that block is unreachable.

### After (`-O1`: mem2reg, instcombine, simplifycfg, SLP vectorizer)

```llvm
define float @dot(<3 x float> %a, <3 x float> %b) {
entry:
  %mul = fmul <3 x float> %a, %b
  %0 = call reassoc float @llvm.vector.reduce.fadd.v3f32(float 0.000000e+00, <3 x float> %mul)
  ret float %0
}
```

`mem2reg` promotes the two stack slots to SSA values, which deletes the
`alloca`/`store`/`load` traffic. `simplifycfg` drops the unreachable
`after.ret` block. `dot` goes from 10 instructions to 3. `main` goes from 11
to 4, because the vector literals are passed straight to the call as
constants:

```diff
 define i32 @main() {
 entry:
-  %v2 = alloca <3 x float>, align 16
-  %v1 = alloca <3 x float>, align 16
-  store <3 x float> <float 1.000000e+00, float 2.000000e+00, float 3.000000e+00>, ptr %v1, align 16
-  store <3 x float> <float 4.000000e+00, float 5.000000e+00, float 6.000000e+00>, ptr %v2, align 16
-  %v11 = load <3 x float>, ptr %v1, align 16
-  %v22 = load <3 x float>, ptr %v2, align 16
-  %call = call float @dot(<3 x float> %v11, <3 x float> %v22)
+  %call = call float @dot(<3 x float> <float 1.000000e+00, float 2.000000e+00, float 3.000000e+00>, <3 x float> <float 4.000000e+00, float 5.000000e+00, float 6.000000e+00>)
   %0 = fpext float %call to double
-  %1 = call i32 (ptr, ...) @printf(ptr @.fmt, double %0)
+  %1 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.fmt, double %0)
   ret i32 0
-
-after.ret:                                        ; No predecessors!
-  unreachable
 }
```

(Excerpt of [`ir/dot_O0_vs_O1.diff`](ir/dot_O0_vs_O1.diff).)

### Further (`-O2`: LLVM's default pipeline)

`dot` itself stays the same apart from attributes (`tail call`,
`local_unnamed_addr`). The change is in `main`. `dot` is inlined, and
`a * b` is folded at compile time into the constant `<4, 10, 18>`:

```llvm
define noundef i32 @main() local_unnamed_addr #0 {
entry:
  %0 = tail call reassoc float @llvm.vector.reduce.fadd.v3f32(float 0.000000e+00, <3 x float> <float 4.000000e+00, float 1.000000e+01, float 1.800000e+01>)
  %1 = fpext float %0 to double
  %2 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @.fmt, double %1)
  ret i32 0
}
```

LLVM 18 does not constant-fold the `vector.reduce.fadd` intrinsic, so the last
step (4 + 10 + 18 = 32) still happens at run time.

## The benchmark kernel (`benchmarks/bench_dot.ndlang`)

```
func kernel(n: int) -> float {
    let a: vec<4> = [1.0, 2.0, 3.0, 4.0];
    let b: vec<4> = [0.5, 0.25, 0.125, 0.0625];
    let acc: float = 0.0;
    for i in 0..n {
        let s: float = i;
        acc = acc + sum(a * b * (s / 1000000.0 + 1.0));
    }
    return acc;
}
```

At `-O0` the loop body ([`ir/bench_O0.ll`](ir/bench_O0.ll)) reloads `a`, `b`,
`acc`, `i` and `s` from the stack and recomputes `a * b` on every iteration:

```llvm
for.body:                                         ; preds = %for.cond
  %i2 = load i32, ptr %i, align 4
  %widen = sitofp i32 %i2 to float
  store float %widen, ptr %s, align 4
  %acc3 = load float, ptr %acc, align 4
  %a4 = load <4 x float>, ptr %a, align 16
  %b5 = load <4 x float>, ptr %b, align 16
  %mul = fmul <4 x float> %a4, %b5
  %s6 = load float, ptr %s, align 4
  %div = fdiv float %s6, 1.000000e+06
  %add = fadd float %div, 1.000000e+00
  %splat.splatinsert = insertelement <4 x float> poison, float %add, i64 0
  %splat.splat = shufflevector <4 x float> %splat.splatinsert, <4 x float> poison, <4 x i32> zeroinitializer
  %mul7 = fmul <4 x float> %mul, %splat.splat
  %0 = call reassoc float @llvm.vector.reduce.fadd.v4f32(float 0.000000e+00, <4 x float> %mul7)
  %add8 = fadd float %acc3, %0
  store float %add8, ptr %acc, align 4
  br label %for.inc
```

At `-O2` ([`ir/bench_O2.ll`](ir/bench_O2.ll)) these things change:

- **SSA promotion.** `i` and `acc` become `phi` nodes, so the loop body has
  no loads or stores left.
- **Constant folding and LICM.** `a * b` is loop-invariant, so it is computed
  once at compile time as `<0.5, 0.5, 0.375, 0.25>`.
- **Loop unrolling.** `kernel` is unrolled 8× with an epilogue loop for
  `n mod 8`. In `main`, where `kernel` is inlined and the trip count is the
  constant 200 000 000, the loop is unrolled 50×.

One unrolled iteration in `kernel`:

```llvm
for.body:                                         ; preds = %for.body, %for.body.preheader.new
  %i.014 = phi i32 [ 0, %for.body.preheader.new ], [ %i.next.7, %for.body ]
  %acc.013 = phi float [ 0.000000e+00, %for.body.preheader.new ], [ %add8.7, %for.body ]
  %niter = phi i32 [ 0, %for.body.preheader.new ], [ %niter.next.7, %for.body ]
  %widen = sitofp i32 %i.014 to float
  %div = fdiv float %widen, 1.000000e+06
  %add = fadd float %div, 1.000000e+00
  %splat.splatinsert = insertelement <4 x float> poison, float %add, i64 0
  %splat.splat = shufflevector <4 x float> %splat.splatinsert, <4 x float> poison, <4 x i32> zeroinitializer
  %mul7 = fmul <4 x float> %splat.splat, <float 5.000000e-01, float 5.000000e-01, float 3.750000e-01, float 2.500000e-01>
  %1 = tail call reassoc float @llvm.vector.reduce.fadd.v4f32(float 0.000000e+00, <4 x float> %mul7)
  %add8 = fadd float %acc.013, %1
  %i.next = or disjoint i32 %i.014, 1
  ; ... 7 more copies, then:
  %i.next.7 = add nuw nsw i32 %i.014, 8
  %niter.next.7 = add i32 %niter, 8
  %niter.ncmp.7 = icmp eq i32 %niter.next.7, %unroll_iter
  br i1 %niter.ncmp.7, label %for.end.loopexit.unr-lcssa, label %for.body
```

**The loop vectorizer does not fire on this loop.** There is no `vector.body`
block in the `-O2` output. The running sum `acc = acc + ...` is emitted as a
plain `fadd`, with no fast-math flags. Vectorizing it would mean adding the
iterations in a different order, and LLVM will not do that without
`reassoc`. Only the per-iteration `sum()` carries `reassoc`, so the
vectorization in this kernel is the explicit `<4 x float>` arithmetic NDLang
emits for `vec<4>`, not anything the loop vectorizer adds. The equal results
in the table below show that `-O2` kept the strict summation order.

## Benchmark: `-O0` vs `-O2`

`scripts/benchmark.py` compiles `benchmarks/bench_dot.ndlang` at `-O0` and
`-O2` and times each executable (200 million loop iterations):

```bash
python scripts/benchmark.py build/ndlangc --runs 5
```

| Build | Median wall time (5 runs) | Printed result |
|-------|--------------------------:|----------------|
| `-O0` | 0.656 s | `8589934592.000000` |
| `-O2` | 0.275 s | `8589934592.000000` |
| **Speed-up** | **2.38×** | identical |

Measured on a GitHub Actions `ubuntu-24.04` runner: AMD EPYC 9V74, 4 vCPUs,
LLVM 18.1.3 ([`ir/benchmark.txt`](ir/benchmark.txt),
[`ir/machine.txt`](ir/machine.txt)). Timings on shared CI runners vary by
roughly ±10 % from run to run.

The result is 2³³ because `acc` is a 32-bit `float`. Once it reaches 2³³,
the gap between neighbouring floats is 1024. Each per-iteration term is at
most about 330, less than half that gap, so it rounds away and `acc` stops
growing. Both builds print the same value, which confirms that `-O2` kept
the program's floating-point behaviour.

## Pass levels

| Level | Pipeline |
|-------|----------|
| `-O0` | none |
| `-O1` | `mem2reg` (SSA promotion), `instcombine`, `simplifycfg`, `SLPVectorizer`, `instcombine` |
| `-O2` | LLVM's full default `-O2` pipeline (adds inlining, LICM, loop unrolling, the loop vectorizer and more) |

## Second architecture (stretch goal)

Configure with `-DNDLANG_ALL_TARGETS=ON`, then lower the same IR through a
different `TargetMachine`:

```bash
ndlangc examples/02_dot_product.ndlang --target aarch64-linux-gnu -o dot_arm.o
```
