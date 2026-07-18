#include "frontend/parser.hpp"
#include "common/error.hpp"

namespace aegis {

Parser::Parser(std::vector<Token> toks) : t_(std::move(toks)) {}

const Token& Parser::peek(size_t k) const {
    static const Token endTok{TT::END, "", 0};
    if (pos_ + k >= t_.size()) return endTok;
    return t_[pos_ + k];
}
const Token& Parser::next() { return t_[pos_++]; }
bool Parser::atEnd() const { return peek().type == TT::END; }
bool Parser::check(TT ty) const { return peek().type == ty; }

void Parser::skipNewlines() {
    while (peek().type == TT::NEWLINE) next();
}

Program Parser::parse() {
    Program prog;
    while (!atEnd()) {
        skipNewlines();
        if (atEnd()) break;
        TT ty = peek().type;
        if (ty == TT::KW_fn) {
            prog.fns.push_back(parseFn());
    } else if (ty == TT::KW_package) {
        next(); // consume 'package'
        prog.packages.push_back(parsePackageName());
        skipNewlines();
    } else if (ty == TT::KW_import) {
        next(); // consume 'import'
        if (check(TT::STRING_PART)) {
            prog.imports.push_back(next().text);
        } else if (check(TT::IDENT)) {
            prog.imports.push_back(next().text);
        } else {
            throw CompileError("import 后需要模块路径（字符串或模块名）");
        }
        skipNewlines();
        } else if (ty == TT::KW_agent || ty == TT::KW_tool || ty == TT::KW_chain) {
            skipBlock();
            prog.notes.push_back("agent/tool/chain 暂未执行（骨架阶段）");
        } else {
            throw CompileError("顶层仅允许 fn / package / import / agent / tool / chain，遇到: " + peek().text);
        }
    }
    return prog;
}

void Parser::skipLine() {
    while (!atEnd() && peek().type != TT::NEWLINE) next();
    if (!atEnd()) next(); // consume NEWLINE
}

void Parser::skipBlock() {
    // consume header line
    while (!atEnd() && peek().type != TT::NEWLINE) next();
    if (!atEnd()) next();
    if (peek().type == TT::INDENT) {
        next(); // INDENT
        int depth = 1;
        while (!atEnd() && depth > 0) {
            if (peek().type == TT::INDENT) ++depth;
            else if (peek().type == TT::DEDENT) --depth;
            next();
        }
    }
}

StmtPtr Parser::parseFn() {
    next(); // KW_fn
    if (!check(TT::IDENT)) throw CompileError("fn 后需要函数名");
    std::string name = next().text;

    auto fn = std::make_shared<Stmt>();
    fn->k = Stmt::SFn;
    fn->name = name;

    if (check(TT::LPAREN)) {
        next();
        while (!check(TT::RPAREN)) {
            if (!check(TT::IDENT)) throw CompileError("函数参数需要名字");
            std::string pname = next().text;
            std::string ptype;
            if (check(TT::COLON)) { next(); if (!check(TT::IDENT)) throw CompileError("参数类型缺失"); ptype = next().text; }
            fn->params.emplace_back(pname, ptype);
            if (check(TT::COMMA)) next();
        }
        next(); // RPAREN
    }

    // optional return type annotation
    if (check(TT::ARROW)) {
        next(); // consume ->
        if (!check(TT::IDENT)) throw CompileError("返回值类型缺失");
        next(); // skip return type name
    }

    if (check(TT::KW_requires)) {
        next();
        for (;;) {
            if (!check(TT::IDENT)) throw CompileError("requires 后需要能力名");
            fn->req_caps.push_back(next().text);
            if (check(TT::COLON)) { next(); if (!check(TT::IDENT)) throw CompileError("能力类型缺失"); next(); }
            if (check(TT::COMMA)) { next(); continue; }
            break;
        }
    }

    fn->body = parseBlock();
    return fn;
}

std::vector<StmtPtr> Parser::parseBlock() {
    skipNewlines();
    if (!check(TT::INDENT)) throw CompileError("需要缩进块 (INDENT)");
    next(); // INDENT
    std::vector<StmtPtr> stmts;
    while (!atEnd() && peek().type != TT::DEDENT) {
        skipNewlines();
        if (peek().type == TT::DEDENT) break;
        if (atEnd()) break;
        stmts.push_back(parseStmt());
    }
    if (peek().type == TT::DEDENT) next();
    return stmts;
}

StmtPtr Parser::parseStmt() {
    TT ty = peek().type;
    if (ty == TT::KW_let) return parseLet();
    if (ty == TT::KW_return) return parseReturn();
    if (ty == TT::KW_if) return parseIf();
    if (ty == TT::KW_while) return parseWhile();
    if (ty == TT::KW_break) { next(); auto s = std::make_shared<Stmt>(); s->k = Stmt::SBreak; skipNewlines(); return s; }
    if (ty == TT::KW_continue) { next(); auto s = std::make_shared<Stmt>(); s->k = Stmt::SContinue; skipNewlines(); return s; }
    if (ty == TT::IDENT && peek(1).type == TT::EQ) return parseAssign();

    ExprPtr e = parseExpr();
    skipNewlines();
    auto s = std::make_shared<Stmt>();
    s->k = Stmt::SExpr;
    s->init = e;
    return s;
}

StmtPtr Parser::parseLet() {
    next(); // KW_let
    bool mut = false;
    if (check(TT::KW_mut)) { next(); mut = true; }
    std::string typeAnno;
    std::string name;
    if (check(TT::IDENT) && peek(1).type == TT::COLON) {
        typeAnno = next().text;
        next(); // COLON
        if (!check(TT::IDENT)) throw CompileError("类型注解后需要变量名");
        name = next().text;
    } else if (check(TT::IDENT)) {
        name = next().text;
    } else {
        throw CompileError("let 后需要变量名");
    }
    ExprPtr init;
    if (check(TT::EQ)) { next(); init = parseExpr(); }
    skipNewlines();
    auto s = std::make_shared<Stmt>();
    s->k = Stmt::SLet; s->mut = mut; s->name = name; s->typeAnno = typeAnno; s->init = init;
    return s;
}

StmtPtr Parser::parseAssign() {
    std::string name = next().text; // IDENT
    next(); // EQ
    ExprPtr v = parseExpr();
    skipNewlines();
    auto s = std::make_shared<Stmt>();
    s->k = Stmt::SAssign; s->name = name; s->value = v;
    return s;
}

StmtPtr Parser::parseReturn() {
    next(); // KW_return
    ExprPtr v;
    if (peek().type != TT::NEWLINE && peek().type != TT::DEDENT && peek().type != TT::END)
        v = parseExpr();
    skipNewlines();
    auto s = std::make_shared<Stmt>();
    s->k = Stmt::SReturn; s->value = v;
    return s;
}

StmtPtr Parser::parseIf() {
    next(); // KW_if
    ExprPtr cond = parseExpr();
    auto s = std::make_shared<Stmt>();
    s->k = Stmt::SIf; s->cond = cond;
    s->body = parseBlock();
    skipNewlines();
    if (check(TT::KW_else)) {
        next();
        s->elseB = parseBlock();
    }
    return s;
}

StmtPtr Parser::parseWhile() {
    next(); // KW_while
    ExprPtr cond = parseExpr();
    auto s = std::make_shared<Stmt>();
    s->k = Stmt::SWhile; s->cond = cond;
    s->body = parseBlock();
    return s;
}

ExprPtr Parser::parseExpr() { return parseOr(); }

ExprPtr Parser::parseOr() {
    ExprPtr left = parseAnd();
    while (check(TT::PIPEPIPE) || check(TT::KW_or)) {
        next(); ExprPtr r = parseAnd();
        auto e = std::make_shared<Expr>(); e->k = Expr::EBin; e->s = "or"; e->lhs = left; e->rhs = r;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseAnd() {
    ExprPtr left = parseEquality();
    while (check(TT::AMPAMP) || check(TT::KW_and)) {
        next(); ExprPtr r = parseEquality();
        auto e = std::make_shared<Expr>(); e->k = Expr::EBin; e->s = "and"; e->lhs = left; e->rhs = r;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseEquality() {
    ExprPtr left = parseRelational();
    while (check(TT::EQEQ) || check(TT::NEQ)) {
        std::string op = (next().type == TT::EQEQ) ? "==" : "!=";
        ExprPtr r = parseRelational();
        auto e = std::make_shared<Expr>(); e->k = Expr::EBin; e->s = op; e->lhs = left; e->rhs = r;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseRelational() {
    ExprPtr left = parseAdditive();
    while (check(TT::LT) || check(TT::GT) || check(TT::LE) || check(TT::GE)) {
        std::string op = next().text;
        ExprPtr r = parseAdditive();
        auto e = std::make_shared<Expr>(); e->k = Expr::EBin; e->s = op; e->lhs = left; e->rhs = r;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseAdditive() {
    ExprPtr left = parseMultiplicative();
    while (check(TT::PLUS) || check(TT::MINUS)) {
        std::string op = next().text;
        ExprPtr r = parseMultiplicative();
        auto e = std::make_shared<Expr>(); e->k = Expr::EBin; e->s = op; e->lhs = left; e->rhs = r;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseMultiplicative() {
    ExprPtr left = parseUnary();
    while (check(TT::STAR) || check(TT::SLASH) || check(TT::PERCENT)) {
        std::string op = next().text;
        ExprPtr r = parseUnary();
        auto e = std::make_shared<Expr>(); e->k = Expr::EBin; e->s = op; e->lhs = left; e->rhs = r;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseUnary() {
    if (check(TT::MINUS) || check(TT::KW_not) || check(TT::BANG)) {
        std::string op = next().text;
        ExprPtr operand = parseUnary();
        auto e = std::make_shared<Expr>(); e->k = Expr::EUn; e->s = (op == "-") ? "neg" : "not"; e->lhs = operand;
        return e;
    }
    return parsePrimary();
}

ExprPtr Parser::parsePrimary() {
    TT ty = peek().type;
    if (ty == TT::INT) { auto e=std::make_shared<Expr>(); e->k=Expr::EInt; e->i=std::stoll(next().text); return e; }
    if (ty == TT::FLOAT) { auto e=std::make_shared<Expr>(); e->k=Expr::EFloat; e->f=std::stod(next().text); return e; }
    if (ty == TT::KW_true) { next(); auto e=std::make_shared<Expr>(); e->k=Expr::EBool; e->b=true; return e; }
    if (ty == TT::KW_false) { next(); auto e=std::make_shared<Expr>(); e->k=Expr::EBool; e->b=false; return e; }
    if (ty == TT::STRING_PART || ty == TT::INTERP_OPEN) return parseString();
    if (ty == TT::LPAREN) {
        next(); ExprPtr e = parseExpr(); if (!check(TT::RPAREN)) throw CompileError("缺少 )"); next(); return e;
    }
    if (ty == TT::IDENT || ty == TT::KW_in || ty == TT::KW_out) {
        std::string name = next().text;
        if (check(TT::LPAREN)) {
            next();
            std::vector<ExprPtr> args;
            while (!check(TT::RPAREN)) {
                args.push_back(parseExpr());
                if (check(TT::COMMA)) next();
            }
            next(); // RPAREN
            auto e = std::make_shared<Expr>(); e->k = Expr::ECall; e->s = name; e->exprs = std::move(args);
            return e;
        }
        auto e = std::make_shared<Expr>(); e->k = Expr::EVar; e->s = name;
        return e;
    }
    throw CompileError("无法解析的表达式，遇到: " + peek().text);
}

std::string Parser::parsePackageName() {
    std::string path;
    auto accept = [&]() -> bool {
        if (check(TT::IDENT) || (peek().type >= TT::KW_let && peek().type <= TT::KW_continue)) {
            path += next().text;
            return true;
        }
        return false;
    };
    if (!accept()) throw CompileError("package 后需要模块名");
    while (check(TT::DOT)) {
        next();
        path += ".";
        if (!accept()) throw CompileError("package 路径中需要模块名");
    }
    return path;
}

ExprPtr Parser::parseString() {
    auto e = std::make_shared<Expr>();
    e->k = Expr::EStr;
    if (peek().type == TT::STRING_PART) { e->parts.push_back(next().text); }
    else e->parts.push_back("");
    while (peek().type == TT::INTERP_OPEN) {
        next(); // INTERP_OPEN
        e->exprs.push_back(parseExpr());
        if (!check(TT::INTERP_CLOSE)) throw CompileError("字符串插值未闭合 }");
        next(); // INTERP_CLOSE
        if (peek().type == TT::STRING_PART) e->parts.push_back(next().text);
        else e->parts.push_back("");
    }
    return e;
}

} // namespace aegis
