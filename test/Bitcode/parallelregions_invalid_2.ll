; RUN: not llvm-as < %s
;
; XFAIL: This should probably fail

define void @test_parallel_regions5() {
  fork i32 0 []

task0:
  br label %joinBB

task1:
  br label %joinBB

task2:
  br label %joinBB

joinBB:
  join i32 1, label %endBB

endBB:
  ret void
}
