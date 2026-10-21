// FOTR Xbox: [language] text, the game's own text in another language (our own translations).
//
// The game reads all its text from two tables, dialog\Hud_English.bin (menus, hints, items, the quest journal, signs)
// and dialog\Dialogs_English.bin (what is spoken, shown in the dialogue boxes). Each is: u32 1, u32 n, n names (u16
// length, the name, a u32 index after each but the last; the last name takes the index left over), u32, u32 m, then
// m entries in index order: u16 length and the UTF-16 text, u16 length and the sound's name, u32 colour, u32, u32 k,
// and k colour runs (u32 c, then c stops: u32 character position, u32 colour; highlighted words).
// (The game also knows German, French, Spanish and Italian tables, Dialogs_<language>.bin, from the European discs;
// the US disc has only English, and its other files - voices, films - are English only too.)
//
// With a language chosen, the two tables are served from memory (kernel::SetFileProvider) as the player's own files
// with each text replaced by its translation from lang/<code>.txt beside the exe: lines "hud:<name>" or
// "dlg:<name>", a tab, the text (UTF-8, "\n" a new line, '#' starts a comment line). A translated entry keeps its
// sound and colour but loses its highlighted words (their positions are the English text's). An entry without a
// translation stays English. The voices stay English. The game's fonts have every letter of German, French, Spanish,
// Italian, Portuguese, Dutch and Swedish; for Polish and Russian, open fonts with those letters are served from fonts/<code>/ beside the
// exe in place of the game's (body.ttf for fonts\MyriadMM.ttf, title.ttf for fonts\neuehuo_.ttf; fonts/README.txt).
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace fotr {
uint32_t g_text_language = 0;  // [language] text: an index into kTextLanguages
const char* const kTextLanguages[] = {"off", "de", "fr", "es", "it", "pt", "pl", "nl", "sv", "ru"};
}  // namespace fotr

namespace {
std::mutex g_lock;
bool g_tried[2] = {};
kernel::FileData g_table[2];  // Hud, Dialogs
std::unordered_map<std::string, std::string> g_lines;  // "hud:<name>" -> UTF-8 text
bool g_lines_read = false;

const char* Code() {
    uint32_t i = fotr::g_text_language;
    return i && i < sizeof(fotr::kTextLanguages) / sizeof(*fotr::kTextLanguages) ? fotr::kTextLanguages[i] : nullptr;
}

bool Ends(const std::string& s, const char* tail) {
    size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

void ReadLines() {
    if (g_lines_read) return;
    g_lines_read = true;
    std::filesystem::path p = app::ExeDir() / "lang" / (std::string(Code()) + ".txt");
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        LOGW("fotr: text language %s: no %s", Code(), p.string().c_str());
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        std::string text;
        for (size_t i = tab + 1; i < line.size(); i++) {
            if (line[i] == '\\' && i + 1 < line.size() && line[i + 1] == 'n') {
                text += '\n';
                i++;
            } else {
                text += line[i];
            }
        }
        g_lines[line.substr(0, tab)] = std::move(text);
    }
    LOGR("fotr: text language %s, %zu lines from %s", Code(), g_lines.size(), p.string().c_str());
}

void Utf16(const std::string& s, std::vector<uint16_t>& out) {
    out.clear();
    for (size_t i = 0; i < s.size();) {
        uint32_t c = uint8_t(s[i]);
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + size_t(n) >= s.size() + (n ? 0 : 1)) {
            out.push_back('?');
            i++;
            continue;
        }
        c = n == 0 ? c : n == 1 ? c & 0x1F : n == 2 ? c & 0x0F : c & 0x07;
        for (int k = 1; k <= n; k++) c = c << 6 | (uint8_t(s[i + size_t(k)]) & 0x3F);
        i += size_t(n) + 1;
        if (c >= 0x10000) c = '?';  // the game's text is 16-bit
        out.push_back(uint16_t(c));
    }
}

struct Reader {
    const std::vector<uint8_t>& d;
    size_t p = 0;
    bool bad = false;
    uint32_t U32() {
        if (p + 4 > d.size()) return bad = true, 0;
        uint32_t v;
        std::memcpy(&v, &d[p], 4);
        p += 4;
        return v;
    }
    uint16_t U16() {
        if (p + 2 > d.size()) return bad = true, 0;
        uint16_t v;
        std::memcpy(&v, &d[p], 2);
        p += 2;
        return v;
    }
    void Skip(size_t n) {
        if (p + n > d.size()) bad = true;
        else p += n;
    }
};

// the player's table with the translations in; null when it is not the layout above
kernel::FileData Build(const std::vector<uint8_t>& d, const char* prefix, int& done) {
    Reader r{d};
    r.U32();
    uint32_t n = r.U32();
    if (r.bad || n > 100000) return nullptr;
    std::vector<std::string> names(n);
    std::vector<std::string> by_index(n);
    std::vector<bool> named(n, false);
    std::vector<std::string> order;
    for (uint32_t i = 0; i < n && !r.bad; i++) {
        uint16_t l = r.U16();
        if (r.p + l > d.size()) return nullptr;
        std::string name(reinterpret_cast<const char*>(&d[r.p]), l);
        r.Skip(l);
        order.push_back(name);
        if (i + 1 < n) {
            uint32_t idx = r.U32();
            if (idx >= n) return nullptr;
            by_index[idx] = name;
            named[idx] = true;
        }
    }
    for (uint32_t i = 0; i < n; i++)
        if (!named[i]) by_index[i] = order.empty() ? std::string() : order.back();
    r.U32();
    uint32_t m = r.U32();
    if (r.bad || m != n) return nullptr;
    auto out = std::make_shared<std::vector<uint8_t>>(d.begin(), d.begin() + long(r.p));
    std::vector<uint16_t> text;
    for (uint32_t i = 0; i < m; i++) {
        size_t start = r.p;
        uint16_t l = r.U16();
        r.Skip(size_t(l) * 2);
        size_t sound = r.p;
        uint16_t ls = r.U16();
        r.Skip(ls);
        r.Skip(8);  // colour, u32
        size_t runs_at = r.p;
        uint32_t k = r.U32();
        for (uint32_t j = 0; j < k && !r.bad; j++) r.Skip(size_t(r.U32()) * 8);
        if (r.bad) return nullptr;
        auto t = g_lines.find(std::string(prefix) + by_index[i]);
        if (t == g_lines.end()) {
            out->insert(out->end(), d.begin() + long(start), d.begin() + long(r.p));
            continue;
        }
        Utf16(t->second, text);
        if (text.size() > 0xFFFF) text.resize(0xFFFF);
        uint16_t tl = uint16_t(text.size());
        out->push_back(uint8_t(tl));
        out->push_back(uint8_t(tl >> 8));
        for (uint16_t c : text) {
            out->push_back(uint8_t(c));
            out->push_back(uint8_t(c >> 8));
        }
        out->insert(out->end(), d.begin() + long(sound), d.begin() + long(runs_at));  // sound, colour, u32
        out->insert(out->end(), 4, 0);                                               // no highlighted words
        done++;
    }
    if (r.p != d.size()) return nullptr;
    return out;
}

kernel::FileData Table(int which) {
    std::lock_guard l(g_lock);
    if (g_tried[which]) return g_table[which];
    g_tried[which] = true;
    ReadLines();
    if (g_lines.empty()) return nullptr;
    const char* file = which ? "d:\\dialog\\Dialogs_English.bin" : "d:\\dialog\\Hud_English.bin";
    std::vector<uint8_t> d;
    if (!kernel::ReadGameFile(file, d)) {
        LOGW("fotr: text language: %s not read", file);
        return nullptr;
    }
    int done = 0;
    g_table[which] = Build(d, which ? "dlg:" : "hud:", done);
    if (!g_table[which]) LOGW("fotr: text language: %s not as expected, it stays English", file);
    else LOGR("fotr: text language %s: %d of the lines in %s translated", Code(), done, file);
    return g_table[which];
}

// Polish and Russian: the game's fonts lack their letters
kernel::FileData Font(const char* name) {
    std::string code = Code();
    if (code != "pl" && code != "ru") return nullptr;
    static std::mutex lock;
    static std::unordered_map<std::string, kernel::FileData> fonts;
    std::lock_guard l(lock);
    auto it = fonts.find(name);
    if (it != fonts.end()) return it->second;
    std::filesystem::path p = app::ExeDir() / "fonts" / code / name;
    std::ifstream f(p, std::ios::binary);
    kernel::FileData data;
    if (f) data = std::make_shared<const std::vector<uint8_t>>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    else LOGW("fotr: text language %s: no %s, the game's own font is used", code.c_str(), p.string().c_str());
    fonts[name] = data;
    return data;
}

kernel::FileData Provide(const std::string& path) {
    if (Ends(path, "\\dialog\\hud_english.bin")) return Table(0);
    if (Ends(path, "\\dialog\\dialogs_english.bin")) return Table(1);
    if (Ends(path, "\\fonts\\myriadmm.ttf")) return Font("body.ttf");
    if (Ends(path, "\\fonts\\neuehuo_.ttf")) return Font("title.ttf");
    return nullptr;
}
}  // namespace

namespace fotr {
// GameInfo::init: after the settings load
void InitLanguage() {
    if (!Code()) return;
    kernel::SetFileProvider(Provide);
}
}  // namespace fotr
