; RUN: opt -S < %s -o=%t | tail -n +4 %s | diff %t -
; RUN: llvm-as < %s -o=- | llvm-dis -o=%t - | tail -n +4 %s | diff %t -

; ModuleID = '<stdin>'
source_filename = "<stdin>"

define void @valid_parallel_region0() {
  fork i32 0 [label %task0, label %task1, label %task2]

task0:                                            ; preds = %0
  br label %joinBB

task1:                                            ; preds = %0
  br label %joinBB

task2:                                            ; preds = %0
  br label %joinBB

joinBB:                                           ; preds = %task2, %task1, %task0
  join i32 0, label %endBB

endBB:
  ret void
}

define void @valid_parallel_region1() {
  fork i32 0 force [label %task0, label %task1, label %task2]

task0:                                            ; preds = %0
  br label %joinBB

task1:                                            ; preds = %0
  br label %joinBB

task2:                                            ; preds = %0
  br label %joinBB

joinBB:                                           ; preds = %task2, %task1, %task0
  join i32 0, label %endBB

endBB:
  ret void
}

define void @valid_parallel_region2() {
  fork i32 0 [label %taskI, label %task0, label %task0, label %task0, label %joinBB]

taskI:                                            ; preds = %0
  fork i32 0 interior [label %task0, label %task1, label %task2]

task0:                                            ; preds = %taskI, %0, %0, %0
  br label %joinBB

task1:                                            ; preds = %taskI
  br label %joinBB

task2:                                            ; preds = %taskI
  br label %joinBB

joinBB:                                           ; preds = %task2, %task1, %task0, %0
  join i32 0, label %endBB

endBB:
  ret void
}

define void @valid_parallel_region3() {
  fork i32 0 force [label %taskI, label %task0, label %task0, label %task0, label %joinBB]

taskI:                                            ; preds = %0
  fork i32 0 force interior [label %task0, label %task1, label %task2]

task0:                                            ; preds = %taskI, %0, %0, %0
  br label %joinBB

task1:                                            ; preds = %taskI
  br label %joinBB

task2:                                            ; preds = %taskI
  br label %joinBB

joinBB:                                           ; preds = %task2, %task1, %task0, %0
  join i32 0, label %endBB

endBB:
  ret void
}

define void @valid_parallel_region4() {
  fork i32 42 force [label %taskI, label %task0, label %task0, label %task0, label %joinBB]

taskI:                                            ; preds = %0
  fork i32 42 force interior [label %task0, label %task1, label %task2]

task0:                                            ; preds = %taskI, %0, %0, %0
  br label %joinBB

task1:                                            ; preds = %taskI
  br label %joinBB

task2:                                            ; preds = %taskI
  br label %joinBB

joinBB:                                           ; preds = %task2, %task1, %task0, %0
  join i32 42, label %endBB

endBB:
  ret void
}

define void @valid_parallel_region5(i32 %N) {
  %add = add i32 %N, 1
  fork i32 42 force [label %taskI, label %task0, label %task0, label %task0, label %joinBB]

taskI:                                            ; preds = %0
  fork i32 42 force interior [i32 %add, i32 %N] [label %task0, label %task1, label %task2]

task0:                                            ; preds = %taskI, %0, %0, %0
  br label %joinBB

task1:                                            ; preds = %taskI
  br label %joinBB

task2:                                            ; preds = %taskI
  br label %joinBB

joinBB:                                           ; preds = %task2, %task1, %task0, %0
  join i32 42, label %endBB

endBB:
  ret void
}
