#include "checker/typecheck.hpp"
#include "common/error.hpp"
#include <unordered_map>
#include <unordered_set>

namespace leash {

void Checker::check(const Program& prog, const std::vector<std::string>&,
                    const std::vector<std::string>& globalNames) {
    // 0) 收集 JSON 导入产生的全局变量名
    for (const auto& g : globalNames) globals_.insert(g);

    // 1) build function table
    for (const auto& f : prog.fns) {
        FuncInfo info;
        info.arity = (int)f->params.size();
        info.caps = f->req_caps;
        if (!funcs_.emplace(f->name, info).second)
            throw CompileError("重复定义函数: " + f->name);
    }
    if (funcs_.find("main") == funcs_.end())
        throw CompileError("缺少入口函数 main");

        // 2) check each function body
    for (const auto& f : prog.fns) {
        Ctx c;
        c.scopes.emplace_back();
        for (const auto& cap : f->req_caps) c.caps.insert(cap);
        c.caps.insert("io"); // out/in 全局可用
        for (const auto& p : f->params) declare(c, p.first);
        for (const auto& s : f->body) checkStmt(c, *s);
        c.scopes.pop_back();
    }
}

void Checker::checkStmt(Ctx& c, const Stmt& s) {
    switch (s.k) {
        case Stmt::SLet: {
            if (s.init) checkExpr(c, *s.init);
            declare(c, s.name);
            break;
        }
        case Stmt::SAssign: {
            if (s.target && s.target->k == Expr::EIndex) {
                checkExpr(c, *s.target->lhs);
                checkExpr(c, *s.target->rhs);
                if (s.target->rhs2) checkExpr(c, *s.target->rhs2);
            } else {
                if (!lookup(c, s.name) && globals_.count(s.name) == 0) throw CompileError("赋值给未定义变量: " + s.name);
            }
            if (s.value) checkExpr(c, *s.value);
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
            // 循环内有效性由编译器保证（loops 栈）
            break;
        case Stmt::SFor: {
            checkExpr(c, *s.init);
            enter(c);
            declare(c, s.name);
            if (!s.name2.empty()) declare(c, s.name2);
            for (const auto& b : s.body) checkStmt(c, *b);
            leave(c);
            break;
        }
        case Stmt::SFn:
            // nested fn not supported in skeleton
            throw CompileError("函数内部不能嵌套定义函数");
    }
}

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
        case Expr::ECall: {
            if (e.s == "out" || e.s == "in") {
                for (const auto& a : e.exprs) checkExpr(c, *a);
                break;
            }
            auto it = funcs_.find(e.s);
            if (it == funcs_.end())
                throw CompileError("未知函数或标识符: " + e.s);
            // chat 支持 1 或 2 个参数（system,user 或 user），不强制固定数量
            if (e.s != "chat" && (int)e.exprs.size() != it->second.arity)
                throw CompileError("函数 " + e.s + " 参数数量不符: 期望 " +
                                   std::to_string(it->second.arity) + " 实参 " +
                                   std::to_string(e.exprs.size()));
            for (const auto& a : e.exprs) checkExpr(c, *a);
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

} // namespace leash
