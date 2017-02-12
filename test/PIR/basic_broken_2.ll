; RUN: not not opt -parallel-regions -analyze < %s 2> %t
; RUN: FileCheck --input-file %t %s
; REQUIRES: asserts
; CHECK: Parallel region fork does not dominate join instruction

declare void @foo();

define void @region_entered_without_fork2(i1 %cond) {
entry:
  br i1 %cond, label %fork_entry, label %non_fork_entry

non_fork_entry:
  br label %cont

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
