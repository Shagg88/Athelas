// A small JSON reader: enough for a port's --describe-settings list and a build kit's kit.json
#pragma once
#include <string>
#include <vector>
#include <string.h>
#include <stdlib.h>

namespace json {
struct JVal {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false; double n = 0; std::string s;
    std::vector<JVal> a; std::vector<std::pair<std::string, JVal>> o;
    const JVal *get(const char *k) const { for (auto &p : o) if (p.first == k) return &p.second; return nullptr; }
    std::string str(const char *k) const { const JVal *v = get(k); return v && v->t == STR ? v->s : ""; }
};
struct JParse {
    const char *p, *e; bool bad = false;
    void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++; }
    bool lit(const char *w) { size_t n = strlen(w); if ((size_t)(e - p) >= n && !memcmp(p, w, n)) { p += n; return true; } return false; }
    std::string string_() {
        std::string r; p++;   // opening quote
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                p++;
                switch (*p) {
                case 'n': r += '\n'; break; case 't': r += '\t'; break; case 'r': break;
                case 'b': case 'f': break;
                case 'u': {
                    unsigned c = p + 4 < e ? (unsigned)strtoul(std::string(p + 1, 4).c_str(), nullptr, 16) : '?';
                    p += 4;
                    if (c < 0x80) r += (char)c;
                    else if (c < 0x800) { r += (char)(0xC0 | (c >> 6)); r += (char)(0x80 | (c & 63)); }
                    else { r += (char)(0xE0 | (c >> 12)); r += (char)(0x80 | ((c >> 6) & 63)); r += (char)(0x80 | (c & 63)); }
                    break;
                }
                default: r += *p;
                }
                p++;
            } else r += *p++;
        }
        if (p < e) p++; else bad = true;
        return r;
    }
    JVal value(int depth = 0) {
        JVal v; ws();
        if (p >= e || depth > 32) { bad = true; return v; }
        if (*p == '{') {
            v.t = JVal::OBJ; p++; ws();
            if (p < e && *p == '}') { p++; return v; }
            while (!bad) {
                ws(); if (p >= e || *p != '"') { bad = true; break; }
                std::string k = string_(); ws();
                if (p >= e || *p != ':') { bad = true; break; }
                p++;
                v.o.push_back({ k, value(depth + 1) }); ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == '}') { p++; break; }
                bad = true;
            }
        } else if (*p == '[') {
            v.t = JVal::ARR; p++; ws();
            if (p < e && *p == ']') { p++; return v; }
            while (!bad) {
                v.a.push_back(value(depth + 1)); ws();
                if (p < e && *p == ',') { p++; continue; }
                if (p < e && *p == ']') { p++; break; }
                bad = true;
            }
        } else if (*p == '"') { v.t = JVal::STR; v.s = string_(); }
        else if (lit("true")) { v.t = JVal::BOOL; v.b = true; }
        else if (lit("false")) { v.t = JVal::BOOL; }
        else if (lit("null")) {}
        else { char *end; v.n = strtod(p, &end); if (end == p) bad = true; else { v.t = JVal::NUM; p = end; } }
        return v;
    }
};
}
