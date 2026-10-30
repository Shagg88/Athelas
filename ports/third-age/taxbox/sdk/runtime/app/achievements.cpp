// Achievements: the game says what each one is (GameInfo::achievements) and when one is earned (ach::Unlock); this
// keeps them in achievements.txt beside the log (the launcher shows the list from it), shows a note in the corner
// when one is earned ([achievements] notify), and lists them on the in-game menu's Achievements page.
//
// achievements.txt, rewritten whenever something changes:
//   [unlocked]  id = 2026-10-16 01:23    (local time it was earned)
//   [counters]  name = 123               (running totals a game keeps, such as enemies defeated)
//   [list]      id = title | description (every achievement, for the launcher, in the game's order)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <cstdlib>
#include <string>
#include <thread>

#include <imgui.h>

#include "app/app.h"
#include "core/log.h"

namespace ach {

namespace {

std::mutex g_lock;
bool g_loaded = false;
std::map<std::string, std::string> g_unlocked;  // id -> when
std::map<std::string, uint64_t> g_counters;
struct Toast {
    std::string title, desc;
    double shown = -1;  // ImGui time it went up, -1 not yet
};
std::deque<Toast> g_toasts;

// beside the log Athelas gives (the game's folder there), or beside the exe when started on its own: the same file
std::filesystem::path File() {
    return (app::Args().log.empty() ? app::ExeDir() : app::LogFolder()) / "achievements.txt";
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

const Def* Find(const std::string& id) {
    for (const Def& d : app::kGame.achievements)
        if (id == d.id) return &d;
    return nullptr;
}

// lock held
void Save() {
    std::error_code ec;
    std::filesystem::create_directories(File().parent_path(), ec);
    std::filesystem::path tmp = File();
    tmp += ".new";
    {
        std::ofstream o(tmp, std::ios::binary);
        if (!o) return;
        o << "; Achievements earned in this game (kept by the game; Athelas shows them).\r\n[unlocked]\r\n";
        for (const Def& d : app::kGame.achievements) {
            auto it = g_unlocked.find(d.id);
            if (it != g_unlocked.end()) o << d.id << " = " << it->second << "\r\n";
        }
        o << "\r\n[counters]\r\n";
        for (auto& [k, v] : g_counters) o << k << " = " << v << "\r\n";
        o << "\r\n[list]\r\n";
        for (const Def& d : app::kGame.achievements) o << d.id << " = " << d.title << " | " << d.desc << "\r\n";
    }
    std::filesystem::rename(tmp, File(), ec);
    if (ec) {
        // a plain copy, not std::filesystem::copy_file: that calls CopyFile2, which Wine 9 (and so CrossOver on a
        // Mac) doesn't have, and the game closed at the second start when the rename had failed once
        std::ifstream i(tmp, std::ios::binary);
        std::ofstream o(File(), std::ios::binary | std::ios::trunc);
        if (i && o) o << i.rdbuf();
        i.close();
        if (o) {
            o.close();
            std::filesystem::remove(tmp, ec);
        }
    }
}

// lock held
void Load() {
    if (g_loaded) return;
    g_loaded = true;
    std::ifstream in(File());
    std::string line, section;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';') continue;
        if (line.front() == '[') {
            section = line;
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
        if (section == "[unlocked]" && Find(k)) g_unlocked[k] = v;
        else if (section == "[counters]") g_counters[k] = std::strtoull(v.c_str(), nullptr, 10);
    }
    LOGR("achievements: %zu of %zu earned (%s)", g_unlocked.size(), app::kGame.achievements.size(),
         File().string().c_str());
    if (!app::kGame.achievements.empty()) Save();  // the list for the launcher, from the first run
    // testing: XBE_TEST_ACHIEVEMENT=<id> earns that one 20 s in (to see the note)
    if (const char* t = std::getenv("XBE_TEST_ACHIEVEMENT")) {
        std::string id = t;
        std::thread([id] {
            std::this_thread::sleep_for(std::chrono::seconds(20));
            Unlock(id.c_str());
        }).detach();
    }
}

}  // namespace

void Start() {
    std::lock_guard lock(g_lock);
    Load();
}

void Unlock(const char* id) {
    std::lock_guard lock(g_lock);
    Load();
    if (g_unlocked.count(id)) return;
    const Def* d = Find(id);
    if (!d) return;
    std::time_t t = std::time(nullptr);
    char when[32];
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", std::localtime(&t));
    g_unlocked[id] = when;
    Save();
    LOGR("achievements: earned \"%s\" (%s)", d->title, id);
    if (app::Get().achievement_notes) g_toasts.push_back({d->title, d->desc});
}

bool Has(const char* id) {
    std::lock_guard lock(g_lock);
    Load();
    return g_unlocked.count(id) != 0;
}

namespace {
void Virtues(uint32_t& dealt, uint32_t& taken) {
    static uint32_t d = 100, t = 100;
    static bool read = [] {
        if (const char* v = std::getenv("ATHELAS_VIRTUES")) {
            unsigned a = 100, b = 100;
            if (std::sscanf(v, "%u,%u", &a, &b) == 2) {
                d = std::clamp(a, 25u, 300u);    // virtues (a bonus) and Fate (tougher enemies) together
                t = std::clamp(b, 25u, 300u);
                LOGI("virtues: damage dealt %u%%, damage taken %u%%", d, t);
            }
        }
        return true;
    }();
    (void)read;
    dealt = d;
    taken = t;
}
}  // namespace
uint32_t VirtueDealt() { uint32_t d, t; Virtues(d, t); return d; }
uint32_t VirtueTaken() { uint32_t d, t; Virtues(d, t); return t; }

uint64_t Count(const char* name) {
    std::lock_guard lock(g_lock);
    Load();
    auto it = g_counters.find(name);
    return it == g_counters.end() ? 0 : it->second;
}

uint64_t Add(const char* name, uint64_t n) {
    std::lock_guard lock(g_lock);
    Load();
    uint64_t v = g_counters[name] += n;
    // saved every so often, not on every count; Unlock and the next Add after a pause save it too
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - last > std::chrono::seconds(10)) {
        last = now;
        Save();
    }
    return v;
}

void Flush() {
    std::lock_guard lock(g_lock);
    if (g_loaded) Save();
}

// the renderer's ImGui frame (UiLock held): the newest note in the bottom right corner for six seconds
void DrawNotes(float scale) {
    std::lock_guard lock(g_lock);
    if (g_toasts.empty()) return;
    double now = ImGui::GetTime();
    Toast& t = g_toasts.front();
    if (t.shown < 0) t.shown = now;
    double age = now - t.shown;
    if (age > 6.0) {
        g_toasts.pop_front();
        return;
    }
    float alpha = float(std::min({1.0, age / 0.3, (6.0 - age) / 0.6}));
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float pad = std::max(12.0f, vp->Size.y / 50.0f);
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - pad, vp->Pos.y + vp->Size.y - pad), ImGuiCond_Always, ImVec2(1, 1));
    ImGui::SetNextWindowBgAlpha(0.85f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::Begin("##achievement", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::SetWindowFontScale(scale);
    ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "Achievement earned");
    ImGui::SetWindowFontScale(scale * 1.2f);
    ImGui::TextUnformatted(t.title.c_str());
    ImGui::SetWindowFontScale(scale * 0.85f);
    ImGui::TextDisabled("%s", t.desc.c_str());
    ImGui::End();
    ImGui::PopStyleVar();
}

// the in-game menu's Achievements page
void DrawList() {
    std::lock_guard lock(g_lock);
    Load();
    const auto& all = app::kGame.achievements;
    ImGui::Text("%zu of %zu earned", g_unlocked.size(), all.size());
    ImGui::Separator();
    for (const Def& d : all) {
        auto it = g_unlocked.find(d.id);
        bool got = it != g_unlocked.end();
        ImGui::TextColored(got ? ImVec4(0.95f, 0.80f, 0.45f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s  %s",
                           got ? "+" : "-", d.title);
        if (got) {
            ImGui::SameLine();
            ImGui::TextDisabled("  %s", it->second.c_str());
        }
        ImGui::Indent();
        ImGui::TextDisabled("%s", d.desc);
        ImGui::Unindent();
    }
}

// ---- deeds every game shares, Fate, the records of level runs
bool Fate() {
    static const bool on = [] {
        const char* e = std::getenv("ATHELAS_FATE");
        bool f = e && *e == '1';
        if (f) LOGI("fate: on (enemies count twice towards the kill deeds)");
        return f;
    }();
    return on;
}
bool FellowshipOn() {
    static const bool on = [] {
        const char* e = std::getenv("ATHELAS_FELLOWSHIP");
        return e && *e == '1';
    }();
    return on;
}

namespace {
struct KillDeed { uint64_t n; const char *id, *title, *desc; };
const KillDeed kKills[] = {
    {100, "kills_100", "A Hundred Foes", "Defeat 100 enemies."},
    {250, "kills_250", "Foe-hewer", "Defeat 250 enemies."},
    {500, "kills_500", "Sword of the West", "Defeat 500 enemies."},
    {1000, "kills_1000", "A Thousand Foes", "Defeat 1,000 enemies."},
    {5000, "kills_5000", "Bane of the Enemy", "Defeat 5,000 enemies."},
};
std::mutex g_run_lock;
std::string g_run;  // the level being played (its deed id), empty between levels
std::chrono::steady_clock::time_point g_run_at;
uint64_t g_run_kills = 0;
}  // namespace

void SharedDeeds(std::vector<Def>& v) {
    for (const KillDeed& k : kKills) v.push_back({k.id, k.title, k.desc});
    v.push_back({"fate_one", "Fated", "Complete a level on Fate."});
    v.push_back({"fate_all", "Doom-sworn", "Complete every level on Fate."});
}

void Defeated() {
    uint64_t by = Fate() ? 2 : 1;
    uint64_t n = Add("enemies_defeated", by);
    std::string run;
    {
        std::lock_guard lock(g_run_lock);
        run = g_run;
        g_run_kills += 1;
    }
    if (!run.empty()) Add(("kills@" + run).c_str(), 1);
    for (const KillDeed& k : kKills)
        if (n >= k.n) Unlock(k.id);
}

void LevelStarted(const char* id) {
    std::lock_guard lock(g_run_lock);
    if (g_run == id) return;  // (the same level's files again)
    g_run = id;
    g_run_at = std::chrono::steady_clock::now();
    g_run_kills = 0;
}

bool FateDone(const char* id) { return Count((std::string("fate@") + id).c_str()) > 0; }

void LevelDone(const char* id) {
    long long secs = -1;
    uint64_t kills = 0;
    {
        // only the level being played, once: a save loading (every level it holds "new") or a results screen
        // opened again records nothing
        std::lock_guard lock(g_run_lock);
        if (g_run != id) return;
        secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_run_at).count();
        kills = g_run_kills;
        g_run.clear();
    }
    Add((std::string("completed@") + id).c_str(), 1);
    Add("levels_completed", 1);
    if (Fate()) {
        Add((std::string("fate@") + id).c_str(), 1);
        Unlock("fate_one");
    }
    char when[32] = "";
    std::time_t t = std::time(nullptr);
    if (std::tm* lt = std::localtime(&t)) std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", lt);
    std::error_code ec;
    std::filesystem::path f = File().parent_path() / "records.txt";
    std::filesystem::create_directories(f.parent_path(), ec);
    bool fresh = !std::filesystem::exists(f, ec);
    std::ofstream o(f, std::ios::binary | std::ios::app);
    if (!o) return;
    if (fresh) o << "; Each level completed: when | level | seconds (-1: not timed) | enemies defeated | fate\r\n";
    o << when << " | " << id << " | " << secs << " | " << kills << " | " << (Fate() ? 1 : 0) << "\r\n";
    LOGR("record: %s in %llds, %llu enemies%s", id, secs, (unsigned long long)kills, Fate() ? " (Fate)" : "");
}

}  // namespace ach
