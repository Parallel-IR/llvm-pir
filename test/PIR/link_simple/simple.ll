declare void @bar();
declare void @foo(i32, i32*);
declare void @foo2(i32, float);
declare i32 @read_int();
declare float @read_float();

define void @simpleParallelRegion() {
entry:
  %x = alloca i32, align 4
  %y = alloca float, align 4
  %z = alloca i32*, align 8
  %call = call i32 @read_int()
  store i32 %call, i32* %x, align 4
  %call2 = call float @read_float()
  store float %call2, float* %y, align 4
  store i32* %x, i32** %z, align 8
  fork label %forked, %cont

forked:
  %0 = load i32, i32* %x, align 4
  %1 = load i32*, i32** %z, align 8
  call void @foo(i32 %0, i32* %1)
  halt label %cont

cont:
  %2 = load i32, i32* %x, align 4
  %3 = load float, float* %y, align 4
  call void @foo2(i32 %2, float %3)
  join label %join

join:
  ret void
}
