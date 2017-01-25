; RUN: opt -sequentialize-pir -S < %s | FileCheck %s

declare void @foo();

define void @simple() {
entry:
  fork label %forked, %cont
; CHECK:      entry
; CHECK-NEXT: br label %forked

forked:
  call void @foo()
  halt label %cont
; CHECK:      forked
; CHECK-NEXT: call void @foo
; CHECK-NEXT: br label %cont

cont:
  call void @foo()
  join label %join
; CHECK:      cont
; CHECK-NEXT: call void @foo
; CHECK-NEXT: br label %join

join:
  ret void
}


define void @nested() {
entry:
  fork label %forked.outer, %cont.outer
; CHECK:      entry
; CHECK-NEXT: br label %forked.outer

forked.outer:
  call void @foo()
  fork label %forked.inner, %cont.inner
; CHECK:      forked.outer
; CHECK-NEXT: call void @foo
; CHECK-NEXT: br label %forked.inner

forked.inner:
  call void @foo()
  br label %forked.inner.body
; CHECK:      forked.inner
; CHECK-NEXT: call void @foo
; CHECK-NEXT: br label %forked.inner.body

forked.inner.body:
  call void @foo()
  halt label %cont.inner
; CHECK:      forked.inner.body
; CHECK-NEXT: call void @foo
; CHECK-NEXT: br label %cont.inner

cont.inner:
  call void @foo()
  join label %forked.outer.end
; CHECK:      cont.inner
; CHECK-NEXT: call void @foo
; CHECK-NEXT: br label %forked.outer.end

forked.outer.end:
  halt label %cont.outer
; CHECK:      forked.outer.end
; CHECK-NEXT: br label %cont.outer

cont.outer:
  call void @foo()
  join label %join
; CHECK:      cont
; CHECK-NEXT: call void @foo
; CHECK-NEXT: br label %join

join:
  ret void
}
