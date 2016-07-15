; RUN: not llvm-as < %s

define void @test_parallel_region1() {
  fork 0 [label %task0, label %task1, label %task2]

task0:
  br label %joinBB

task1:
  br label %joinBB

task2:
  br label %joinBB

joinBB:
  join i32 0, label %endBB

endBB:
  ret void
}
