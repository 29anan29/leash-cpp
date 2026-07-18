#include "host/host.hpp"
#include "common/error.hpp"
#include <iostream>

namespace aegis {

void AuditLog::log(const std::string& entry) {
    entries.push_back(entry);
    std::cerr << "[audit] " << entry << std::endl;
}
void AuditLog::printAll() const {
    for (const auto& e : entries) std::cerr << "[audit] " << e << std::endl;
}

std::shared_ptr<Capability> HostContext::provideCap(const std::string& name) const {
    if (name == "io") return std::make_shared<IOCapability>();
    // in the future: net, model, file, etc.
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

} // namespace aegis
