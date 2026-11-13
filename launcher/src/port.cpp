// Athelas (the launcher) - starting a game's PC port and editing its settings (see port.h)
#include "port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>

// ---------------------------------------------------------------------------
// Running the program

RunResult run_capture(const std::string &exe, const std::string &args, std::string &out, DWORD timeoutMs)
{
    RunResult r;
    r.status = RUN_NOT_STARTED;
    out.clear();
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return r;
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
    if (!ok) { CloseHandle(rd); return r; }
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
        if (GetTickCount() - start > timeoutMs) {
            TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 1000);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(rd);
            r.status = RUN_TIMED_OUT;
            return r;
        }
    }
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(rd);
    r.status = RUN_EXITED;
    r.code = code;
    return r;
}

// ---------------------------------------------------------------------------
// A small JSON reader: enough for the --describe-settings list

#include "json.h"
namespace {
using json::JVal;
using json::JParse;
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

std::string exit_code_text(const RunResult &rc)
{
    if (rc.status == RUN_TIMED_OUT) return "timed out: no answer in time";
    if (rc.status == RUN_NOT_STARTED) return "it couldn't be started";
    unsigned u = (unsigned)rc.code;
    if (u < 0x10000) return "exit " + std::to_string(u);
    char b[32]; snprintf(b, sizeof b, "exit 0x%08X", u);
    const char *why = nullptr;
    switch (u) {
    case 0xC0000135: why = "a DLL it needs is missing"; break;
    case 0xC0000139: why = "a function it needs is missing from a DLL (an older Windows or Wine?)"; break;
    case 0xC0000142: why = "a DLL it needs failed to start"; break;
    case 0xC0000005: why = "it crashed (access violation)"; break;
    case 0xC000001D: why = "it crashed (an instruction this processor doesn't have)"; break;
    case 0xC00000FD: why = "it crashed (stack overflow)"; break;
    case 0xC0000409: why = "it crashed (stack buffer overrun)"; break;
    case 0xC000007B: why = "a program or DLL for the wrong architecture (32/64-bit, x64/ARM)"; break;
    case 0xC0000017: why = "out of memory"; break;
    case 0xC000013A: why = "it was closed (Ctrl+C)"; break;
    }
    return why ? std::string(b) + ": " + why : std::string(b);
}

namespace {
// Runs one query, up to 'tries' times (the first start of a large program can be slow, e.g. under Wine while it
// sets up; --version gets a second try, and once it has answered the program has started at least once); everything is written to the log when there is one.
RunResult query_run(const std::string &exe, const char *arg, std::string &out, FILE *log, int tries, DWORD ms)
{
    RunResult rc;
    for (int attempt = 1; attempt <= tries; attempt++) {
        DWORD t0 = GetTickCount();
        rc = run_capture(exe, arg, out, ms);
        if (log) {
            fprintf(log, "%s (try %d): %s after %.1f s, %u bytes printed\n", arg, attempt, rc.ok() ? "exit 0" : exit_code_text(rc).c_str(),
                    (GetTickCount() - t0) / 1000.0, (unsigned)out.size());
            if (!rc.ok() || out.size() < 4096) fprintf(log, "----\n%s\n----\n", out.c_str());
            fflush(log);
        }
        if (rc.ok() || rc.status == RUN_NOT_STARTED) break;
    }
    return rc;
}
}

// What a port answers to --version and --describe-settings is fixed in its program, so it is kept beside the log
// (port-query.cache) and used again for as long as the program is the same file (path, size and time): opening a
// game's page doesn't start the port twice each time. A cache that can't be read is ignored and written anew.
namespace {
std::string exe_stamp(const std::string &exe)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(exe.c_str(), GetFileExInfoStandard, &fa)) return "";
    char b[96];
    snprintf(b, sizeof b, "v1|%lu:%lu|%lu:%lu|", (unsigned long)fa.nFileSizeHigh, (unsigned long)fa.nFileSizeLow,
             (unsigned long)fa.ftLastWriteTime.dwHighDateTime, (unsigned long)fa.ftLastWriteTime.dwLowDateTime);
    return b + exe;
}
bool cache_read(const std::string &path, const std::string &exe, std::string &version, std::string &json)
{
    std::string stamp = exe_stamp(exe), text;
    FILE *f = stamp.empty() ? nullptr : fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (8u << 20)) text.append(buf, n);
    fclose(f);
    size_t l1 = text.find('\n'), l2 = l1 == std::string::npos ? l1 : text.find('\n', l1 + 1);
    if (l2 == std::string::npos || text.compare(0, l1, stamp) != 0) return false;
    version = text.substr(l1 + 1, l2 - l1 - 1);
    json = text.substr(l2 + 1);
    return !version.empty() && !json.empty();
}
void cache_write(const std::string &path, const std::string &exe, const std::string &version, const std::string &json)
{
    std::string stamp = exe_stamp(exe), tmp = path + ".tmp";
    if (stamp.empty() || version.find('\n') != std::string::npos) return;
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    std::string text = stamp + "\n" + version + "\n" + json;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;
    if (!ok || !MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileA(tmp.c_str());
}
bool query_impl(Port &p, FILE *log, const std::string &cachePath, bool useCache);
}

bool port_query(Port &p, const std::string &logPath)
{
    FILE *log = logPath.empty() ? nullptr : fopen(logPath.c_str(), "w");
    struct Closer { FILE *f; ~Closer() { if (f) fclose(f); } } closer{ log };
    if (log) fprintf(log, "Asking %s\n", p.exe.c_str());
    size_t slash = logPath.find_last_of("\\/");
    std::string cachePath = slash == std::string::npos ? std::string() : logPath.substr(0, slash + 1) + "port-query.cache";
    if (!cachePath.empty() && query_impl(p, log, cachePath, true)) return true;
    return query_impl(p, log, cachePath, false);   // no cache, or one that couldn't be used: the port is asked
}

namespace {
bool query_impl(Port &p, FILE *log, const std::string &cachePath, bool useCache)
{
    p.version.clear(); p.settings.clear(); p.presets.clear(); p.err.clear();
    std::string out;
    if (useCache) {
        if (!cache_read(cachePath, p.exe, p.version, out)) return false;
        if (log) fprintf(log, "(answers kept from an earlier start of this same program: port-query.cache)\n--version: %s\n--describe-settings: %u bytes\n", p.version.c_str(), (unsigned)out.size());
    } else {
        // one try each, so a port that never answers holds the launcher's window up at most 75 s (review)
        RunResult rc = query_run(p.exe, "--version", out, log, 1, 45000);
        if (!rc.ok()) { p.err = "The port didn't answer (--version, " + exit_code_text(rc) + ")."; return false; }
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
        p.version = out;
        rc = query_run(p.exe, "--describe-settings", out, log, 1, 30000);
        if (!rc.ok()) { p.err = "The port didn't list its settings (" + exit_code_text(rc) + ")."; return false; }
    }
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
        if (const JVal *v = j.get("choice_labels")) for (auto &c : v->a) s.choiceLabels.push_back(c.t == JVal::STR ? c.s : num_text(c.n));
        if (s.choiceLabels.size() != s.choices.size()) s.choiceLabels.clear();   // only used when there's one per choice
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
    if (!useCache && !cachePath.empty()) cache_write(cachePath, p.exe, p.version, out);
    return true;
}
}  // namespace

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
    // [[array]]: a table of its own, never one a setting is in (a name no [section] can have)
    if (t.size() >= 4 && t[0] == '[' && t[1] == '[') { size_t e = t.find("]]"); if (e != std::string::npos) return "\x02" + trim(t.substr(2, e - 2)); }
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
    else if (!v.empty()) {
        // the closing quote: in "..." a backslash escapes the next character ("a\\" ends after the second backslash)
        size_t q = std::string::npos;
        for (size_t i = 1; i < v.size(); i++) {
            if (v[0] == '"' && v[i] == '\\') { i++; continue; }
            if (v[i] == v[0]) { q = i; break; }
        }
        if (q != std::string::npos) v = v.substr(0, q + 1);
    }
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

void PortSettingsFile::remove(const std::string &sec, const std::string &key)
{
    std::string cur;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string s = line_section(lines[i]);
        if (s != "\x01") { cur = s; continue; }
        std::string k, v;
        if (cur == sec && line_kv(lines[i], k, v) && k == key) { lines.erase(lines.begin() + i); return; }
    }
}

bool settings_file_load(const std::string &path, PortSettingsFile &f)
{
    f.lines.clear();
    f.unreadable = false;
    FILE *fp = fopen(path.c_str(), "rb");
    // there but locked for a moment (OneDrive, antivirus, the port still closing): a few tries, then marked unreadable
    for (int t = 0; !fp && t < 5 && GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; t++) {
        Sleep(100);
        fp = fopen(path.c_str(), "rb");
    }
    if (!fp) { f.unreadable = GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; return false; }
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
    if (f.unreadable) return false;   // never over a file that couldn't be read
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
