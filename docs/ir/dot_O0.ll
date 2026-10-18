; ModuleID = '../examples/02_dot_product.ndlang'
source_filename = "../examples/02_dot_product.ndlang"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.fmt = private unnamed_addr constant [4 x i8] c"%f\0A\00", align 1

declare i32 @printf(ptr, ...)

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

define i32 @main() {
entry:
  %v2 = alloca <3 x float>, align 16
  %v1 = alloca <3 x float>, align 16
  store <3 x float> <float 1.000000e+00, float 2.000000e+00, float 3.000000e+00>, ptr %v1, align 16
  store <3 x float> <float 4.000000e+00, float 5.000000e+00, float 6.000000e+00>, ptr %v2, align 16
  %v11 = load <3 x float>, ptr %v1, align 16
  %v22 = load <3 x float>, ptr %v2, align 16
  %call = call float @dot(<3 x float> %v11, <3 x float> %v22)
  %0 = fpext float %call to double
  %1 = call i32 (ptr, ...) @printf(ptr @.fmt, double %0)
  ret i32 0

after.ret:                                        ; No predecessors!
  unreachable
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.vector.reduce.fadd.v3f32(float, <3 x float>) #0

attributes #0 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
