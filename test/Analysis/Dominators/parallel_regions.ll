; RUN: opt -analyze -domtree -verify < %s | FileCheck %s --check-prefix=LOCAL
; TODO: Add global dominance checks

define void @parallel_region0() {
; LOCAL:       [1] %parallel_region0 {0,7}
; LOCAL-NEXT:    [2] %task0 {1,2}
; LOCAL-NEXT:    [2] %join {3,4}
; LOCAL-NEXT:    [2] %task1 {5,6}
parallel_region0:
  fork i32 0 [label %task0, label %task1, label %join]

task0:
  br label %join

task1:
  br label %join

join:
  join i32 0
  ret void
}

define void @parallel_region1() {
; LOCAL:       [1] %parallel_region1 {0,11}
; LOCAL-NEXT:    [2] %task0.1 {1,4}
; LOCAL-NEXT:      [3] %task0.2 {2,3}
; LOCAL-NEXT:    [2] %join {5,6}
; LOCAL-NEXT:    [2] %task1 {7,8}
; LOCAL-NEXT:    [2] %task2 {9,10}
parallel_region1:
  fork i32 0 force [label %task0.1, label %task1, label %task2]

task0.1:
  br label %task0.2

task0.2:
  br label %join

task1:
  br label %join

task2:
  br label %join

join:
  join i32 0
  ret void
}

define void @parallel_region2() {
; LOCAL:       [1] %parallel_region2 {0,19}
; LOCAL-NEXT:    [2] %pre_fork {1,18}
; LOCAL-NEXT:      [3] %fork {2,17}
; LOCAL-NEXT:        [4] %task0.1 {3,6}
; LOCAL-NEXT:          [5] %task0.2 {4,5}
; LOCAL-NEXT:        [4] %join {7,10}
; LOCAL-NEXT:          [5] %after_join {8,9}
; LOCAL-NEXT:        [4] %task1 {11,12}
; LOCAL-NEXT:        [4] %task1_2 {13,14}
; LOCAL-NEXT:        [4] %task2 {15,16}
parallel_region2:
  br label %pre_fork

pre_fork:
  br label %fork

fork:
  fork i32 0 force [label %task0.1, label %task1, label %task2]

task0.1:
  br label %task0.2

task0.2:
  br label %join

task1:
  br label %task1_2

task2:
  br label %task1_2

task1_2:
  br label %join

join:
  join i32 0
  br label %after_join

after_join:
  ret void
}

define void @parallel_region_multi_entry(i1 %c) {
; LOCAL:       [1] %parallel_region_multi_entry {0,17}
; LOCAL-NEXT:    [2] %pre_fork {1,16}
; LOCAL-NEXT:      [3] %fork0 {2,5}
; LOCAL-NEXT:        [4] %task0 {3,4}
; LOCAL-NEXT:      [3] %join {6,9}
; LOCAL-NEXT:        [4] %after_join {7,8}
; LOCAL-NEXT:      [3] %task_shared {10,11}
; LOCAL-NEXT:      [3] %fork1 {12,15}
; LOCAL-NEXT:        [4] %task2 {13,14}
parallel_region_multi_entry:
  br label %pre_fork

pre_fork:
  br i1 %c, label %fork0, label %fork1

fork0:
  fork i32 0 [label %task0, label %task_shared]

fork1:
  fork i32 0 [label %task_shared, label %task2]

task0:
  br label %join

task_shared:
  br label %join

task2:
  br label %join

join:
  join i32 0
  br label %after_join

after_join:
  ret void
}

define void @parallel_loop0(i32 %N) {
; LOCAL:       [1] %parallel_loop0 {0,13}
; LOCAL-NEXT:    [2] %header {1,12}
; LOCAL-NEXT:      [3] %exit {2,3}
; LOCAL-NEXT:      [3] %join {4,5}
; LOCAL-NEXT:      [3] %fork_iteration {6,11}
; LOCAL-NEXT:        [4] %body {7,10}
; LOCAL-NEXT:          [5] %body.next {8,9}
parallel_loop0:
  fork i32 0 [label %header]

header:
  %i = phi i32 [0, %parallel_loop0], [%i.inc, %fork_iteration]
  %exit_cnd = icmp sge i32 %i, %N
  br i1 %exit_cnd, label %exit, label %fork_iteration

fork_iteration:
  %i.inc = add i32 %i, 1
  fork i32 0 interior [label %body, label %header]

body:
  %use_i = add i32 %i, 1
  br label %body.next

body.next:
  %use_i_again = add i32 %i, %use_i
  br label %join

exit:
  br label %join

join:
  join i32 0
  ret void
}
