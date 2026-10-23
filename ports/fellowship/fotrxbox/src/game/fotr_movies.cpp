// FOTR Xbox: [graphics] skip_logos, the opening movies skipped. At boot the front end (0x933C0) plays logo_1; each
// logo's end (0x93550) reads the number after "logo_" (0x768CE, in eax at 0x935A4) and plays the next one up to
// logo_5, then starts the title screen (0x933C0 with 0, 0, 1). With skip_logos the number read is 5, so the title
// screen follows logo_1 (a fifth of a second of black). Starting the title screen at boot instead of logo_1 is not
// the same: the front end then also starts the intro film (fmv_2) a second later.
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "gpu/backend.h"
#include "gpu/texture.h"

namespace fotr {
bool g_skip_logos = false;  // settings.toml [graphics] skip_logos
}

// [[hook]] 0x935A4
// (void: a hook without on_true is declared void in the generated code; with MSVC name mangling a bool definition
// would not link)
void FOTR_LogoEnded(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (fotr::g_skip_logos && ctx.eax < 5) {
        LOG_FIRST(1, "fotr: opening logos skipped");
        ctx.eax = 5;
    }
}

// Movies on screen: the game plays its .xmv files itself (the XMV library decodes into two 640x480 YUY2 textures,
// 0x81068000 / 0x810FE000) and draws each frame as one full-screen quad. While such frames come, the renderer shows
// them by the movie settings (gpu::g_movie: movie_display, movie_sharpening, cutscene_audio_delay), at 4:3 or with
// the film's middle filling the width, rather than as a game frame (stretched to 16:9 with widescreen on).
// Which movie it is comes from the file opened (GameInfo::file_opened): the story films (movies\English\fmv_*) are
// letterboxed in the 4:3 frame, so "fit" shows their middle 16:9; the logos and the rest are 4:3 pictures, shown 4:3.
// start_menu.xmv is the title screen's moving background with the menu drawn over it: the game's picture.

namespace fotr {
void WideHud(const gpu::State& s);
void AchievementsFile(const char* name);
}

namespace {
std::atomic<int> g_kind{0};  // the movie opened last: 0 none or the title screen's, 1 a 4:3 picture, 2 a letterboxed film
std::atomic<int64_t> g_drawn{0};
constexpr int kOtherDrawsMixed = 48;  // more draws than this between movie frames: the game drawing over it
std::atomic<int> g_other{0};

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void Watch(const gpu::State& s) {
    fotr::WideHud(s);  // fotr_camera.cpp
    gpu::TextureDesc d;
    bool frame = g_kind && s.textures[0] && gpu::Describe(s.textures[0], d) && d.format == 0x24 && d.width == 640 &&
                 d.height == 480;
    if (!frame) {
        if (++g_other > kOtherDrawsMixed) gpu::g_movie = false;
        return;
    }
    if (g_other.exchange(0) > kOtherDrawsMixed) return;
    static std::once_flag watcher;
    std::call_once(watcher, [] {
        std::thread([] {  // the flag drops a quarter second after the last movie frame
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (gpu::g_movie && NowMs() - g_drawn > 250) {
                    gpu::g_movie = false;
                    // a movie frame that came while this looked: the game thread sets g_drawn before g_movie
                    if (NowMs() - g_drawn <= 250) gpu::g_movie = true;
                }
            }
        }).detach();
    });
    g_drawn = NowMs();
    gpu::g_movie_shape = g_kind == 2 ? gpu::kMovieFit : gpu::kMovie43;
    gpu::g_movie = true;
}
const int kWatch = (gpu::g_draw_observer = Watch, 0);
}  // namespace

namespace fotr {
// GameInfo::file_opened
void PromptsFile(const char* name);  // fotr_prompts.cpp
void FileOpened(const char* name) {
    AchievementsFile(name);  // fotr_achievements.cpp
    PromptsFile(name);
    const char* m = std::strstr(name, "\\movies\\");
    if (!m) m = std::strstr(name, "\\Movies\\");
    if (!m) return;
    std::string n = m + 8;
    for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
    int kind = n.find("start_menu") != std::string::npos ? 0 : n.find("fmv_") != std::string::npos ? 2 : 1;
    // subtitles\<name>.srt, when the player has one (the title screen's moving background has nothing spoken)
    if (kind) {
        std::string stem = n.substr(n.find_last_of('\\') + 1);
        app::MovieSubtitles(stem.substr(0, stem.find('.')));
    }
    if (g_kind.exchange(kind) != kind || kind) LOGI("fotr: movie %s (%s)", n.c_str(), kind == 2 ? "letterboxed film" : kind ? "4:3" : "title screen background");
}
}  // namespace fotr
