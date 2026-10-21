// The Return of the King: [Language] Subtitles in ROTKFix.ini, the game's text in another language as subtitles.
//
// The game's interface text is a table of named lines (GlobScen.scx, resource type 'Tbus' subtype 5), looked up by
// name with GetText 0xF6DC0 (cdecl: key, language, flags; 31 callers, the menus' text widgets every frame); its
// spoken lines' subtitles are tables in each level's .scx, keyed by the line's sound number and shown by
// ShowSubtitle 0xF7A80 (cdecl: number) once per line. Our own translations are in lang/<code>.txt beside the exe:
// a line's name, a tab, the text; a spoken line as <level>:<0xNUMBER> (cra01:0x020C0100), the numbers being the
// level's own. UTF-8; '|' or "\n" for a new line; '#' starts a comment line.
//
// The interface text: the lines the game asks for within a quarter of a second (a screen draws its widgets every
// frame) are the screen's; their translation shows at the bottom, joined with " · ", as long as the screen does.
// A spoken line: our translation shows in place of the game's English subtitle, for its time.
// Off (the default), or a language without a file, nothing changes.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

namespace rotk {
uint32_t EowynText(const std::string& key);  // rotk_eowyn.cpp
extern bool g_eowyn;
uint32_t g_subtitles = 0;  // [Language] Subtitles: an index into kSubtitleCodes
const char* const kSubtitleCodes[] = {"off", "de", "fr", "es", "it", "pt", "pl", "nl", "sv", "ru"};
constexpr size_t kSubtitleLanguages = sizeof(kSubtitleCodes) / sizeof(*kSubtitleCodes);
}  // namespace rotk

namespace {
std::mutex g_lock;
uint32_t g_loaded = 0xFFFFFFFF;
std::unordered_map<std::string, std::string> g_lines;  // "inGameSOD" or "cra01:0x020c0100" -> our text
std::string g_level;                                   // the level open ("cra01"), for its spoken lines
std::vector<std::string> g_window;                     // the lines asked for in the current quarter second
std::chrono::steady_clock::time_point g_window_start;
std::chrono::steady_clock::time_point g_spoken_until;  // a spoken line's subtitle showing: the screen's lines wait

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// as it shows: '|' and "\n" a new line; the game's font codes: § the trade mark sign, £ and ¥ the upgrade screen's
// dividers (left out); runs of '+' in spoken lines (marks for the voice, not text) left out
std::string Clean(std::string s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '|') o += '\n';
        else if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') o += '\n', i++;
        else if (s.compare(i, 2, "\xC2\xA7") == 0) o += "\xE2\x84\xA2", i++;
        else if (s.compare(i, 2, "\xC2\xA3") == 0 || s.compare(i, 2, "\xC2\xA5") == 0) i++;
        else if (s[i] == '+' && i + 1 < s.size() && s[i + 1] == '+') {
            while (i + 1 < s.size() && s[i + 1] == '+') i++;
        } else o += s[i];
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

void Load(uint32_t lang) {
    g_loaded = lang;
    g_lines.clear();
    if (!lang || lang >= rotk::kSubtitleLanguages) return;
    std::filesystem::path p = app::ExeDir() / "lang" / (std::string(rotk::kSubtitleCodes[lang]) + ".txt");
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        LOGW("subtitles: no %s", p.string().c_str());
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        if (line.empty() || line[0] == '#') continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos || !tab) continue;
        std::string text = Clean(line.substr(tab + 1));
        if (!text.empty()) g_lines[Lower(line.substr(0, tab))] = text;
    }
    LOGI("subtitles: %zu lines in %s", g_lines.size(), p.string().c_str());
}

bool Ready() {
    uint32_t lang = rotk::g_subtitles;
    if (!lang) return false;
    if (lang != g_loaded) Load(lang);
    return !g_lines.empty();
}

// the screen's lines: shown once the quarter second is over, for a second (asked for again, kept showing)
void Flush() {
    std::string text;
    std::vector<const std::string*> said;
    for (const std::string& k : g_window) {
        auto it = g_lines.find(k);
        if (it == g_lines.end()) continue;
        bool again = false;
        for (const std::string* s : said) again = again || *s == it->second;
        if (again) continue;
        said.push_back(&it->second);
        if (!text.empty()) text += "  \xC2\xB7  ";  // " · "
        text += it->second;
    }
    if (!text.empty() && std::chrono::steady_clock::now() > g_spoken_until) app::Subtitle(text, 1.0);
}
}  // namespace

namespace rotk {
// each file the game opens (rotk_controls.cpp, lower case): a level's .scx tells whose spoken lines play
void LangFile(const std::string& name) {
    size_t at = name.find("\\data\\game\\");
    if (at == std::string::npos || name.size() < 4 || name.compare(name.size() - 4, 4, ".scx") != 0) return;
    std::string rest = name.substr(at + 11);
    size_t slash = rest.find('\\');
    if (slash == std::string::npos) return;  // globscen.scx
    std::string level = rest.substr(slash + 1, rest.size() - slash - 1 - 4);
    if (level.rfind("fro", 0) == 0) return;  // the front end's own scene
    std::lock_guard lock(g_lock);
    g_level = level;
}
}  // namespace rotk

X86_EXTERN_FUNC(__imp__sub_000F6DC0);
X86_EXTERN_FUNC(__imp__sub_000F7A80);

// const char* GetText(const char* key, const char* language, int flags), cdecl
X86_FUNC(sub_000F6DC0) {
    uint32_t key = ::kernel::R32(ctx.esp + 4);
    if (rotk::g_eowyn && key) {  // Faramir's name as Éowyn's (rotk_eowyn.cpp)
        char k[16] = {};
        for (uint32_t i = 0; i < sizeof k - 1; i++) {
            k[i] = char(std::tolower(::kernel::G<uint8_t>(key + i)));
            if (!k[i]) break;
        }
        if (uint32_t ours = rotk::EowynText(k)) {
            ::kernel::Return(ctx, ours, 0);  // cdecl: the caller pops the arguments
            return;
        }
    }
    if (rotk::g_subtitles && key) {
        std::string k;
        for (uint32_t i = 0; i < 96; i++) {
            char c = char(::kernel::G<uint8_t>(key + i));
            if (!c) break;
            k += c;
        }
        k = Lower(k);
        static const bool trace = std::getenv("ROTK_TEXT_TRACE") != nullptr;
        if (trace) LOGI("rotk: text %s", k.c_str());
        // the help bar every screen has (Change selection, Go back...): left out, it would be in every subtitle
        // and the triggers' letters (L, R) the screens show beside their controls
        if (!k.empty() && k[0] != '@' && k.rfind("help", 0) != 0 && k != "lefttriggerbtnname" &&
            k != "righttriggerbtnname") {
            std::lock_guard lock(g_lock);
            if (Ready()) {
                auto now = std::chrono::steady_clock::now();
                if (now - g_window_start > std::chrono::milliseconds(250)) {
                    Flush();
                    g_window.clear();
                    g_window_start = now;
                }
                bool have = false;
                for (const std::string& w : g_window) have = have || w == k;
                if (!have && g_window.size() < 8) g_window.push_back(k);
            }
        }
    }
    __imp__sub_000F6DC0(ctx, base);
}

// void ShowSubtitle(uint32_t line), cdecl: a spoken line's subtitle; ours in its place when there is one
X86_FUNC(sub_000F7A80) {
    uint32_t id = ::kernel::R32(ctx.esp + 4);
    static const bool trace = std::getenv("ROTK_TEXT_TRACE") != nullptr;
    if (trace) LOGI("rotk: spoken line %s:0x%08x", g_level.c_str(), id);
    if (rotk::g_subtitles) {
        std::lock_guard lock(g_lock);
        if (Ready()) {
            char key[48];
            std::snprintf(key, sizeof key, "%s:0x%08x", g_level.c_str(), id);
            auto it = g_lines.find(key);
            if (it != g_lines.end()) {
                // about as long as the line is spoken: 2 s, and a little more for each letter, at most 8 s
                size_t letters = 0;  // UTF-8: a letter's first byte
                for (char c : it->second) letters += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
                double secs = std::min(8.0, 2.0 + 0.055 * double(letters));
                app::Subtitle(it->second, secs);
                g_spoken_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(int(secs * 1000));
                ::kernel::Return(ctx, 0, 0);  // cdecl: the caller pops the argument
                return;
            }
        }
    }
    __imp__sub_000F7A80(ctx, base);
}
