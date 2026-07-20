#include "codegen/compiler.hpp"
#include "common/error.hpp"
#include <cassert>

namespace leash {

std::vector<Function> Compiler::compile(const Program& prog, const std::vector<std::string>& packages,
                                  const std::vector<std::string>& globalNames) {
    packages_ = packages;
    globals_.clear();
    for (const auto& g : globalNames) globals_.insert(g);
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
        if (c.func->methodNames[i] == name && c.func->methodArity[i] == arity)
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
        case Expr::ENil: {
            reg = c.nextReg++;
            int k = constIdx(c, Value::nil());
            emit(c, Op::CONST, reg, k);
            return reg;
        }
        case Expr::EVar: {
            auto it = c.slots.find(e.s);
            if (it == c.slots.end()) {
                // 全局变量（来自 JSON 导入的顶层键）
                if (globals_.count(e.s)) {
                    reg = c.nextReg++;
                    int k = strIdx(c, e.s);
                    emit(c, Op::GET_GLOBAL, reg, k);
                    return reg;
                }
                throw CompileError("内部错误: 未定义变量 " + e.s);
            }
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
                std::string m = (e.s == "out") ? "print" : "read_line";
                int arity = (int)e.exprs.size();
                int argBase = c.nextReg;
                c.nextReg += arity;
                for (int i = 0; i < arity; ++i) {
                    int r = compileExpr(c, *e.exprs[i]);
                    if (r != argBase + i)
                        emit(c, Op::GET_LOCAL, argBase + i, r);
                }
                int method = methodIdx(c, m, arity);
                reg = c.nextReg++;
                emit(c, Op::INVOKE_CAP, reg, cap, method, argBase);
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
        case Expr::EList: {
            int n = (int)e.exprs.size();
            int start = c.nextReg;
            c.nextReg += n;
            for (int i = 0; i < n; ++i) {
                int r = compileExpr(c, *e.exprs[i]);
                if (r != start + i)
                    emit(c, Op::GET_LOCAL, start + i, r);
            }
            reg = c.nextReg++;
            emit(c, Op::MAKE_LIST, reg, start, n);
            return reg;
        }
        case Expr::EIndex: {
            int contReg = compileExpr(c, *e.lhs);
            int idxReg = compileExpr(c, *e.rhs);
            reg = c.nextReg++;
            if (e.slice) {
                int endReg = e.rhs2 ? compileExpr(c, *e.rhs2) : -1;
                emit(c, Op::GET_INDEX, reg, contReg, idxReg, endReg);
            } else {
                emit(c, Op::GET_INDEX, reg, contReg, idxReg);
            }
            return reg;
        }
        case Expr::EMap: {
            reg = c.nextReg++;
            emit(c, Op::MAKE_MAP, reg);
            for (auto& pr : e.pairs) {
                int kReg = compileExpr(c, *pr.first);
                int vReg = compileExpr(c, *pr.second);
                emit(c, Op::SET_INDEX, vReg, reg, kReg);
            }
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
            if (s.target && s.target->k == Expr::EIndex) {
                // 索引赋值：container[index] = value
                int contReg = compileExpr(c, *s.target->lhs);
                int idxReg = compileExpr(c, *s.target->rhs);
                int valReg = compileExpr(c, *s.value);
                emit(c, Op::SET_INDEX, valReg, contReg, idxReg);
                break;
            }
            auto it = c.slots.find(s.name);
            if (it == c.slots.end()) {
                if (globals_.count(s.name)) {
                    int r = compileExpr(c, *s.value);
                    int k = strIdx(c, s.name);
                    emit(c, Op::SET_GLOBAL, r, k);
                    break;
                }
                throw CompileError("内部错误: 赋值给未定义变量 " + s.name);
            }
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
        case Stmt::SFor: {
            bool two = !s.name2.empty();
            // 槽位：idxSlot（下标） + 循环变量槽（name / name2）
            int idxSlot = c.nextSlot++;
            int varSlot = c.nextSlot++;
            int varSlot2 = two ? c.nextSlot++ : -1;
            if (c.nextReg < c.nextSlot) c.nextReg = c.nextSlot;
            c.slots[s.name] = varSlot;
            if (two) c.slots[s.name2] = varSlot2;

            // 求值可迭代对象
            int collReg = compileExpr(c, *s.init);

            // isMap = coll 是映射？
            int isMapR = c.nextReg++;
            emit(c, Op::IS_MAP, isMapR, collReg);

            // klR = 迭代用的“键列表”：map -> json_keys(coll)，否则 -> coll 本身
            // （json_keys 调用必须包在 isMap 分支里，避免对非映射调用报错）
            int klR = c.nextReg++;
            emit(c, Op::GET_LOCAL, klR, collReg);            // 默认：非 map
            int jmpMap = makeLabel(c);
            emit(c, Op::JMP_IF_FALSE, isMapR, -1);
            c.patches[jmpMap].push_back(c.func->code.size() - 1);
            int jkFn = funcIdx("json_keys");
            int jkArg = c.nextReg; c.nextReg += 1;
            emit(c, Op::GET_LOCAL, jkArg, collReg);
            int jkR = c.nextReg++;
            emit(c, Op::CALL, jkR, jkFn, jkArg);
            emit(c, Op::GET_LOCAL, klR, jkR);                // map：用键列表
            patchLabel(c, jmpMap);

            // idx = 0
            int zk = constIdx(c, Value::makeInt(0));
            int zr = c.nextReg++;
            emit(c, Op::CONST, zr, zk);
            emit(c, Op::SET_LOCAL, zr, idxSlot);

            int loopStart = (int)c.func->code.size();

            // len(klR)
            int lenFn = funcIdx("len");
            int argBase = c.nextReg; c.nextReg += 1;
            emit(c, Op::GET_LOCAL, argBase, klR);
            int lenReg = c.nextReg++;
            emit(c, Op::CALL, lenReg, lenFn, argBase);
            // idxVal
            int idxVal = c.nextReg++;
            emit(c, Op::GET_LOCAL, idxVal, idxSlot);
            // cond = idxVal < len
            int condReg = c.nextReg++;
            emit(c, Op::LT, condReg, idxVal, lenReg);

            int endLabel = makeLabel(c);
            emit(c, Op::JMP_IF_FALSE, condReg, -1);
            c.patches[endLabel].push_back(c.func->code.size() - 1);
            c.loops.push_back({loopStart, endLabel});

            // keyFromList = klR[idxVal]（map: 字符串键；list/str: 元素本身）
            int keyFromList = c.nextReg++;
            emit(c, Op::GET_INDEX, keyFromList, klR, idxVal);

            if (two) {
                // 默认（list/str）：name=下标，name2=元素
                emit(c, Op::SET_LOCAL, idxVal, varSlot);
                emit(c, Op::SET_LOCAL, keyFromList, varSlot2);
                // map 分支才需要 coll[key]：必须包在 isMap 内
                int jmp2 = makeLabel(c);
                emit(c, Op::JMP_IF_FALSE, isMapR, -1);
                c.patches[jmp2].push_back(c.func->code.size() - 1);
                int valByKey = c.nextReg++;
                emit(c, Op::GET_INDEX, valByKey, collReg, keyFromList);
                emit(c, Op::SET_LOCAL, keyFromList, varSlot);  // map：name = 键
                emit(c, Op::SET_LOCAL, valByKey, varSlot2);     // map：name2 = 值
                patchLabel(c, jmp2);
            } else {
                // 单变量：map 绑键、list/str 绑元素，统一都是 keyFromList
                emit(c, Op::SET_LOCAL, keyFromList, varSlot);
            }

            for (const auto& st : s.body) compileStmt(c, *st);

            c.loops.pop_back();

            // idx = idx + 1
            int ok = constIdx(c, Value::makeInt(1));
            int oneR = c.nextReg++;
            emit(c, Op::CONST, oneR, ok);
            int idxR = c.nextReg++;
            emit(c, Op::GET_LOCAL, idxR, idxSlot);
            int incR = c.nextReg++;
            emit(c, Op::ADD, incR, idxR, oneR);
            emit(c, Op::SET_LOCAL, incR, idxSlot);

            emit(c, Op::JMP, 0, loopStart);
            patchLabel(c, endLabel);
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

} // namespace leash
