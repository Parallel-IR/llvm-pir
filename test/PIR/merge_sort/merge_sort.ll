; ModuleID = 'OMPMergeSort_NoRegions.cpp'
source_filename = "OMPMergeSort_NoRegions.cpp"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%"class.std::ios_base::Init" = type { i8 }
%"struct.__gnu_cxx::__ops::_Val_less_iter" = type { i8 }
%"struct.__gnu_cxx::__ops::_Iter_less_val" = type { i8 }
%"struct.std::random_access_iterator_tag" = type { i8 }

$_ZSt11upper_boundIPiiET_S1_S1_RKT0_ = comdat any

$_ZSt11lower_boundIPiiET_S1_S1_RKT0_ = comdat any

$_ZSt13__upper_boundIPiiN9__gnu_cxx5__ops14_Val_less_iterEET_S4_S4_RKT0_T1_ = comdat any

$_ZN9__gnu_cxx5__ops15__val_less_iterEv = comdat any

$_ZSt8distanceIPiENSt15iterator_traitsIT_E15difference_typeES2_S2_ = comdat any

$_ZSt7advanceIPilEvRT_T0_ = comdat any

$_ZNK9__gnu_cxx5__ops14_Val_less_iterclIKiPiEEbRT_T0_ = comdat any

$_ZSt10__distanceIPiENSt15iterator_traitsIT_E15difference_typeES2_S2_St26random_access_iterator_tag = comdat any

$_ZSt19__iterator_categoryIPiENSt15iterator_traitsIT_E17iterator_categoryERKS2_ = comdat any

$_ZSt9__advanceIPilEvRT_T0_St26random_access_iterator_tag = comdat any

$_ZSt13__lower_boundIPiiN9__gnu_cxx5__ops14_Iter_less_valEET_S4_S4_RKT0_T1_ = comdat any

$_ZN9__gnu_cxx5__ops15__iter_less_valEv = comdat any

$_ZNK9__gnu_cxx5__ops14_Iter_less_valclIPiKiEEbT_RT0_ = comdat any

@_ZStL8__ioinit = internal global %"class.std::ios_base::Init" zeroinitializer, align 1
@__dso_handle = external hidden global i8
@llvm.global_ctors = appending global [1 x { i32, void ()*, i8* }] [{ i32, void ()*, i8* } { i32 65535, void ()* @_GLOBAL__sub_I_OMPMergeSort_NoRegions.cpp, i8* null }]

; Function Attrs: noinline uwtable
define internal void @__cxx_global_var_init() #0 section ".text.startup" {
entry:
  call void @_ZNSt8ios_base4InitC1Ev(%"class.std::ios_base::Init"* @_ZStL8__ioinit)
  %0 = call i32 @__cxa_atexit(void (i8*)* bitcast (void (%"class.std::ios_base::Init"*)* @_ZNSt8ios_base4InitD1Ev to void (i8*)*), i8* getelementptr inbounds (%"class.std::ios_base::Init", %"class.std::ios_base::Init"* @_ZStL8__ioinit, i32 0, i32 0), i8* @__dso_handle) #2
  ret void
}

declare void @_ZNSt8ios_base4InitC1Ev(%"class.std::ios_base::Init"*) unnamed_addr #1

declare void @_ZNSt8ios_base4InitD1Ev(%"class.std::ios_base::Init"*) unnamed_addr #1

; Function Attrs: nounwind
declare i32 @__cxa_atexit(void (i8*)*, i8*, i8*) #2

; Function Attrs: noinline uwtable
define void @parallel_merge(i32* %xs, i32* %xe, i32* %ys, i32* %ye, i32* %zs, i1 zeroext %destroy) #0 {
entry:
  %xs.addr = alloca i32*, align 8
  %xe.addr = alloca i32*, align 8
  %ys.addr = alloca i32*, align 8
  %ye.addr = alloca i32*, align 8
  %zs.addr = alloca i32*, align 8
  %destroy.addr = alloca i8, align 1
  %MERGE_CUT_OFF = alloca i64, align 8
  %xm = alloca i32*, align 8
  %ym = alloca i32*, align 8
  store i32* %xs, i32** %xs.addr, align 8
  store i32* %xe, i32** %xe.addr, align 8
  store i32* %ys, i32** %ys.addr, align 8
  store i32* %ye, i32** %ye.addr, align 8
  store i32* %zs, i32** %zs.addr, align 8
  %frombool = zext i1 %destroy to i8
  store i8 %frombool, i8* %destroy.addr, align 1
  store i64 2000, i64* %MERGE_CUT_OFF, align 8
  %0 = load i32*, i32** %xe.addr, align 8
  %1 = load i32*, i32** %xs.addr, align 8
  %sub.ptr.lhs.cast = ptrtoint i32* %0 to i64
  %sub.ptr.rhs.cast = ptrtoint i32* %1 to i64
  %sub.ptr.sub = sub i64 %sub.ptr.lhs.cast, %sub.ptr.rhs.cast
  %sub.ptr.div = sdiv exact i64 %sub.ptr.sub, 4
  %2 = load i32*, i32** %ye.addr, align 8
  %3 = load i32*, i32** %ys.addr, align 8
  %sub.ptr.lhs.cast1 = ptrtoint i32* %2 to i64
  %sub.ptr.rhs.cast2 = ptrtoint i32* %3 to i64
  %sub.ptr.sub3 = sub i64 %sub.ptr.lhs.cast1, %sub.ptr.rhs.cast2
  %sub.ptr.div4 = sdiv exact i64 %sub.ptr.sub3, 4
  %add = add nsw i64 %sub.ptr.div, %sub.ptr.div4
  %cmp = icmp ugt i64 %add, 2000
  br i1 %cmp, label %if.then, label %if.else37

if.then:                                          ; preds = %entry
  %4 = load i32*, i32** %xe.addr, align 8
  %5 = load i32*, i32** %xs.addr, align 8
  %sub.ptr.lhs.cast5 = ptrtoint i32* %4 to i64
  %sub.ptr.rhs.cast6 = ptrtoint i32* %5 to i64
  %sub.ptr.sub7 = sub i64 %sub.ptr.lhs.cast5, %sub.ptr.rhs.cast6
  %sub.ptr.div8 = sdiv exact i64 %sub.ptr.sub7, 4
  %6 = load i32*, i32** %ye.addr, align 8
  %7 = load i32*, i32** %ys.addr, align 8
  %sub.ptr.lhs.cast9 = ptrtoint i32* %6 to i64
  %sub.ptr.rhs.cast10 = ptrtoint i32* %7 to i64
  %sub.ptr.sub11 = sub i64 %sub.ptr.lhs.cast9, %sub.ptr.rhs.cast10
  %sub.ptr.div12 = sdiv exact i64 %sub.ptr.sub11, 4
  %cmp13 = icmp slt i64 %sub.ptr.div8, %sub.ptr.div12
  br i1 %cmp13, label %if.then14, label %if.else

if.then14:                                        ; preds = %if.then
  %8 = load i32*, i32** %ys.addr, align 8
  %9 = load i32*, i32** %ye.addr, align 8
  %10 = load i32*, i32** %ys.addr, align 8
  %sub.ptr.lhs.cast15 = ptrtoint i32* %9 to i64
  %sub.ptr.rhs.cast16 = ptrtoint i32* %10 to i64
  %sub.ptr.sub17 = sub i64 %sub.ptr.lhs.cast15, %sub.ptr.rhs.cast16
  %sub.ptr.div18 = sdiv exact i64 %sub.ptr.sub17, 4
  %div = sdiv i64 %sub.ptr.div18, 2
  %add.ptr = getelementptr inbounds i32, i32* %8, i64 %div
  store i32* %add.ptr, i32** %ym, align 8
  %11 = load i32*, i32** %xs.addr, align 8
  %12 = load i32*, i32** %xe.addr, align 8
  %13 = load i32*, i32** %ym, align 8
  %call = call i32* @_ZSt11upper_boundIPiiET_S1_S1_RKT0_(i32* %11, i32* %12, i32* dereferenceable(4) %13)
  store i32* %call, i32** %xm, align 8
  br label %if.end

if.else:                                          ; preds = %if.then
  %14 = load i32*, i32** %xs.addr, align 8
  %15 = load i32*, i32** %xe.addr, align 8
  %16 = load i32*, i32** %xs.addr, align 8
  %sub.ptr.lhs.cast19 = ptrtoint i32* %15 to i64
  %sub.ptr.rhs.cast20 = ptrtoint i32* %16 to i64
  %sub.ptr.sub21 = sub i64 %sub.ptr.lhs.cast19, %sub.ptr.rhs.cast20
  %sub.ptr.div22 = sdiv exact i64 %sub.ptr.sub21, 4
  %div23 = sdiv i64 %sub.ptr.div22, 2
  %add.ptr24 = getelementptr inbounds i32, i32* %14, i64 %div23
  store i32* %add.ptr24, i32** %xm, align 8
  %17 = load i32*, i32** %ys.addr, align 8
  %18 = load i32*, i32** %ye.addr, align 8
  %19 = load i32*, i32** %xm, align 8
  %call25 = call i32* @_ZSt11lower_boundIPiiET_S1_S1_RKT0_(i32* %17, i32* %18, i32* dereferenceable(4) %19)
  store i32* %call25, i32** %ym, align 8
  br label %if.end

if.end:                                           ; preds = %if.else, %if.then14
  fork label %par.mrg.frkd, %par.mrg.cont

par.mrg.frkd:
  %20 = load i32*, i32** %xs.addr, align 8
  %21 = load i32*, i32** %xm, align 8
  %22 = load i32*, i32** %ys.addr, align 8
  %23 = load i32*, i32** %ym, align 8
  %24 = load i32*, i32** %zs.addr, align 8
  %25 = load i8, i8* %destroy.addr, align 1
  %tobool = trunc i8 %25 to i1
  call void @parallel_merge(i32* %20, i32* %21, i32* %22, i32* %23, i32* %24, i1 zeroext %tobool)
  halt label %par.mrg.cont

par.mrg.cont:
  %26 = load i32*, i32** %xm, align 8
  %27 = load i32*, i32** %xe.addr, align 8
  %28 = load i32*, i32** %ym, align 8
  %29 = load i32*, i32** %ye.addr, align 8
  %30 = load i32*, i32** %zs.addr, align 8
  %31 = load i32*, i32** %xm, align 8
  %32 = load i32*, i32** %xs.addr, align 8
  %sub.ptr.lhs.cast26 = ptrtoint i32* %31 to i64
  %sub.ptr.rhs.cast27 = ptrtoint i32* %32 to i64
  %sub.ptr.sub28 = sub i64 %sub.ptr.lhs.cast26, %sub.ptr.rhs.cast27
  %sub.ptr.div29 = sdiv exact i64 %sub.ptr.sub28, 4
  %add.ptr30 = getelementptr inbounds i32, i32* %30, i64 %sub.ptr.div29
  %33 = load i32*, i32** %ym, align 8
  %34 = load i32*, i32** %ys.addr, align 8
  %sub.ptr.lhs.cast31 = ptrtoint i32* %33 to i64
  %sub.ptr.rhs.cast32 = ptrtoint i32* %34 to i64
  %sub.ptr.sub33 = sub i64 %sub.ptr.lhs.cast31, %sub.ptr.rhs.cast32
  %sub.ptr.div34 = sdiv exact i64 %sub.ptr.sub33, 4
  %add.ptr35 = getelementptr inbounds i32, i32* %add.ptr30, i64 %sub.ptr.div34
  %35 = load i8, i8* %destroy.addr, align 1
  %tobool36 = trunc i8 %35 to i1
  call void @parallel_merge(i32* %26, i32* %27, i32* %28, i32* %29, i32* %add.ptr35, i1 zeroext %tobool36)
  join label %if.end38

if.else37:                                        ; preds = %entry
  %36 = load i32*, i32** %xs.addr, align 8
  %37 = load i32*, i32** %xe.addr, align 8
  %38 = load i32*, i32** %ys.addr, align 8
  %39 = load i32*, i32** %ye.addr, align 8
  %40 = load i32*, i32** %zs.addr, align 8
  call void @serial_merge(i32* %36, i32* %37, i32* %38, i32* %39, i32* %40)
  br label %if.end38

if.end38:                                         ; preds = %if.else37, %if.end
  ret void
}

; Function Attrs: noinline uwtable
define linkonce_odr i32* @_ZSt11upper_boundIPiiET_S1_S1_RKT0_(i32* %__first, i32* %__last, i32* dereferenceable(4) %__val) #0 comdat {
entry:
  %__first.addr = alloca i32*, align 8
  %__last.addr = alloca i32*, align 8
  %__val.addr = alloca i32*, align 8
  %agg.tmp = alloca %"struct.__gnu_cxx::__ops::_Val_less_iter", align 1
  %undef.agg.tmp = alloca %"struct.__gnu_cxx::__ops::_Val_less_iter", align 1
  store i32* %__first, i32** %__first.addr, align 8
  store i32* %__last, i32** %__last.addr, align 8
  store i32* %__val, i32** %__val.addr, align 8
  %0 = load i32*, i32** %__first.addr, align 8
  %1 = load i32*, i32** %__last.addr, align 8
  %2 = load i32*, i32** %__val.addr, align 8
  call void @_ZN9__gnu_cxx5__ops15__val_less_iterEv()
  %call = call i32* @_ZSt13__upper_boundIPiiN9__gnu_cxx5__ops14_Val_less_iterEET_S4_S4_RKT0_T1_(i32* %0, i32* %1, i32* dereferenceable(4) %2)
  ret i32* %call
}

; Function Attrs: noinline uwtable
define linkonce_odr i32* @_ZSt11lower_boundIPiiET_S1_S1_RKT0_(i32* %__first, i32* %__last, i32* dereferenceable(4) %__val) #0 comdat {
entry:
  %__first.addr = alloca i32*, align 8
  %__last.addr = alloca i32*, align 8
  %__val.addr = alloca i32*, align 8
  %agg.tmp = alloca %"struct.__gnu_cxx::__ops::_Iter_less_val", align 1
  %undef.agg.tmp = alloca %"struct.__gnu_cxx::__ops::_Iter_less_val", align 1
  store i32* %__first, i32** %__first.addr, align 8
  store i32* %__last, i32** %__last.addr, align 8
  store i32* %__val, i32** %__val.addr, align 8
  %0 = load i32*, i32** %__first.addr, align 8
  %1 = load i32*, i32** %__last.addr, align 8
  %2 = load i32*, i32** %__val.addr, align 8
  call void @_ZN9__gnu_cxx5__ops15__iter_less_valEv()
  %call = call i32* @_ZSt13__lower_boundIPiiN9__gnu_cxx5__ops14_Iter_less_valEET_S4_S4_RKT0_T1_(i32* %0, i32* %1, i32* dereferenceable(4) %2)
  ret i32* %call
}

declare void @serial_merge(i32*, i32*, i32*, i32*, i32*) #1

; Function Attrs: noinline uwtable
define void @parallel_stable_sort_aux(i32* %xs, i32* %xe, i32* %zs, i32 %inplace) #0 {
entry:
  %xs.addr = alloca i32*, align 8
  %xe.addr = alloca i32*, align 8
  %zs.addr = alloca i32*, align 8
  %inplace.addr = alloca i32, align 4
  %SORT_CUT_OFF = alloca i64, align 8
  %xm = alloca i32*, align 8
  %zm = alloca i32*, align 8
  %ze = alloca i32*, align 8
  store i32* %xs, i32** %xs.addr, align 8
  store i32* %xe, i32** %xe.addr, align 8
  store i32* %zs, i32** %zs.addr, align 8
  store i32 %inplace, i32* %inplace.addr, align 4
  store i64 500, i64* %SORT_CUT_OFF, align 8
  %0 = load i32*, i32** %xe.addr, align 8
  %1 = load i32*, i32** %xs.addr, align 8
  %sub.ptr.lhs.cast = ptrtoint i32* %0 to i64
  %sub.ptr.rhs.cast = ptrtoint i32* %1 to i64
  %sub.ptr.sub = sub i64 %sub.ptr.lhs.cast, %sub.ptr.rhs.cast
  %sub.ptr.div = sdiv exact i64 %sub.ptr.sub, 4
  %cmp = icmp ule i64 %sub.ptr.div, 500
  br i1 %cmp, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %2 = load i32*, i32** %xs.addr, align 8
  %3 = load i32*, i32** %xe.addr, align 8
  %4 = load i32*, i32** %zs.addr, align 8
  %5 = load i32, i32* %inplace.addr, align 4
  call void @stable_sort_base_case(i32* %2, i32* %3, i32* %4, i32 %5)
  br label %if.end22

if.else:                                          ; preds = %entry
  %6 = load i32*, i32** %xs.addr, align 8
  %7 = load i32*, i32** %xe.addr, align 8
  %8 = load i32*, i32** %xs.addr, align 8
  %sub.ptr.lhs.cast1 = ptrtoint i32* %7 to i64
  %sub.ptr.rhs.cast2 = ptrtoint i32* %8 to i64
  %sub.ptr.sub3 = sub i64 %sub.ptr.lhs.cast1, %sub.ptr.rhs.cast2
  %sub.ptr.div4 = sdiv exact i64 %sub.ptr.sub3, 4
  %div = sdiv i64 %sub.ptr.div4, 2
  %add.ptr = getelementptr inbounds i32, i32* %6, i64 %div
  store i32* %add.ptr, i32** %xm, align 8
  %9 = load i32*, i32** %zs.addr, align 8
  %10 = load i32*, i32** %xm, align 8
  %11 = load i32*, i32** %xs.addr, align 8
  %sub.ptr.lhs.cast5 = ptrtoint i32* %10 to i64
  %sub.ptr.rhs.cast6 = ptrtoint i32* %11 to i64
  %sub.ptr.sub7 = sub i64 %sub.ptr.lhs.cast5, %sub.ptr.rhs.cast6
  %sub.ptr.div8 = sdiv exact i64 %sub.ptr.sub7, 4
  %add.ptr9 = getelementptr inbounds i32, i32* %9, i64 %sub.ptr.div8
  store i32* %add.ptr9, i32** %zm, align 8
  %12 = load i32*, i32** %zs.addr, align 8
  %13 = load i32*, i32** %xe.addr, align 8
  %14 = load i32*, i32** %xs.addr, align 8
  %sub.ptr.lhs.cast10 = ptrtoint i32* %13 to i64
  %sub.ptr.rhs.cast11 = ptrtoint i32* %14 to i64
  %sub.ptr.sub12 = sub i64 %sub.ptr.lhs.cast10, %sub.ptr.rhs.cast11
  %sub.ptr.div13 = sdiv exact i64 %sub.ptr.sub12, 4
  %add.ptr14 = getelementptr inbounds i32, i32* %12, i64 %sub.ptr.div13
  store i32* %add.ptr14, i32** %ze, align 8
  fork label %par.srt.frkd, %par.srt.cont

par.srt.frkd:
  %15 = load i32*, i32** %xs.addr, align 8
  %16 = load i32*, i32** %xm, align 8
  %17 = load i32*, i32** %zs.addr, align 8
  %18 = load i32, i32* %inplace.addr, align 4
  %tobool = icmp ne i32 %18, 0
  %lnot = xor i1 %tobool, true
  %conv = zext i1 %lnot to i32
  call void @parallel_stable_sort_aux(i32* %15, i32* %16, i32* %17, i32 %conv)
  halt label %par.srt.cont

par.srt.cont:
  %19 = load i32*, i32** %xm, align 8
  %20 = load i32*, i32** %xe.addr, align 8
  %21 = load i32*, i32** %zm, align 8
  %22 = load i32, i32* %inplace.addr, align 4
  %tobool15 = icmp ne i32 %22, 0
  %lnot16 = xor i1 %tobool15, true
  %conv17 = zext i1 %lnot16 to i32
  call void @parallel_stable_sort_aux(i32* %19, i32* %20, i32* %21, i32 %conv17)
  join label %par.srt.join

par.srt.join:
  %23 = load i32, i32* %inplace.addr, align 4
  %tobool18 = icmp ne i32 %23, 0
  br i1 %tobool18, label %if.then19, label %if.else21

if.then19:                                        ; preds = %if.else
  %24 = load i32*, i32** %zs.addr, align 8
  %25 = load i32*, i32** %zm, align 8
  %26 = load i32*, i32** %zm, align 8
  %27 = load i32*, i32** %ze, align 8
  %28 = load i32*, i32** %xs.addr, align 8
  %29 = load i32, i32* %inplace.addr, align 4
  %cmp20 = icmp eq i32 %29, 2
  call void @parallel_merge(i32* %24, i32* %25, i32* %26, i32* %27, i32* %28, i1 zeroext %cmp20)
  br label %if.end

if.else21:                                        ; preds = %if.else
  %30 = load i32*, i32** %xs.addr, align 8
  %31 = load i32*, i32** %xm, align 8
  %32 = load i32*, i32** %xm, align 8
  %33 = load i32*, i32** %xe.addr, align 8
  %34 = load i32*, i32** %zs.addr, align 8
  call void @parallel_merge(i32* %30, i32* %31, i32* %32, i32* %33, i32* %34, i1 zeroext false)
  br label %if.end

if.end:                                           ; preds = %if.else21, %if.then19
  br label %if.end22

if.end22:                                         ; preds = %if.end, %if.then
  ret void
}

declare void @stable_sort_base_case(i32*, i32*, i32*, i32) #1

; Function Attrs: noinline uwtable
define linkonce_odr i32* @_ZSt13__upper_boundIPiiN9__gnu_cxx5__ops14_Val_less_iterEET_S4_S4_RKT0_T1_(i32* %__first, i32* %__last, i32* dereferenceable(4) %__val) #0 comdat {
entry:
  %__comp = alloca %"struct.__gnu_cxx::__ops::_Val_less_iter", align 1
  %__first.addr = alloca i32*, align 8
  %__last.addr = alloca i32*, align 8
  %__val.addr = alloca i32*, align 8
  %__len = alloca i64, align 8
  %__half = alloca i64, align 8
  %__middle = alloca i32*, align 8
  store i32* %__first, i32** %__first.addr, align 8
  store i32* %__last, i32** %__last.addr, align 8
  store i32* %__val, i32** %__val.addr, align 8
  %0 = load i32*, i32** %__first.addr, align 8
  %1 = load i32*, i32** %__last.addr, align 8
  %call = call i64 @_ZSt8distanceIPiENSt15iterator_traitsIT_E15difference_typeES2_S2_(i32* %0, i32* %1)
  store i64 %call, i64* %__len, align 8
  br label %while.cond

while.cond:                                       ; preds = %if.end, %entry
  %2 = load i64, i64* %__len, align 8
  %cmp = icmp sgt i64 %2, 0
  br i1 %cmp, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %3 = load i64, i64* %__len, align 8
  %shr = ashr i64 %3, 1
  store i64 %shr, i64* %__half, align 8
  %4 = load i32*, i32** %__first.addr, align 8
  store i32* %4, i32** %__middle, align 8
  %5 = load i64, i64* %__half, align 8
  call void @_ZSt7advanceIPilEvRT_T0_(i32** dereferenceable(8) %__middle, i64 %5)
  %6 = load i32*, i32** %__val.addr, align 8
  %7 = load i32*, i32** %__middle, align 8
  %call1 = call zeroext i1 @_ZNK9__gnu_cxx5__ops14_Val_less_iterclIKiPiEEbRT_T0_(%"struct.__gnu_cxx::__ops::_Val_less_iter"* %__comp, i32* dereferenceable(4) %6, i32* %7)
  br i1 %call1, label %if.then, label %if.else

if.then:                                          ; preds = %while.body
  %8 = load i64, i64* %__half, align 8
  store i64 %8, i64* %__len, align 8
  br label %if.end

if.else:                                          ; preds = %while.body
  %9 = load i32*, i32** %__middle, align 8
  store i32* %9, i32** %__first.addr, align 8
  %10 = load i32*, i32** %__first.addr, align 8
  %incdec.ptr = getelementptr inbounds i32, i32* %10, i32 1
  store i32* %incdec.ptr, i32** %__first.addr, align 8
  %11 = load i64, i64* %__len, align 8
  %12 = load i64, i64* %__half, align 8
  %sub = sub nsw i64 %11, %12
  %sub2 = sub nsw i64 %sub, 1
  store i64 %sub2, i64* %__len, align 8
  br label %if.end

if.end:                                           ; preds = %if.else, %if.then
  br label %while.cond

while.end:                                        ; preds = %while.cond
  %13 = load i32*, i32** %__first.addr, align 8
  ret i32* %13
}

; Function Attrs: noinline nounwind uwtable
define linkonce_odr void @_ZN9__gnu_cxx5__ops15__val_less_iterEv() #3 comdat {
entry:
  %retval = alloca %"struct.__gnu_cxx::__ops::_Val_less_iter", align 1
  ret void
}

; Function Attrs: noinline uwtable
define linkonce_odr i64 @_ZSt8distanceIPiENSt15iterator_traitsIT_E15difference_typeES2_S2_(i32* %__first, i32* %__last) #0 comdat {
entry:
  %__first.addr = alloca i32*, align 8
  %__last.addr = alloca i32*, align 8
  %agg.tmp = alloca %"struct.std::random_access_iterator_tag", align 1
  %undef.agg.tmp = alloca %"struct.std::random_access_iterator_tag", align 1
  store i32* %__first, i32** %__first.addr, align 8
  store i32* %__last, i32** %__last.addr, align 8
  %0 = load i32*, i32** %__first.addr, align 8
  %1 = load i32*, i32** %__last.addr, align 8
  call void @_ZSt19__iterator_categoryIPiENSt15iterator_traitsIT_E17iterator_categoryERKS2_(i32** dereferenceable(8) %__first.addr)
  %call = call i64 @_ZSt10__distanceIPiENSt15iterator_traitsIT_E15difference_typeES2_S2_St26random_access_iterator_tag(i32* %0, i32* %1)
  ret i64 %call
}

; Function Attrs: noinline uwtable
define linkonce_odr void @_ZSt7advanceIPilEvRT_T0_(i32** dereferenceable(8) %__i, i64 %__n) #0 comdat {
entry:
  %__i.addr = alloca i32**, align 8
  %__n.addr = alloca i64, align 8
  %__d = alloca i64, align 8
  %agg.tmp = alloca %"struct.std::random_access_iterator_tag", align 1
  %undef.agg.tmp = alloca %"struct.std::random_access_iterator_tag", align 1
  store i32** %__i, i32*** %__i.addr, align 8
  store i64 %__n, i64* %__n.addr, align 8
  %0 = load i64, i64* %__n.addr, align 8
  store i64 %0, i64* %__d, align 8
  %1 = load i32**, i32*** %__i.addr, align 8
  %2 = load i64, i64* %__d, align 8
  %3 = load i32**, i32*** %__i.addr, align 8
  call void @_ZSt19__iterator_categoryIPiENSt15iterator_traitsIT_E17iterator_categoryERKS2_(i32** dereferenceable(8) %3)
  call void @_ZSt9__advanceIPilEvRT_T0_St26random_access_iterator_tag(i32** dereferenceable(8) %1, i64 %2)
  ret void
}

; Function Attrs: noinline nounwind uwtable
define linkonce_odr zeroext i1 @_ZNK9__gnu_cxx5__ops14_Val_less_iterclIKiPiEEbRT_T0_(%"struct.__gnu_cxx::__ops::_Val_less_iter"* %this, i32* dereferenceable(4) %__val, i32* %__it) #3 comdat align 2 {
entry:
  %this.addr = alloca %"struct.__gnu_cxx::__ops::_Val_less_iter"*, align 8
  %__val.addr = alloca i32*, align 8
  %__it.addr = alloca i32*, align 8
  store %"struct.__gnu_cxx::__ops::_Val_less_iter"* %this, %"struct.__gnu_cxx::__ops::_Val_less_iter"** %this.addr, align 8
  store i32* %__val, i32** %__val.addr, align 8
  store i32* %__it, i32** %__it.addr, align 8
  %this1 = load %"struct.__gnu_cxx::__ops::_Val_less_iter"*, %"struct.__gnu_cxx::__ops::_Val_less_iter"** %this.addr, align 8
  %0 = load i32*, i32** %__val.addr, align 8
  %1 = load i32, i32* %0, align 4
  %2 = load i32*, i32** %__it.addr, align 8
  %3 = load i32, i32* %2, align 4
  %cmp = icmp slt i32 %1, %3
  ret i1 %cmp
}

; Function Attrs: noinline nounwind uwtable
define linkonce_odr i64 @_ZSt10__distanceIPiENSt15iterator_traitsIT_E15difference_typeES2_S2_St26random_access_iterator_tag(i32* %__first, i32* %__last) #3 comdat {
entry:
  %0 = alloca %"struct.std::random_access_iterator_tag", align 1
  %__first.addr = alloca i32*, align 8
  %__last.addr = alloca i32*, align 8
  store i32* %__first, i32** %__first.addr, align 8
  store i32* %__last, i32** %__last.addr, align 8
  %1 = load i32*, i32** %__last.addr, align 8
  %2 = load i32*, i32** %__first.addr, align 8
  %sub.ptr.lhs.cast = ptrtoint i32* %1 to i64
  %sub.ptr.rhs.cast = ptrtoint i32* %2 to i64
  %sub.ptr.sub = sub i64 %sub.ptr.lhs.cast, %sub.ptr.rhs.cast
  %sub.ptr.div = sdiv exact i64 %sub.ptr.sub, 4
  ret i64 %sub.ptr.div
}

; Function Attrs: noinline nounwind uwtable
define linkonce_odr void @_ZSt19__iterator_categoryIPiENSt15iterator_traitsIT_E17iterator_categoryERKS2_(i32** dereferenceable(8)) #3 comdat {
entry:
  %retval = alloca %"struct.std::random_access_iterator_tag", align 1
  %.addr = alloca i32**, align 8
  store i32** %0, i32*** %.addr, align 8
  ret void
}

; Function Attrs: noinline nounwind uwtable
define linkonce_odr void @_ZSt9__advanceIPilEvRT_T0_St26random_access_iterator_tag(i32** dereferenceable(8) %__i, i64 %__n) #3 comdat {
entry:
  %0 = alloca %"struct.std::random_access_iterator_tag", align 1
  %__i.addr = alloca i32**, align 8
  %__n.addr = alloca i64, align 8
  store i32** %__i, i32*** %__i.addr, align 8
  store i64 %__n, i64* %__n.addr, align 8
  %1 = load i64, i64* %__n.addr, align 8
  %2 = load i32**, i32*** %__i.addr, align 8
  %3 = load i32*, i32** %2, align 8
  %add.ptr = getelementptr inbounds i32, i32* %3, i64 %1
  store i32* %add.ptr, i32** %2, align 8
  ret void
}

; Function Attrs: noinline uwtable
define linkonce_odr i32* @_ZSt13__lower_boundIPiiN9__gnu_cxx5__ops14_Iter_less_valEET_S4_S4_RKT0_T1_(i32* %__first, i32* %__last, i32* dereferenceable(4) %__val) #0 comdat {
entry:
  %__comp = alloca %"struct.__gnu_cxx::__ops::_Iter_less_val", align 1
  %__first.addr = alloca i32*, align 8
  %__last.addr = alloca i32*, align 8
  %__val.addr = alloca i32*, align 8
  %__len = alloca i64, align 8
  %__half = alloca i64, align 8
  %__middle = alloca i32*, align 8
  store i32* %__first, i32** %__first.addr, align 8
  store i32* %__last, i32** %__last.addr, align 8
  store i32* %__val, i32** %__val.addr, align 8
  %0 = load i32*, i32** %__first.addr, align 8
  %1 = load i32*, i32** %__last.addr, align 8
  %call = call i64 @_ZSt8distanceIPiENSt15iterator_traitsIT_E15difference_typeES2_S2_(i32* %0, i32* %1)
  store i64 %call, i64* %__len, align 8
  br label %while.cond

while.cond:                                       ; preds = %if.end, %entry
  %2 = load i64, i64* %__len, align 8
  %cmp = icmp sgt i64 %2, 0
  br i1 %cmp, label %while.body, label %while.end

while.body:                                       ; preds = %while.cond
  %3 = load i64, i64* %__len, align 8
  %shr = ashr i64 %3, 1
  store i64 %shr, i64* %__half, align 8
  %4 = load i32*, i32** %__first.addr, align 8
  store i32* %4, i32** %__middle, align 8
  %5 = load i64, i64* %__half, align 8
  call void @_ZSt7advanceIPilEvRT_T0_(i32** dereferenceable(8) %__middle, i64 %5)
  %6 = load i32*, i32** %__middle, align 8
  %7 = load i32*, i32** %__val.addr, align 8
  %call1 = call zeroext i1 @_ZNK9__gnu_cxx5__ops14_Iter_less_valclIPiKiEEbT_RT0_(%"struct.__gnu_cxx::__ops::_Iter_less_val"* %__comp, i32* %6, i32* dereferenceable(4) %7)
  br i1 %call1, label %if.then, label %if.else

if.then:                                          ; preds = %while.body
  %8 = load i32*, i32** %__middle, align 8
  store i32* %8, i32** %__first.addr, align 8
  %9 = load i32*, i32** %__first.addr, align 8
  %incdec.ptr = getelementptr inbounds i32, i32* %9, i32 1
  store i32* %incdec.ptr, i32** %__first.addr, align 8
  %10 = load i64, i64* %__len, align 8
  %11 = load i64, i64* %__half, align 8
  %sub = sub nsw i64 %10, %11
  %sub2 = sub nsw i64 %sub, 1
  store i64 %sub2, i64* %__len, align 8
  br label %if.end

if.else:                                          ; preds = %while.body
  %12 = load i64, i64* %__half, align 8
  store i64 %12, i64* %__len, align 8
  br label %if.end

if.end:                                           ; preds = %if.else, %if.then
  br label %while.cond

while.end:                                        ; preds = %while.cond
  %13 = load i32*, i32** %__first.addr, align 8
  ret i32* %13
}

; Function Attrs: noinline nounwind uwtable
define linkonce_odr void @_ZN9__gnu_cxx5__ops15__iter_less_valEv() #3 comdat {
entry:
  %retval = alloca %"struct.__gnu_cxx::__ops::_Iter_less_val", align 1
  ret void
}

; Function Attrs: noinline nounwind uwtable
define linkonce_odr zeroext i1 @_ZNK9__gnu_cxx5__ops14_Iter_less_valclIPiKiEEbT_RT0_(%"struct.__gnu_cxx::__ops::_Iter_less_val"* %this, i32* %__it, i32* dereferenceable(4) %__val) #3 comdat align 2 {
entry:
  %this.addr = alloca %"struct.__gnu_cxx::__ops::_Iter_less_val"*, align 8
  %__it.addr = alloca i32*, align 8
  %__val.addr = alloca i32*, align 8
  store %"struct.__gnu_cxx::__ops::_Iter_less_val"* %this, %"struct.__gnu_cxx::__ops::_Iter_less_val"** %this.addr, align 8
  store i32* %__it, i32** %__it.addr, align 8
  store i32* %__val, i32** %__val.addr, align 8
  %this1 = load %"struct.__gnu_cxx::__ops::_Iter_less_val"*, %"struct.__gnu_cxx::__ops::_Iter_less_val"** %this.addr, align 8
  %0 = load i32*, i32** %__it.addr, align 8
  %1 = load i32, i32* %0, align 4
  %2 = load i32*, i32** %__val.addr, align 8
  %3 = load i32, i32* %2, align 4
  %cmp = icmp slt i32 %1, %3
  ret i1 %cmp
}

; Function Attrs: noinline uwtable
define internal void @_GLOBAL__sub_I_OMPMergeSort_NoRegions.cpp() #0 section ".text.startup" {
entry:
  call void @__cxx_global_var_init()
  ret void
}

attributes #0 = { noinline uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nounwind }
attributes #3 = { noinline nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.ident = !{!0}

!0 = !{!"clang version 5.0.0 (git@github.com:llvm-mirror/clang.git 84f57d869ba15eb9b12abc57014d530647a00e01) (git@github.com:llvm-mirror/llvm.git c571744eac2b64565cf6acb829ab649656b7af89)"}
