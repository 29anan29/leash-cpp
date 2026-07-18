#include "host/native.hpp"
#include "host/host.hpp"
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <sstream>
#include <array>
#include <memory>
namespace fs = std::filesystem;

namespace aegis {

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
    std::string model  = asStr(args[0], ctx);
    std::string system = asStr(args[1], ctx);
    std::string user   = asStr(args[2], ctx);

    const char* apiKey = std::getenv("OPENAI_API_KEY");

    // 无 API key → mock 模式
    if (!apiKey || !apiKey[0]) {
        std::string mock = "[mock] 模型=" + model + " 收到问题: " + user;
        return Value::makeStr(ctx.store().intern(mock));
    }

    // 构建 JSON body
    std::string body = R"({"model":")" + jsonEscape(model) + R"(","messages":[
        {"role":"system","content":")" + jsonEscape(system) + R"("},
        {"role":"user","content":")" + jsonEscape(user) + R"("}
    ]})";

    // 写入临时文件避免 shell 注入
    std::string tmpPath = "/tmp/aegis_ai_req.json";
    {
        std::ofstream ofs(tmpPath);
        if (!ofs) throw std::runtime_error("无法创建临时文件");
        ofs << body;
    }

    std::string cmd = "curl -s --connect-timeout 15 --max-time 60"
        " -H \"Content-Type: application/json\""
        " -H \"Authorization: Bearer " + std::string(apiKey) + "\""
        " -d @" + tmpPath + " https://api.openai.com/v1/chat/completions";

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
    }};
    return pkg;
}
static Package makeAIPkg() {
    Package pkg{"ai", {
        {"chat", 3, native::ai_chat},
    }};
    return pkg;
}

void initBuiltinPackages() {
    registerPackage(makeFilePkg());
    registerPackage(makeOSPkg());
    registerPackage(makeAIPkg());
}

} // namespace aegis
