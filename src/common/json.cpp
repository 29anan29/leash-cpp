#include "common/json.hpp"
#include <stdexcept>
#include <cmath>
#include <cstdint>

namespace leash {

namespace {

struct Parser {
    const std::string& s;
    size_t pos = 0;
    Store& store;

    Parser(const std::string& text, Store& st) : s(text), store(st) {}

    [[noreturn]] void err(const std::string& m) { throw std::runtime_error("JSON 解析错误: " + m); }

    void skipWs() {
        while (pos < s.size()) {
            char c = s[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') pos++;
            else break;
        }
    }

    char peek() { return pos < s.size() ? s[pos] : '\0'; }

    Value parseValue() {
        skipWs();
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return Value::makeStr(store.intern(parseString()));
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        if (s.compare(pos, 4, "true") == 0) { pos += 4; return Value::makeBool(true); }
        if (s.compare(pos, 5, "false") == 0) { pos += 5; return Value::makeBool(false); }
        if (s.compare(pos, 4, "null") == 0) { pos += 4; return Value::nil(); }
        err("意外的字符: '" + std::string(1, c) + "'");
    }

    Value parseObject() {
        pos++; // consume '{'
        std::map<std::string, Value> m;
        skipWs();
        if (peek() == '}') { pos++; return Value::makeMap(store.internMap(m)); }
        for (;;) {
            skipWs();
            if (peek() != '"') err("对象键必须是字符串");
            std::string key = parseString();
            skipWs();
            if (peek() != ':') err("对象键后需要 ':'");
            pos++; // consume ':'
            Value v = parseValue();
            m[key] = v;
            skipWs();
            char c = peek();
            if (c == ',') { pos++; continue; }
            if (c == '}') { pos++; break; }
            err("对象未正确闭合");
        }
        return Value::makeMap(store.internMap(m));
    }

    Value parseArray() {
        pos++; // consume '['
        std::vector<Value> arr;
        skipWs();
        if (peek() == ']') { pos++; return Value::makeList(store.internList(arr)); }
        for (;;) {
            arr.push_back(parseValue());
            skipWs();
            char c = peek();
            if (c == ',') { pos++; continue; }
            if (c == ']') { pos++; break; }
            err("数组未正确闭合");
        }
        return Value::makeList(store.internList(arr));
    }

    std::string parseString() {
        pos++; // consume opening quote
        std::string out;
        while (pos < s.size()) {
            char c = s[pos++];
            if (c == '"') return out;
            if (c == '\\') {
                if (pos >= s.size()) err("转义不完整");
                char e = s[pos++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (pos + 4 > s.size()) err("unicode 转义不完整");
                        unsigned int cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[pos++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else err("非法 unicode 转义");
                        }
                        // 基本 BMP 处理（不做代理对拼接）
                        if (cp < 0x80) {
                            out += (char)cp;
                        } else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: err("非法转义符");
                }
            } else {
                out += c;
            }
        }
        err("字符串未闭合");
    }

    Value parseNumber() {
        size_t start = pos;
        if (peek() == '-') pos++;
        while (pos < s.size() && ((s[pos] >= '0' && s[pos] <= '9') || s[pos] == '.' ||
               s[pos] == 'e' || s[pos] == 'E' || s[pos] == '+' || s[pos] == '-'))
            pos++;
        std::string num = s.substr(start, pos - start);
        if (num.find('.') != std::string::npos || num.find('e') != std::string::npos ||
            num.find('E') != std::string::npos)
            return Value::makeFloat(std::stod(num));
        return Value::makeInt((int64_t)std::stoll(num));
    }
};

} // namespace

Value parseJson(const std::string& text, Store& store) {
    Parser p(text, store);
    p.skipWs();
    Value v = p.parseValue();
    p.skipWs();
    if (p.pos != text.size())
        throw std::runtime_error("JSON 解析错误: 存在多余字符");
    return v;
}

} // namespace leash
