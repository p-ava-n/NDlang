; ModuleID = '../benchmarks/bench_dot.ndlang'
source_filename = "../benchmarks/bench_dot.ndlang"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.fmt = private unnamed_addr constant [4 x i8] c"%f\0A\00", align 1

declare i32 @printf(ptr, ...)

define float @kernel(i32 %n) {
entry:
  %s = alloca float, align 4
  %i = alloca i32, align 4
  %acc = alloca float, align 4
  %b = alloca <4 x float>, align 16
  %a = alloca <4 x float>, align 16
  %n.addr = alloca i32, align 4
  store i32 %n, ptr %n.addr, align 4
  store <4 x float> <float 1.000000e+00, float 2.000000e+00, float 3.000000e+00, float 4.000000e+00>, ptr %a, align 16
  store <4 x float> <float 5.000000e-01, float 2.500000e-01, float 1.250000e-01, float 6.250000e-02>, ptr %b, align 16
  store float 0.000000e+00, ptr %acc, align 4
  %n1 = load i32, ptr %n.addr, align 4
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %i.cur = load i32, ptr %i, align 4
  %for.cmp = icmp slt i32 %i.cur, %n1
  br i1 %for.cmp, label %for.body, label %for.end

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

for.inc:                                          ; preds = %for.body
  %i.old = load i32, ptr %i, align 4
  %i.next = add nsw i32 %i.old, 1
  store i32 %i.next, ptr %i, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %acc9 = load float, ptr %acc, align 4
  ret float %acc9

after.ret:                                        ; No predecessors!
  unreachable
}

define i32 @main() {
entry:
  %call = call float @kernel(i32 200000000)
  %0 = fpext float %call to double
  %1 = call i32 (ptr, ...) @printf(ptr @.fmt, double %0)
  ret i32 0

after.ret:                                        ; No predecessors!
  unreachable
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.vector.reduce.fadd.v4f32(float, <4 x float>) #0

attributes #0 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
