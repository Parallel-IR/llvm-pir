; RUN: opt -sequentialize-pir -S < %s | FileCheck %s

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"

; void f0(int *A, int N) {
;   int i = 0;
;   #pragma parallel
;   do {
;     A[i] += 1;
;   } while (i++ < N);
; }
;
; Function Attrs: noinline nounwind uwtable
define void @f0(i32* %A, i32 %N) #0 {
entry:
  %tmp = sext i32 %N to i64
  br label %do.body

do.body:                                          ; preds = %do.cond, %entry
  %indvars.iv = phi i64 [ %indvars.iv.next, %do.cond ], [ 0, %entry ]
  br label %pr.begin

pr.begin:
  fork label %forked, %do.cond

forked:
  %arrayidx = getelementptr inbounds i32, i32* %A, i64 %indvars.iv
  %tmp1 = load i32, i32* %arrayidx, align 4
  %add = add nsw i32 %tmp1, 1
  store i32 %add, i32* %arrayidx, align 4
  halt label %do.cond

; CHECK: forked:
; CHECK:   %tmp1 = load i32, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access ![[PID0:[0-9]*]]
; CHECK:   store i32 %add, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access ![[PID0]]
; CHECK:   br label %do.cond

do.cond:                                          ; preds = %do.body
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %cmp = icmp slt i64 %indvars.iv, %tmp
  br i1 %cmp, label %do.body, label %do.end

; CHECK: do.cond:
; CHECK:  br i1 %cmp, label %do.body, label %do.end, !llvm.loop ![[L0:[0-9]*]]

do.end:                                           ; preds = %do.cond
  join label %return

return:
  ret void
}

; void f1(int *A, int N) {
;   int i = N;
;   #pragma parallel
;   do {
;     A[i] += 1;
;   } while (i-- >= 0);
; }
;
; Function Attrs: noinline nounwind uwtable
define void @f1(i32* %A, i32 %N) #0 {
entry:
  %tmp = sext i32 %N to i64
  br label %do.body

do.body:                                          ; preds = %do.cond, %entry
  %indvars.iv = phi i64 [ %indvars.iv.next, %do.cond ], [ %tmp, %entry ]
  br label %pr.begin

pr.begin:
  fork label %forked, %do.cond

forked:
  %arrayidx = getelementptr inbounds i32, i32* %A, i64 %indvars.iv
  %tmp1 = load i32, i32* %arrayidx, align 4
  %add = add nsw i32 %tmp1, 1
  store i32 %add, i32* %arrayidx, align 4
  halt label %do.cond

; CHECK: forked:
; CHECK:   %tmp1 = load i32, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access ![[PID1:[0-9]*]]
; CHECK:   store i32 %add, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access ![[PID1]]
; CHECK:   br label %do.cond

do.cond:                                          ; preds = %do.body
  %indvars.iv.next = add nsw i64 %indvars.iv, -1
  %cmp = icmp sgt i64 %indvars.iv, -1

; CHECK: do.cond:
; CHECK:  br i1 %cmp, label %do.body, label %do.end, !llvm.loop ![[L1:[0-9]*]]

 br i1 %cmp, label %do.body, label %do.end

do.end:                                           ; preds = %do.cond
  join label %return

return:
  ret void
}

; void f2(int *A, int N) {
;   int i = 0;
;   #pragma parallel
;   do {
;     A[i] += 1;
;   } while (i++ < N);
; }
;
; Function Attrs: noinline nounwind uwtable
define void @f2(i32* %A, i32 %N) #0 {
entry:
  %tmp = sext i32 %N to i64
  br label %do.body

do.body:                                          ; preds = %do.cond, %entry
  %indvars.iv = phi i64 [ %indvars.iv.next, %do.cond ], [ 0, %entry ]
  br label %pr.begin

pr.begin:                                         ; preds = %do.body
  fork label %forked, %do.cond

forked:                                           ; preds = %pr.begin
  %arrayidx = getelementptr inbounds i32, i32* %A, i64 %indvars.iv
  %tmp1 = load i32, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access !0
  %add = add nsw i32 %tmp1, 1
  store i32 %add, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access !0
  halt label %do.cond

; CHECK: forked:
; CHECK:   %tmp1 = load i32, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access ![[PID2:[0-9]*]]
; CHECK:   store i32 %add, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access ![[PID2]]
; CHECK:   br label %do.cond

do.cond:                                          ; preds = %forked
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %cmp = icmp slt i64 %indvars.iv, %tmp
  br i1 %cmp, label %do.body, label %do.end, !llvm.loop !1

; CHECK: do.cond:
; CHECK:  br i1 %cmp, label %do.body, label %do.end, !llvm.loop ![[L2:[0-9]*]]

do.end:                                           ; preds = %do.cond
  join label %return

return:                                           ; preds = %do.end
  ret void
}

; void f3(int *A, int N) {
;   int i = N;
;   #pragma parallel
;   do {
;     A[i] += 1;
;   } while (i-- >= 0);
; }
;
; CHECK: @f3
;
; Function Attrs: noinline nounwind uwtable
define void @f3(i32* %A, i32 %N) #0 {
entry:
  %tmp = sext i32 %N to i64
  br label %do.body

do.body:                                          ; preds = %do.cond, %entry
  %indvars.iv = phi i64 [ %indvars.iv.next, %do.cond ], [ %tmp, %entry ]
  br label %pr.begin

pr.begin:                                         ; preds = %do.body
  fork label %forked, %do.cond

; CHECK:      pr.begin:
; CHECK-NEXT:   br label %forked

forked:                                           ; preds = %pr.begin
  %arrayidx = getelementptr inbounds i32, i32* %A, i64 %indvars.iv
  %tmp1 = load i32, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access !2
  %add = add nsw i32 %tmp1, 1
  store i32 %add, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access !2
  halt label %do.cond

; CHECK: forked:
; CHECK:   %tmp1 = load i32, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access ![[PID3:[0-9]*]]
; CHECK:   store i32 %add, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access ![[PID3]]
; CHECK:   br label %do.cond

do.cond:                                          ; preds = %forked
  %indvars.iv.next = add nsw i64 %indvars.iv, -1
  %cmp = icmp sgt i64 %indvars.iv, -1
  br i1 %cmp, label %do.body, label %do.end, !llvm.loop !3

; CHECK: do.cond:
; CHECK:  br i1 %cmp, label %do.body, label %do.end, !llvm.loop ![[L3:[0-9]*]]

do.end:                                           ; preds = %do.cond
  join label %return

return:                                           ; preds = %do.end
  ret void
}

; void f4(int *A, int N) {
;   int i = N;
;   do {
;     #pragma parallel
;     do {
;       A[i] += 1;
;     } while (i-- >= 0);
;   } while (0);
; }
;
; Function Attrs: noinline nounwind uwtable
;
; CHECK: @f4
define void @f4(i32* %A, i32 %N) #0 {
entry:
  %tmp = sext i32 %N to i64
  br label %outer.do.body

outer.do.body:
  br label %do.body

do.body:                                          ; preds = %do.cond, %entry
  %indvars.iv = phi i64 [ %indvars.iv.next, %do.cond ], [ %tmp, %outer.do.body ]
  br label %pr.begin

pr.begin:                                         ; preds = %do.body
  fork label %forked, %do.cond

forked:                                           ; preds = %pr.begin
  %arrayidx = getelementptr inbounds i32, i32* %A, i64 %indvars.iv
  %tmp1 = load i32, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access !4
  %add = add nsw i32 %tmp1, 1
  store i32 %add, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access !4
  halt label %do.cond

; CHECK:  %tmp1 = load i32, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access ![[PID4:[0-9]*]]
; CHECK:  store i32 %add, i32* %arrayidx, align 4, !llvm.mem.parallel_loop_access ![[PID4]]

do.cond:                                          ; preds = %forked
  %indvars.iv.next = add nsw i64 %indvars.iv, -1
  %cmp = icmp sgt i64 %indvars.iv, -1
  br i1 %cmp, label %do.body, label %do.end

; CHECK: do.cond
; CHECK:   br i1 %cmp, label %do.body, label %do.end, !llvm.loop ![[LInner4:[0-9]*]]

do.end:                                           ; preds = %do.cond
  join label %outer.do.cond

outer.do.cond:
  br i1 false, label %outer.do.body, label %outer.do.end, !llvm.loop !5

; CHECK: outer.do.cond
; CHECK:  br i1 false, label %outer.do.body, label %outer.do.end, !llvm.loop ![[LOuter4:[0-9]*]]

outer.do.end:
  br label %return

return:                                           ; preds = %do.end
  ret void
}

!0 = !{!1}
!1 = distinct !{!1}
!2 = !{!3}
!3 = distinct !{!3}
!4 = !{!5}
!5 = distinct !{!5}

; CHECK:  ![[PID0]] = !{![[L0]]}
; CHECK:  ![[L0]] = distinct !{![[L0]]}

; CHECK:  ![[PID1]] = !{![[L1]]}
; CHECK:  ![[L1]] = distinct !{![[L1]]}

; CHECK:  ![[PID2]] = !{![[L2]]}
; CHECK:  ![[L2]] = distinct !{![[L2]]}

; CHECK:  ![[PID3]] = !{![[L3]]}
; CHECK:  ![[L3]] = distinct !{![[L3]]}

; CHECK:  ![[PID4]] = !{![[LOuter4]], ![[LInner4]]}
; CHECK:  ![[LOuter4]] = distinct !{![[LOuter4]]}
; CHECK:  ![[LInner4]] = distinct !{![[LInner4]]}

attributes #0 = { noinline nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
