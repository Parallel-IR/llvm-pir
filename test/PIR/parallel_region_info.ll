; RUN: opt -parallel-regions -analyze < %s | FileCheck %s

; CHECK: Printing analysis 'Detect parallel regions' for function 'simple':
; CHECK: Parallel region:
; CHECK: -  fork label %forked, %cont
; CHECK: Forked Task:
; CHECK:   - Begin: forked
; CHECK:   - End:  halt label %cont
; CHECK: Continuation Task:
; CHECK:   - Begin: cont
; CHECK:   - End:  join label %join

declare void @foo();

define void @simple() {
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

; CHECK: Printing analysis 'Detect parallel regions' for function 'nested':
; CHECK: Parallel region:
; CHECK: -  fork label %forked.outer, %cont.outer
; CHECK: Forked Task:
; CHECK:   - Begin: forked.outer
; CHECK:     Parallel region:
; CHECK:     -  fork label %forked.inner, %cont.inner
; CHECK:     Forked Task:
; CHECK:       - Begin: forked.inner
; CHECK:       - End:  halt label %cont.inner
; CHECK:     Continuation Task:
; CHECK:       - Begin: cont.inner
; CHECK:       - End:  join label %forked.outer.end
; CHECK:   - End:  halt label %cont.outer
; CHECK: Continuation Task:
; CHECK:   - Begin: cont.outer
; CHECK:   - End:  join label %join

define void @nested() {
entry:
  fork label %forked.outer, %cont.outer

forked.outer:
  call void @foo()
  fork label %forked.inner, %cont.inner

forked.inner:
  call void @foo()
  br label %forked.inner.body

forked.inner.body:
  call void @foo()
  halt label %cont.inner

cont.inner:
  call void @foo()
  join label %forked.outer.end

forked.outer.end:
  halt label %cont.outer

cont.outer:
  call void @foo()
  join label %join

join:
  ret void
}
