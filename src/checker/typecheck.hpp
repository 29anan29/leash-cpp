#pragma once
#include "common/ast.hpp"
#include <unordered_map>
#include <unordered_set>

namespace leash {

// Lightweight semantic pass:
//  - builds a function table (name -> arity, required caps)
//  - enforces lexical scoping (no use-before-definition)
//  - enforces capability-in-scope: out / in require `io` in the fn's `requires`
//  - checks arity of user-function calls
// Full Hindley-Milner inference is intentionally out of scope for the skeleton.
class Checker {
public:
    void check(const Program& prog, const std::vector<std::string>& packages = {},
               const std::vector<std::string>& globalNames = {});

private:
    struct FuncInfo { int arity = 0; std::vector<std::string> caps; bool isAgent = false; bool isChain = false; };
    std::unordered_map<std::string, FuncInfo> funcs_;
    std::unordered_set<std::string> globals_;

    struct Ctx {
        std::vector<std::unordered_map<std::string, std::string>> scopes;
        std::unordered_set<std::string> caps;
        std::string funcName;    // 当前正在检查的函数名
        bool isAgent = false;    // 当前函数是否是 agent 关键字声明
        std::unordered_map<std::string, bool> trustedVars; // 变量名 → 是否可信
    };

    void enter(Ctx& c) { c.scopes.emplace_back(); }
    void leave(Ctx& c) { c.scopes.pop_back(); }
    void declare(Ctx& c, const std::string& n) { c.scopes.back()[n] = "?"; }
    bool lookup(Ctx& c, const std::string& n) {
        for (auto it = c.scopes.rbegin(); it != c.scopes.rend(); ++it)
            if (it->count(n)) return true;
        return false;
    }
    // 污点追踪
    bool isTrusted(Ctx& c, const Expr& e);  // 表达式是否可信（无不可信来源）
    void setTrusted(Ctx& c, const std::string& name, bool trusted);
    bool varTrusted(Ctx& c, const std::string& name);

    void checkStmt(Ctx& c, const Stmt& s);
    void checkExpr(Ctx& c, const Expr& e);
};

} // namespace leash
