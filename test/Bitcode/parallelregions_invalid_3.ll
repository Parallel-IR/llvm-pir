; RUN: not llvm-as < %s

define void @test_parallel_region3() {
  fork i32 0 interior force [label %task0, label %task1, label %task2]

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
