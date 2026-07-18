#include "codegen/compiler.hpp"
#include "common/error.hpp"
#include <cassert>

namespace aegis {

std::vector<Function> Compiler::compile(const Program& prog, const std::vector<std::string>& packages) {
    packages_ = packages;
    funcIdxMap_.clear();
    for (size_t i = 0; i < prog.fns.size(); ++i) {
        if (!funcIdxMap_.emplace(prog.fns[i]->name, (int)i).second)
            throw CompileError("重复函数名: " + prog.fns[i]->name);
    }

    std::vector<Function> fns(prog.fns.size());
    for (size_t i = 0; i < prog.fns.size(); ++i) {
        Ctx c;
        fns[i].name = prog.fns[i]->name;
        fns[i].arity = (int)prog.fns[i]->params.size();
        fns[i].capNames = prog.fns[i]->req_caps;
        if (prog.fns[i]->isNative) {
            fns[i].isNative = true;
            fns[i].nativeFn = prog.fns[i]->nativeFn;
            fns[i].num_regs = 0;
            fns[i].num_locals = 0;
            continue; // skip body compilation
        }
        c.func = &fns[i];

        // allocate local slots for params
        for (int p = 0; p < fns[i].arity; ++p) {
            c.slots[prog.fns[i]->params[p].first] = p;
            c.nextSlot = p + 1;
        }
        c.nextReg = c.nextSlot;

        // compile body
        for (const auto& stmt : prog.fns[i]->body)
            compileStmt(c, *stmt);

        // ensure return at end
        if (c.func->code.empty() || c.func->code.back().op != Op::RET)
            emit(c, Op::RET_NIL);

        // resolve pending patches
        for (auto& [lab, indices] : c.patches) {
            int target = c.labelPc[lab]; // may be -1 if not set => error
            (void)target;
            for (size_t idx : indices) {
                c.func->code[idx].b = target;
            }
        }

        c.func->num_regs = c.nextReg;
        c.func->num_locals = c.nextSlot;
    }
    return fns;
}

// ---- helpers ----
void Compiler::emit(Ctx& c, Op op, int32_t a, int32_t b, int32_t c0, int32_t d) {
    c.func->code.push_back({op, a, b, c0, d});
}
int Compiler::makeLabel(Ctx& c) { return c.nextLabel++; }

void Compiler::patchLabel(Ctx& c, int label) {
    c.labelPc[label] = (int)c.func->code.size();
    // apply any forward patches already recorded for this label
    if (c.patches.count(label)) {
        for (size_t idx : c.patches[label]) {
            c.func->code[idx].b = c.labelPc[label];
        }
        c.patches.erase(label);
    }
}

int Compiler::constIdx(Ctx& c, Value v) {
    for (size_t i = 0; i < c.func->consts.size(); ++i)
        if (c.func->consts[i].t == v.t && c.func->consts[i].i == v.i && c.func->consts[i].f == v.f && c.func->consts[i].b == v.b)
            return (int)i;
    c.func->consts.push_back(v);
    return (int)c.func->consts.size() - 1;
}

int Compiler::strIdx(Ctx& c, const std::string& s) {
    for (size_t i = 0; i < c.func->constStrings.size(); ++i)
        if (c.func->constStrings[i] == s)
            return (int)i;
    c.func->constStrings.push_back(s);
    return (int)c.func->constStrings.size() - 1;
}

int Compiler::capIdx(Ctx& c, const std::string& name) {
    for (size_t i = 0; i < c.func->capNames.size(); ++i)
        if (c.func->capNames[i] == name)
            return (int)i;
    // out/in 全局可用，自动注入 io 能力
    if (name == "io") {
        c.func->capNames.push_back("io");
        return (int)c.func->capNames.size() - 1;
    }
    throw CompileError("能力 '" + name + "' 不在函数作用域中（需要在 requires 中声明）");
}

int Compiler::methodIdx(Ctx& c, const std::string& name, int arity) {
    for (size_t i = 0; i < c.func->methodNames.size(); ++i)
        if (c.func->methodNames[i] == name)
            return (int)i;
    c.func->methodNames.push_back(name);
    c.func->methodArity.push_back(arity);
    return (int)c.func->methodNames.size() - 1;
}

int Compiler::funcIdx(const std::string& name) const {
    auto it = funcIdxMap_.find(name);
    if (it == funcIdxMap_.end()) throw CompileError("内部错误: 未找到函数 " + name);
    return it->second;
}

// ---- expression compilation ----
int Compiler::compileExpr(Ctx& c, const Expr& e) {
    int reg;
    switch (e.k) {
        case Expr::EInt: {
            reg = c.nextReg++;
            int k = constIdx(c, Value::makeInt(e.i));
            emit(c, Op::CONST, reg, k);
            return reg;
        }
        case Expr::EFloat: {
            reg = c.nextReg++;
            int k = constIdx(c, Value::makeFloat(e.f));
            emit(c, Op::CONST, reg, k);
            return reg;
        }
        case Expr::EBool: {
            reg = c.nextReg++;
            int k = constIdx(c, Value::makeBool(e.b));
            emit(c, Op::CONST, reg, k);
            return reg;
        }
        case Expr::EStr: {
            if (e.exprs.empty()) {
                // pure literal string
                reg = c.nextReg++;
                int k = strIdx(c, e.parts.empty() ? "" : e.parts[0]);
                emit(c, Op::CONST_STR, reg, k);
                return reg;
            }
            // interpolation: concatenate alternating parts and expressions
            int left = -1;
            for (size_t i = 0; i < e.exprs.size(); ++i) {
                int partReg = c.nextReg++;
                int k = strIdx(c, i < e.parts.size() ? e.parts[i] : "");
                emit(c, Op::CONST_STR, partReg, k);
                int exprReg = compileExpr(c, *e.exprs[i]);
                int concat = c.nextReg++;
                if (left < 0) {
                    // first iteration: concat first part with first expr
                    emit(c, Op::CONCAT, concat, partReg, exprReg);
                } else {
                    emit(c, Op::CONCAT, concat, left, partReg);
                    left = concat;
                    concat = c.nextReg++;
                    emit(c, Op::CONCAT, concat, left, exprReg);
                }
                left = concat;
            }
            // trailing part
            int trailReg = c.nextReg++;
            int k = strIdx(c, e.parts.empty() ? "" : e.parts.back());
            emit(c, Op::CONST_STR, trailReg, k);
            int finalReg = c.nextReg++;
            if (left < 0) {
                // no exprs (shouldn't reach here due to early exit, but just in case)
                emit(c, Op::CONST_STR, finalReg, k);
            } else {
                emit(c, Op::CONCAT, finalReg, left, trailReg);
            }
            return finalReg;
        }
        case Expr::EVar: {
            auto it = c.slots.find(e.s);
            if (it == c.slots.end())
                throw CompileError("内部错误: 未定义变量 " + e.s);
            reg = c.nextReg++;
            emit(c, Op::GET_LOCAL, reg, it->second);
            return reg;
        }
        case Expr::EBin: {
            int l = compileExpr(c, *e.lhs);
            int r = compileExpr(c, *e.rhs);
            reg = c.nextReg++;
            Op op;
            if (e.s == "+")  op = Op::ADD;
            else if (e.s == "-")  op = Op::SUB;
            else if (e.s == "*")  op = Op::MUL;
            else if (e.s == "/")  op = Op::DIV;
            else if (e.s == "%")  op = Op::MOD;
            else if (e.s == "==") op = Op::EQ;
            else if (e.s == "!=") op = Op::NE;
            else if (e.s == "<")  op = Op::LT;
            else if (e.s == ">")  op = Op::GT;
            else if (e.s == "<=") op = Op::LE;
            else if (e.s == ">=") op = Op::GE;
            else if (e.s == "and" || e.s == "&&") op = Op::AND;
            else if (e.s == "or" || e.s == "||")  op = Op::OR;
            else throw CompileError("未知二元运算符: " + e.s);
            emit(c, op, reg, l, r);
            return reg;
        }
        case Expr::EUn: {
            int l = compileExpr(c, *e.lhs);
            reg = c.nextReg++;
            Op op = (e.s == "neg") ? Op::NEG : Op::NOT;
            emit(c, op, reg, l);
            return reg;
        }
        case Expr::ECall: {
            if (e.s == "out" || e.s == "in") {
                int cap = capIdx(c, "io");
                int method = (e.s == "out") ? methodIdx(c, "print", 1) : methodIdx(c, "read_line", 1);
                // Compile the single argument and use its result register as the arg base
                int argReg = (e.exprs.empty()) ? -1 : compileExpr(c, *e.exprs[0]);
                reg = c.nextReg++;
                emit(c, Op::INVOKE_CAP, reg, cap, method, argReg);
                return reg;
            }
            // user function call
            int idx = funcIdx(e.s);
            int n = (int)e.exprs.size();
            int argBase = c.nextReg;
            c.nextReg += n;   // reserve arg slots
            for (int i = 0; i < n; ++i) {
                int r = compileExpr(c, *e.exprs[i]);
                if (r != argBase + i)
                    emit(c, Op::GET_LOCAL, argBase + i, r); // move result to reserved slot
            }
            reg = c.nextReg++;
            emit(c, Op::CALL, reg, idx, argBase);
            return reg;
        }
    }
    return -1; // unreachable
}

// ---- statement compilation ----
void Compiler::compileStmt(Ctx& c, const Stmt& s) {
    switch (s.k) {
        case Stmt::SLet: {
            if (s.init) {
                int r = compileExpr(c, *s.init);
                c.slots[s.name] = c.nextSlot++;
                emit(c, Op::SET_LOCAL, r, c.slots[s.name]);
            } else {
                c.slots[s.name] = c.nextSlot++;
            }
            if (c.nextReg < c.nextSlot) c.nextReg = c.nextSlot;
            break;
        }
        case Stmt::SAssign: {
            auto it = c.slots.find(s.name);
            if (it == c.slots.end())
                throw CompileError("内部错误: 赋值给未定义变量 " + s.name);
            int r = compileExpr(c, *s.value);
            emit(c, Op::SET_LOCAL, r, it->second);
            break;
        }
        case Stmt::SExpr: {
            compileExpr(c, *s.init);
            break;
        }
        case Stmt::SReturn: {
            if (s.value) {
                int r = compileExpr(c, *s.value);
                emit(c, Op::RET, r);
            } else {
                emit(c, Op::RET_NIL);
            }
            break;
        }
        case Stmt::SIf: {
            int condReg = compileExpr(c, *s.cond);
            int elseLabel = makeLabel(c);
            emit(c, Op::JMP_IF_FALSE, condReg, -1); // will patch
            c.patches[elseLabel].push_back(c.func->code.size() - 1);
            for (const auto& st : s.body) compileStmt(c, *st);
            int endLabel = makeLabel(c);
            emit(c, Op::JMP, -1);
            c.patches[endLabel].push_back(c.func->code.size() - 1);
            patchLabel(c, elseLabel);
            for (const auto& st : s.elseB) compileStmt(c, *st);
            patchLabel(c, endLabel);
            break;
        }
        case Stmt::SWhile: {
            int loopStart = (int)c.func->code.size();
            int condReg = compileExpr(c, *s.cond);
            int endLabel = makeLabel(c);
            emit(c, Op::JMP_IF_FALSE, condReg, -1);
            c.patches[endLabel].push_back(c.func->code.size() - 1);
            c.loops.push_back({loopStart, endLabel});
            for (const auto& st : s.body) compileStmt(c, *st);
            c.loops.pop_back();
            emit(c, Op::JMP, 0, loopStart);   // 目标地址放在 b 操作数（与 VM 的 JMP 读取一致）
            patchLabel(c, endLabel);
            break;
        }
        case Stmt::SBreak: {
            if (c.loops.empty()) throw CompileError("break 必须在循环内使用");
            int lab = c.loops.back().endLabel;
            emit(c, Op::JMP, 0, -1);
            c.patches[lab].push_back(c.func->code.size() - 1);
            break;
        }
        case Stmt::SContinue: {
            if (c.loops.empty()) throw CompileError("continue 必须在循环内使用");
            emit(c, Op::JMP, 0, c.loops.back().startPc);
            break;
        }
        case Stmt::SFn:
            throw CompileError("不允许嵌套函数");
    }
}

} // namespace aegis
