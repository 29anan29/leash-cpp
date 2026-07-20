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
    struct FuncInfo { int arity = 0; std::vector<std::string> caps; };
    std::unordered_map<std::string, FuncInfo> funcs_;
    std::unordered_set<std::string> globals_;

    struct Ctx {
        std::vector<std::unordered_map<std::string, std::string>> scopes;
        std::unordered_set<std::string> caps;
    };

    void enter(Ctx& c) { c.scopes.emplace_back(); }
    void leave(Ctx& c) { c.scopes.pop_back(); }
    void declare(Ctx& c, const std::string& n) { c.scopes.back()[n] = "?"; }
    bool lookup(Ctx& c, const std::string& n) {
        for (auto it = c.scopes.rbegin(); it != c.scopes.rend(); ++it)
            if (it->count(n)) return true;
        return false;
    }

    void checkStmt(Ctx& c, const Stmt& s);
    void checkExpr(Ctx& c, const Expr& e);
};

} // namespace leash
