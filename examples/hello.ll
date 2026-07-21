; Leash 编译产物 — LLVM IR
target triple = "x86_64-pc-linux-gnu"

%Value = type { i32, i64, double, i1 }

declare void @leash_runtime_init()
declare i32 @leash_runtime_get_tag(i64*)
declare void @leash_runtime_make_int(i64*, i64)
declare void @leash_runtime_make_float(i64*, double)
declare void @leash_runtime_make_bool(i64*, i1)
declare void @leash_runtime_make_nil(i64*)
declare void @leash_runtime_make_str(i64*, i8*)
declare void @leash_runtime_add(i64*, i64*, i64*)
declare void @leash_runtime_sub(i64*, i64*, i64*)
declare void @leash_runtime_mul(i64*, i64*, i64*)
declare void @leash_runtime_div(i64*, i64*, i64*)
declare void @leash_runtime_mod(i64*, i64*, i64*)
declare void @leash_runtime_neg(i64*, i64*)
declare void @leash_runtime_concat(i64*, i64*, i64*)
declare void @leash_runtime_eq(i64*, i64*, i64*)
declare void @leash_runtime_lt(i64*, i64*, i64*)
declare void @leash_runtime_gt(i64*, i64*, i64*)
declare void @leash_runtime_le(i64*, i64*, i64*)
declare void @leash_runtime_ge(i64*, i64*, i64*)
declare void @leash_runtime_not(i64*, i64*)
declare void @leash_runtime_get_index(i64*, i64*, i64*)
declare i32 @leash_runtime_call(i32, i64*, i32, i64*)
declare void @leash_runtime_invoke_cap(i64*, i64*, i32, i32, i64*)
declare void @leash_runtime_get_global(i64*, i8*)
declare void @leash_runtime_set_global(i8*, i64*)
declare void @leash_runtime_make_list(i64*, i64*, i32)
declare void @leash_runtime_make_map(i64*, i64*)
declare void @leash_runtime_set_index(i64*, i64*, i64*, i64*)

define i32 @main(i32 %argc, i8** %argv) {
  call void @leash_runtime_init()
  %main_val = alloca %Value
  %args = alloca %Value, i32 0
  %ret = call i32 @leash_runtime_call(i32 0, i64* null, i32 0, i64* null)
  ret i32 0
}
