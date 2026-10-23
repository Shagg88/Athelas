// The Two Towers: movies. The player 0xE8B70 (name, mode, ...) plays D:\FMV\<name>.bik; the opening logos are
// played as "logos" (0x7F2F6, its result unused). [graphics] skip_logos (off by default, as on the console) passes
// over that one movie. Mode 0 and 2 stop the movie at a button press (0xE8AA0, 0xE8740); mode 1, which the logos are
// played in, can't be stopped. [graphics] skip_all_videos plays mode 1 as mode 0. While a movie plays the renderer
// shows it by the movie settings (gpu::g_movie).
#include <atomic>
#include <cstring>
#include <string_view>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace tt {
bool g_skip_logos = false;  // settings.toml [graphics] skip_logos
bool g_skip_all_videos = false;  // settings.toml [graphics] skip_all_videos
extern std::atomic<int> g_movie_playing;  // tt_framerate.cpp: no frame rate tricks while a movie plays
}

X86_EXTERN_FUNC(__imp__sub_000E8B70);
X86_FUNC(sub_000E8B70) {
    uint32_t name = ::kernel::R32(ctx.esp + 4);
    if (tt::g_skip_logos && name && !std::strcmp(static_cast<const char*>(::kernel::HostPtr(name)), "logos")) {
        LOGR("tt: opening logos skipped (skip_logos)");
        ::kernel::Return(ctx, 1, 0);  // as if it played to the end; cdecl, the caller pops its arguments
        return;
    }
    const char* what = name ? static_cast<const char*>(::kernel::HostPtr(name)) : "";
    if (tt::g_skip_all_videos && ::kernel::R32(ctx.esp + 8) == 1) ::kernel::W32(ctx.esp + 8, 0);
    LOGI("tt: movie %s (mode %u)", what, ::kernel::R32(ctx.esp + 8));
    // the interviews (*int), the making-of and the game-over Eye (death) fill the 4:3 frame (rows 16 to 462 of 480,
    // death all of it); the story clips and the intro are letterboxed film inside it (rows 66 to 411; roh1_1 54 to
    // 425), and so are the opening logos but for a faint glow (what shows is in rows 60 to 420). The trailer (attract,
    // the idle movie) opens with letterboxed film, then shows gameplay in the full frame; the player chose to have it
    // fill the screen throughout, losing the top and bottom of the gameplay part
    std::string_view nm(what);
    bool full = (nm.size() > 3 && nm.substr(nm.size() - 3) == "int") || nm == "makingof" || nm == "death";
    gpu::g_movie_shape = full ? gpu::kMovie43 : gpu::kMovieFit;
    app::MovieSubtitles(what);  // subtitles\<name>.srt, when the player has one
    tt::g_movie_playing++;
    gpu::g_movie = true;
    __imp__sub_000E8B70(ctx, base);
    gpu::g_movie = --tt::g_movie_playing > 0;
    LOGI("tt: movie %s ended", what);
}
