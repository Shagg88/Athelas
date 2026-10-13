// ROTK Fix Launcher - starting a game's PC port and editing its settings (see port.h)
#include "port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>

// ---------------------------------------------------------------------------
// Running the program

int run_capture(const std::string &exe, const std::string &args, std::string &out, DWORD timeoutMs)
{
    out.clear();
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr; si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi;
    std::string cmd = "\"" + exe + "\" " + args;
    std::string dir = exe.substr(0, exe.find_last_of("\\/"));
    BOOL ok = CreateProcessA(exe.c_str(), &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, dir.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return -1; }
    DWORD start = GetTickCount();
    char buf[4096];
    for (;;) {
        DWORD avail = 0;
        while (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail) {
            DWORD n = 0;
            if (!ReadFile(rd, buf, sizeof buf, &n, NULL) || !n) break;
            out.append(buf, n);
        }
        if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) {
            DWORD n;
            while (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail && ReadFile(rd, buf, sizeof buf, &n, NULL) && n) out.append(buf, n);
            break;
        }
        if (GetTickCount() - start > timeoutMs) { TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 1000); break; }
    }
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(rd);
    return (int)code;
}

// ---------------------------------------------------------------------------
// A small JSON reader: enough for the --describe-settings list

namespace {
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
std::string num_text(double d)
{
    char b[64];
    if (d == (double)(long long)d) snprintf(b, sizeof b, "%lld", (long long)d);
    else snprintf(b, sizeof b, "%g", d);
    return b;
}
}

std::string toml_quote(const std::string &s)
{
    std::string r = "\"";
    for (char c : s) { if (c == '"' || c == '\\') r += '\\'; r += c; }
    return r + "\"";
}
std::string toml_unquote(const std::string &v)
{
    if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) {
        std::string r;
        for (size_t i = 1; i + 1 < v.size(); i++) {
            if (v[0] == '"' && v[i] == '\\' && i + 2 < v.size()) { i++; r += v[i] == 'n' ? '\n' : v[i] == 't' ? '\t' : v[i]; }
            else r += v[i];
        }
        return r;
    }
    return v;
}

bool port_query(Port &p)
{
    p.version.clear(); p.settings.clear(); p.presets.clear(); p.err.clear();
    std::string out;
    int rc = run_capture(p.exe, "--version", out, 15000);
    if (rc != 0) { p.err = "The port didn't answer (--version, exit " + std::to_string(rc) + ")."; return false; }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
    p.version = out;
    rc = run_capture(p.exe, "--describe-settings", out, 15000);
    if (rc != 0) { p.err = "The port didn't list its settings (exit " + std::to_string(rc) + ")."; return false; }
    size_t at = out.find('{');
    if (at == std::string::npos) { p.err = "The port's settings list couldn't be read."; return false; }
    JParse jp{ out.data() + at, out.data() + out.size() };
    JVal root = jp.value();
    const JVal *list = root.get("settings");
    if (jp.bad || !list || list->t != JVal::ARR) { p.err = "The port's settings list couldn't be read."; return false; }
    for (const JVal &j : list->a) {
        PortSetting s;
        s.section = j.str("section"); s.key = j.str("key"); s.type = j.str("type");
        s.label = j.str("label"); s.help = j.str("help");
        if (s.key.empty()) continue;
        if (s.label.empty()) s.label = s.key;
        if (const JVal *v = j.get("min")) s.min = v->n;
        if (const JVal *v = j.get("max")) s.max = v->n;
        if (const JVal *v = j.get("choices")) for (auto &c : v->a) s.choices.push_back(c.t == JVal::STR ? c.s : num_text(c.n));
        if (const JVal *d = j.get("default")) {
            switch (d->t) {
            case JVal::BOOL: s.def = d->b ? "true" : "false"; break;
            case JVal::NUM: s.def = s.type == "choice" || s.type == "string" ? toml_quote(num_text(d->n)) : num_text(d->n); break;
            case JVal::STR: s.def = s.type == "int" || s.type == "float" || s.type == "bool" ? d->s : toml_quote(d->s); break;
            default: s.def = s.type == "bool" ? "false" : s.type == "int" || s.type == "float" ? "0" : "\"\"";
            }
        }
        p.settings.push_back(s);
    }
    if (p.settings.empty()) { p.err = "The port listed no settings."; return false; }
    // presets: { "High": { "graphics.msaa": "4", ... }, ... }
    if (const JVal *pr = root.get("presets")) if (pr->t == JVal::OBJ) for (auto &np : pr->o) {
        if (np.second.t != JVal::OBJ) continue;
        Port::Preset ps; ps.name = np.first;
        for (auto &kv : np.second.o) {
            const PortSetting *st = nullptr;
            for (auto &s : p.settings) if (s.section + "." + s.key == kv.first) st = &s;
            if (!st) continue;   // a setting this list doesn't have: ignored
            const JVal &v = kv.second;
            std::string fv;
            if (v.t == JVal::BOOL) fv = v.b ? "true" : "false";
            else if (v.t == JVal::NUM) fv = st->type == "choice" || st->type == "string" ? toml_quote(num_text(v.n)) : num_text(v.n);
            else if (v.t == JVal::STR) fv = st->type == "choice" || st->type == "string" ? toml_quote(v.s) : v.s;
            else continue;
            ps.values.push_back({ kv.first, fv });
        }
        if (!ps.values.empty()) p.presets.push_back(ps);
    }
    return true;
}

// ---------------------------------------------------------------------------
// settings.toml: plain [section] and key = value lines, edited in place

static std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static std::string line_section(const std::string &l)
{
    std::string t = trim(l);
    if (t.size() >= 2 && t[0] == '[' && t[1] != '[') { size_t e = t.find(']'); if (e != std::string::npos) return trim(t.substr(1, e - 1)); }
    return std::string("\x01");   // not a section line
}
// key and value of a 'key = value' line ("" if it isn't one); the value without a trailing comment
static bool line_kv(const std::string &l, std::string &k, std::string &v)
{
    std::string t = trim(l);
    if (t.empty() || t[0] == '#' || t[0] == '[') return false;
    size_t eq = t.find('=');
    if (eq == std::string::npos) return false;
    k = trim(t.substr(0, eq));
    v = trim(t.substr(eq + 1));
    if (!v.empty() && v[0] != '"' && v[0] != '\'') { size_t h = v.find('#'); if (h != std::string::npos) v = trim(v.substr(0, h)); }
    else if (!v.empty()) { size_t q = v.find(v[0], 1); while (q != std::string::npos && v[0] == '"' && v[q - 1] == '\\') q = v.find('"', q + 1); if (q != std::string::npos) v = v.substr(0, q + 1); }
    return !k.empty();
}

std::string PortSettingsFile::get(const std::string &sec, const std::string &key, const std::string &def) const
{
    std::string cur;
    for (const std::string &l : lines) {
        std::string s = line_section(l);
        if (s != "\x01") { cur = s; continue; }
        std::string k, v;
        if (cur == sec && line_kv(l, k, v) && k == key) return v;
    }
    return def;
}

void PortSettingsFile::set(const std::string &sec, const std::string &key, const std::string &value)
{
    std::string cur;
    int secEnd = -1;   // where the section's last key line is
    bool inSec = false;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string s = line_section(lines[i]);
        if (s != "\x01") { cur = s; inSec = cur == sec; if (inSec) secEnd = (int)i; continue; }
        std::string k, v;
        if (inSec && line_kv(lines[i], k, v)) {
            secEnd = (int)i;
            if (k == key) { lines[i] = key + " = " + value; return; }
        }
    }
    if (secEnd >= 0) { lines.insert(lines.begin() + secEnd + 1, key + " = " + value); return; }
    if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
    lines.push_back("[" + sec + "]");
    lines.push_back(key + " = " + value);
}

bool settings_file_load(const std::string &path, PortSettingsFile &f)
{
    f.lines.clear();
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    std::string text; char b[4096]; size_t n;
    while ((n = fread(b, 1, sizeof b, fp)) > 0) text.append(b, n);
    fclose(fp);
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text.erase(0, 3);
    size_t s = 0;
    while (s < text.size()) {
        size_t e = text.find('\n', s);
        std::string l = text.substr(s, e == std::string::npos ? std::string::npos : e - s);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        f.lines.push_back(l);
        if (e == std::string::npos) break;
        s = e + 1;
    }
    return true;
}

bool settings_file_save(const std::string &path, const PortSettingsFile &f)
{
    std::string text;
    for (const std::string &l : f.lines) text += l + "\r\n";
    std::string tmp = path + ".tmp";
    FILE *fp = fopen(tmp.c_str(), "wb");
    if (!fp) return false;
    bool ok = fwrite(text.data(), 1, text.size(), fp) == text.size();
    ok = fclose(fp) == 0 && ok;
    if (!ok) { DeleteFileA(tmp.c_str()); return false; }
    return MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// ---------------------------------------------------------------------------
// Playing

HANDLE port_start(const std::string &exe, const std::string &args, std::string &err)
{
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    std::string cmd = "\"" + exe + "\" " + args;
    std::string dir = exe.substr(0, exe.find_last_of("\\/"));
    if (!CreateProcessA(exe.c_str(), &cmd[0], NULL, NULL, FALSE, 0, NULL, dir.c_str(), &si, &pi)) {
        err = "Could not start " + exe.substr(exe.find_last_of("\\/") + 1) + " (error " + std::to_string(GetLastError()) + ").";
        return NULL;
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

std::string port_log_reason(const std::string &logPath)
{
    FILE *fp = fopen(logPath.c_str(), "rb");
    if (!fp) return "";
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    long from = size > 8192 ? size - 8192 : 0;
    fseek(fp, from, SEEK_SET);
    std::string t(size - from, '\0');
    t.resize(fread(&t[0], 1, t.size(), fp));
    fclose(fp);
    size_t at = t.rfind("Exit (");
    if (at == std::string::npos) return "";
    size_t c = t.find("): ", at), e = t.find_first_of("\r\n", at);
    if (c == std::string::npos || (e != std::string::npos && c > e)) return "";
    return trim(t.substr(c + 3, e == std::string::npos ? std::string::npos : e - c - 3));
}
