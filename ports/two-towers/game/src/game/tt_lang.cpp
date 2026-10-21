// The Two Towers: [language] subtitles in settings.toml, the game's text in another language as subtitles.
//
// The game's text is 463 numbered lines (startup.BFF's text table, read by the lookups in tt_text.cpp); every release
// numbers them alike. Our own translations are in lang/<code>.txt beside the exe, one line each: the number, a tab,
// the text (UTF-8; "\n" for a new line; '#' starts a comment line). The game's menus, prompts and descriptions stay as
// they are; when the game looks lines up (a screen opening, the selection moving onto an upgrade or a save, a
// message) their translation shows at the bottom of the screen: the lines looked up together, joined with " · ".
// Off (the default), or a language without a file, nothing changes.
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "core/log.h"

namespace tt {
uint32_t g_subtitles = 0;  // [language] subtitles: an index into kSubtitleCodes
bool InMenus();            // tt_extras.cpp
const char* const kSubtitleCodes[] = {"off", "de", "fr", "es", "it", "pt", "pl", "nl", "sv", "ru"};
}  // namespace tt

namespace {
std::mutex g_lock;
uint32_t g_loaded = 0xFFFFFFFF;  // the language whose file is in g_lines
std::unordered_map<uint32_t, std::string> g_lines;
std::vector<uint32_t> g_batch;  // the lines of the current burst of lookups
std::chrono::steady_clock::time_point g_last;

std::string Unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            o += '\n';
            i++;
        } else {
            o += s[i];
        }
    }
    return o;
}

void Load(uint32_t lang) {
    g_loaded = lang;
    g_lines.clear();
    if (!lang || lang >= sizeof(tt::kSubtitleCodes) / sizeof(*tt::kSubtitleCodes)) return;
    std::filesystem::path p = app::ExeDir() / "lang" / (std::string(tt::kSubtitleCodes[lang]) + ".txt");
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        LOGW("subtitles: no %s", p.string().c_str());
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // a BOM
        if (line.empty() || line[0] == '#') continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        char* end = nullptr;
        unsigned long id = std::strtoul(line.c_str(), &end, 10);
        if (end != line.c_str() + tab) continue;
        std::string text = Unescape(line.substr(tab + 1));
        // the game's font draws @ as the copyright sign and * as the trade mark sign (the legal lines)
        for (size_t at; (at = text.find('@')) != std::string::npos;) text.replace(at, 1, "\xC2\xA9");
        for (size_t at; (at = text.find('*')) != std::string::npos;) text.replace(at, 1, "\xE2\x84\xA2");
        if (!text.empty()) g_lines[uint32_t(id)] = text;
    }
    LOGI("subtitles: %zu lines in %s", g_lines.size(), p.string().c_str());
}
}  // namespace

namespace tt {
// each line the game looks up (tt_text.cpp), on any thread
void TextShown(uint32_t id) {
    uint32_t lang = g_subtitles;
    if (!lang) return;
    std::lock_guard lock(g_lock);
    if (lang != g_loaded) Load(lang);
    // the help bar every screen has (Help, Navigate, Select, Back): left out, it would be in every subtitle;
    if (id == 2 || (id >= 10 && id <= 12) || id == 181) return;  // and "English", asked for at start
    auto it = g_lines.find(id);
    if (it == g_lines.end()) return;
    auto now = std::chrono::steady_clock::now();
    // lookups within a tenth of a second of each other are one burst: a screen asking for its lines
    if (now - g_last > std::chrono::milliseconds(100)) g_batch.clear();
    g_last = now;
    for (uint32_t b : g_batch)
        if (b == id) return;
    // a screen opening with many lines at once (the title asks for its menus' too): the first eight
    constexpr size_t kMost = 8;
    if (g_batch.size() >= kMost) return;
    g_batch.push_back(id);
    std::string text;
    std::vector<const std::string*> said;  // two lines alike (two "Back to menu"s): once
    for (uint32_t b : g_batch) {
        const std::string& t = g_lines[b];
        bool again = false;
        for (const std::string* p : said) again = again || *p == t;
        if (again) continue;
        said.push_back(&t);
        if (!text.empty()) text += "  \xC2\xB7  ";  // " · "
        text += t;
    }
    app::Subtitle(text, InMenus() ? 8.0 : 5.0);
}
}  // namespace tt
