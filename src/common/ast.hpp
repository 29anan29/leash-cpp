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
    enum K { EInt, EFloat, EBool, EStr, EVar, EBin, EUn, ECall, EIndex, EList, EMap, ENil } k;

    int64_t i = 0;
    double  f = 0.0;
    bool    b = false;

    std::string s;                 // var name / callee name / op
    std::vector<std::string> parts; // EStr: literal parts, size == exprs.size()+1
    std::vector<ExprPtr> exprs;     // EStr exprs / ECall args / EList elements
    std::vector<std::pair<ExprPtr,ExprPtr>> pairs; // EMap: key/value pairs

    ExprPtr lhs, rhs;              // EBin / EUn(operand in lhs) / EIndex(container, index)
    ExprPtr rhs2;                  // EIndex slice end (when slice == true)
    bool slice = false;            // EIndex: is it a slice [i:j]?
    bool mut = false;              // reserved

    Expr() = default;
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

    std::vector<std::pair<std::string, std::string>> params; // SFn (name,type)
    std::vector<std::string> req_caps;            // SFn capability names
    std::vector<StmtPtr> body;                     // SFn / SIf(then) / SWhile
    std::vector<StmtPtr> elseB;                    // SIf
    bool isNative = false;                         // built-in (no user body)
    NativeFn nativeFn = nullptr;                   // C++ impl for native

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
