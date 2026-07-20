#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace leash {

// Tagged value. User values never hold raw heap pointers: strings/lists/maps/funcs/caps
// are stored in the VM's indexed stores and referenced by an index in `i`.
struct Value {
    enum class T { Nil, Int, Float, Bool, Str, List, Map, Func, Cap } t = T::Nil;
    int64_t i = 0;     // Int payload, or store index for Str/List/Map/Func/Cap
    double  f = 0.0;   // Float payload
    bool    b = false; // Bool payload

    Value() = default;
    static Value nil() { return Value{}; }
    static Value makeInt(int64_t v)   { Value vv; vv.t = T::Int;  vv.i = v; return vv; }
    static Value makeFloat(double v)  { Value vv; vv.t = T::Float; vv.f = v; return vv; }
    static Value makeBool(bool v)     { Value vv; vv.t = T::Bool; vv.b = v; return vv; }
    static Value makeStr(uint32_t idx){ Value vv; vv.t = T::Str;  vv.i = (int64_t)idx; return vv; }
    static Value makeList(uint32_t idx){ Value vv; vv.t = T::List; vv.i = (int64_t)idx; return vv; }
    static Value makeMap(uint32_t idx){ Value vv; vv.t = T::Map;  vv.i = (int64_t)idx; return vv; }
    static Value makeFunc(uint32_t idx){ Value vv; vv.t = T::Func; vv.i = (int64_t)idx; return vv; }
    static Value makeCap(uint32_t idx){ Value vv; vv.t = T::Cap;  vv.i = (int64_t)idx; return vv; }

    uint32_t idx() const { return (uint32_t)i; }
};

// Indexed stores shared by the VM and host capabilities.
struct Store {
    std::vector<std::string> strings;
    std::vector<std::vector<Value>> lists;
    std::vector<std::map<std::string, Value>> maps;

    uint32_t intern(const std::string& s) { strings.push_back(s); return (uint32_t)strings.size() - 1; }
    const std::string& str(uint32_t i) const { return strings[i]; }
    uint32_t internList(const std::vector<Value>& l) { lists.push_back(l); return (uint32_t)lists.size() - 1; }
    uint32_t internMap(const std::map<std::string, Value>& m) { maps.push_back(m); return (uint32_t)maps.size() - 1; }
};

// Convert any value to a human-readable string using the store.
inline std::string valueToStr(const Store& store, const Value& v) {
    switch (v.t) {
        case Value::T::Nil:   return "nil";
        case Value::T::Int:   return std::to_string(v.i);
        case Value::T::Float: return std::to_string(v.f);
        case Value::T::Bool:  return v.b ? "true" : "false";
        case Value::T::Str:   return store.str(v.idx());
        case Value::T::List: {
            const auto& l = store.lists[v.idx()];
            std::string s = "[";
            for (size_t k = 0; k < l.size(); ++k) { if (k) s += ", "; s += valueToStr(store, l[k]); }
            return s + "]";
        }
        case Value::T::Map: {
            const auto& m = store.maps[v.idx()];
            std::string s = "{";
            bool first = true;
            for (const auto& kv : m) {
                if (!first) s += ", ";
                first = false;
                s += "\"" + kv.first + "\": " + valueToStr(store, kv.second);
            }
            return s + "}";
        }
        case Value::T::Func:  return "[fn]";
        case Value::T::Cap:   return "[cap]";
    }
    return "?";
}

} // namespace leash
