#include "host/host.hpp"
#include "common/error.hpp"
#include <iostream>
#include <fstream>

namespace leash {

void AuditLog::log(const std::string& entry) {
    entries.push_back(entry);
}
void AuditLog::printAll() const {
    if (entries.empty()) return;
    if (outputPath.empty()) {
        for (const auto& e : entries) std::cerr << "[audit] " << e << std::endl;
    } else {
        std::ofstream ofs(outputPath);
        if (ofs) {
            for (const auto& e : entries) ofs << "[audit] " << e << "\n";
        }
    }
}

std::shared_ptr<Capability> HostContext::provideCap(const std::string& name) const {
    if (name == "io") return std::make_shared<IOCapability>();
    if (name == "owner") return std::make_shared<OwnerCapability>();
    // 原生包（file/os/ai/model/time/random/crypto/http/net/re）封装为能力句柄：
    // 脚本必须 requires 该能力才能调用，否则编译期/运行期拒绝。
    auto cap = makeCapability(name);
    if (cap) return cap;
    return nullptr; // host policy rejected
}

Value IOCapability::invoke(const std::string& method, const std::vector<Value>& args, HostContext& ctx) {
    auto& store = ctx.store();
    ctx.audit().log("io." + method + " called");
    if (method == "print") {
        for (const auto& a : args)
            std::cout << valueToStr(store, a);
        std::cout << std::endl;
        return Value::nil();
    }
    if (method == "read_line") {
        for (const auto& a : args)
            std::cout << valueToStr(store, a);
        std::cout.flush();
        std::string line;
        std::getline(std::cin, line);
        return Value::makeStr(store.intern(line));
    }
    throw RuntimeError("IO 能力不支持方法: " + method);
}

Value OwnerCapability::invoke(const std::string& method, const std::vector<Value>& args, HostContext& ctx) {
    (void)ctx;
    if (method == "verify") {
        // 检查调用者提供的所有者名是否匹配环境变量 LEASH_OWNER（默认 "owner"）
        std::string expected = "owner";
        if (const char* env = std::getenv("LEASH_OWNER")) expected = env;
        if (args.empty()) return Value::makeBool(false);
        std::string challenge = valueToStr(ctx.store(), args[0]);
        return Value::makeBool(challenge == expected);
    }
    throw RuntimeError("owner 能力不支持方法: " + method);
}

} // namespace leash
