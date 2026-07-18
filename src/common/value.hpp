#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace aegis {

// Tagged value. User values never hold raw heap pointers: strings/lists/funcs/caps
// are stored in the VM's indexed stores and referenced by an index in `i`.
struct Value {
    enum class T { Nil, Int, Float, Bool, Str, List, Func, Cap } t = T::Nil;
    int64_t i = 0;     // Int payload, or store index for Str/List/Func/Cap
    double  f = 0.0;   // Float payload
    bool    b = false; // Bool payload

    Value() = default;
    static Value nil() { return Value{}; }
    static Value makeInt(int64_t v)   { Value vv; vv.t = T::Int;  vv.i = v; return vv; }
    static Value makeFloat(double v)  { Value vv; vv.t = T::Float; vv.f = v; return vv; }
    static Value makeBool(bool v)     { Value vv; vv.t = T::Bool; vv.b = v; return vv; }
    static Value makeStr(uint32_t idx){ Value vv; vv.t = T::Str;  vv.i = (int64_t)idx; return vv; }
    static Value makeList(uint32_t idx){ Value vv; vv.t = T::List; vv.i = (int64_t)idx; return vv; }
    static Value makeFunc(uint32_t idx){ Value vv; vv.t = T::Func; vv.i = (int64_t)idx; return vv; }
    static Value makeCap(uint32_t idx){ Value vv; vv.t = T::Cap;  vv.i = (int64_t)idx; return vv; }

    uint32_t idx() const { return (uint32_t)i; }
};

// Indexed stores shared by the VM and host capabilities.
struct Store {
    std::vector<std::string> strings;
    std::vector<std::vector<Value>> lists;

    uint32_t intern(const std::string& s) { strings.push_back(s); return (uint32_t)strings.size() - 1; }
    const std::string& str(uint32_t i) const { return strings[i]; }
};

// Convert any value to a human-readable string using the store.
inline std::string valueToStr(const Store& store, const Value& v) {
    switch (v.t) {
        case Value::T::Nil:   return "nil";
        case Value::T::Int:   return std::to_string(v.i);
        case Value::T::Float: return std::to_string(v.f);
        case Value::T::Bool:  return v.b ? "true" : "false";
        case Value::T::Str:   return store.str(v.idx());
        case Value::T::List:  return "[list]";
        case Value::T::Func:  return "[fn]";
        case Value::T::Cap:   return "[cap]";
    }
    return "?";
}

} // namespace aegis
