; RUN: opt -pir2omp < %s | llvm-dis | FileCheck %s

; see if the call to fork was emitted
; CHECK: call void (%ident_t*, i32, void (i32*, i32*, ...)*, ...) @__kmpc_fork_call(%ident_t* @0, i32 0, void (i32*, i32*, ...)* @simple.entry)

; see if the region outlined function was declared
; CHECK: define internal void @simple.entry(i32* noalias %.global_tid., i32* noalias %.bound_tid., ...)

; see if the static for initialization runtime call was emitted
; CHECK: call void @__kmpc_for_static_init_4(%ident_t* @0, i32 %1, i32 34, i32* %.omp.sections.il., i32* %.omp.sections.lb., i32* %.omp.sections.ub., i32* %.omp.sections.st., i32 1, i32 1)

; see if the forked region outlined function was called and that it is the
; body of the first section
; CHECK: .omp.sections.case:
; CHECK-NEXT: call void @simple.entry.forked()

; see if the cont region outlined function was called and that it is the
; body of the second section
; CHECK: .omp.sections.case1:
; CHECK-NEXT: call void @simple.entry.cont()

; see if the static for finish runtime call was emitted
; CHECK: call void @__kmpc_for_static_fini(%ident_t* @0, i32 %1)

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
