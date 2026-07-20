#pragma once
#include "common/ast.hpp"
#include "frontend/lexer.hpp"
#include <vector>

namespace leash {

class Parser {
public:
    explicit Parser(std::vector<Token> toks);
    Program parse();

private:
    std::vector<Token> t_;
    size_t pos_ = 0;

    const Token& peek(size_t k = 0) const;
    const Token& next();
    bool atEnd() const;
    void skipNewlines();
    bool check(TT t) const;
    std::string parsePackageName();

    StmtPtr parseFn();
    void skipLine();
    void skipBlock();              // for agent/tool/chain stubs
    std::vector<StmtPtr> parseBlock();

    StmtPtr parseStmt();
    StmtPtr parseLet();
    StmtPtr parseAssign();
    StmtPtr parseReturn();
    StmtPtr parseIf();
    StmtPtr parseWhile();
    StmtPtr parseFor();

    ExprPtr parseExpr();
    ExprPtr parseOr();
    ExprPtr parseAnd();
    ExprPtr parseEquality();
    ExprPtr parseRelational();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parseUnary();
    ExprPtr parsePrimary();
    ExprPtr parseString();
    ExprPtr parseList();                 // [a, b, c]
    ExprPtr parseMap();                  // {"k": v, ...}
    ExprPtr parseIndexSuffix(ExprPtr base); // 处理 var[idx] / var[i:j] 后缀
};

} // namespace leash
