; RUN: not llvm-as < %s

define void @test_parallel_regions4() {
  fork i32 0 nterior [label %task0, label %task1, label %task2]

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
