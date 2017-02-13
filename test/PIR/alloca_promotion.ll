; RUN: opt -mem2reg -sroa -S < %s | FileCheck %s

declare void @foo();
declare void @bar(i32);

; Verify we do not promote allocas that are used inside and outside a
; parallel region.
define i32 @alloca_used_in_seq_and_par_code() {
entry:
; CHECK: %local_alloca = alloca i32
  %local_alloca = alloca i32
  fork label %forked, label %cont

forked:                                       ; preds = %entry
; CHECK: store i32 0, i32* %local_alloca
  store i32 0, i32* %local_alloca
  call void @foo()
  halt label %cont

cont:                                         ; preds = %entry, %forked
  call void @foo()
; CHECK: store i32 1, i32* %local_alloca
  store i32 1, i32* %local_alloca
  join label %join

join:                                         ; preds = %cont
; CHECK: %val = load i32, i32* %local_alloca
  %val = load i32, i32* %local_alloca
  ret i32 %val
}

; Verify we do not promote allocas even if they are used only inside a parallel
; region but defined outside.
define i32 @alloca_used_only_in_par_code() {
entry:
; CHECK: alloca i32
  %local_alloca = alloca i32
  fork label %forked, label %cont

forked:                                       ; preds = %entry
  store i32 0, i32* %local_alloca
  call void @foo()
  halt label %cont

cont:                                         ; preds = %entry, %forked
  call void @foo()
  store i32 1, i32* %local_alloca
  %val = load i32, i32* %local_alloca
  join label %join

join:                                         ; preds = %cont
; CHECK: ret i32 %val
  ret i32 %val
}

; Verify we do promote allocas that are used only outside a parallel region.
define i32 @alloca_used_only_in_seq_code() {
entry:
; CHECK-NOT: alloca i32
  %local_alloca = alloca i32
  store i32 0, i32* %local_alloca
  fork label %forked, label %cont

forked:                                       ; preds = %entry
  call void @foo()
  halt label %cont

cont:                                         ; preds = %entry, %forked
  call void @foo()
  %val = load i32, i32* %local_alloca
  join label %join

join:                                         ; preds = %cont
  store i32 1, i32* %local_alloca
; CHECK: ret i32 0
  ret i32 %val
}
