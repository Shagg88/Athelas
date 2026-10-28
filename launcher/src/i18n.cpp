// Athelas - the launcher in other languages. See i18n.h.
#include "i18n.h"
#include "config.h"   // exe_dir

#include <windows.h>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>

extern void (*GImTextTranslate)(const char **text, const char **text_end);
extern const char *(*GImFormatTranslate)(const char *fmt);

namespace {
std::unordered_map<std::string, std::string> g_map;            // English -> translation
std::unordered_map<std::string, const std::string *> g_cache;  // text as drawn -> what to draw (nullptr: as it is)
std::deque<std::string> g_store;                               // stable storage for what the cache points to
std::unordered_map<std::string, std::vector<const std::string *>> g_pieces;   // first 3 bytes -> keys, longest first
std::string g_code = "en";

std::string unescape(const std::string &s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { o += '\n'; i++; }
        else o += s[i];
    }
    return o;
}
std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
// the printf fields of a text, in order ("%d", "%s", "%.1f" ...; "%%" not counted)
std::vector<std::string> fields(const std::string &s)
{
    std::vector<std::string> v;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '%') continue;
        if (i + 1 < s.size() && s[i + 1] == '%') { i++; continue; }
        size_t j = i + 1;
        while (j < s.size() && strchr("-+#0123456789.*hlzjt", s[j])) j++;   // (not a space: "100% gives" is no field)
        if (j < s.size() && strchr("diuxXfFeEgGcsp", s[j])) { v.push_back(s.substr(i, j - i + 1)); i = j; }
    }
    return v;
}
bool word_char(unsigned char c) { return isalnum(c) || c >= 0x80; }

void build_pieces()
{
    g_pieces.clear();
    for (auto &kv : g_map) {
        if (kv.first.size() < 4 || kv.first.find('%') != std::string::npos) continue;
        g_pieces[kv.first.substr(0, 3)].push_back(&kv.first);
    }
    for (auto &kv : g_pieces)
        std::sort(kv.second.begin(), kv.second.end(), [](const std::string *a, const std::string *b) { return a->size() > b->size(); });
}
// A message put together at run time ("Shortcut made: " + a name): the known pieces in it, longest first, where
// they start and end at a word's edge. Not in text with a path in it (a folder named like a word stays as it is).
bool replace_pieces(const std::string &text, std::string &out)
{
    if (text.find('\\') != std::string::npos || text.find(":/") != std::string::npos) return false;
    size_t i = 0;
    bool any = false;
    out.clear();
    while (i < text.size()) {
        bool hit = false;
        if (i + 3 <= text.size() && (i == 0 || !word_char((unsigned char)text[i - 1]) || !word_char((unsigned char)text[i]))) {
            auto it = g_pieces.find(text.substr(i, 3));
            if (it != g_pieces.end())
                for (const std::string *k : it->second) {
                    size_t n = k->size();
                    if (text.compare(i, n, *k) != 0) continue;
                    if (i + n < text.size() && word_char((unsigned char)text[i + n]) && word_char((unsigned char)(*k)[n - 1])) continue;
                    out += g_map[*k];
                    i += n;
                    hit = any = true;
                    break;
                }
        }
        if (!hit) out += text[i++];
    }
    return any;
}

// what to draw for this text, or nullptr when it stays as it is
const std::string *lookup(const std::string &text)
{
    auto c = g_cache.find(text);
    if (c != g_cache.end()) return c->second;
    const std::string *r = nullptr;
    auto it = g_map.find(text);
    if (it != g_map.end()) r = &it->second;
    else {
        size_t a = text.find_first_not_of(" "), b = text.find_last_not_of(" ");
        std::string done;
        if (a != std::string::npos && (a > 0 || b + 1 < text.size())) {
            auto t2 = g_map.find(text.substr(a, b - a + 1));
            if (t2 != g_map.end()) { g_store.push_back(text.substr(0, a) + t2->second + text.substr(b + 1)); r = &g_store.back(); }
        }
        if (!r && text.size() >= 4 && replace_pieces(text, done)) { g_store.push_back(done); r = &g_store.back(); }
    }
    // (messages with changing numbers in them fill these up: emptied at the end of a frame, never in the middle of
    // one, as callers still hold pointers into them - i18n_frame_end)
    g_cache[text] = r;
    return r;
}

std::unordered_set<const char *> g_given;   // translations handed out: measured again inside ImGui, not looked up again
void text_hook(const char **text, const char **end)
{
    if (g_map.empty() || !*text || g_given.count(*text)) return;
    const char *e = *end ? *end : *text + strlen(*text);
    if (e - *text < 2 || e - *text > 4000) return;
    const std::string *t = lookup(std::string(*text, e));
    if (t) { *text = t->c_str(); *end = t->c_str() + t->size(); g_given.insert(t->c_str()); }
}
const char *format_hook(const char *fmt)
{
    if (g_map.empty() || !fmt || !strchr(fmt, '%')) return fmt;
    auto it = g_map.find(fmt);
    return it != g_map.end() ? it->second.c_str() : fmt;
}
}  // namespace

const std::vector<Language> &languages()
{
    static const std::vector<Language> v = {
        { "en", "English" }, { "es", "Espa\xC3\xB1ol" }, { "fr", "Fran\xC3\xA7" "ais" }, { "de", "Deutsch" },
        { "it", "Italiano" }, { "pt-BR", "Portugu\xC3\xAAs (Brasil)" }, { "pl", "Polski" },
        { "ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9" },
        { "ja", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" }, { "zh-Hans", "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87" },
        { "ko", "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4" },
    };
    return v;
}

std::string i18n_auto_code()
{
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
    case LANG_SPANISH: return "es";
    case LANG_FRENCH: return "fr";
    case LANG_GERMAN: return "de";
    case LANG_ITALIAN: return "it";
    case LANG_PORTUGUESE: return "pt-BR";
    case LANG_POLISH: return "pl";
    case LANG_RUSSIAN: return "ru";
    case LANG_JAPANESE: return "ja";
    case LANG_CHINESE: return "zh-Hans";
    case LANG_KOREAN: return "ko";
    default: return "en";
    }
}

std::string i18n_system_font(const std::string &code)
{
    static const char *ja[] = { "YuGothM.ttc", "meiryo.ttc", "msgothic.ttc", nullptr };
    static const char *zh[] = { "msyh.ttc", "simsun.ttc", "simhei.ttf", nullptr };
    static const char *ko[] = { "malgun.ttf", "gulim.ttc", nullptr };
    const char **list = code == "ja" ? ja : code == "zh-Hans" ? zh : code == "ko" ? ko : nullptr;
    if (!list) return "";
    char win[MAX_PATH] = "C:\\Windows";
    GetWindowsDirectoryA(win, MAX_PATH);
    for (int i = 0; list[i]; i++) {
        std::string p = std::string(win) + "\\Fonts\\" + list[i];
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return "";
}

void i18n_load(const std::string &choice)
{
    g_map.clear(); g_cache.clear(); g_store.clear(); g_pieces.clear(); g_given.clear();
    GImTextTranslate = nullptr; GImFormatTranslate = nullptr;
    g_code = choice == "auto" || choice.empty() ? i18n_auto_code() : choice;
    if (g_code == "en") return;
    std::string path = exe_dir() + "\\lang\\" + g_code + ".txt", text;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (16u << 20)) text.append(buf, n);
    fclose(f);
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB) line = line.substr(3);   // BOM
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.rfind(" = ");   // the last one: English text may hold " = ", translations don't
        if (eq == std::string::npos) continue;
        std::string en = unescape(trim(t.substr(0, eq))), tx = unescape(trim(t.substr(eq + 3)));
        if (en.empty() || tx.empty()) continue;
        if (fields(en) != fields(tx)) continue;   // a format string must keep its fields
        g_map[en] = tx;
    }
    if (g_map.empty()) return;
    build_pieces();
    GImTextTranslate = text_hook;
    GImFormatTranslate = format_hook;
}

std::string i18n_current() { return g_code; }

void i18n_frame_end()
{
    if (g_cache.size() > 20000 || g_store.size() > 20000) { g_cache.clear(); g_store.clear(); g_given.clear(); }
}

std::string tr(const std::string &english)
{
    if (g_map.empty()) return english;
    const std::string *t = lookup(english);
    return t ? *t : english;
}
const char *tr(const char *english)
{
    if (g_map.empty() || !english) return english;
    const std::string *t = lookup(english);
    if (t) g_given.insert(t->c_str());
    return t ? t->c_str() : english;
}
