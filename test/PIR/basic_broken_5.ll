; RUN: not not opt -parallel-regions -analyze < %s 2> %t
; RUN: FileCheck --input-file %t %s
; REQUIRES: asserts
; CHECK: A parallel region was not terminated

declare void @foo();

define void @forked_task_may_not_reach_halt_1(i1 %cond) {
entry:
  fork label %forked, %cont

forked:
  call void @foo()
  br i1 %cond, label %halt_block, label %non_halt_block

non_halt_block:
  ret void

halt_block:
  halt label %cont

cont:
  call void @foo()
  join label %join

join:
  ret void
}
