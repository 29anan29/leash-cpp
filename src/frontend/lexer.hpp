#pragma once
#include <string>
#include <vector>

namespace leash {

enum class TT {
    END, NEWLINE, INDENT, DEDENT,
    IDENT, INT, FLOAT, STRING_PART, INTERP_OPEN, INTERP_CLOSE,
    LPAREN, RPAREN, LBRACKET, RBRACKET, LBRACE, RBRACE, COMMA, COLON, DOT, ARROW,
    EQ, EQEQ, NEQ, PLUS, MINUS, STAR, SLASH, PERCENT,
    LT, GT, LE, GE, BANG, AMPAMP, PIPEPIPE,
    PLUSEQ, MINUSEQ, STAREQ, SLASHEQ, PERCENTEQ,
    // keywords
    KW_let, KW_mut, KW_fn, KW_if, KW_else, KW_match, KW_for, KW_while, KW_loop,
    KW_return, KW_type, KW_cap, KW_requires, KW_import, KW_package, KW_unsafe,
    KW_tool, KW_agent, KW_chain,
    KW_true, KW_false, KW_nil, KW_in, KW_out, KW_and, KW_or, KW_not, KW_break, KW_continue
};

struct Token {
    TT type;
    std::string text;
    int line = 0;
};

class Lexer {
public:
    static std::vector<Token> tokenize(const std::string& src);
    static TT keyword(const std::string& s);
};

} // namespace leash
