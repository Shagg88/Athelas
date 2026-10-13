// The Two Towers: movies. The player 0xE8B70 (name, mode, ...) plays D:\FMV\<name>.bik; the opening logos are
// played as "logos" (0x7F2F6, its result unused). [graphics] skip_logos (off by default, as on the console) passes
// over that one movie.
#include <atomic>
#include <cstring>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace tt {
bool g_skip_logos = false;  // settings.toml [graphics] skip_logos
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
    tt::g_movie_playing++;
    __imp__sub_000E8B70(ctx, base);
    tt::g_movie_playing--;
}
