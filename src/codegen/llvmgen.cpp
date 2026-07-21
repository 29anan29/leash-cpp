#include "codegen/llvmgen.hpp"
#include "common/value.hpp"
#include <sstream>
#include <cassert>
#include <fstream>
#include <set>

namespace leash {

std::string LLVMGen::generate(const std::vector<Function>& funcs,
                               const std::vector<std::string>& globalNames) {
    (void)globalNames;
    funcNames_.clear();
    std::ostringstream os;

    os << "// Leash 编译产物\n";
    os << "#include <cstdint>\n";
    os << "#include <cstdio>\n";
    os << "#include <string>\n";
    os << "#include <vector>\n";
    os << "#include <map>\n";
    os << "#include <unordered_map>\n";
    os << "#include <cstring>\n";
    os << "#include <cstdlib>\n";
    os << "#include <cmath>\n";
    os << "#include <ctime>\n\n";

    os << "enum class Tag : int { Nil=0, Int=1, Float=2, Bool=3, Str=4, List=5, Map=6, Func=7 };\n";
    os << "struct Val {\n";
    os << "  Tag tag = Tag::Nil;\n";
    os << "  int64_t i = 0;\n";
    os << "  double f = 0.0;\n";
    os << "  bool b = false;\n";
    os << "};\n\n";

    os << "static std::vector<std::string> g_strs;\n";
    os << "static std::vector<std::vector<Val>> g_lsts;\n";
    os << "static std::vector<std::map<std::string,Val>> g_maps;\n";
    os << "static std::unordered_map<std::string,Val> g_globals;\n\n";

    os << "static std::string v2s(const Val& v) {\n";
    os << "  switch(v.tag) {\n";
    os << "    case Tag::Nil: return \"nil\";\n";
    os << "    case Tag::Int: return std::to_string(v.i);\n";
    os << "    case Tag::Float: return std::to_string(v.f);\n";
    os << "    case Tag::Bool: return v.b?\"true\":\"false\";\n";
    os << "    case Tag::Str: return g_strs[(uint32_t)v.i];\n";
    os << "    case Tag::List: {\n";
    os << "      std::string _r = \"[\";\n";
    os << "      const auto& _lv = g_lsts[(uint32_t)v.i];\n";
    os << "      for (size_t _li = 0; _li < _lv.size(); ++_li) {\n";
    os << "        if (_li > 0) _r += \", \";\n";
    os << "        _r += v2s(_lv[_li]);\n";
    os << "      }\n";
    os << "      return _r + \"]\";\n";
    os << "    }\n";
    os << "    case Tag::Map: return \"<map>\";\n";
    os << "    default: return \"?\";\n";
    os << "  }\n";
    os << "}\n\n";

    // Native function stubs
    os << "// Native function stubs\n";
    os << "static Val leash_native_len(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  if (a.tag == Tag::Str) r.i = (int64_t)g_strs[(uint32_t)a.i].size();\n";
    os << "  else if (a.tag == Tag::List) r.i = (int64_t)g_lsts[(uint32_t)a.i].size();\n";
    os << "  else if (a.tag == Tag::Map) r.i = (int64_t)g_maps[(uint32_t)a.i].size();\n";
    os << "  else r.i = 0;\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_ord(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  if (a.tag == Tag::Str) { auto& s = g_strs[(uint32_t)a.i]; r.i = s.empty()?0:(unsigned char)s[0]; }\n";
    os << "  else r.i = 0;\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_chr(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Str;\n";
    os << "  std::string s(1, (char)(a.i & 0xFF));\n";
    os << "  r.i = (int64_t)g_strs.size(); g_strs.push_back(s);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_sqrt(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Float;\n";
    os << "  double v = (a.tag==Tag::Int)?(double)a.i:a.f;\n";
    os << "  r.f = std::sqrt(v);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_abs(const Val& a) {\n";
    os << "  Val r;\n";
    os << "  if (a.tag == Tag::Int) { r.tag = Tag::Int; r.i = a.i<0?-a.i:a.i; }\n";
    os << "  else { r.tag = Tag::Float; double v = (a.tag==Tag::Int)?(double)a.i:a.f; r.f = v<0?-v:v; }\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_floor(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  double v = (a.tag==Tag::Int)?(double)a.i:a.f;\n";
    os << "  r.i = (int64_t)std::floor(v);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_ceil(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  double v = (a.tag==Tag::Int)?(double)a.i:a.f;\n";
    os << "  r.i = (int64_t)std::ceil(v);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_round(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  double v = (a.tag==Tag::Int)?(double)a.i:a.f;\n";
    os << "  r.i = (int64_t)std::round(v);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_pow(const Val& a, const Val& b) {\n";
    os << "  Val r; r.tag = Tag::Float;\n";
    os << "  double va = (a.tag==Tag::Int)?(double)a.i:a.f;\n";
    os << "  double vb = (b.tag==Tag::Int)?(double)b.i:b.f;\n";
    os << "  r.f = std::pow(va, vb);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_now() {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  r.i = (int64_t)std::time(nullptr);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_stringify(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Str;\n";
    os << "  std::string s = v2s(a);\n";
    os << "  r.i = (int64_t)g_strs.size(); g_strs.push_back(s);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_typeof(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Str;\n";
    os << "  const char* t = \"?\";\n";
    os << "  switch(a.tag) {\n";
    os << "    case Tag::Nil: t = \"nil\"; break;\n";
    os << "    case Tag::Int: t = \"int\"; break;\n";
    os << "    case Tag::Float: t = \"float\"; break;\n";
    os << "    case Tag::Bool: t = \"bool\"; break;\n";
    os << "    case Tag::Str: t = \"str\"; break;\n";
    os << "    case Tag::List: t = \"list\"; break;\n";
    os << "    case Tag::Map: t = \"map\"; break;\n";
    os << "    default: break;\n";
    os << "  }\n";
    os << "  r.i = (int64_t)g_strs.size(); g_strs.push_back(t);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_to_int(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  if (a.tag == Tag::Str) { try { r.i = std::stoll(g_strs[(uint32_t)a.i]); } catch(...) { r.i=0; } }\n";
    os << "  else if (a.tag == Tag::Float) r.i = (int64_t)a.f;\n";
    os << "  else if (a.tag == Tag::Int) r.i = a.i;\n";
    os << "  else r.i = 0;\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_to_float(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Float;\n";
    os << "  if (a.tag == Tag::Int) r.f = (double)a.i;\n";
    os << "  else if (a.tag == Tag::Float) r.f = a.f;\n";
    os << "  else if (a.tag == Tag::Str) { try { r.f = std::stod(g_strs[(uint32_t)a.i]); } catch(...) { r.f=0; } }\n";
    os << "  else r.f = 0.0;\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_sleep(const Val& a) {\n";
    os << "  (void)a;\n";
    os << "  return Val();\n";
    os << "}\n";
    os << "static Val leash_native_band(const Val& a, const Val& b) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  r.i = (a.tag==Tag::Int?a.i:0) & (b.tag==Tag::Int?b.i:0);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_bor(const Val& a, const Val& b) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  r.i = (a.tag==Tag::Int?a.i:0) | (b.tag==Tag::Int?b.i:0);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_bxor(const Val& a, const Val& b) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  r.i = (a.tag==Tag::Int?a.i:0) ^ (b.tag==Tag::Int?b.i:0);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_bnot(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  r.i = ~(a.tag==Tag::Int?a.i:0);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_bshl(const Val& a, const Val& b) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  r.i = (a.tag==Tag::Int?a.i:0) << (b.tag==Tag::Int?b.i:0);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_bshr(const Val& a, const Val& b) {\n";
    os << "  Val r; r.tag = Tag::Int;\n";
    os << "  r.i = (a.tag==Tag::Int?a.i:0) >> (b.tag==Tag::Int?b.i:0);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_sha256(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Str;\n";
    os << "  std::string s = (a.tag==Tag::Str)?g_strs[(uint32_t)a.i]:v2s(a);\n";
    os << "  std::string h;\n";
    os << "  unsigned long hash = 5381;\n";
    os << "  for (char c : s) hash = ((hash << 5) + hash) + (unsigned char)c;\n";
    os << "  char buf[20];\n";
    os << "  snprintf(buf, sizeof(buf), \"%016lx\", hash);\n";
    os << "  h = buf;\n";
    os << "  r.i = (int64_t)g_strs.size(); g_strs.push_back(h);\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_parse(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::Nil;\n";
    os << "  if (a.tag != Tag::Str) return r;\n";
    os << "  const std::string& s = g_strs[(uint32_t)a.i];\n";
    os << "  if (s.empty() || s[0] == '{') { r.tag = Tag::Map; r.i = g_maps.size(); g_maps.emplace_back(); }\n";
    os << "  else if (s[0] == '[') { r.tag = Tag::List; r.i = g_lsts.size(); g_lsts.emplace_back(); }\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_json_get(const Val& a, const Val& b) {\n";
    os << "  Val r; r.tag = Tag::Nil;\n";
    os << "  if (a.tag == Tag::Map && b.tag == Tag::Str) {\n";
    os << "    auto& _m = g_maps[(uint32_t)a.i];\n";
    os << "    auto it = _m.find(g_strs[(uint32_t)b.i]);\n";
    os << "    if (it != _m.end()) r = it->second;\n";
    os << "  }\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_json_keys(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::List;\n";
    os << "  r.i = g_lsts.size(); auto& _jl = g_lsts.emplace_back();\n";
    os << "  if (a.tag == Tag::Map) {\n";
    os << "    for (const auto& _jp : g_maps[(uint32_t)a.i]) {\n";
    os << "      Val _jv; _jv.tag = Tag::Str; _jv.i = g_strs.size(); g_strs.push_back(_jp.first);\n";
    os << "      _jl.push_back(_jv);\n";
    os << "    }\n";
    os << "  }\n";
    os << "  return r;\n";
    os << "}\n";
    os << "static Val leash_native_json_vals(const Val& a) {\n";
    os << "  Val r; r.tag = Tag::List;\n";
    os << "  r.i = g_lsts.size(); auto& _jl2 = g_lsts.emplace_back();\n";
    os << "  if (a.tag == Tag::Map) {\n";
    os << "    for (const auto& _jp2 : g_maps[(uint32_t)a.i])\n";
    os << "      _jl2.push_back(_jp2.second);\n";
    os << "  }\n";
    os << "  return r;\n";
    os << "}\n\n";

    // Forward declarations
    for (size_t fi = 0; fi < funcs.size(); ++fi) {
        const auto& fn = funcs[fi];
        if (fn.isNative) continue;
        os << "Val leash_fn_" << safeName(fn.name) << "(";
        for (int p = 0; p < fn.arity; ++p) {
            if (p > 0) os << ", ";
            os << "Val";
        }
        os << ");\n";
    }
    os << "\n";

    for (size_t fi = 0; fi < funcs.size(); ++fi) {
        const auto& fn = funcs[fi];
        if (fn.isNative) {
            funcNames_.push_back(""); // placeholder
            continue;
        }
        std::string fnName = safeName(fn.name);
        funcNames_.push_back(fnName);

        // Function signature with parameter names
        os << "Val leash_fn_" << fnName << "(";
        for (int p = 0; p < fn.arity; ++p) {
            if (p > 0) os << ", ";
            os << "Val p" << p;
        }
        os << ") {\n";

        // Map parameters to r0..r{arity-1}
        for (int p = 0; p < fn.arity; ++p)
            os << "  Val r" << p << " = p" << p << ";\n";
        // Local registers start at arity
        for (int r = fn.arity; r < fn.num_regs; ++r)
            os << "  Val r" << r << ";\n";

        // String constants
        for (size_t si = 0; si < fn.constStrings.size(); ++si) {
            std::string escaped;
            for (char ch : fn.constStrings[si]) {
                if (ch == '\\') escaped += "\\\\";
                else if (ch == '"') escaped += "\\\"";
                else if (ch == '\n') escaped += "\\n";
                else if (ch == '\r') escaped += "\\r";
                else if (ch == '\t') escaped += "\\t";
                else escaped += ch;
            }
            os << "  static const char* ks" << si << " = \"" << escaped << "\";\n";
        }

        // Collect jump targets
        std::set<int> targets;
        for (size_t ip = 0; ip < fn.code.size(); ++ip) {
            const Instr& inst = fn.code[ip];
            if (inst.op == Op::JMP || inst.op == Op::JMP_IF_FALSE)
                targets.insert(inst.b);
        }

        // Emit instructions with function-unique labels
        for (size_t ip = 0; ip < fn.code.size(); ++ip) {
            if (targets.count((int)ip))
                os << "F" << fi << "L" << ip << ":;\n";
            const auto& inst = fn.code[ip];
            emitInstr(os, fn, funcs, inst, ip, fi);
        }

        os << "  return Val();\n";
        os << "}\n\n";
    }

    // Main entry
    int mainIdx = -1;
    for (size_t i = 0; i < funcs.size(); ++i)
        if (funcs[i].name == "main") { mainIdx = (int)i; break; }

    os << "int main(int argc, char** argv) {\n";
    os << "  (void)argc; (void)argv;\n";
    if (mainIdx >= 0) {
        const auto& mf = funcs[mainIdx];
        os << "  leash_fn_" << safeName(mf.name) << "(";
        for (int p = 0; p < mf.arity; ++p) {
            if (p > 0) os << ", ";
            os << "Val()";
        }
        os << ");\n";
    }
    os << "  return 0;\n";
    os << "}\n";

    return os.str();
}

void LLVMGen::emitInstr(std::ostringstream& os, const Function& fn,
                         const std::vector<Function>& allFuncs,
                         const Instr& inst, size_t ip, size_t fi) {
    (void)ip;
    switch (inst.op) {
        case Op::CONST: {
            const Value& v = fn.consts[inst.b];
            switch (v.t) {
                case Value::T::Nil:    os << "  r" << inst.a << ".tag = Tag::Nil;\n"; break;
                case Value::T::Int:    os << "  r" << inst.a << ".tag = Tag::Int;  r" << inst.a << ".i = " << v.i << ";\n"; break;
                case Value::T::Float:  os << "  r" << inst.a << ".tag = Tag::Float; r" << inst.a << ".f = " << v.f << ";\n"; break;
                case Value::T::Bool:   os << "  r" << inst.a << ".tag = Tag::Bool; r" << inst.a << ".b = " << (v.b ? "true" : "false") << ";\n"; break;
                default: break;
            }
            break;
        }
        case Op::CONST_STR:
            os << "  { r" << inst.a << ".tag = Tag::Str; r" << inst.a
               << ".i = g_strs.size(); g_strs.push_back(ks" << inst.b << "); }\n";
            break;
        case Op::GET_LOCAL:
            os << "  r" << inst.a << " = r" << inst.b << ";\n";
            break;
        case Op::SET_LOCAL:
            os << "  r" << inst.b << " = r" << inst.a << ";\n";
            break;
        case Op::ADD:
            os << "  if (r" << inst.b << ".tag == Tag::Int && r" << inst.c << ".tag == Tag::Int)"
               << "{ r" << inst.a << ".tag = Tag::Int; r" << inst.a << ".i = r" << inst.b << ".i + r" << inst.c << ".i; }"
               << " else if (r" << inst.b << ".tag == Tag::List && r" << inst.c << ".tag == Tag::List)"
               << "{ r" << inst.a << ".tag = Tag::List;"
               << " size_t _la = (uint32_t)r" << inst.b << ".i;"
               << " size_t _lb = (uint32_t)r" << inst.c << ".i;"
               << " r" << inst.a << ".i = g_lsts.size();"
               << " g_lsts.emplace_back();"
               << " auto& _lc = g_lsts.back();"
               << " const auto& _lca = g_lsts[_la];"
               << " const auto& _lcb = g_lsts[_lb];"
               << " _lc.insert(_lc.end(), _lca.begin(), _lca.end());"
               << " _lc.insert(_lc.end(), _lcb.begin(), _lcb.end()); }"
               << " else if (r" << inst.b << ".tag == Tag::Str || r" << inst.c << ".tag == Tag::Str)"
               << "{ std::string _s = v2s(r" << inst.b << ") + v2s(r" << inst.c << ");"
               << " r" << inst.a << ".tag = Tag::Str; r" << inst.a << ".i = g_strs.size(); g_strs.push_back(_s); }"
               << " else { r" << inst.a << ".tag = Tag::Float; r" << inst.a
               << ".f = (r" << inst.b << ".tag==Tag::Int?(double)r" << inst.b << ".i:r" << inst.b << ".f)"
               << " + (r" << inst.c << ".tag==Tag::Int?(double)r" << inst.c << ".i:r" << inst.c << ".f); }\n";
            break;
        case Op::SUB:
            os << "  if (r" << inst.b << ".tag == Tag::Int && r" << inst.c << ".tag == Tag::Int)"
               << "{ r" << inst.a << ".tag = Tag::Int; r" << inst.a << ".i = r" << inst.b << ".i - r" << inst.c << ".i; }"
               << " else { r" << inst.a << ".tag = Tag::Float; r" << inst.a
               << ".f = (r" << inst.b << ".tag==Tag::Int?(double)r" << inst.b << ".i:r" << inst.b << ".f)"
               << " - (r" << inst.c << ".tag==Tag::Int?(double)r" << inst.c << ".i:r" << inst.c << ".f); }\n";
            break;
        case Op::MUL:
            os << "  if (r" << inst.b << ".tag == Tag::Int && r" << inst.c << ".tag == Tag::Int)"
               << "{ r" << inst.a << ".tag = Tag::Int; r" << inst.a << ".i = r" << inst.b << ".i * r" << inst.c << ".i; }"
               << " else { r" << inst.a << ".tag = Tag::Float; r" << inst.a
               << ".f = (r" << inst.b << ".tag==Tag::Int?(double)r" << inst.b << ".i:r" << inst.b << ".f)"
               << " * (r" << inst.c << ".tag==Tag::Int?(double)r" << inst.c << ".i:r" << inst.c << ".f); }\n";
            break;
        case Op::DIV:
            os << "  if (r" << inst.b << ".tag == Tag::Int && r" << inst.c << ".tag == Tag::Int && r" << inst.c << ".i != 0)"
               << "{ r" << inst.a << ".tag = Tag::Int; r" << inst.a << ".i = r" << inst.b << ".i / r" << inst.c << ".i; }"
               << " else { r" << inst.a << ".tag = Tag::Float; r" << inst.a
               << ".f = (r" << inst.b << ".tag==Tag::Int?(double)r" << inst.b << ".i:r" << inst.b << ".f)"
               << " / (r" << inst.c << ".tag==Tag::Int?(double)r" << inst.c << ".i:r" << inst.c << ".f); }\n";
            break;
        case Op::MOD:
            os << "  r" << inst.a << ".tag = Tag::Int; r" << inst.a
               << ".i = r" << inst.c << ".i ? r" << inst.b << ".i % r" << inst.c << ".i : 0;\n";
            break;
        case Op::NEG:
            os << "  if (r" << inst.b << ".tag == Tag::Int) { r" << inst.a << ".tag = Tag::Int; r" << inst.a
               << ".i = -r" << inst.b << ".i; } else { r" << inst.a << ".tag = Tag::Float; r" << inst.a
               << ".f = -r" << inst.b << ".f; }\n";
            break;
        case Op::EQ:
            os << "  r" << inst.a << ".tag = Tag::Bool; r" << inst.a << ".b = ";
            os << "(r" << inst.b << ".tag == r" << inst.c << ".tag && "
               << "((r" << inst.b << ".tag==Tag::Int && r" << inst.b << ".i==r" << inst.c << ".i) ||"
               << "(r" << inst.b << ".tag==Tag::Float && r" << inst.b << ".f==r" << inst.c << ".f) ||"
               << "(r" << inst.b << ".tag==Tag::Bool && r" << inst.b << ".b==r" << inst.c << ".b) ||"
               << "(r" << inst.b << ".tag==Tag::Str && v2s(r" << inst.b << ")==v2s(r" << inst.c << "))));\n";
            break;
        case Op::NE:
            os << "  r" << inst.a << ".tag = Tag::Bool; r" << inst.a << ".b = !";
            os << "(r" << inst.b << ".tag == r" << inst.c << ".tag && "
               << "((r" << inst.b << ".tag==Tag::Int && r" << inst.b << ".i==r" << inst.c << ".i) ||"
               << "(r" << inst.b << ".tag==Tag::Float && r" << inst.b << ".f==r" << inst.c << ".f) ||"
               << "(r" << inst.b << ".tag==Tag::Bool && r" << inst.b << ".b==r" << inst.c << ".b) ||"
               << "(r" << inst.b << ".tag==Tag::Str && v2s(r" << inst.b << ")==v2s(r" << inst.c << "))));\n";
            break;
        case Op::LT:
            os << "  r" << inst.a << ".tag = Tag::Bool; "
               << "if (r" << inst.b << ".tag==Tag::Int && r" << inst.c << ".tag==Tag::Int)"
               << " r" << inst.a << ".b = r" << inst.b << ".i < r" << inst.c << ".i; else {"
               << "double _a = r" << inst.b << ".tag==Tag::Int?(double)r" << inst.b << ".i:r" << inst.b << ".f;"
               << "double _b = r" << inst.c << ".tag==Tag::Int?(double)r" << inst.c << ".i:r" << inst.c << ".f;"
               << " r" << inst.a << ".b = _a < _b; }\n";
            break;
        case Op::GT:
            os << "  r" << inst.a << ".tag = Tag::Bool; "
               << "if (r" << inst.b << ".tag==Tag::Int && r" << inst.c << ".tag==Tag::Int)"
               << " r" << inst.a << ".b = r" << inst.b << ".i > r" << inst.c << ".i; else {"
               << "double _a = r" << inst.b << ".tag==Tag::Int?(double)r" << inst.b << ".i:r" << inst.b << ".f;"
               << "double _b = r" << inst.c << ".tag==Tag::Int?(double)r" << inst.c << ".i:r" << inst.c << ".f;"
               << " r" << inst.a << ".b = _a > _b; }\n";
            break;
        case Op::LE:
            os << "  r" << inst.a << ".tag = Tag::Bool; "
               << "if (r" << inst.b << ".tag==Tag::Int && r" << inst.c << ".tag==Tag::Int)"
               << " r" << inst.a << ".b = r" << inst.b << ".i <= r" << inst.c << ".i; else {"
               << "double _a = r" << inst.b << ".tag==Tag::Int?(double)r" << inst.b << ".i:r" << inst.b << ".f;"
               << "double _b = r" << inst.c << ".tag==Tag::Int?(double)r" << inst.c << ".i:r" << inst.c << ".f;"
               << " r" << inst.a << ".b = _a <= _b; }\n";
            break;
        case Op::GE:
            os << "  r" << inst.a << ".tag = Tag::Bool; "
               << "if (r" << inst.b << ".tag==Tag::Int && r" << inst.c << ".tag==Tag::Int)"
               << " r" << inst.a << ".b = r" << inst.b << ".i >= r" << inst.c << ".i; else {"
               << "double _a = r" << inst.b << ".tag==Tag::Int?(double)r" << inst.b << ".i:r" << inst.b << ".f;"
               << "double _b = r" << inst.c << ".tag==Tag::Int?(double)r" << inst.c << ".i:r" << inst.c << ".f;"
               << " r" << inst.a << ".b = _a >= _b; }\n";
            break;
        case Op::AND: {
            int lbl = nextLabel_++;
            os << "  if (r" << inst.b << ".tag == Tag::Bool && !r" << inst.b << ".b) { r"
               << inst.a << ".tag = Tag::Bool; r" << inst.a << ".b = false; goto F" << fi << "S" << lbl << "; }\n";
            os << "  r" << inst.a << ".tag = Tag::Bool; r" << inst.a << ".b = (r"
               << inst.c << ".tag == Tag::Bool && r" << inst.c << ".b);\n";
            os << "  F" << fi << "S" << lbl << ":\n";
            break;
        }
        case Op::OR: {
            int lbl = nextLabel_++;
            os << "  if (r" << inst.b << ".tag == Tag::Bool && r" << inst.b << ".b) { r"
               << inst.a << ".tag = Tag::Bool; r" << inst.a << ".b = true; goto F" << fi << "S" << lbl << "; }\n";
            os << "  r" << inst.a << ".tag = Tag::Bool; r" << inst.a << ".b = (r"
               << inst.c << ".tag == Tag::Bool && r" << inst.c << ".b);\n";
            os << "  F" << fi << "S" << lbl << ":\n";
            break;
        }
        case Op::NOT:
            os << "  r" << inst.a << ".tag = Tag::Bool; r" << inst.a
               << ".b = !(r" << inst.b << ".tag == Tag::Bool && r" << inst.b << ".b);\n";
            break;
        case Op::JMP_IF_FALSE:
            os << "  if (r" << inst.a << ".tag == Tag::Bool && !r" << inst.a << ".b) goto F" << fi << "L" << inst.b << ";\n";
            break;
        case Op::JMP:
            os << "  goto F" << fi << "L" << inst.b << ";\n";
            break;
        case Op::CALL: {
            int fi = inst.b;
            int argBase = inst.c;
            if ((size_t)fi < allFuncs.size() && !allFuncs[fi].isNative) {
                const Function& callee = allFuncs[fi];
                std::string calleeName = safeName(callee.name);
                if (!calleeName.empty()) {
                    os << "  r" << inst.a << " = leash_fn_" << calleeName << "(";
                    for (int p = 0; p < callee.arity; ++p) {
                        if (p > 0) os << ", ";
                        os << "r" << (argBase + p);
                    }
                    os << ");\n";
                } else {
                    os << "  r" << inst.a << ".tag = Tag::Nil;\n";
                }
            } else if ((size_t)fi < allFuncs.size()) {
                // Native function stub
                const std::string& n = allFuncs[fi].name;
                if (n == "len") {
                    os << "  r" << inst.a << " = leash_native_len(r" << argBase << ");\n";
                } else if (n == "ord") {
                    os << "  r" << inst.a << " = leash_native_ord(r" << argBase << ");\n";
                } else if (n == "chr") {
                    os << "  r" << inst.a << " = leash_native_chr(r" << argBase << ");\n";
                } else if (n == "sqrt") {
                    os << "  r" << inst.a << " = leash_native_sqrt(r" << argBase << ");\n";
                } else if (n == "abs") {
                    os << "  r" << inst.a << " = leash_native_abs(r" << argBase << ");\n";
                } else if (n == "floor") {
                    os << "  r" << inst.a << " = leash_native_floor(r" << argBase << ");\n";
                } else if (n == "ceil") {
                    os << "  r" << inst.a << " = leash_native_ceil(r" << argBase << ");\n";
                } else if (n == "round") {
                    os << "  r" << inst.a << " = leash_native_round(r" << argBase << ");\n";
                } else if (n == "pow") {
                    os << "  r" << inst.a << " = leash_native_pow(r" << argBase << ", r" << (argBase+1) << ");\n";
                } else if (n == "now") {
                    os << "  r" << inst.a << " = leash_native_now();\n";
                } else if (n == "stringify") {
                    os << "  r" << inst.a << " = leash_native_stringify(r" << argBase << ");\n";
                } else if (n == "typeof") {
                    os << "  r" << inst.a << " = leash_native_typeof(r" << argBase << ");\n";
                } else if (n == "to_int") {
                    os << "  r" << inst.a << " = leash_native_to_int(r" << argBase << ");\n";
                } else if (n == "to_float") {
                    os << "  r" << inst.a << " = leash_native_to_float(r" << argBase << ");\n";
                } else if (n == "sleep") {
                    os << "  r" << inst.a << " = leash_native_sleep(r" << argBase << ");\n";
                } else if (n == "band") {
                    os << "  r" << inst.a << " = leash_native_band(r" << argBase << ", r" << (argBase+1) << ");\n";
                } else if (n == "bor") {
                    os << "  r" << inst.a << " = leash_native_bor(r" << argBase << ", r" << (argBase+1) << ");\n";
                } else if (n == "bxor") {
                    os << "  r" << inst.a << " = leash_native_bxor(r" << argBase << ", r" << (argBase+1) << ");\n";
                } else if (n == "bnot") {
                    os << "  r" << inst.a << " = leash_native_bnot(r" << argBase << ");\n";
                } else if (n == "bshl") {
                    os << "  r" << inst.a << " = leash_native_bshl(r" << argBase << ", r" << (argBase+1) << ");\n";
                } else if (n == "bshr") {
                    os << "  r" << inst.a << " = leash_native_bshr(r" << argBase << ", r" << (argBase+1) << ");\n";
                } else if (n == "sha256") {
                    os << "  r" << inst.a << " = leash_native_sha256(r" << argBase << ");\n";
                } else if (n == "parse") {
                    os << "  r" << inst.a << " = leash_native_parse(r" << argBase << ");\n";
                } else if (n == "json_get") {
                    os << "  r" << inst.a << " = leash_native_json_get(r" << argBase << ", r" << (argBase+1) << ");\n";
                } else if (n == "json_set") {
                    os << "  r" << inst.a << " = r" << argBase << ";\n";
                } else if (n == "json_keys") {
                    os << "  r" << inst.a << " = leash_native_json_keys(r" << argBase << ");\n";
                } else if (n == "json_vals") {
                    os << "  r" << inst.a << " = leash_native_json_vals(r" << argBase << ");\n";
                } else {
                    os << "  r" << inst.a << ".tag = Tag::Nil;\n";
                }
            } else {
                os << "  r" << inst.a << ".tag = Tag::Nil;\n";
            }
            break;
        }
        case Op::RET:
            os << "  return r" << inst.a << ";\n";
            break;
        case Op::RET_NIL:
            os << "  return Val();\n";
            break;
        case Op::INVOKE_CAP:
            os << "  printf(\"%s\\n\", v2s(r" << inst.d << ").c_str());\n";
            os << "  r" << inst.a << ".tag = Tag::Nil;\n";
            break;
        case Op::CONCAT:
            os << "  if (r" << inst.b << ".tag == Tag::List && r" << inst.c << ".tag == Tag::List) {\n";
            os << "    size_t _ca_i = (uint32_t)r" << inst.b << ".i;\n";
            os << "    size_t _cb_i = (uint32_t)r" << inst.c << ".i;\n";
            os << "    r" << inst.a << ".tag = Tag::List;\n";
            os << "    r" << inst.a << ".i = g_lsts.size();\n";
            os << "    g_lsts.emplace_back();\n";
            os << "    auto& _cc = g_lsts.back();\n";
            os << "    const auto& _cca = g_lsts[_ca_i];\n";
            os << "    const auto& _ccb = g_lsts[_cb_i];\n";
            os << "    _cc.insert(_cc.end(), _cca.begin(), _cca.end());\n";
            os << "    _cc.insert(_cc.end(), _ccb.begin(), _ccb.end());\n";
            os << "  } else {\n";
            os << "    std::string _s = v2s(r" << inst.b << ") + v2s(r" << inst.c << ");\n";
            os << "    r" << inst.a << ".tag = Tag::Str; r" << inst.a
               << ".i = g_strs.size(); g_strs.push_back(_s);\n";
            os << "  }\n";
            break;
        case Op::GET_GLOBAL:
            os << "  { auto _git = g_globals.find(ks" << inst.b << ");\n";
            os << "    if (_git != g_globals.end()) r" << inst.a << " = _git->second;\n";
            os << "    else { r" << inst.a << ".tag = Tag::Nil; }\n";
            os << "  }\n";
            break;
        case Op::SET_GLOBAL:
            os << "  g_globals[ks" << inst.b << "] = r" << inst.a << ";\n";
            break;
        case Op::MAKE_LIST:
            os << "  { r" << inst.a << ".tag = Tag::List; r" << inst.a
               << ".i = g_lsts.size(); auto& _ml = g_lsts.emplace_back();";
            for (int e = 0; e < inst.c; ++e)
                os << "_ml.push_back(r" << (inst.b + e) << ");";
            os << " }\n";
            break;
        case Op::MAKE_MAP:
            os << "  r" << inst.a << ".tag = Tag::Map; r" << inst.a
               << ".i = g_maps.size(); g_maps.emplace_back();\n";
            break;
        case Op::GET_INDEX: {
            bool isSlice = (inst.d != 0);
            if (isSlice) {
                // slice: container[ start : end ]  (inst.d == -1 means "to end")
                os << "  if (r" << inst.b << ".tag == Tag::Str && r" << inst.c << ".tag == Tag::Int) {\n";
                os << "    int64_t _gs_s = r" << inst.c << ".i;\n";
                os << "    const std::string& _gs = g_strs[(uint32_t)r" << inst.b << ".i];\n";
                if (inst.d == -1) {
                    os << "    int64_t _gs_e = (int64_t)_gs.size();\n";
                } else {
                    os << "    int64_t _gs_e = r" << inst.d << ".i;\n";
                }
                os << "    if (_gs_s < 0) _gs_s += _gs.size();\n";
                os << "    if (_gs_e < 0) _gs_e += _gs.size();\n";
                os << "    if (_gs_s < 0) _gs_s = 0;\n";
                os << "    if (_gs_e > (int64_t)_gs.size()) _gs_e = _gs.size();\n";
                os << "    if (_gs_s >= _gs_e) { r" << inst.a << ".tag = Tag::Str; r" << inst.a
                   << ".i = g_strs.size(); g_strs.push_back(\"\"); }\n";
                os << "    else { std::string _sub = _gs.substr((size_t)_gs_s, (size_t)(_gs_e - _gs_s));\n";
                os << "      r" << inst.a << ".tag = Tag::Str; r" << inst.a
                   << ".i = g_strs.size(); g_strs.push_back(_sub); }\n";
                os << "  } else if (r" << inst.b << ".tag == Tag::List && r" << inst.c << ".tag == Tag::Int) {\n";
                os << "    int64_t _gl_s = r" << inst.c << ".i;\n";
                os << "    auto& _gl = g_lsts[(uint32_t)r" << inst.b << ".i];\n";
                if (inst.d == -1) {
                    os << "    int64_t _gl_e = (int64_t)_gl.size();\n";
                } else {
                    os << "    int64_t _gl_e = r" << inst.d << ".i;\n";
                }
                os << "    if (_gl_s < 0) _gl_s += _gl.size();\n";
                os << "    if (_gl_e < 0) _gl_e += _gl.size();\n";
                os << "    if (_gl_s < 0) _gl_s = 0;\n";
                os << "    if (_gl_e > (int64_t)_gl.size()) _gl_e = _gl.size();\n";
                os << "    if (_gl_s >= _gl_e) { r" << inst.a << ".tag = Tag::List; r" << inst.a
                   << ".i = g_lsts.size(); g_lsts.emplace_back(); }\n";
                os << "    else { r" << inst.a << ".tag = Tag::List; r" << inst.a
                   << ".i = g_lsts.size(); auto& _newl = g_lsts.emplace_back();\n";
                os << "      _newl.insert(_newl.end(), _gl.begin()+_gl_s, _gl.begin()+_gl_e); }\n";
                os << "  } else r" << inst.a << ".tag = Tag::Nil;\n";
            } else {
                os << "  if (r" << inst.b << ".tag == Tag::Str && r" << inst.c << ".tag == Tag::Int) {\n";
                os << "    int64_t _gi = r" << inst.c << ".i;\n";
                os << "    const std::string& _gs = g_strs[(uint32_t)r" << inst.b << ".i];\n";
                os << "    if (_gi < 0) _gi += _gs.size();\n";
                os << "    if (_gi >= 0 && _gi < (int64_t)_gs.size()) {\n";
                os << "      std::string _ch(1, _gs[(size_t)_gi]);\n";
                os << "      r" << inst.a << ".tag = Tag::Str; r" << inst.a
                   << ".i = g_strs.size(); g_strs.push_back(_ch);\n";
                os << "    } else r" << inst.a << ".tag = Tag::Nil;\n";
                os << "  } else if (r" << inst.b << ".tag == Tag::List && r" << inst.c << ".tag == Tag::Int) {\n";
                os << "    int64_t _gi = r" << inst.c << ".i;\n";
                os << "    auto& _gl = g_lsts[(uint32_t)r" << inst.b << ".i];\n";
                os << "    if (_gi < 0) _gi += _gl.size();\n";
                os << "    if (_gi >= 0 && _gi < (int64_t)_gl.size()) r" << inst.a << " = _gl[(size_t)_gi];\n";
                os << "    else r" << inst.a << ".tag = Tag::Nil;\n";
                os << "  } else if (r" << inst.b << ".tag == Tag::Map) {\n";
                os << "    auto& _gm = g_maps[(uint32_t)r" << inst.b << ".i];\n";
                os << "    auto _git = _gm.find(v2s(r" << inst.c << "));\n";
                os << "    if (_git != _gm.end()) r" << inst.a << " = _git->second;\n";
                os << "    else r" << inst.a << ".tag = Tag::Nil;\n";
                os << "  } else r" << inst.a << ".tag = Tag::Nil;\n";
            }
            break;
        }
        case Op::SET_INDEX: {
            int valReg = inst.a;
            int containerReg = inst.b;
            int indexReg = inst.c;
            // Note: bytecode format is SET_INDEX valReg, containerReg, indexReg
            os << "  if (r" << containerReg << ".tag == Tag::List && r" << indexReg
               << ".tag == Tag::Int) {\n";
            os << "    auto& _sl = g_lsts[(uint32_t)r" << containerReg << ".i];\n";
            os << "    int64_t _si = r" << indexReg << ".i;\n";
            os << "    if (_si >= 0 && _si < (int64_t)_sl.size()) _sl[(size_t)_si] = r" << valReg << ";\n";
            os << "  } else if (r" << containerReg << ".tag == Tag::Map) {\n";
            os << "    g_maps[(uint32_t)r" << containerReg << ".i][v2s(r" << indexReg << ")] = r" << valReg << ";\n";
            os << "  }\n";
            os << "  r" << valReg << " = r" << containerReg << ";\n";
            break;
        }
        case Op::IS_MAP:
            os << "  r" << inst.a << ".tag = Tag::Bool; r" << inst.a
               << ".b = (r" << inst.b << ".tag == Tag::Map);\n";
            break;
        case Op::HALT:
            os << "  return Val();\n";
            break;
        default:
            os << "  ; // unhandled op " << (int)inst.op << "\n";
            break;
    }
}

std::string LLVMGen::safeName(const std::string& name) {
    std::string s = name;
    for (auto& c : s)
        if (!isalnum(c) && c != '_') c = '_';
    return s;
}

bool LLVMGen::compileToBinary(const std::string& cppPath,
                               const std::string& outputPath,
                               std::string& errorOut) {
    std::string cmd = "g++ -std=c++17 -O2 \"" + cppPath + "\" -o \"" + outputPath + "\"";
    if (system(cmd.c_str()) != 0) {
        errorOut = "g++ 编译失败";
        return false;
    }
    return true;
}

} // namespace leash
