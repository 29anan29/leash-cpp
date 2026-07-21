#include "checker/typecheck.hpp"
#include "common/error.hpp"
#include <unordered_map>
#include <unordered_set>

namespace leash {

// 需要能力的安全敏感裸全局函数 → 所需能力名
static const std::unordered_map<std::string, std::string> kSensitiveFns = {
    {"chat", "model"},
    {"read", "file"}, {"write", "file"}, {"exists", "file"}, {"delete", "file"}, {"append", "file"},
    {"now", "time"}, {"sleep", "time"},
    {"get", "net"}, {"post", "net"},
    {"env", "os"}, {"exit", "os"}, {"cwd", "os"}, {"is_tty", "os"},
    {"sha256", "crypto"},
    {"int", "random"}, {"float", "random"}, {"choice", "random"},
};

void Checker::check(const Program& prog, const std::vector<std::string>&,
                    const std::vector<std::string>& globalNames) {
    for (const auto& g : globalNames) globals_.insert(g);

    // build function table
    for (const auto& f : prog.fns) {
        FuncInfo info;
        info.arity = (int)f->params.size();
        info.caps = f->req_caps;
        info.isAgent = f->isAgent;
        info.isChain = f->isChain;
        if (!funcs_.emplace(f->name, info).second)
            throw CompileError("重复定义函数: " + f->name);
    }
    if (funcs_.find("main") == funcs_.end())
        throw CompileError("缺少入口函数 main");

    // check each function body
    for (const auto& f : prog.fns) {
        Ctx c;
        c.scopes.emplace_back();
        for (const auto& cap : f->req_caps) c.caps.insert(cap);
        c.caps.insert("io");
        c.funcName = f->name;
        c.isAgent = f->isAgent;
        // 注册参数：@untrusted 参数标记为不可信
        for (const auto& p : f->params) {
            declare(c, p.name);
            if (p.untrusted) c.trustedVars[p.name] = false;
        }
        for (const auto& s : f->body) checkStmt(c, *s);
        c.scopes.pop_back();
    }
}

// ========== 污点追踪辅助 ==========

void Checker::setTrusted(Ctx& c, const std::string& name, bool trusted) {
    c.trustedVars[name] = trusted;
}

bool Checker::varTrusted(Ctx& c, const std::string& name) {
    auto it = c.trustedVars.find(name);
    if (it == c.trustedVars.end()) return true; // 未标注 = 可信
    return it->second;
}

bool Checker::isTrusted(Ctx& c, const Expr& e) {
    switch (e.k) {
        case Expr::EInt:
        case Expr::EFloat:
        case Expr::EBool:
        case Expr::EStr:
        case Expr::ENil:
            return true; // 字面量总是可信
        case Expr::EVar:
            return varTrusted(c, e.s);
        case Expr::EBin:
            return isTrusted(c, *e.lhs) && isTrusted(c, *e.rhs);
        case Expr::EUn:
            return isTrusted(c, *e.lhs);
        case Expr::ECall: {
            // 函数返回值默认可信（除非调用了不可信参数的函数——简化处理）
            // 但安全敏感函数的结果也应可信（它们执行的是受控操作）
            for (const auto& a : e.exprs)
                if (!isTrusted(c, *a)) return false;
            return true;
        }
        case Expr::ECapCall: {
            // 能力调用的结果可信
            for (const auto& a : e.exprs)
                if (!isTrusted(c, *a)) return false;
            return true;
        }
        case Expr::EList:
            for (const auto& el : e.exprs)
                if (!isTrusted(c, *el)) return false;
            return true;
        case Expr::EMap:
            for (auto& pr : e.pairs)
                if (!isTrusted(c, *pr.first) || !isTrusted(c, *pr.second)) return false;
            return true;
        case Expr::EIndex:
            return isTrusted(c, *e.lhs) && isTrusted(c, *e.rhs) &&
                   (!e.rhs2 || isTrusted(c, *e.rhs2));
        default:
            return true;
    }
}

// ========== 检查表达式（不可信数据溯源） ==========

void Checker::checkExpr(Ctx& c, const Expr& e) {
    switch (e.k) {
        case Expr::EInt:
        case Expr::EFloat:
        case Expr::EBool:
        case Expr::EStr:
        case Expr::ENil:
            break;
        case Expr::EVar: {
            if (!lookup(c, e.s) && globals_.count(e.s) == 0)
                throw CompileError("未定义变量: " + e.s);
            break;
        }
        case Expr::EBin: {
            checkExpr(c, *e.lhs);
            checkExpr(c, *e.rhs);
            break;
        }
        case Expr::EUn: {
            checkExpr(c, *e.lhs);
            break;
        }
        case Expr::ECapCall: {
            // 必须 requires 过能力
            if (c.caps.count(e.capName) == 0)
                throw CompileError("能力 '" + e.capName + "' 不在函数作用域中（需要在 requires 中声明）");
            // 不可信数据不能传入能力调用（防 Confused Deputy）
            for (const auto& a : e.exprs) {
                checkExpr(c, *a);
                if (!isTrusted(c, *a))
                    throw CompileError("能力调用参数包含不可信数据（来自 @untrusted 参数），可能引发 Confused Deputy 攻击");
            }
            break;
        }
        case Expr::ECall: {
            if (e.s == "out" || e.s == "in") {
                for (const auto& a : e.exprs) {
                    checkExpr(c, *a);
                    // out/in 也不应输出不可信数据到不可控通道
                    if (!isTrusted(c, *a) && e.s == "out")
                        throw CompileError("不可信数据不能输出到 stdout（可能来自 @untrusted 参数）");
                }
                break;
            }
            // 安全敏感函数需能力声明
            if (c.funcName != "main") {
                auto sit = kSensitiveFns.find(e.s);
                if (sit != kSensitiveFns.end() && c.caps.count(sit->second) == 0)
                    throw CompileError("函数 '" + e.s + "' 需要 '" + sit->second +
                                       "' 能力，请在函数 " + c.funcName + " 的 requires 中声明");
            }
            // 安全敏感函数拒绝不可信参数（Confused Deputy 防护）
            if (kSensitiveFns.count(e.s)) {
                for (const auto& a : e.exprs) {
                    checkExpr(c, *a);
                    if (!isTrusted(c, *a))
                        throw CompileError("敏感函数 " + e.s + " 的参数包含不可信数据（来自 @untrusted 参数）");
                }
            } else {
                for (const auto& a : e.exprs) checkExpr(c, *a);
            }
            auto it = funcs_.find(e.s);
            if (it == funcs_.end())
                throw CompileError("未知函数或标识符: " + e.s);
            // 权限再委派防护：agent 函数不能调用其他 agent 函数
            if (c.isAgent && it->second.isAgent)
                throw CompileError("agent 函数 " + c.funcName + " 不能调用另一个 agent 函数 " + e.s + "（防权限再委派）");
            if (e.s != "chat" && (int)e.exprs.size() != it->second.arity)
                throw CompileError("函数 " + e.s + " 参数数量不符: 期望 " +
                                   std::to_string(it->second.arity) + " 实参 " +
                                   std::to_string(e.exprs.size()));
            break;
        }
        case Expr::EList: {
            for (const auto& el : e.exprs) checkExpr(c, *el);
            break;
        }
        case Expr::EIndex: {
            checkExpr(c, *e.lhs);
            checkExpr(c, *e.rhs);
            if (e.rhs2) checkExpr(c, *e.rhs2);
            break;
        }
        case Expr::EMap: {
            for (auto& pr : e.pairs) {
                checkExpr(c, *pr.first);
                checkExpr(c, *pr.second);
            }
            break;
        }
    }
}

void Checker::checkStmt(Ctx& c, const Stmt& s) {
    switch (s.k) {
        case Stmt::SLet: {
            if (s.init) {
                checkExpr(c, *s.init);
                setTrusted(c, s.name, isTrusted(c, *s.init));
            }
            declare(c, s.name);
            break;
        }
        case Stmt::SAssign: {
            if (s.target && s.target->k == Expr::EIndex) {
                checkExpr(c, *s.target->lhs);
                checkExpr(c, *s.target->rhs);
                if (s.target->rhs2) checkExpr(c, *s.target->rhs2);
            } else {
                if (!lookup(c, s.name) && globals_.count(s.name) == 0)
                    throw CompileError("赋值给未定义变量: " + s.name);
            }
            if (s.value) {
                checkExpr(c, *s.value);
                // 简单变量赋值传播可信度
                if (s.target && s.target->k == Expr::EVar)
                    setTrusted(c, s.target->s, isTrusted(c, *s.value));
            }
            break;
        }
        case Stmt::SReturn: {
            if (s.value) checkExpr(c, *s.value);
            break;
        }
        case Stmt::SExpr: {
            if (s.init) checkExpr(c, *s.init);
            break;
        }
        case Stmt::SIf: {
            checkExpr(c, *s.cond);
            enter(c);
            for (const auto& b : s.body) checkStmt(c, *b);
            leave(c);
            enter(c);
            for (const auto& b : s.elseB) checkStmt(c, *b);
            leave(c);
            break;
        }
        case Stmt::SWhile: {
            checkExpr(c, *s.cond);
            enter(c);
            for (const auto& b : s.body) checkStmt(c, *b);
            leave(c);
            break;
        }
        case Stmt::SBreak:
        case Stmt::SContinue:
            break;
        case Stmt::SFor: {
            checkExpr(c, *s.init);
            enter(c);
            declare(c, s.name);
            if (!s.name2.empty()) declare(c, s.name2);
            // 循环变量默认可信（由迭代器提供）
            for (const auto& b : s.body) checkStmt(c, *b);
            leave(c);
            break;
        }
        case Stmt::SFn:
            throw CompileError("函数内部不能嵌套定义函数");
    }
}

} // namespace leash