; ModuleID = '../for_pir.cpp'
source_filename = "../for_pir.cpp"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%"class.std::basic_ostream" = type { i32 (...)**, %"class.std::basic_ios" }
%"class.std::basic_ios" = type { %"class.std::ios_base", %"class.std::basic_ostream"*, i8, i8, %"class.std::basic_streambuf"*, %"class.std::ctype"*, %"class.std::num_put"*, %"class.std::num_get"* }
%"class.std::ios_base" = type { i32 (...)**, i64, i64, i32, i32, i32, %"struct.std::ios_base::_Callback_list"*, %"struct.std::ios_base::_Words", [8 x %"struct.std::ios_base::_Words"], i32, %"struct.std::ios_base::_Words"*, %"class.std::locale" }
%"struct.std::ios_base::_Callback_list" = type { %"struct.std::ios_base::_Callback_list"*, void (i32, %"class.std::ios_base"*, i32)*, i32, i32 }
%"struct.std::ios_base::_Words" = type { i8*, i64 }
%"class.std::locale" = type { %"class.std::locale::_Impl"* }
%"class.std::locale::_Impl" = type { i32, %"class.std::locale::facet"**, i64, %"class.std::locale::facet"**, i8** }
%"class.std::locale::facet" = type <{ i32 (...)**, i32, [4 x i8] }>
%"class.std::basic_streambuf" = type { i32 (...)**, i8*, i8*, i8*, i8*, i8*, i8*, %"class.std::locale" }
%"class.std::ctype" = type <{ %"class.std::locale::facet.base", [4 x i8], %struct.__locale_struct*, i8, [7 x i8], i32*, i32*, i16*, i8, [256 x i8], [256 x i8], i8, [6 x i8] }>
%"class.std::locale::facet.base" = type <{ i32 (...)**, i32 }>
%struct.__locale_struct = type { [13 x %struct.__locale_data*], i16*, i32*, i32*, [13 x i8*] }
%struct.__locale_data = type opaque
%"class.std::num_put" = type { %"class.std::locale::facet.base", [4 x i8] }
%"class.std::num_get" = type { %"class.std::locale::facet.base", [4 x i8] }
%"class.std::ios_base::Init" = type { i8 }

@_ZSt4cout = external global %"class.std::basic_ostream", align 8
@.str = private unnamed_addr constant [2 x i8] c"(\00", align 1
@.str.1 = private unnamed_addr constant [3 x i8] c", \00", align 1
@.str.2 = private unnamed_addr constant [2 x i8] c")\00", align 1
@_ZStL8__ioinit = internal global %"class.std::ios_base::Init" zeroinitializer, align 1
@__dso_handle = external global i8
@llvm.global_ctors = appending global [1 x { i32, void ()*, i8* }] [{ i32, void ()*, i8* } { i32 65535, void ()* @_GLOBAL__sub_I_for_pir.cpp, i8* null }]

; Function Attrs: noinline norecurse uwtable
define i32 @main(i32 %argc, i8** %argv) #0 {
entry:
  %retval = alloca i32, align 4
  %argc.addr = alloca i32, align 4
  %argv.addr = alloca i8**, align 8
  %i = alloca i32, align 4
  %n = alloca i32, align 4
  %a = alloca float*, align 8
  %b = alloca float*, align 8
  %i3 = alloca i32, align 4
  %i15 = alloca i32, align 4
  store i32 0, i32* %retval, align 4
  store i32 %argc, i32* %argc.addr, align 4
  store i8** %argv, i8*** %argv.addr, align 8
  %0 = load i32, i32* %argc.addr, align 4
  %1 = load i8**, i8*** %argv.addr, align 8
  %call = call i32 @_Z9init_sizeiPPc(i32 %0, i8** %1)
  store i32 %call, i32* %n, align 4
  %2 = load i32, i32* %n, align 4
  %call1 = call float* @_Z9alloc_arri(i32 %2)
  store float* %call1, float** %a, align 8
  %3 = load i32, i32* %n, align 4
  %call2 = call float* @_Z9alloc_arri(i32 %3)
  store float* %call2, float** %b, align 8
  store i32 0, i32* %i3, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %4 = load i32, i32* %i3, align 4
  %5 = load i32, i32* %n, align 4
  %cmp = icmp slt i32 %4, %5
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %6 = load i32, i32* %i3, align 4
  %conv = sitofp i32 %6 to float
  %7 = load float*, float** %a, align 8
  %8 = load i32, i32* %i3, align 4
  %idxprom = sext i32 %8 to i64
  %arrayidx = getelementptr inbounds float, float* %7, i64 %idxprom
  store float %conv, float* %arrayidx, align 4
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %9 = load i32, i32* %i3, align 4
  %inc = add nsw i32 %9, 1
  store i32 %inc, i32* %i3, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  store i32 0, i32* %i, align 4
  br label %for.cond4

for.cond4:                                        ; preds = %for.end
  %10 = load i32, i32* %i, align 4
  %11 = load i32, i32* %n, align 4
  %cmp5 = icmp slt i32 %10, %11
  br i1 %cmp5, label %for.fork, label %for.end14

for.fork:                                         ; preds = %for.inc11, %for.cond4
  fork label %for.body6, %for.inc11

for.body6:                                        ; preds = %for.fork
  %call7 = call i32 @omp_get_thread_num()
  %conv8 = sitofp i32 %call7 to float
  %12 = load float*, float** %b, align 8
  %13 = load i32, i32* %i, align 4
  %idxprom9 = sext i32 %13 to i64
  %arrayidx10 = getelementptr inbounds float, float* %12, i64 %idxprom9
  store float %conv8, float* %arrayidx10, align 4
  halt label %for.inc11

for.inc11:                                        ; preds = %for.body6, %for.fork
  %14 = load i32, i32* %i, align 4
  %inc12 = add nsw i32 %14, 1
  store i32 %inc12, i32* %i, align 4
  %15 = load i32, i32* %i, align 4
  %16 = load i32, i32* %n, align 4
  %cmp13 = icmp slt i32 %15, %16
  br i1 %cmp13, label %for.fork, label %for.join

for.join:                                         ; preds = %for.inc11
  join label %for.end14

for.end14:                                        ; preds = %for.join, %for.cond4
  store i32 0, i32* %i15, align 4
  br label %for.cond16

for.cond16:                                       ; preds = %for.inc29, %for.end14
  %17 = load i32, i32* %i15, align 4
  %18 = load i32, i32* %n, align 4
  %cmp17 = icmp slt i32 %17, %18
  br i1 %cmp17, label %for.body18, label %for.end31

for.body18:                                       ; preds = %for.cond16
  %call19 = call dereferenceable(272) %"class.std::basic_ostream"* @_ZStlsISt11char_traitsIcEERSt13basic_ostreamIcT_ES5_PKc(%"class.std::basic_ostream"* dereferenceable(272) @_ZSt4cout, i8* getelementptr inbounds ([2 x i8], [2 x i8]* @.str, i32 0, i32 0))
  %19 = load float*, float** %a, align 8
  %20 = load i32, i32* %i15, align 4
  %idxprom20 = sext i32 %20 to i64
  %arrayidx21 = getelementptr inbounds float, float* %19, i64 %idxprom20
  %21 = load float, float* %arrayidx21, align 4
  %call22 = call dereferenceable(272) %"class.std::basic_ostream"* @_ZNSolsEf(%"class.std::basic_ostream"* %call19, float %21)
  %call23 = call dereferenceable(272) %"class.std::basic_ostream"* @_ZStlsISt11char_traitsIcEERSt13basic_ostreamIcT_ES5_PKc(%"class.std::basic_ostream"* dereferenceable(272) %call22, i8* getelementptr inbounds ([3 x i8], [3 x i8]* @.str.1, i32 0, i32 0))
  %22 = load float*, float** %b, align 8
  %23 = load i32, i32* %i15, align 4
  %idxprom24 = sext i32 %23 to i64
  %arrayidx25 = getelementptr inbounds float, float* %22, i64 %idxprom24
  %24 = load float, float* %arrayidx25, align 4
  %call26 = call dereferenceable(272) %"class.std::basic_ostream"* @_ZNSolsEf(%"class.std::basic_ostream"* %call23, float %24)
  %call27 = call dereferenceable(272) %"class.std::basic_ostream"* @_ZStlsISt11char_traitsIcEERSt13basic_ostreamIcT_ES5_PKc(%"class.std::basic_ostream"* dereferenceable(272) %call26, i8* getelementptr inbounds ([2 x i8], [2 x i8]* @.str.2, i32 0, i32 0))
  %call28 = call dereferenceable(272) %"class.std::basic_ostream"* @_ZNSolsEPFRSoS_E(%"class.std::basic_ostream"* %call27, %"class.std::basic_ostream"* (%"class.std::basic_ostream"*)* @_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_)
  br label %for.inc29

for.inc29:                                        ; preds = %for.body18
  %25 = load i32, i32* %i15, align 4
  %inc30 = add nsw i32 %25, 1
  store i32 %inc30, i32* %i15, align 4
  br label %for.cond16

for.end31:                                        ; preds = %for.cond16
  %call32 = call dereferenceable(272) %"class.std::basic_ostream"* @_ZNSolsEPFRSoS_E(%"class.std::basic_ostream"* @_ZSt4cout, %"class.std::basic_ostream"* (%"class.std::basic_ostream"*)* @_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_)
  ret i32 0
}

declare i32 @_Z9init_sizeiPPc(i32, i8**) #1

declare float* @_Z9alloc_arri(i32) #1

declare i32 @omp_get_thread_num() #1

declare dereferenceable(272) %"class.std::basic_ostream"* @_ZStlsISt11char_traitsIcEERSt13basic_ostreamIcT_ES5_PKc(%"class.std::basic_ostream"* dereferenceable(272), i8*) #1

declare dereferenceable(272) %"class.std::basic_ostream"* @_ZNSolsEf(%"class.std::basic_ostream"*, float) #1

declare dereferenceable(272) %"class.std::basic_ostream"* @_ZNSolsEPFRSoS_E(%"class.std::basic_ostream"*, %"class.std::basic_ostream"* (%"class.std::basic_ostream"*)*) #1

declare dereferenceable(272) %"class.std::basic_ostream"* @_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_(%"class.std::basic_ostream"* dereferenceable(272)) #1

; Function Attrs: noinline uwtable
define internal void @__cxx_global_var_init() #2 section ".text.startup" {
entry:
  call void @_ZNSt8ios_base4InitC1Ev(%"class.std::ios_base::Init"* @_ZStL8__ioinit)
  %0 = call i32 @__cxa_atexit(void (i8*)* bitcast (void (%"class.std::ios_base::Init"*)* @_ZNSt8ios_base4InitD1Ev to void (i8*)*), i8* getelementptr inbounds (%"class.std::ios_base::Init", %"class.std::ios_base::Init"* @_ZStL8__ioinit, i32 0, i32 0), i8* @__dso_handle) #4
  ret void
}

declare void @_ZNSt8ios_base4InitC1Ev(%"class.std::ios_base::Init"*) unnamed_addr #1

; Function Attrs: nounwind
declare void @_ZNSt8ios_base4InitD1Ev(%"class.std::ios_base::Init"*) unnamed_addr #3

; Function Attrs: nounwind
declare i32 @__cxa_atexit(void (i8*)*, i8*, i8*) #4

; Function Attrs: noinline uwtable
define internal void @_GLOBAL__sub_I_for_pir.cpp() #2 section ".text.startup" {
entry:
  call void @__cxx_global_var_init()
  ret void
}

attributes #0 = { noinline norecurse uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { noinline uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { nounwind "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #4 = { nounwind }

!llvm.ident = !{!0}

!0 = !{!"clang version 5.0.0 (git@github.com:Parallel-IR/clang-pir.git 1910d83716bc3b952c49c48846dfd0687ad26b63) (git@github.com:Parallel-IR/llvm-pir.git 77cdff238a4ef12d3ae9244af3228a2061eae35e)"}
