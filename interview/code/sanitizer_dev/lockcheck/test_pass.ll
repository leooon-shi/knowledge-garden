ckcheckPass.cpp
: FileCheck test for the Lockcheck pass.
de dsanitızer_dev > lockcheck ye test_pass.li
: opt -load-pass-plugin=./LockCheckPass.so -passes-lockcheck -S %s | FileCheck %s %struct.Acct - type { 132, 164 )
@g_ mu = global [40 x i8] zeroinitializer
@acct - global %struct.Acct zeroinitializer
@ann - private constant [26 x 18] c"lockcheck. guarded_by:g_mulee", section "11vm. metadata" @other - private constant [8 x 18] c"unused/1e0", section "11vm. metadata"
@file = private constant [6 x 18] c"t.cleeleelee", section "11vm.metadata"
declare ptr @l1vm.ptr.annotation.pe.pe(ptr, ptr, ptr, 132, ptr)
; A read through the annotated pointer: check with is write = e, before the load.
define 164 @read guarded() {
; CHECK-LABEL: define 164 @read_guarded(
I
; CHECK:
call void @ lockcheck_access (ptr @g_mu, 132 e,
; CHECK-NEXT: load 164, ptr %p
Xf - getelementptr %struct.Acct, ptr @acct, 132 0, 132 1
Xp= call ptr @11vm.ptr.annotation.pe.p0(ptr%f, ptr @ann, ptr @file, 132 1, ptr null)
%v = load 164, ptr %p
ret 164 %v
; A write: is_write = 1, before the store.
define void @write guarded(164 %v) {
; CHECK-LABEL: define void @write_guarded(
; CHECK:
call void @_lockcheck_access (ptr @g_mu, 132 1,
; CHECK-NEXT: store 164 %v, ptr %p
Xf - getelementptr %struct.Acct, ptr @acct, 132 0, 132 1
%p- call ptr @11vm.ptr.annotation.p8.p0(ptr %f, ptr @ann, ptr @file,132 1, ptr null) store 164 %v, ptr %p ret void
; An unannotated field is left alone.
define 132 @read_unguarded () {
; CHECK-LABEL: define 132 @read_unguarded(
S CHECK-NOT:
_lockcheck_access
; CHECK:
ret 132
Xv = load i32, ptr @acct
ret i32 %v
; Annotations that aren't ours (different prefix) are ignored.
define i64 @foreign_annotation() {
; CHECK-LABEL: define 164 @foreign_annotation( S CHECK-NOT:
lockcheck_access
6 CHECK:
ret 164
%f = getelementptr %struct.Acct, ptr @acct, 132 0, 132 1
%p - callptr @l1vm.ptr.annotation.p0.p0(ptr %f, ptr @other, ptr @file, 132 1, ptr null)
%v = load 164, ptr %p
ret 164 %y