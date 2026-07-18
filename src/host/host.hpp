#pragma once
#include "common/value.hpp"
#include <memory>
#include <vector>
#include <string>

namespace aegis {

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

    std::shared_ptr<Capability> provideCap(const std::string& name) const;

private:
    Store* store_;
    AuditLog* audit_;
};

struct IOCapability final : Capability {
    std::string name() const override { return "io"; }
    Value invoke(const std::string& method, const std::vector<Value>& args, HostContext& ctx) override;
};

} // namespace aegis
