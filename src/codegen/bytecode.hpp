#pragma once
#include <string>
#include <vector>
#include "common/value.hpp"

namespace leash {

struct HostContext;
using NativeFn = Value(*)(const std::vector<Value>&, HostContext&);

enum class Op : uint8_t {
    CONST,        // a = consts[b]
    CONST_STR,    // a = Str(intern(constStrings[b]))
    GET_LOCAL,    // a = locals[b]
    SET_LOCAL,    // locals[b] = a
    ADD, SUB, MUL, DIV, MOD, NEG,
    LT, GT, LE, GE, EQ, NE,
    AND, OR, NOT,
    JMP_IF_FALSE, // if !regs[a] jmp b
    JMP,          // jmp a
    CALL,         // r = call(func=b, args regs [c .. c+arity-1])
    RET,          // return regs[a]
    RET_NIL,
    INVOKE_CAP,   // r = cap(method); a=dst, b=capNameIdx, c=methodIdx, d=argBase
    CONCAT,       // a = str(b) ++ str(c)
    GET_GLOBAL,   // a = globals[ constStrings[b] ]
    SET_GLOBAL,   // globals[ constStrings[b] ] = regs[a]
    GET_INDEX,    // a = container[ index ]  (或切片 [i:j]：c=index, d=end)
    SET_INDEX,    // container[ index ] = regs[a]  (b=container, c=index)
    MAKE_LIST,    // a = [regs[b] .. regs[b+c-1]]
    MAKE_MAP,     // a = new empty map
    IS_MAP,       // a = (regs[b].t == Map)
    HALT
};

struct Instr {
    Op op;
    int32_t a = 0, b = 0, c = 0, d = 0;
};

struct Function {
    std::string name;
    std::vector<Instr> code;
    std::vector<Value> consts;          // int/float/bool/nil constants
    std::vector<std::string> constStrings;
    int num_regs = 0;
    int num_locals = 0;                 // includes parameters
    int arity = 0;
    std::vector<std::string> capNames;  // capability names this fn may use
    std::vector<std::string> methodNames; // method name table for INVOKE_CAP
    std::vector<int> methodArity;          // arity of each method
    bool isNative = false;               // true for built-in package functions
    NativeFn nativeFn = nullptr;          // native implementation (when isNative)

    // 资源与确定性注解（§14），由 main 的注解驱动整个程序的 VM 配置
    int64_t fuel = -1;
    int64_t timeoutMs = -1;
    bool deterministic = false;
    bool isAgent = false;        // agent 关键字声明
    bool isChain = false;        // chain/rag 关键字声明
    int64_t maxSteps = -1;       // @max_steps(N)：agent 最大步数
    bool audit = false;          // @audit：cap 调用记审计日志
    std::string sourcePkg;       // originating package, for grouping
};

} // namespace leash
