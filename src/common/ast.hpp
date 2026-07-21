#pragma once
#include "common/value.hpp"
#include <string>
#include <vector>
#include <memory>

namespace leash {

// Forward declarations for native (built-in) function support
struct HostContext;
using NativeFn = Value(*)(const std::vector<Value>&, HostContext&);

// ---------- Expressions ----------
struct Expr;
using ExprPtr = std::shared_ptr<Expr>;
struct Expr {
    enum K { EInt, EFloat, EBool, EStr, EVar, EBin, EUn, ECall, ECapCall, EIndex, EList, EMap, ENil } k;

    int64_t i = 0;
    double  f = 0.0;
    bool    b = false;

    std::string s;                 // var name / callee name / op
    std::string capName;           // ECapCall: 能力名（如 file / net / model）
    std::vector<std::string> parts; // EStr: literal parts, size == exprs.size()+1
    std::vector<ExprPtr> exprs;     // EStr exprs / ECall args / EList elements
    std::vector<std::pair<ExprPtr,ExprPtr>> pairs; // EMap: key/value pairs

    ExprPtr lhs, rhs;              // EBin / EUn(operand in lhs) / EIndex(container, index)
    ExprPtr rhs2;                  // EIndex slice end (when slice == true)
    bool slice = false;            // EIndex: is it a slice [i:j]?
    bool mut = false;              // reserved

    Expr() = default;
};

// ---------- Function parameter ----------
struct Param {
    std::string name;
    std::string type;
    bool untrusted = false;    // @untrusted：参数来自不可信外部源
};

// ---------- Statements ----------
struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;
struct Stmt {
    enum K { SLet, SAssign, SExpr, SReturn, SFn, SIf, SWhile, SBreak, SContinue, SFor } k;

    bool mut = false;
    std::string name;
    std::string name2;            // 可选的第二个循环变量（for a, b in ...）
    std::string typeAnno;                         // explicit type, e.g. "int"
    ExprPtr init;                                 // SLet
    ExprPtr value;                                // SAssign / SReturn
    ExprPtr target;                               // SAssign: lvalue (EVar or EIndex)
    ExprPtr cond;                                 // SIf / SWhile

    std::vector<Param> params; // SFn parameters
    std::vector<std::string> req_caps;            // SFn capability names
    std::vector<StmtPtr> body;                     // SFn / SIf(then) / SWhile
    std::vector<StmtPtr> elseB;                    // SIf
    bool isNative = false;                         // built-in (no user body)
    NativeFn nativeFn = nullptr;                   // C++ impl for native
    bool isTool = false;       // tool 关键字声明
    bool isAgent = false;      // agent 关键字声明
    bool isChain = false;      // chain 关键字声明
    int64_t maxSteps = -1;     // @max_steps(N)：agent/chain 最大步骤数（-1 = 无限制）
    std::vector<std::string> allowedTools; // agent 允许的工具白名单

    // 资源与确定性注解（§14）：在 fn 头部用 @fuel/@timeout/@deterministic 声明
    int64_t fuel = -1;          // @fuel(N)：本函数步数上限（-1 = 未指定，沿用全局默认）
    int64_t timeoutMs = -1;     // @timeout(Nms|Ns)：超时毫秒（-1 = 未指定）
    bool deterministic = false;    // @deterministic：禁止非确定源（now/random/sleep）
    bool audit = false;            // @audit：此函数内所有 cap 调用记入审计日志

    Stmt() = default;
};

struct Program {
    std::vector<StmtPtr> fns;
    std::vector<std::string> packages;         // from package <name> lines
    std::vector<std::string> imports;          // from import "x.ae" lines
    std::vector<std::string> jsonImports;      // from import "x.json" lines (顶层键变全局变量)
    std::vector<std::string> notes;
};

} // namespace leash
