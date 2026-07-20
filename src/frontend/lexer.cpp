#include "frontend/lexer.hpp"
#include "common/error.hpp"
#include <cctype>
#include <unordered_map>

namespace leash {

static const std::unordered_map<std::string, TT>& kwmap() {
    static const std::unordered_map<std::string, TT> m = {
        {"let",TT::KW_let},{"mut",TT::KW_mut},{"fn",TT::KW_fn},{"if",TT::KW_if},
        {"else",TT::KW_else},{"match",TT::KW_match},{"for",TT::KW_for},{"while",TT::KW_while},
        {"loop",TT::KW_loop},{"return",TT::KW_return},{"type",TT::KW_type},{"cap",TT::KW_cap},
        {"requires",TT::KW_requires},{"import",TT::KW_import},{"package",TT::KW_package},
        {"unsafe",TT::KW_unsafe},{"tool",TT::KW_tool},{"agent",TT::KW_agent},{"chain",TT::KW_chain},
        {"true",TT::KW_true},{"false",TT::KW_false},{"nil",TT::KW_nil},
        {"in",TT::KW_in},{"out",TT::KW_out},{"and",TT::KW_and},{"or",TT::KW_or},
        {"not",TT::KW_not},{"break",TT::KW_break},{"continue",TT::KW_continue},
    };
    return m;
}

TT Lexer::keyword(const std::string& s) {
    auto it = kwmap().find(s);
    return it == kwmap().end() ? TT::IDENT : it->second;
}

// Scan one physical line (already stripped of trailing newline). Skips // and /* */ comments.
// Appends tokens; handles string literals with {} interpolation.
static void scanLine(const std::string& line, int lineNo, std::vector<Token>& out) {
    const size_t n = line.size();
    size_t p = 0;
    auto push = [&](TT t, std::string txt){ out.push_back(Token{t, std::move(txt), lineNo}); };

    while (p < n) {
        char c = line[p];

        // whitespace
        if (std::isspace((unsigned char)c)) { ++p; continue; }

        // comments
        if (c == '/' && p + 1 < n && line[p+1] == '/') break; // rest of line is comment
        if (c == '/' && p + 1 < n && line[p+1] == '*') {
            p += 2;
            while (p + 1 < n && !(line[p] == '*' && line[p+1] == '/')) ++p;
            p += 2; continue;
        }

        // string literal (with interpolation)
        if (c == '"') {
            bool raw = (p > 0 && line[p-1] == 'r');
            std::string buf;
            p++; // skip opening quote
            while (p < n && line[p] != '"') {
                // 转义优先于插值：\{ 表示字面量 {
                if (line[p] == '\\' && p + 1 < n) {
                    char e = line[p+1];
                    switch (e) {
                        case 'n': buf.push_back('\n'); break;
                        case 't': buf.push_back('\t'); break;
                        case '\\': buf.push_back('\\'); break;
                        case '"': buf.push_back('"'); break;
                        default: buf.push_back(e); break;
                    }
                    p += 2; continue;
                }
                if (!raw && line[p] == '{') {
                    if (!buf.empty()) { push(TT::STRING_PART, buf); buf.clear(); }
                    push(TT::INTERP_OPEN, "");
                    p++; // skip '{'
                    int depth = 1;
                    while (p < n && depth > 0) {
                        if (line[p] == '{') ++depth;
                        else if (line[p] == '}') { --depth; if (depth == 0) { ++p; break; } }
                        buf.push_back(line[p]); p++;
                    }
                    if (depth != 0) throw CompileError("未闭合的字符串插值 {");
                    scanLine(buf, lineNo, out);
                    push(TT::INTERP_CLOSE, "");
                    buf.clear();
                    continue;
                }
                buf.push_back(line[p]); p++;
            }
            if (p >= n) throw CompileError("未闭合的字符串字面量");
            p++; // skip closing "
            push(TT::STRING_PART, buf);
            continue;
        }

        // number
        if (std::isdigit((unsigned char)c) || (c == '.' && p+1<n && std::isdigit((unsigned char)line[p+1]))) {
            std::string num;
            bool isFloat = false;
            while (p < n && (std::isdigit((unsigned char)line[p]) || line[p]=='.' || line[p]=='e' || line[p]=='E' || line[p]=='+'||line[p]=='-')) {
                if (line[p]=='.' || line[p]=='e' || line[p]=='E') isFloat = true;
                num.push_back(line[p]); p++;
            }
            if (isFloat) push(TT::FLOAT, num);
            else push(TT::INT, num);
            continue;
        }

        // identifier / keyword
        if (std::isalpha((unsigned char)c) || c == '_') {
            std::string id;
            while (p < n && (std::isalnum((unsigned char)line[p]) || line[p]=='_')) { id.push_back(line[p]); p++; }
            push(Lexer::keyword(id), id);
            continue;
        }

        // multi-char punctuation
        if (c == '-' && p+1<n && line[p+1]=='>') { push(TT::ARROW,"->"); p+=2; continue; }
        if (c == '=' && p+1<n) {
            char nx = line[p+1];
            if (nx == '=') { push(TT::EQEQ,"=="); p+=2; continue; }
        }
        if (c == '+' && p+1<n && line[p+1]=='=') { push(TT::PLUSEQ,"+="); p+=2; continue; }
        if (c == '-' && p+1<n && line[p+1]=='=') { push(TT::MINUSEQ,"-="); p+=2; continue; }
        if (c == '*' && p+1<n && line[p+1]=='=') { push(TT::STAREQ,"*="); p+=2; continue; }
        if (c == '/' && p+1<n && line[p+1]=='=') { push(TT::SLASHEQ,"/="); p+=2; continue; }
        if (c == '%' && p+1<n && line[p+1]=='=') { push(TT::PERCENTEQ,"%="); p+=2; continue; }
        if (c == '!' && p+1<n && line[p+1]=='=') { push(TT::NEQ,"!="); p+=2; continue; }
        if (c == '<' && p+1<n && line[p+1]=='=') { push(TT::LE,"<="); p+=2; continue; }
        if (c == '>' && p+1<n && line[p+1]=='=') { push(TT::GE,">="); p+=2; continue; }
        if (c == '&' && p+1<n && line[p+1]=='&') { push(TT::AMPAMP,"&&"); p+=2; continue; }
        if (c == '|' && p+1<n && line[p+1]=='|') { push(TT::PIPEPIPE,"||"); p+=2; continue; }

        // single-char punctuation
        switch (c) {
            case '(': push(TT::LPAREN,"("); p++; continue;
            case ')': push(TT::RPAREN,")"); p++; continue;
        case '[': push(TT::LBRACKET,"["); p++; continue;
        case ']': push(TT::RBRACKET,"]"); p++; continue;
        case '{': push(TT::LBRACE,"{"); p++; continue;
        case '}': push(TT::RBRACE,"}"); p++; continue;
            case ',': push(TT::COMMA,","); p++; continue;
            case ':': push(TT::COLON,":"); p++; continue;
            case '.': push(TT::DOT,"."); p++; continue;
            case '=': push(TT::EQ,"="); p++; continue;
            case '+': push(TT::PLUS,"+"); p++; continue;
            case '-': push(TT::MINUS,"-"); p++; continue;
            case '*': push(TT::STAR,"*"); p++; continue;
            case '/': push(TT::SLASH,"/"); p++; continue;
            case '%': push(TT::PERCENT,"%"); p++; continue;
            case '<': push(TT::LT,"<"); p++; continue;
            case '>': push(TT::GT,">"); p++; continue;
            case '!': push(TT::BANG,"!"); p++; continue;
            default: throw CompileError("无法识别的字符: '" + std::string(1,c) + "'");
        }
    }
}

std::vector<Token> Lexer::tokenize(const std::string& src) {
    std::vector<Token> toks;
    std::vector<int> indentStack = {0};
    auto push = [&](TT t, std::string txt, int ln){ toks.push_back(Token{t,std::move(txt),ln}); };

    size_t pos = 0;
    int lineNo = 1;
    bool firstRealLine = true;

    while (pos <= src.size()) {
        // find end of current line
        size_t nl = src.find('\n', pos);
        std::string line = (nl == std::string::npos) ? src.substr(pos) : src.substr(pos, nl - pos);
        if (nl == std::string::npos) pos = src.size() + 1;
        else pos = nl + 1;

        // compute leading whitespace (tabs count as 1 column for simplicity)
        size_t w = 0;
        while (w < line.size() && (line[w]==' '||line[w]=='\t')) ++w;
        std::string content = line.substr(w);

        // skip blank / comment-only lines
        bool isBlank = true;
        for (char ch : content) if (!std::isspace((unsigned char)ch) && !(ch=='/'&&false)) { isBlank=false; break; }
        // also treat pure-comment lines as blank
        std::string trimmed = content;
        // crude: if first non-space is // it's a comment
        size_t np = 0; while (np<trimmed.size() && std::isspace((unsigned char)trimmed[np])) ++np;
        if (trimmed.substr(np).rfind("//",0)==0) isBlank = true;

        if (isBlank) { ++lineNo; continue; }

        int width = (int)w;

        if (!firstRealLine) {
            if (width == indentStack.back()) push(TT::NEWLINE, "", lineNo);
            else if (width > indentStack.back()) { indentStack.push_back(width); push(TT::INDENT, "", lineNo); }
            else {
                while (!indentStack.empty() && indentStack.back() > width) {
                    indentStack.pop_back();
                    push(TT::DEDENT, "", lineNo);
                }
                if (indentStack.empty() || indentStack.back() < width)
                    throw CompileError("缩进不一致 (行 " + std::to_string(lineNo) + ")");
                push(TT::NEWLINE, "", lineNo);
            }
        } else {
            if (width > indentStack.back()) { indentStack.push_back(width); push(TT::INDENT, "", lineNo); }
            firstRealLine = false;
        }

        scanLine(content, lineNo, toks);
        push(TT::NEWLINE, "", lineNo);
        ++lineNo;
    }

    while (indentStack.size() > 1) { indentStack.pop_back(); push(TT::DEDENT, "", lineNo); }
    push(TT::END, "", lineNo);
    return toks;
}

} // namespace leash
