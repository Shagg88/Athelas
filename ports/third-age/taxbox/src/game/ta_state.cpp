// The Third Age (Xbox): what the game is doing, from the files it opens. Each scene is a pair of files,
// d:\Data\Game\<group>\<scene>.scx and .sas: e98c is the front end (e98c03 the main menu), e01c01 the first level.
// GameInfo::in_menus: in the front end the keys bound to Start press B, so Esc goes back as on PC games.
//
// Movies: the game streams each one from a .vid file (D:\Data\Movies\<name>\<name>.vid) and draws its frames itself.
// A read from a .vid file in the last 5 seconds means a movie is on screen (the game reads its movies in bursts, up to
// 3.7 s apart in job 088's log, so a shorter window let the movie flicker between the two ways of showing it) (gpu::g_movie), checked at each draw
// (gpu::g_draw_observer), so the renderer shows it by [graphics] movie_display like the other games' movies instead of
// stretching it with widescreen. The opening logos (logo.vid) fill the 4:3 frame; the story movies are widescreen
// films with black bars above and below (test 087's screenshot of e1c1f01).
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"

namespace ta { extern std::atomic<uint32_t> g_movie_slot; }  // ta_movies.cpp

namespace {
std::mutex g_lock;
std::string g_scene;               // "e98c03", the last scene opened
std::atomic<bool> g_front{false};  // the front end's scene group (e98c)
std::atomic<int64_t> g_vid_read{0};  // steady clock, ms: the last read from a movie file
constexpr int64_t kMovieGapMs = 5000;  // reads this far apart still count as one movie playing

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// each draw: a movie is playing while its file is being read
// (job 105: the old test, a .vid read in the last 5 s, kept the movie shape on for 5 s after a movie ended or was
// skipped, so the menu and the game after a film flipped from the movie's shape to widescreen. Now the player's own
// state is read: the slot its frame step last ran on (ta_movies.cpp), state 3 sound started or 4 playing.)
void Observe(const gpu::State&) {
    uint32_t slot = ta::g_movie_slot.load();
    uint32_t st = slot ? ::kernel::R32(slot + 0xC0) : 0;
    bool movie = st == 3 || st == 4;
    if (gpu::g_movie.load() != movie) {
        gpu::g_movie = movie;
        LOGR("ta: movie %s", movie ? "playing" : "over");
    }
}

// lower case, the name between the last slash and the extension
std::string SceneName(const char* path) {
    const char* slash = std::strrchr(path, '\\');
    const char* fwd = std::strrchr(path, '/');
    if (!slash || (fwd && fwd > slash)) slash = fwd;
    std::string s = slash ? slash + 1 : path;
    for (char& c : s) c = char(c >= 'A' && c <= 'Z' ? c + 32 : c);
    size_t dot = s.rfind('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
}
}  // namespace

namespace ta {
extern uint32_t g_movie_zoom;  // ta_movies.cpp
void AchievementsScene(const std::string& scene);  // ta_achievements.cpp
// GameInfo::file_opened (any thread)
void FileOpened(const char* name) {
    size_t n = std::strlen(name);
    if (n < 4 || (std::strcmp(name + n - 4, ".scx") && std::strcmp(name + n - 4, ".SCX"))) return;
    std::string scene = SceneName(name);
    if (scene == "globscen" || scene == "loadonce") return;  // loaded with every scene
    bool front = scene.rfind("e98", 0) == 0;
    {
        std::lock_guard lock(g_lock);
        if (scene == g_scene) return;
        g_scene = scene;
    }
    g_front = front;
    LOGR("ta: scene %s%s", scene.c_str(), front ? " (front end)" : "");
    if (!front) AchievementsScene(scene);
}

// GameInfo::file_read (any thread): a read from a movie file
void FileRead(const char* name) {
    size_t n = std::strlen(name);
    if (n < 4 || (std::strcmp(name + n - 4, ".vid") && std::strcmp(name + n - 4, ".VID"))) return;
    if (!g_vid_read.load() || NowMs() - g_vid_read.load() >= kMovieGapMs) {
        // a new movie: its shape from its name
        bool logo = SceneName(name) == "logo";
        // all movies fit the screen (the logos too, at Shagg's request); the zoom is for the films only
        gpu::g_movie_shape = gpu::kMovieFit;
        gpu::g_movie_fit_zoom = logo ? 1.0f : ta::g_movie_zoom / 100.0f;
        LOGR("ta: movie %s (fit, zoom %u%%)", SceneName(name).c_str(), logo ? 100u : ta::g_movie_zoom);
    }
    g_vid_read = NowMs();
}

// GameInfo::init: the movie test at each draw
void InitMovies() { gpu::g_draw_observer = Observe; }

// GameInfo::in_menus: the front end's menus are up
bool InMenus() { return g_front; }

std::string Scene() {
    std::lock_guard lock(g_lock);
    return g_scene;
}

// GameInfo::activity: the area being played, for the Discord status ("" in the front end: "In the menus"). Each scene's
// area as the program's own map names give it (map_e1c01_eregion, map_e1c02_wmoria, map_e1c04_emoria,
// map_e2c01_eegullies, map_e2c03_rohan, map_e2c05_hdeep, map_e3c01_osgiliath, map_e3c02_mtirith, map_e3c03_pfields).
std::string Activity() {
    static const struct {
        const char* scene;
        const char* area;
    } kAreas[] = {{"e01c01", "Eregion"},           {"e01c02", "Western Moria"},   {"e01c04", "Eastern Moria"},
                  {"e02c01", "Eastemnet Gullies"}, {"e02c03", "Plains of Rohan"}, {"e02c05", "Helm's Deep"},
                  {"e03c01", "Osgiliath"},         {"e03c02", "Minas Tirith"},    {"e03c03", "Pelennor Fields"}};
    if (g_front) return {};
    std::string scene = Scene();
    for (const auto& a : kAreas)
        if (scene == a.scene) return a.area;
    return {};
}
}  // namespace ta
