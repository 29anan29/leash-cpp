#pragma once
#include "common/value.hpp"
#include "codegen/bytecode.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace aegis {

struct NativeFuncDef {
    std::string name;
    int arity = 0;
    NativeFn fn = nullptr;
};

struct Package {
    std::string name;
    std::vector<NativeFuncDef> funcs;
};

void registerPackage(const Package& pkg);
const Package* findPackage(const std::string& name);
void initBuiltinPackages();

namespace native {
    Value file_read(const std::vector<Value>&, HostContext&);
    Value file_write(const std::vector<Value>&, HostContext&);
    Value file_exists(const std::vector<Value>&, HostContext&);
    Value file_delete(const std::vector<Value>&, HostContext&);
    Value file_append(const std::vector<Value>&, HostContext&);

    Value os_env(const std::vector<Value>&, HostContext&);
    Value os_exit(const std::vector<Value>&, HostContext&);
    Value os_cwd(const std::vector<Value>&, HostContext&);

    Value ai_chat(const std::vector<Value>&, HostContext&);
}

} // namespace aegis
