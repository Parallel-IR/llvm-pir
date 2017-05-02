declare void @foo(i32);
declare i32 @read_int();

define void @simpleParallelRegion() {
entry:
  %x = alloca i32, align 4
  %y = alloca i32, align 4
  %call = call i32 @read_int()
  store i32 %call, i32* %x, align 4
  %call2 = call i32 @read_int()
  store i32 %call2, i32* %y, align 4
  fork label %forked, %cont

forked:
  %0 = load i32, i32* %x, align 4
  call void @foo(i32 %0)
  halt label %cont

cont:
  %1 = load i32, i32* %y, align 4
  call void @foo(i32 %1)
  join label %join

join:
  ret void
}
