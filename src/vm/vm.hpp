#pragma once
#include "codegen/bytecode.hpp"
#include "host/host.hpp"
#include "common/value.hpp"
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <chrono>

namespace leash {

class VM {
public:
    VM(const std::vector<Function>& funcs, HostContext& ctx, int64_t fuel = 1'000'000)
        : funcs_(funcs), ctx_(ctx), globalFuel_(fuel) {}

    Value run();

    // JSON 导入产生的全局变量（顶层键 -> 值）
    void setGlobals(std::unordered_map<std::string, Value> g) { globals_ = std::move(g); }

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
    static constexpr int kMaxCallDepth = 200;  // 最大调用深度防护
    int64_t timeoutMs_ = -1;
    bool deterministic_ = false;
    std::chrono::steady_clock::time_point timeoutStart_;
    std::vector<Frame> frames_;
    std::unordered_map<std::string, Value> globals_;

    int fnIdx(const std::string& name) const;
    Value executeFrame(Frame& f);
};

} // namespace leash
