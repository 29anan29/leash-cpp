#pragma once
#include "common/value.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace leash {

struct AuditLog {
    std::vector<std::string> entries;
    void log(const std::string& entry);
    void printAll() const;
};

class HostContext;
struct Capability {
    virtual ~Capability() = default;
    virtual std::string name() const = 0;
    virtual Value invoke(const std::string& method, const std::vector<Value>& args, HostContext& ctx) = 0;
};

class HostContext {
public:
    HostContext(Store& store, AuditLog& audit) : store_(&store), audit_(&audit) {}

    Store& store() const { return *store_; }
    AuditLog& audit() const { return *audit_; }

    // JSON 导入产生的全局变量（顶层键 -> 值），供 chat 等原生函数读取
    void setGlobals(std::unordered_map<std::string, Value> g) { globals_ = std::move(g); }
    const std::unordered_map<std::string, Value>& globals() const { return globals_; }
    Value getGlobal(const std::string& name) const {
        auto it = globals_.find(name);
        return it == globals_.end() ? Value::nil() : it->second;
    }
    // 运行时重新赋值全局（如 import "leash.json" 注入的 model / api_key / base_url）
    void setGlobal(const std::string& name, const Value& v) { globals_[name] = v; }

    std::shared_ptr<Capability> provideCap(const std::string& name) const;

private:
    Store* store_;
    AuditLog* audit_;
    std::unordered_map<std::string, Value> globals_;
};

struct IOCapability final : Capability {
    std::string name() const override { return "io"; }
    Value invoke(const std::string& method, const std::vector<Value>& args, HostContext& ctx) override;
};

} // namespace leash
