; RUN: not not opt -parallel-regions -analyze < %s 2> %t
; RUN: FileCheck --input-file %t %s
; REQUIRES: asserts
; CHECK: Basic block cannot belong to two different parallel tasks

declare void @foo();

define void @forked_task_may_not_reach_halt_1(i1 %cond) {
entry:
  fork label %forked, %cont

forked:
  call void @foo()
  br i1 %cond, label %halt_block, label %non_halt_block

non_halt_block:
  br label %cont

halt_block:
  halt label %cont

cont:
  call void @foo()
  join label %join

join:
  ret void
}
