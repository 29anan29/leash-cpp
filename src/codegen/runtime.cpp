// Leash 编译模式运行时 — 提供 LLVM IR 声明的 leash_runtime_* 函数
// 编译：g++ -std=c++17 -O2 -c runtime.cpp -o libleash_runtime.o
// 链接：g++ output.o libleash_runtime.o -o output

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <cstdlib>

// ========== Value 类型（与 value.hpp 一致） ==========
enum class Tag : int32_t { Nil = 0, Int = 1, Float = 2, Bool = 3, Str = 4, List = 5, Map = 6, Func = 7, Cap = 8 };

struct Value {
    Tag tag = Tag::Nil;
    int64_t i = 0;
    double f = 0.0;
    bool b = false;
};

// ========== Store（字符串/列表/映射池） ==========
struct Store {
    std::vector<std::string> strings;
    std::vector<std::vector<Value>> lists;
    std::vector<std::map<std::string, Value>> maps;

    uint32_t intern(const std::string& s) {
        strings.push_back(s);
        return (uint32_t)strings.size() - 1;
    }
};

static Store g_store;

// ========== 全局变量（JSON 注入 + 运行时赋值） ==========
static std::unordered_map<std::string, Value> g_globals;

static std::string valToStr(const Value& v) {
    switch (v.tag) {
        case Tag::Nil: return "nil";
        case Tag::Int: return std::to_string(v.i);
        case Tag::Float: return std::to_string(v.f);
        case Tag::Bool: return v.b ? "true" : "false";
        case Tag::Str: return g_store.strings[(uint32_t)v.i];
        default: return "<opaque>";
    }
}

// ========== 运行时函数实现 ==========

extern "C" {

void leash_runtime_init() {
    // 初始化（预留）
}

int32_t leash_runtime_get_tag(int64_t* vptr) {
    return (int32_t)((Value*)vptr)->tag;
}

void leash_runtime_make_nil(Value* v) { v->tag = Tag::Nil; v->i = 0; v->f = 0; v->b = false; }
void leash_runtime_make_int(Value* v, int64_t i) { v->tag = Tag::Int; v->i = i; }
void leash_runtime_make_float(Value* v, double f) { v->tag = Tag::Float; v->f = f; }
void leash_runtime_make_bool(Value* v, bool b) { v->tag = Tag::Bool; v->b = b; }
void leash_runtime_make_str(Value* v, const char* s) { v->tag = Tag::Str; v->i = g_store.intern(s); }

void leash_runtime_add(Value* dst, Value* a, Value* b) {
    if (a->tag == Tag::Int && b->tag == Tag::Int) { dst->tag = Tag::Int; dst->i = a->i + b->i; }
    else { dst->tag = Tag::Float; dst->f = (a->tag == Tag::Int ? (double)a->i : a->f) + (b->tag == Tag::Int ? (double)b->i : b->f); }
}
void leash_runtime_sub(Value* dst, Value* a, Value* b) {
    if (a->tag == Tag::Int && b->tag == Tag::Int) { dst->tag = Tag::Int; dst->i = a->i - b->i; }
    else { dst->tag = Tag::Float; dst->f = (a->tag == Tag::Int ? (double)a->i : a->f) - (b->tag == Tag::Int ? (double)b->i : b->f); }
}
void leash_runtime_mul(Value* dst, Value* a, Value* b) {
    if (a->tag == Tag::Int && b->tag == Tag::Int) { dst->tag = Tag::Int; dst->i = a->i * b->i; }
    else { dst->tag = Tag::Float; dst->f = (a->tag == Tag::Int ? (double)a->i : a->f) * (b->tag == Tag::Int ? (double)b->i : b->f); }
}
void leash_runtime_div(Value* dst, Value* a, Value* b) {
    if (a->tag == Tag::Int && b->tag == Tag::Int && b->i != 0) { dst->tag = Tag::Int; dst->i = a->i / b->i; }
    else { dst->tag = Tag::Float; double da = (a->tag == Tag::Int ? (double)a->i : a->f); double db = (b->tag == Tag::Int ? (double)b->i : b->f); dst->f = da / db; }
}
void leash_runtime_mod(Value* dst, Value* a, Value* b) {
    dst->tag = Tag::Int; dst->i = (b->i != 0) ? a->i % b->i : 0;
}
void leash_runtime_neg(Value* dst, Value* a) {
    if (a->tag == Tag::Int) { dst->tag = Tag::Int; dst->i = -a->i; }
    else { dst->tag = Tag::Float; dst->f = -a->f; }
}
void leash_runtime_concat(Value* dst, Value* a, Value* b) {
    std::string sa = valToStr(*a);
    std::string sb = valToStr(*b);
    dst->tag = Tag::Str; dst->i = g_store.intern(sa + sb);
}
void leash_runtime_eq(Value* dst, Value* a, Value* b) {
    bool eq = false;
    if (a->tag == Tag::Int && b->tag == Tag::Int) eq = (a->i == b->i);
    else if (a->tag == Tag::Float && b->tag == Tag::Float) eq = (a->f == b->f);
    else if (a->tag == Tag::Bool && b->tag == Tag::Bool) eq = (a->b == b->b);
    else if (a->tag == Tag::Str && b->tag == Tag::Str) eq = (g_store.strings[(uint32_t)a->i] == g_store.strings[(uint32_t)b->i]);
    dst->tag = Tag::Bool; dst->b = eq;
}
void leash_runtime_lt(Value* dst, Value* a, Value* b) {
    bool lt = false;
    if (a->tag == Tag::Int && b->tag == Tag::Int) lt = (a->i < b->i);
    else { double da = (a->tag == Tag::Int ? (double)a->i : a->f); double db = (b->tag == Tag::Int ? (double)b->i : b->f); lt = (da < db); }
    dst->tag = Tag::Bool; dst->b = lt;
}
void leash_runtime_gt(Value* dst, Value* a, Value* b) {
    bool gt = false;
    if (a->tag == Tag::Int && b->tag == Tag::Int) gt = (a->i > b->i);
    else { double da = (a->tag == Tag::Int ? (double)a->i : a->f); double db = (b->tag == Tag::Int ? (double)b->i : b->f); gt = (da > db); }
    dst->tag = Tag::Bool; dst->b = gt;
}
void leash_runtime_le(Value* dst, Value* a, Value* b) {
    bool le = false;
    if (a->tag == Tag::Int && b->tag == Tag::Int) le = (a->i <= b->i);
    else { double da = (a->tag == Tag::Int ? (double)a->i : a->f); double db = (b->tag == Tag::Int ? (double)b->i : b->f); le = (da <= db); }
    dst->tag = Tag::Bool; dst->b = le;
}
void leash_runtime_ge(Value* dst, Value* a, Value* b) {
    bool ge = false;
    if (a->tag == Tag::Int && b->tag == Tag::Int) ge = (a->i >= b->i);
    else { double da = (a->tag == Tag::Int ? (double)a->i : a->f); double db = (b->tag == Tag::Int ? (double)b->i : b->f); ge = (da >= db); }
    dst->tag = Tag::Bool; dst->b = ge;
}
void leash_runtime_not(Value* dst, Value* a) {
    dst->tag = Tag::Bool; dst->b = !(a->tag == Tag::Bool && a->b);
}
void leash_runtime_get_global(Value* dst, const char* name) {
    auto it = g_globals.find(name);
    if (it != g_globals.end()) *dst = it->second;
    else leash_runtime_make_nil(dst);
}
void leash_runtime_set_global(const char* name, Value* v) {
    g_globals[name] = *v;
}
void leash_runtime_make_list(Value* dst, int64_t* /*unused*/, int32_t /*unused*/) {
    dst->tag = Tag::List; dst->i = g_store.lists.size();
    g_store.lists.emplace_back();
}
void leash_runtime_make_map(Value* dst, int64_t*) {
    dst->tag = Tag::Map; dst->i = g_store.maps.size();
    g_store.maps.emplace_back();
}

int32_t leash_runtime_call(int32_t funcIdx, int64_t* /*capEnv*/, int32_t /*arity*/, int64_t* /*args*/) {
    // 在 LLVM IR 生成模式下，用户函数已被内联为 LLVM 函数直接调用。
    // 此桥接仅用于原生函数。
    return 0;
}

void leash_runtime_invoke_cap(Value* dst, int64_t* /*capEnv*/, int32_t /*capIdx*/, int32_t /*methodIdx*/, int64_t* /*args*/) {
    // 简化实现：能力调用在编译模式下暂用 stderr 代理
    leash_runtime_make_nil(dst);
}

void leash_runtime_get_index(Value* dst, Value* container, Value* index) {
    if (container->tag == Tag::Str && index->tag == Tag::Int) {
        const std::string& s = g_store.strings[(uint32_t)container->i];
        int64_t idx = index->i;
        if (idx < 0) idx = (int64_t)s.size() + idx;
        if (idx >= 0 && idx < (int64_t)s.size()) {
            std::string ch(1, s[(size_t)idx]);
            dst->tag = Tag::Str; dst->i = g_store.intern(ch);
        } else leash_runtime_make_nil(dst);
    } else if (container->tag == Tag::List && index->tag == Tag::Int) {
        auto& lst = g_store.lists[(uint32_t)container->i];
        int64_t idx = index->i;
        if (idx < 0) idx = (int64_t)lst.size() + idx;
        if (idx >= 0 && idx < (int64_t)lst.size()) *dst = lst[(size_t)idx];
        else leash_runtime_make_nil(dst);
    } else if (container->tag == Tag::Map) {
        auto& mp = g_store.maps[(uint32_t)container->i];
        std::string key = valToStr(*index);
        auto it = mp.find(key);
        if (it != mp.end()) *dst = it->second;
        else leash_runtime_make_nil(dst);
    } else leash_runtime_make_nil(dst);
}

void leash_runtime_set_index(Value* dst, Value* container, Value* index, Value* val) {
    if (container->tag == Tag::List && index->tag == Tag::Int) {
        auto& lst = g_store.lists[(uint32_t)container->i];
        int64_t idx = index->i;
        if (idx >= 0 && idx < (int64_t)lst.size()) lst[(size_t)idx] = *val;
    } else if (container->tag == Tag::Map) {
        auto& mp = g_store.maps[(uint32_t)container->i];
        std::string key = valToStr(*index);
        mp[key] = *val;
    }
    *dst = *container;
}

} // extern "C"
