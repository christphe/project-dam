#include "menu_json.h"

namespace menu {
namespace {

struct Parser {
    const char* p;
    const char* end;
    const char* begin;
    std::string err;

    bool fail(const char* msg) {
        if (err.empty()) {
            int line = 1;
            for (const char* q = begin; q < p && q < end; q++)
                if (*q == '\n') line++;
            err = "line " + std::to_string(line) + ": " + msg;
        }
        return false;
    }
    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }
    bool lit(const char* w) {
        const char* q = p;
        for (; *w; w++, q++)
            if (q >= end || *q != *w) return false;
        p = q;
        return true;
    }
    static void utf8(std::string& s, unsigned c) {
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
        else { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
    }
    bool string(std::string* out) {
        if (p >= end || *p != '"') return fail("expected a string");
        p++;
        while (p < end && *p != '"') {
            if (*p == '\n') return fail("newline inside a string");
            if (*p == '\\') {
                p++;
                if (p >= end) return fail("unfinished escape");
                switch (*p) {
                case '"': *out += '"'; break;
                case '\\': *out += '\\'; break;
                case '/': *out += '/'; break;
                case 'b': *out += '\b'; break;
                case 'f': *out += '\f'; break;
                case 'n': *out += '\n'; break;
                case 'r': *out += '\r'; break;
                case 't': *out += '\t'; break;
                case 'u': {
                    unsigned c = 0;
                    for (int i = 0; i < 4; i++) {
                        p++;
                        if (p >= end) return fail("bad \\u escape");
                        const char h = *p;
                        c = c * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                    }
                    utf8(*out, c);
                    break;
                }
                default: return fail("unknown escape");
                }
                p++;
            } else {
                *out += *p++;
            }
        }
        if (p >= end) return fail("unterminated string");
        p++;
        return true;
    }
    bool number(double* out) {
        const char* q = p;
        bool neg = false;
        if (q < end && *q == '-') { neg = true; q++; }
        if (q >= end || *q < '0' || *q > '9') return fail("expected a number");
        double v = 0;
        while (q < end && *q >= '0' && *q <= '9') v = v * 10 + (*q++ - '0');
        if (q < end && *q == '.') {
            q++;
            double f = 0.1;
            while (q < end && *q >= '0' && *q <= '9') { v += (*q++ - '0') * f; f *= 0.1; }
        }
        if (q < end && (*q == 'e' || *q == 'E')) {
            q++;
            bool eneg = false;
            if (q < end && (*q == '+' || *q == '-')) eneg = *q++ == '-';
            int e = 0;
            while (q < end && *q >= '0' && *q <= '9') e = e * 10 + (*q++ - '0');
            while (e-- > 0) v = eneg ? v / 10 : v * 10;
        }
        p = q;
        *out = neg ? -v : v;
        return true;
    }
    bool value(Json* j, int depth) {
        if (depth > 32) return fail("nested too deep");
        ws();
        if (p >= end) return fail("unexpected end");
        if (*p == '{') {
            p++;
            j->t = Json::Obj;
            ws();
            if (p < end && *p == '}') { p++; return true; }
            for (;;) {
                ws();
                std::string key;
                if (!string(&key)) return false;
                ws();
                if (p >= end || *p != ':') return fail("expected ':'");
                p++;
                Json v;
                if (!value(&v, depth + 1)) return false;
                j->o.emplace_back(std::move(key), std::move(v));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (*p == '[') {
            p++;
            j->t = Json::Arr;
            ws();
            if (p < end && *p == ']') { p++; return true; }
            for (;;) {
                Json v;
                if (!value(&v, depth + 1)) return false;
                j->a.push_back(std::move(v));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (*p == '"') { j->t = Json::Str; return string(&j->s); }
        if (lit("true")) { j->t = Json::Bool; j->b = true; return true; }
        if (lit("false")) { j->t = Json::Bool; j->b = false; return true; }
        if (lit("null")) { j->t = Json::Null; return true; }
        j->t = Json::Num;
        return number(&j->n);
    }
};

}  // namespace

bool parseJson(const char* text, size_t len, Json* out, std::string* err) {
    Parser ps{ text, text + len, text, std::string() };
    if (!ps.value(out, 0)) {
        if (err) *err = ps.err;
        return false;
    }
    ps.ws();
    if (ps.p != ps.end) {
        ps.fail("unexpected text after the value");
        if (err) *err = ps.err;
        return false;
    }
    return true;
}

}  // namespace menu
