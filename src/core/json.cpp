#include "core/json.h"

#include <cstdlib>

namespace Json {

namespace {

constexpr int kMaxDepth = 32;

struct Parser {
    explicit Parser(const std::string& text) : s(text) {}
    const std::string& s;
    size_t i = 0;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }

    bool literal(const char* word) {
        size_t n = 0;
        while (word[n]) ++n;
        if (s.compare(i, n, word) != 0) return false;
        i += n;
        return true;
    }

    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | cp >> 6); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += char(0xE0 | cp >> 12); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | cp >> 18); out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }

    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        out.clear();
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return false;
            char e = s[i++];
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
                uint32_t cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && s.compare(i, 2, "\\u") == 0) {
                    i += 2;
                    uint32_t lo;
                    if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool value(Value& v, int depth) {
        if (depth > kMaxDepth) return false;
        ws();
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '"') { v.type = Value::Type::String; return string(v.str); }
        if (c == '{') {
            v.type = Value::Type::Object;
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            while (true) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return false;
                ++i;
                Value child;
                if (!value(child, depth + 1)) return false;
                v.keys.push_back(key);
                v.items.push_back(std::move(child));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return false;
            }
        }
        if (c == '[') {
            v.type = Value::Type::Array;
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            while (true) {
                Value child;
                if (!value(child, depth + 1)) return false;
                v.items.push_back(std::move(child));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return false;
            }
        }
        if (literal("true")) { v.type = Value::Type::Bool; v.boolean = true; return true; }
        if (literal("false")) { v.type = Value::Type::Bool; v.boolean = false; return true; }
        if (literal("null")) { v.type = Value::Type::Null; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = i;
            ++i;
            while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' ||
                                    s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
            v.type = Value::Type::Number;
            v.str = s.substr(start, i - start);
            return true;
        }
        return false;
    }
};

} // namespace

const Value* Value::get(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (size_t k = 0; k < keys.size(); ++k) if (keys[k] == key) return &items[k];
    return nullptr;
}

int64_t Value::asInt(int64_t fallback) const {
    if (type != Type::Number || str.empty()) return fallback;
    return strtoll(str.c_str(), nullptr, 10);
}

bool parse(const std::string& text, Value& out) {
    out = Value();
    Parser p(text);
    if (!p.value(out, 0)) return false;
    p.ws();
    return p.i == text.size();
}

std::string quote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default: out += c; break;
        }
    }
    out += '"';
    return out;
}

std::string stringify(const Value& v) {
    switch (v.type) {
    case Value::Type::Null: return "null";
    case Value::Type::Bool: return v.boolean ? "true" : "false";
    case Value::Type::Number: return v.str;
    case Value::Type::String: return quote(v.str);
    case Value::Type::Array: {
        std::string out = "[";
        for (size_t k = 0; k < v.items.size(); ++k) {
            if (k) out += ',';
            out += stringify(v.items[k]);
        }
        return out + "]";
    }
    case Value::Type::Object: {
        std::string out = "{";
        for (size_t k = 0; k < v.items.size(); ++k) {
            if (k) out += ',';
            out += quote(v.keys[k]) + ":" + stringify(v.items[k]);
        }
        return out + "}";
    }
    }
    return "null";
}

} // namespace Json
