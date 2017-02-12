; RUN: not not opt -parallel-regions -analyze < %s 2> %t
; RUN: FileCheck --input-file %t %s
; REQUIRES: asserts
; CHECK: Parallel region fork does not dominate halt instruction

declare void @foo();

define void @region_entered_without_fork1(i1 %cond) {
entry:
  br i1 %cond, label %fork_entry, label %non_fork_entry

non_fork_entry:
  br label %forked

fork_entry:
  fork label %forked, %cont

forked:
  call void @foo()
  halt label %cont

cont:
  call void @foo()
  join label %join

join:
  ret void
}
