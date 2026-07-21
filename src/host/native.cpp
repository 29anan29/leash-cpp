#include "host/native.hpp"
#include "host/host.hpp"
#include "common/json.hpp"
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <sstream>
#include <array>
#include <memory>
#include <chrono>
#include <random>
#include <thread>
#include <regex>
#include <unistd.h>
#include <sstream>
#include <iomanip>
namespace fs = std::filesystem;

namespace leash {

static std::unordered_map<std::string, Package>& registry() {
    static std::unordered_map<std::string, Package> reg;
    return reg;
}

void registerPackage(const Package& pkg) {
    registry()[pkg.name] = pkg;
}

const Package* findPackage(const std::string& name) {
    auto it = registry().find(name);
    return it == registry().end() ? nullptr : &it->second;
}

static std::string asStr(const Value& v, HostContext& ctx) {
    return valueToStr(ctx.store(), v);
}

// ======== file package ========
Value native::file_read(const std::vector<Value>& args, HostContext& ctx) {
    std::string path = asStr(args[0], ctx);
    std::ifstream ifs(path);
    if (!ifs) throw std::runtime_error("无法打开文件: " + path);
    return Value::makeStr(ctx.store().intern(std::string((std::istreambuf_iterator<char>(ifs)),
                                                         std::istreambuf_iterator<char>())));
}
Value native::file_write(const std::vector<Value>& args, HostContext& ctx) {
    std::string path = asStr(args[0], ctx);
    std::string data = asStr(args[1], ctx);
    std::ofstream ofs(path);
    if (!ofs) throw std::runtime_error("无法写入文件: " + path);
    ofs << data;
    return Value::nil();
}
Value native::file_exists(const std::vector<Value>& args, HostContext& ctx) {
    return Value::makeBool(fs::exists(asStr(args[0], ctx)));
}
Value native::file_delete(const std::vector<Value>& args, HostContext& ctx) {
    fs::remove(asStr(args[0], ctx));
    return Value::nil();
}
Value native::file_append(const std::vector<Value>& args, HostContext& ctx) {
    std::string path = asStr(args[0], ctx);
    std::string data = asStr(args[1], ctx);
    std::ofstream ofs(path, std::ios::app);
    if (!ofs) throw std::runtime_error("无法追加文件: " + path);
    ofs << data;
    return Value::nil();
}

// ======== os package ========
Value native::os_env(const std::vector<Value>& args, HostContext& ctx) {
    const char* val = std::getenv(asStr(args[0], ctx).c_str());
    return Value::makeStr(ctx.store().intern(val ? val : ""));
}
Value native::os_exit(const std::vector<Value>& args, HostContext&) {
    int code = (args[0].t == Value::T::Int) ? (int)args[0].i : 0;
    std::exit(code);
    return Value::nil();
}
Value native::os_cwd(const std::vector<Value>& args, HostContext& ctx) {
    (void)args;
    return Value::makeStr(ctx.store().intern(fs::current_path().string()));
}
Value native::os_is_tty(const std::vector<Value>& args, HostContext& ctx) {
    (void)args; (void)ctx;
    return Value::makeBool(::isatty(STDIN_FILENO) != 0);
}

// ======== ai package (LangChain 风格 LLM 调用) ========
static std::string execCmd(const std::string& cmd) {
    std::array<char, 4096> buf;
    std::string result;
    auto deleter = [](FILE* f) { if (f) pclose(f); };
    std::unique_ptr<FILE, decltype(deleter)> pipe(popen(cmd.c_str(), "r"), deleter);
    if (!pipe) throw std::runtime_error("执行命令失败: " + cmd);
    while (fgets(buf.data(), (int)buf.size(), pipe.get()) != nullptr)
        result += buf.data();
    return result;
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

static std::string extractContent(const std::string& json) {
    // 简单提取 "content":"..." 字段值
    const std::string key = "\"content\":\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return "(无响应)";
    pos += key.size();
    std::string val;
    for (; pos < json.size(); ++pos) {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            if (json[pos + 1] == '"') { val += '"'; pos++; }
            else if (json[pos + 1] == 'n') { val += '\n'; pos++; }
            else if (json[pos + 1] == '\\') { val += '\\'; pos++; }
            else val += json[pos];
        } else if (json[pos] == '"') {
            break;
        } else {
            val += json[pos];
        }
    }
    return val;
}

Value native::ai_chat(const std::vector<Value>& args, HostContext& ctx) {
    // 参数形态：chat(user) 或 chat(system, user)
    std::string system, user;
    if (args.size() >= 2) { system = asStr(args[0], ctx); user = asStr(args[1], ctx); }
    else if (!args.empty()) { user = asStr(args[0], ctx); }
    else throw std::runtime_error("chat 至少需要一个参数（用户问题）");

    // 配置来自 import "leash.json" 注入的全局变量
    auto asText = [&](const std::string& name) {
        Value v = ctx.getGlobal(name);
        if (v.t == Value::T::Nil) return std::string();
        return valueToStr(ctx.store(), v);
    };
    std::string model   = asText("model");
    std::string apiKey  = asText("api_key");
    std::string baseUrl = asText("base_url");
    if (model.empty())   model = "gpt-4o-mini";
    if (baseUrl.empty()) baseUrl = "https://api.openai.com/v1/chat/completions";

    // 无 API key → mock 模式
    if (apiKey.empty() || apiKey == "nil") {
        std::string mock = "[mock] 模型=" + model + " 收到问题: " + user;
        return Value::makeStr(ctx.store().intern(mock));
    }

    // 构建 JSON body
    std::string body = R"({"model":")" + jsonEscape(model) + R"(","messages":[
        {"role":"system","content":")" + jsonEscape(system) + R"("},
        {"role":"user","content":")" + jsonEscape(user) + R"("}
    ]})";

    // 写入临时文件避免 shell 注入
    std::string tmpPath = "/tmp/leash_ai_req.json";
    {
        std::ofstream ofs(tmpPath);
        if (!ofs) throw std::runtime_error("无法创建临时文件");
        ofs << body;
    }

    std::string cmd = "curl -s --connect-timeout 15 --max-time 60"
        " -H \"Content-Type: application/json\""
        " -H \"Authorization: Bearer " + apiKey + "\""
        " -d @" + tmpPath + " " + baseUrl;

    std::string resp;
    try {
        resp = execCmd(cmd);
    } catch (...) {
        fs::remove(tmpPath);
        throw;
    }
    fs::remove(tmpPath);

    std::string content = extractContent(resp);
    return Value::makeStr(ctx.store().intern(content));
}

// ======== core package（始终可用） ========
Value native::core_len(const std::vector<Value>& args, HostContext& ctx) {
    if (args.empty()) return Value::makeInt(0);
    const Value& v = args[0];
    switch (v.t) {
        case Value::T::Str:  return Value::makeInt((int64_t)ctx.store().str(v.idx()).size());
        case Value::T::List: return Value::makeInt((int64_t)ctx.store().lists[v.idx()].size());
        case Value::T::Map:  return Value::makeInt((int64_t)ctx.store().maps[v.idx()].size());
        default: return Value::makeInt(0);
    }
}
Value native::core_ord(const std::vector<Value>& args, HostContext& ctx) {
    const std::string& s = ctx.store().str(args[0].idx());
    if (s.empty()) return Value::makeInt(0);
    return Value::makeInt((int64_t)(unsigned char)s[0]);
}
Value native::core_chr(const std::vector<Value>& args, HostContext& ctx) {
    int c = (int)args[0].i;
    if (c < 0 || c > 255) c = '?';
    std::string s(1, (char)c);
    return Value::makeStr(ctx.store().intern(s));
}
static double asNum(const Value& v) {
    return (v.t == Value::T::Float) ? v.f : (double)v.i;
}
Value native::core_sqrt(const std::vector<Value>& args, HostContext&) {
    return Value::makeFloat(std::sqrt(asNum(args[0])));
}
Value native::core_pow(const std::vector<Value>& args, HostContext&) {
    return Value::makeFloat(std::pow(asNum(args[0]), asNum(args[1])));
}
Value native::core_floor(const std::vector<Value>& args, HostContext&) {
    return Value::makeInt((int64_t)std::floor(asNum(args[0])));
}
Value native::core_ceil(const std::vector<Value>& args, HostContext&) {
    return Value::makeInt((int64_t)std::ceil(asNum(args[0])));
}
Value native::core_round(const std::vector<Value>& args, HostContext&) {
    return Value::makeInt((int64_t)std::round(asNum(args[0])));
}
Value native::core_abs(const std::vector<Value>& args, HostContext&) {
    double d = asNum(args[0]);
    return (d < 0) ? Value::makeFloat(-d) : args[0];
}
Value native::core_to_int(const std::vector<Value>& args, HostContext&) {
    return Value::makeInt((int64_t)asNum(args[0]));
}
Value native::core_to_float(const std::vector<Value>& args, HostContext&) {
    return Value::makeFloat(asNum(args[0]));
}
Value native::core_band(const std::vector<Value>& args, HostContext&) {
    return Value::makeInt(args[0].i & args[1].i);
}
Value native::core_bor(const std::vector<Value>& args, HostContext&) {
    return Value::makeInt(args[0].i | args[1].i);
}
Value native::core_bxor(const std::vector<Value>& args, HostContext&) {
    return Value::makeInt(args[0].i ^ args[1].i);
}
Value native::core_bnot(const std::vector<Value>& args, HostContext&) {
    return Value::makeInt(~args[0].i);
}
Value native::core_bshl(const std::vector<Value>& args, HostContext&) {
    return Value::makeInt(args[0].i << args[1].i);
}
Value native::core_bshr(const std::vector<Value>& args, HostContext&) {
    return Value::makeInt(args[0].i >> args[1].i);
}

// ======== json package ========
static std::string jsonStringify(const Store& store, const Value& v, int depth = 0) {
    switch (v.t) {
        case Value::T::Nil:   return "null";
        case Value::T::Bool:  return v.b ? "true" : "false";
        case Value::T::Int:   return std::to_string(v.i);
        case Value::T::Float: return std::to_string(v.f);
        case Value::T::Str:   return "\"" + jsonEscape(store.str(v.idx())) + "\"";
        case Value::T::List: {
            std::string s = "[";
            const auto& l = store.lists[v.idx()];
            for (size_t k = 0; k < l.size(); ++k) { if (k) s += ", "; s += jsonStringify(store, l[k], depth + 1); }
            return s + "]";
        }
        case Value::T::Map: {
            std::string s = "{";
            const auto& m = store.maps[v.idx()];
            bool first = true;
            for (const auto& kv : m) {
                if (!first) s += ", ";
                first = false;
                s += "\"" + jsonEscape(kv.first) + "\": " + jsonStringify(store, kv.second, depth + 1);
            }
            return s + "}";
        }
        default: return "null";
    }
}
Value native::json_parse(const std::vector<Value>& args, HostContext& ctx) {
    std::string text = valueToStr(ctx.store(), args[0]);
    return parseJson(text, ctx.store());
}
Value native::json_stringify(const std::vector<Value>& args, HostContext& ctx) {
    return Value::makeStr(ctx.store().intern(jsonStringify(ctx.store(), args[0])));
}
Value native::json_get(const std::vector<Value>& args, HostContext& ctx) {
    const Value& m = args[0];
    if (m.t != Value::T::Map) return Value::nil();
    const std::string& key = valueToStr(ctx.store(), args[1]);
    const auto& mp = ctx.store().maps[m.idx()];
    auto it = mp.find(key);
    return it == mp.end() ? Value::nil() : it->second;
}
Value native::json_set(const std::vector<Value>& args, HostContext& ctx) {
    const Value& m = args[0];
    if (m.t != Value::T::Map) return Value::nil();
    const std::string& key = valueToStr(ctx.store(), args[1]);
    ctx.store().maps[m.idx()][key] = args[2];
    return m;
}
Value native::json_keys(const std::vector<Value>& args, HostContext& ctx) {
    const Value& m = args[0];
    if (m.t != Value::T::Map) return Value::makeList(ctx.store().internList({}));
    const auto& mp = ctx.store().maps[m.idx()];
    std::vector<Value> out;
    for (const auto& kv : mp) out.push_back(Value::makeStr(ctx.store().intern(kv.first)));
    return Value::makeList(ctx.store().internList(out));
}
Value native::json_vals(const std::vector<Value>& args, HostContext& ctx) {
    const Value& m = args[0];
    if (m.t != Value::T::Map) return Value::makeList(ctx.store().internList({}));
    const auto& mp = ctx.store().maps[m.idx()];
    std::vector<Value> out;
    for (const auto& kv : mp) out.push_back(kv.second);
    return Value::makeList(ctx.store().internList(out));
}

// ======== time package ========
Value native::time_now(const std::vector<Value>& args, HostContext& ctx) {
    (void)args;
    if (ctx.deterministic()) throw std::runtime_error("确定性作用域内禁止非确定源: now");
    auto now = std::chrono::system_clock::now().time_since_epoch();
    int64_t secs = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    return Value::makeInt(secs);
}
Value native::time_sleep(const std::vector<Value>& args, HostContext& ctx) {
    if (ctx.deterministic()) throw std::runtime_error("确定性作用域内禁止非确定源: sleep");
    double s = (args[0].t == Value::T::Int) ? (double)args[0].i : args[0].f;
    if (s < 0) s = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds((long long)(s * 1000)));
    return Value::nil();
}

// ======== random package ========
static std::mt19937_64& rng() {
    static std::mt19937_64 e(std::random_device{}());
    return e;
}
Value native::rand_int(const std::vector<Value>& args, HostContext& ctx) {
    if (ctx.deterministic()) throw std::runtime_error("确定性作用域内禁止非确定源: random.int");
    int64_t a = args[0].i, b = args[1].i;
    if (b < a) std::swap(a, b);
    std::uniform_int_distribution<int64_t> d(a, b);
    return Value::makeInt(d(rng()));
}
Value native::rand_float(const std::vector<Value>& args, HostContext& ctx) {
    (void)args;
    if (ctx.deterministic()) throw std::runtime_error("确定性作用域内禁止非确定源: random.float");
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return Value::makeFloat(d(rng()));
}
Value native::rand_choice(const std::vector<Value>& args, HostContext& ctx) {
    if (ctx.deterministic()) throw std::runtime_error("确定性作用域内禁止非确定源: random.choice");
    const auto& l = ctx.store().lists[args[0].idx()];
    if (l.empty()) return Value::nil();
    std::uniform_int_distribution<size_t> d(0, l.size() - 1);
    return l[d(rng())];
}

// ======== crypto package (SHA-256) ========
static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
static std::string sha256hex(const std::string& in) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::vector<uint8_t> msg(in.begin(), in.end());
    uint64_t bitlen = (uint64_t)msg.size() * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back((uint8_t)(bitlen >> (i * 8)));
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = ((uint32_t)msg[off+i*4]<<24)|((uint32_t)msg[off+i*4+1]<<16)|((uint32_t)msg[off+i*4+2]<<8)|((uint32_t)msg[off+i*4+3]);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15]>>3);
            uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2]>>10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    std::stringstream ss;
    for (int i = 0; i < 8; ++i) ss << std::hex << std::setw(8) << std::setfill('0') << h[i];
    return ss.str();
}
Value native::crypto_sha256(const std::vector<Value>& args, HostContext& ctx) {
    return Value::makeStr(ctx.store().intern(sha256hex(valueToStr(ctx.store(), args[0]))));
}

// ======== http package ========
static std::string curlCall(const std::string& url, const std::string& method, const std::string& body) {
    std::string tmpPath = "/tmp/leash_http_req.json";
    if (!body.empty()) {
        std::ofstream ofs(tmpPath);
        if (ofs) ofs << body;
    }
    std::string cmd = "curl -s --connect-timeout 15 --max-time 60 -X " + method;
    if (!body.empty()) cmd += " -H \"Content-Type: application/json\" -d @" + tmpPath;
    cmd += " \"" + url + "\"";
    std::string resp = execCmd(cmd);
    if (!body.empty()) fs::remove(tmpPath);
    return resp;
}
Value native::http_get(const std::vector<Value>& args, HostContext& ctx) {
    return Value::makeStr(ctx.store().intern(curlCall(valueToStr(ctx.store(), args[0]), "GET", "")));
}
Value native::http_post(const std::vector<Value>& args, HostContext& ctx) {
    std::string body = (args.size() > 1) ? valueToStr(ctx.store(), args[1]) : "";
    return Value::makeStr(ctx.store().intern(curlCall(valueToStr(ctx.store(), args[0]), "POST", body)));
}

// ======== re package ========
Value native::re_match(const std::vector<Value>& args, HostContext& ctx) {
    try {
        std::regex re(valueToStr(ctx.store(), args[0]));
        return Value::makeBool(std::regex_search(valueToStr(ctx.store(), args[1]), re));
    } catch (...) { return Value::makeBool(false); }
}
Value native::re_find(const std::vector<Value>& args, HostContext& ctx) {
    std::vector<Value> out;
    try {
        std::regex re(valueToStr(ctx.store(), args[0]));
        std::string s = valueToStr(ctx.store(), args[1]);
        auto it = std::sregex_iterator(s.begin(), s.end(), re);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) out.push_back(Value::makeStr(ctx.store().intern(it->str())));
    } catch (...) {}
    return Value::makeList(ctx.store().internList(out));
}

// ---- register all built-in packages ----
static Package makeFilePkg() {
    Package pkg{"file", {
        {"read",   1, native::file_read},
        {"write",  2, native::file_write},
        {"exists", 1, native::file_exists},
        {"delete", 1, native::file_delete},
        {"append", 2, native::file_append},
    }};
    return pkg;
}
static Package makeOSPkg() {
    Package pkg{"os", {
        {"env",  1, native::os_env},
        {"exit", 1, native::os_exit},
        {"cwd",  0, native::os_cwd},
        {"is_tty", 0, native::os_is_tty},
    }};
    return pkg;
}
static Package makeAIPkg() {
    Package pkg{"ai", {
        {"chat", 2, native::ai_chat},
    }};
    return pkg;
}

static Package makeCorePkg() {
    Package pkg{"core", {
        {"len", 1, native::core_len},
        {"ord", 1, native::core_ord},
        {"chr", 1, native::core_chr},
        {"sqrt", 1, native::core_sqrt},
        {"pow", 2, native::core_pow},
        {"floor", 1, native::core_floor},
        {"ceil", 1, native::core_ceil},
        {"round", 1, native::core_round},
        {"abs", 1, native::core_abs},
        {"to_int", 1, native::core_to_int},
        {"to_float", 1, native::core_to_float},
        {"band", 2, native::core_band},
        {"bor", 2, native::core_bor},
        {"bxor", 2, native::core_bxor},
        {"bnot", 1, native::core_bnot},
        {"bshl", 2, native::core_bshl},
        {"bshr", 2, native::core_bshr},
    }};
    return pkg;
}

static Package makeJsonPkg() {
    Package pkg{"json", {
        {"parse", 1, native::json_parse},
        {"stringify", 1, native::json_stringify},
        {"json_get", 2, native::json_get},
        {"json_set", 3, native::json_set},
        {"json_keys", 1, native::json_keys},
        {"json_vals", 1, native::json_vals},
    }};
    return pkg;
}

static Package makeTimePkg() {
    Package pkg{"time", {
        {"now", 0, native::time_now},
        {"sleep", 1, native::time_sleep},
    }};
    return pkg;
}

static Package makeRandPkg() {
    Package pkg{"random", {
        {"int", 2, native::rand_int},
        {"float", 0, native::rand_float},
        {"choice", 1, native::rand_choice},
    }};
    return pkg;
}

static Package makeCryptoPkg() {
    Package pkg{"crypto", {
        {"sha256", 1, native::crypto_sha256},
    }};
    return pkg;
}

static Package makeHttpPkg() {
    Package pkg{"http", {
        {"get", 1, native::http_get},
        {"post", 2, native::http_post},
    }};
    return pkg;
}

static Package makeRePkg() {
    Package pkg{"re", {
        {"ismatch", 2, native::re_match},
        {"find", 2, native::re_find},
    }};
    return pkg;
}

// ======== 能力包装器：把原生包的方法封装成能力句柄 ========
struct PackageCapability final : Capability {
    std::string nm;
    std::unordered_map<std::string, NativeFn> m;
    std::string name() const override { return nm; }
    Value invoke(const std::string& method, const std::vector<Value>& args, HostContext& ctx) override {
        auto it = m.find(method);
        if (it == m.end()) throw std::runtime_error(nm + " 能力不支持方法: " + method);
        return it->second(args, ctx);
    }
};

std::shared_ptr<Capability> makeCapability(const std::string& name) {
    auto cap = std::make_shared<PackageCapability>();
    cap->nm = name;
    auto add = [&](const char* meth, NativeFn fn) { cap->m[meth] = fn; };
    if (name == "file") {
        add("read", native::file_read);   add("write", native::file_write);
        add("exists", native::file_exists); add("delete", native::file_delete);
        add("append", native::file_append);
    } else if (name == "os") {
        add("env", native::os_env); add("exit", native::os_exit);
        add("cwd", native::os_cwd); add("is_tty", native::os_is_tty);
    } else if (name == "ai" || name == "model") {
        add("chat", native::ai_chat);
    } else if (name == "time") {
        add("now", native::time_now); add("sleep", native::time_sleep);
    } else if (name == "random") {
        add("int", native::rand_int); add("float", native::rand_float); add("choice", native::rand_choice);
    } else if (name == "crypto") {
        add("sha256", native::crypto_sha256);
    } else if (name == "http" || name == "net") {
        add("get", native::http_get); add("post", native::http_post);
    } else if (name == "re") {
        add("ismatch", native::re_match); add("find", native::re_find);
    } else {
        return nullptr;
    }
    return cap;
}

void initBuiltinPackages() {
    registerPackage(makeFilePkg());
    registerPackage(makeOSPkg());
    registerPackage(makeAIPkg());
    registerPackage(makeCorePkg());
    registerPackage(makeJsonPkg());
    registerPackage(makeTimePkg());
    registerPackage(makeRandPkg());
    registerPackage(makeCryptoPkg());
    registerPackage(makeHttpPkg());
    registerPackage(makeRePkg());
}

} // namespace leash
