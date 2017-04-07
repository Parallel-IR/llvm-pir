declare void @foo();

define void @simpleParallelRegion() {
entry:
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
