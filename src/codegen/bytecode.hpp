#pragma once
#include <string>
#include <vector>
#include "common/value.hpp"

namespace aegis {

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
    std::string sourcePkg;                // originating package, for grouping
};

} // namespace aegis
