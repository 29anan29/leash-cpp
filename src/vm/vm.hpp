#pragma once
#include "codegen/bytecode.hpp"
#include "host/host.hpp"
#include "common/value.hpp"
#include <vector>
#include <memory>

namespace aegis {

class VM {
public:
    VM(const std::vector<Function>& funcs, HostContext& ctx, int64_t fuel = 1'000'000)
        : funcs_(funcs), ctx_(ctx), globalFuel_(fuel) {}

    Value run();

private:
    struct Frame {
        const Function* func = nullptr;
        std::vector<Value> regs;
        size_t ip = 0;
        int retReg = -1;         // where to store return value in caller
        std::vector<std::shared_ptr<Capability>> capEnv;
    };

    const std::vector<Function>& funcs_;
    HostContext& ctx_;
    int64_t globalFuel_;
    std::vector<Frame> frames_;

    int fnIdx(const std::string& name) const;
    Value executeFrame(Frame& f);
};

} // namespace aegis
