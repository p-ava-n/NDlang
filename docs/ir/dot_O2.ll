; ModuleID = '../examples/02_dot_product.ndlang'
source_filename = "../examples/02_dot_product.ndlang"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@.fmt = private unnamed_addr constant [4 x i8] c"%f\0A\00", align 1

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr nocapture noundef readonly, ...) local_unnamed_addr #0

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define float @dot(<3 x float> %a, <3 x float> %b) local_unnamed_addr #1 {
entry:
  %mul = fmul <3 x float> %a, %b
  %0 = tail call reassoc float @llvm.vector.reduce.fadd.v3f32(float 0.000000e+00, <3 x float> %mul)
  ret float %0
}

; Function Attrs: nofree nounwind
define noundef i32 @main() local_unnamed_addr #0 {
entry:
  %0 = tail call reassoc float @llvm.vector.reduce.fadd.v3f32(float 0.000000e+00, <3 x float> <float 4.000000e+00, float 1.000000e+01, float 1.800000e+01>)
  %1 = fpext float %0 to double
  %2 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @.fmt, double %1)
  ret i32 0
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.vector.reduce.fadd.v3f32(float, <3 x float>) #2

attributes #0 = { nofree nounwind }
attributes #1 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) }
attributes #2 = { mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none) }
