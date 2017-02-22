; RUN: not llvm-as < %s

declare void @foo();

define i32 @test() {
entry:
  fork label %forked

forked:                                       ; preds = %entry
  call void @foo()
  halt label %cont

cont:                                         ; preds = %entry, %forked
  call void @foo()
  join label %join

join:                                         ; preds = %cont
  ret i32 0
}
