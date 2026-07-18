#include "vm/vm.hpp"
#include "common/error.hpp"
#include <cmath>
#include <cassert>

namespace aegis {

// ---- helpers ----
static double toDouble(const Value& v) {
    if (v.t == Value::T::Int) return (double)v.i;
    if (v.t == Value::T::Float) return v.f;
    throw RuntimeError("需要数值类型");
}
static bool lt(const Value& a, const Value& b) { return toDouble(a) < toDouble(b); }
static bool gt(const Value& a, const Value& b) { return toDouble(a) > toDouble(b); }
static bool le(const Value& a, const Value& b) { return toDouble(a) <= toDouble(b); }
static bool ge(const Value& a, const Value& b) { return toDouble(a) >= toDouble(b); }
static bool eq(const Value& a, const Value& b, const Store& store) {
    if (a.t != b.t) return false;
    switch (a.t) {
        case Value::T::Int:   return a.i == b.i;
        case Value::T::Float: return a.f == b.f;
        case Value::T::Bool:  return a.b == b.b;
        case Value::T::Str:   return store.str((uint32_t)a.i) == store.str((uint32_t)b.i);
        case Value::T::Nil:   return true;
        default: return false;
    }
}

// ---- VM ----
int VM::fnIdx(const std::string& name) const {
    for (size_t i = 0; i < funcs_.size(); ++i)
        if (funcs_[i].name == name) return (int)i;
    throw RuntimeError("函数未找到: " + name);
}

Value VM::run() {
    int mainIdx = fnIdx("main");
    const Function& mainFn = funcs_[mainIdx];

    // build capEnv for main
    std::vector<std::shared_ptr<Capability>> capEnv;
    for (const auto& capName : mainFn.capNames) {
        auto cap = ctx_.provideCap(capName);
        if (!cap) throw RuntimeError("宿主拒绝提供能力: " + capName);
        capEnv.push_back(std::move(cap));
    }

    Frame f;
    f.func = &mainFn;
    f.regs.resize(mainFn.num_regs);
    f.capEnv = std::move(capEnv);
    frames_.push_back(std::move(f));

    while (!frames_.empty()) {
        Frame& frame = frames_.back();
        const Function* func = frame.func;
        if (frame.ip >= func->code.size()) {
            frames_.pop_back();
            continue;
        }

        if (--globalFuel_ <= 0)
            throw RuntimeError("燃料耗尽");

        const Instr& instr = func->code[frame.ip];
        Op op = instr.op;

        switch (op) {
            case Op::HALT:
                return Value::nil();

            case Op::CONST:
                frame.regs[instr.a] = func->consts[instr.b];
                frame.ip++;
                break;

            case Op::CONST_STR: {
                uint32_t idx = ctx_.store().intern(func->constStrings[instr.b]);
                frame.regs[instr.a] = Value::makeStr(idx);
                frame.ip++;
                break;
            }
            case Op::GET_LOCAL:
                frame.regs[instr.a] = frame.regs[instr.b];
                frame.ip++;
                break;
            case Op::SET_LOCAL:
                frame.regs[instr.b] = frame.regs[instr.a];
                frame.ip++;
                break;

            case Op::ADD: {
                auto& l = frame.regs[instr.b];
                auto& r = frame.regs[instr.c];
                if (l.t == Value::T::Str || r.t == Value::T::Str) {
                    std::string s = valueToStr(ctx_.store(), l) + valueToStr(ctx_.store(), r);
                    frame.regs[instr.a] = Value::makeStr(ctx_.store().intern(s));
                } else if (l.t == Value::T::Int && r.t == Value::T::Int)
                    frame.regs[instr.a] = Value::makeInt(l.i + r.i);
                else
                    frame.regs[instr.a] = Value::makeFloat(toDouble(l) + toDouble(r));
                frame.ip++; break;
            }
            case Op::SUB: {
                auto& l = frame.regs[instr.b], &r = frame.regs[instr.c];
                if (l.t == Value::T::Int && r.t == Value::T::Int)
                    frame.regs[instr.a] = Value::makeInt(l.i - r.i);
                else
                    frame.regs[instr.a] = Value::makeFloat(toDouble(l) - toDouble(r));
                frame.ip++; break;
            }
            case Op::MUL: {
                auto& l = frame.regs[instr.b], &r = frame.regs[instr.c];
                if (l.t == Value::T::Int && r.t == Value::T::Int)
                    frame.regs[instr.a] = Value::makeInt(l.i * r.i);
                else
                    frame.regs[instr.a] = Value::makeFloat(toDouble(l) * toDouble(r));
                frame.ip++; break;
            }
            case Op::DIV: {
                auto& l = frame.regs[instr.b], &r = frame.regs[instr.c];
                if (l.t == Value::T::Int && r.t == Value::T::Int && r.i != 0)
                    frame.regs[instr.a] = Value::makeInt(l.i / r.i);
                else
                    frame.regs[instr.a] = Value::makeFloat(toDouble(l) / toDouble(r));
                frame.ip++; break;
            }
            case Op::MOD: {
                auto& l = frame.regs[instr.b], &r = frame.regs[instr.c];
                if (l.t != Value::T::Int || r.t != Value::T::Int)
                    throw RuntimeError("% 仅支持整数");
                if (r.i == 0) throw RuntimeError("除以零");
                frame.regs[instr.a] = Value::makeInt(l.i % r.i);
                frame.ip++; break;
            }
            case Op::NEG: {
                auto& v = frame.regs[instr.b];
                if (v.t == Value::T::Int) frame.regs[instr.a] = Value::makeInt(-v.i);
                else if (v.t == Value::T::Float) frame.regs[instr.a] = Value::makeFloat(-v.f);
                else throw RuntimeError("数值取负失败");
                frame.ip++; break;
            }
            case Op::NOT: {
                auto& v = frame.regs[instr.b];
                if (v.t != Value::T::Bool) throw RuntimeError("not 需要布尔值");
                frame.regs[instr.a] = Value::makeBool(!v.b);
                frame.ip++; break;
            }
            case Op::LT:  frame.regs[instr.a] = Value::makeBool(lt(frame.regs[instr.b], frame.regs[instr.c])); frame.ip++; break;
            case Op::GT:  frame.regs[instr.a] = Value::makeBool(gt(frame.regs[instr.b], frame.regs[instr.c])); frame.ip++; break;
            case Op::LE:  frame.regs[instr.a] = Value::makeBool(le(frame.regs[instr.b], frame.regs[instr.c])); frame.ip++; break;
            case Op::GE:  frame.regs[instr.a] = Value::makeBool(ge(frame.regs[instr.b], frame.regs[instr.c])); frame.ip++; break;
            case Op::EQ:  frame.regs[instr.a] = Value::makeBool(eq(frame.regs[instr.b], frame.regs[instr.c], ctx_.store())); frame.ip++; break;
            case Op::NE:  frame.regs[instr.a] = Value::makeBool(!eq(frame.regs[instr.b], frame.regs[instr.c], ctx_.store())); frame.ip++; break;

            case Op::AND: {
                auto& v = frame.regs[instr.b];
                if (v.t != Value::T::Bool) throw RuntimeError("&& 需要布尔值");
                if (!v.b) { frame.regs[instr.a] = Value::makeBool(false); }
                else {
                    auto& w = frame.regs[instr.c];
                    if (w.t != Value::T::Bool) throw RuntimeError("&& 需要布尔值");
                    frame.regs[instr.a] = w;
                }
                frame.ip++; break;
            }
            case Op::OR: {
                auto& v = frame.regs[instr.b];
                if (v.t != Value::T::Bool) throw RuntimeError("|| 需要布尔值");
                if (v.b) { frame.regs[instr.a] = Value::makeBool(true); }
                else {
                    auto& w = frame.regs[instr.c];
                    if (w.t != Value::T::Bool) throw RuntimeError("|| 需要布尔值");
                    frame.regs[instr.a] = w;
                }
                frame.ip++; break;
            }
            case Op::JMP_IF_FALSE: {
                auto& v = frame.regs[instr.a];
                if (v.t != Value::T::Bool) throw RuntimeError("条件需要布尔值");
                if (!v.b) frame.ip = (size_t)instr.b;
                else frame.ip++;
                break;
            }
            case Op::JMP:
                frame.ip = (size_t)instr.b;
                break;

            case Op::CALL: {
                const Function& callee = funcs_[instr.b];
                int arity = callee.arity;
                int retReg = instr.a;
                frame.ip++; // advance past CALL

                // Native function — execute directly
                if (callee.isNative) {
                    std::vector<Value> args;
                    for (int i = 0; i < arity; ++i)
                        args.push_back(frame.regs[instr.c + i]);
                    Value result = callee.nativeFn(args, ctx_);
                    frame.regs[retReg] = result;
                    break;
                }

                // User function — push a new frame
                Frame calleeFrame;
                calleeFrame.func = &callee;
                calleeFrame.regs.resize(callee.num_regs);
                calleeFrame.retReg = retReg;
                for (int i = 0; i < arity; ++i)
                    calleeFrame.regs[i] = frame.regs[instr.c + i];
                // propagate capabilities from caller to callee by name
                calleeFrame.capEnv.resize(callee.capNames.size());
                for (size_t ci = 0; ci < callee.capNames.size(); ++ci) {
                    const auto& cname = callee.capNames[ci];
                    // find in caller's capEnv
                    bool found = false;
                    for (size_t fj = 0; fj < frame.func->capNames.size(); ++fj) {
                        if (frame.func->capNames[fj] == cname) {
                            calleeFrame.capEnv[ci] = frame.capEnv[fj];
                            found = true; break;
                        }
                    }
                    if (!found)
                        throw RuntimeError("函数 " + callee.name + " 缺少能力: " + cname);
                }
                calleeFrame.ip = 0;
                frames_.push_back(std::move(calleeFrame));
                break;
            }
            case Op::RET: {
                Value retVal = frame.regs[instr.a];
                int dstReg = frame.retReg;
                frames_.pop_back();
                if (!frames_.empty()) {
                    Frame& caller = frames_.back();
                    if (dstReg >= 0) caller.regs[dstReg] = retVal;
                } else {
                    return retVal;
                }
                break;
            }
            case Op::RET_NIL: {
                int dstReg = frame.retReg;
                frames_.pop_back();
                if (!frames_.empty()) {
                    Frame& caller = frames_.back();
                    if (dstReg >= 0) caller.regs[dstReg] = Value::nil();
                }
                break;
            }
            case Op::INVOKE_CAP: {
                auto& capPtr = frame.capEnv[instr.b];
                if (!capPtr) throw RuntimeError("能力句柄无效");
                int arity = func->methodArity[instr.c];
                std::vector<Value> args;
                for (int i = 0; i < arity; ++i)
                    args.push_back(frame.regs[instr.d + i]);
                Value result = capPtr->invoke(func->methodNames[instr.c], args, ctx_);
                frame.regs[instr.a] = result;
                frame.ip++;
                break;
            }
            case Op::CONCAT: {
                auto& l = frame.regs[instr.b];
                auto& r = frame.regs[instr.c];
                std::string s = valueToStr(ctx_.store(), l) + valueToStr(ctx_.store(), r);
                uint32_t idx = ctx_.store().intern(s);
                frame.regs[instr.a] = Value::makeStr(idx);
                frame.ip++;
                break;
            }
            default:
                throw RuntimeError("未知字节码操作");
        }
    }
    return Value::nil();
}

} // namespace aegis
