#pragma once
#include "common/value.hpp"
#include "codegen/bytecode.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace leash {

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
    Value os_is_tty(const std::vector<Value>&, HostContext&);

    Value ai_chat(const std::vector<Value>&, HostContext&);

    Value core_len(const std::vector<Value>&, HostContext&);
    Value core_ord(const std::vector<Value>&, HostContext&);
    Value core_chr(const std::vector<Value>&, HostContext&);
    Value core_sqrt(const std::vector<Value>&, HostContext&);
    Value core_pow(const std::vector<Value>&, HostContext&);
    Value core_floor(const std::vector<Value>&, HostContext&);
    Value core_ceil(const std::vector<Value>&, HostContext&);
    Value core_round(const std::vector<Value>&, HostContext&);
    Value core_abs(const std::vector<Value>&, HostContext&);
    Value core_to_int(const std::vector<Value>&, HostContext&);
    Value core_to_float(const std::vector<Value>&, HostContext&);
    Value core_band(const std::vector<Value>&, HostContext&);
    Value core_bor(const std::vector<Value>&, HostContext&);
    Value core_bxor(const std::vector<Value>&, HostContext&);
    Value core_bnot(const std::vector<Value>&, HostContext&);
    Value core_bshl(const std::vector<Value>&, HostContext&);
    Value core_bshr(const std::vector<Value>&, HostContext&);

    Value json_parse(const std::vector<Value>&, HostContext&);
    Value json_stringify(const std::vector<Value>&, HostContext&);
    Value json_get(const std::vector<Value>&, HostContext&);
    Value json_set(const std::vector<Value>&, HostContext&);
    Value json_keys(const std::vector<Value>&, HostContext&);
    Value json_vals(const std::vector<Value>&, HostContext&);

    Value time_now(const std::vector<Value>&, HostContext&);
    Value time_sleep(const std::vector<Value>&, HostContext&);

    Value rand_int(const std::vector<Value>&, HostContext&);
    Value rand_float(const std::vector<Value>&, HostContext&);
    Value rand_choice(const std::vector<Value>&, HostContext&);

    Value crypto_sha256(const std::vector<Value>&, HostContext&);

    Value http_get(const std::vector<Value>&, HostContext&);
    Value http_post(const std::vector<Value>&, HostContext&);

    Value re_match(const std::vector<Value>&, HostContext&);
    Value re_find(const std::vector<Value>&, HostContext&);
}

} // namespace leash
