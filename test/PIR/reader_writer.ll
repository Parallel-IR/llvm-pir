; RUN: llvm-as < %s | llvm-dis | FileCheck %s
; RUN: opt < %s -o - | opt -o - -S | FileCheck %s
; RUN: opt < %s -o - | opt -o - -S | opt -o - | opt -o - -S | FileCheck %s

declare void @foo();

define i32 @test() {
entry:
  ; CHECK: fork label %forked, %cont
  fork label %forked, %cont

forked:                                       ; preds = %entry
  call void @foo()
  ; CHECK: halt label %cont
  halt label %cont

cont:                                         ; preds = %entry, %forked
  call void @foo()
  ; CHECK: join label %join
  join label %join

join:                                         ; preds = %cont
  ret i32 0
}
