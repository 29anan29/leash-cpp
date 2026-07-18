#pragma once
#include "codegen/bytecode.hpp"
#include "common/ast.hpp"
#include <vector>
#include <unordered_map>

namespace aegis {

class Compiler {
public:
    std::vector<Function> compile(const Program& prog, const std::vector<std::string>& packages = {});

private:
    struct Ctx {
        Function* func = nullptr;
        std::unordered_map<std::string, int> slots; // name -> register slot
        int nextSlot = 0;     // next available local slot
        int nextReg = 0;      // next available temp register
        int nextLabel = 0;
        std::unordered_map<int, std::vector<size_t>> patches; // label -> instr indices to patch
        std::unordered_map<int, int> labelPc;                 // label -> resolved pc offset
        struct LoopCtx { int startPc; int endLabel; };
        std::vector<LoopCtx> loops;                          // 嵌套循环上下文（break/continue 用）
    };

    // Opcode convenience
    void emit(Ctx& c, Op op, int32_t a = 0, int32_t b = 0, int32_t c0 = 0, int32_t d = 0);
    int makeLabel(Ctx& c);
    void patchLabel(Ctx& c, int label);
    int constIdx(Ctx& c, Value v);
    int strIdx(Ctx& c, const std::string& s);
    int capIdx(Ctx& c, const std::string& name);
    int methodIdx(Ctx& c, const std::string& name, int arity);
    int funcIdx(const std::string& name) const;

    int compileExpr(Ctx& c, const Expr& e); // returns result register
    void compileStmt(Ctx& c, const Stmt& s);

    std::unordered_map<std::string, int> funcIdxMap_;
    std::vector<std::string> packages_;
};

} // namespace aegis
