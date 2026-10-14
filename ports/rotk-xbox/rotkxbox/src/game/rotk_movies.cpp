// ROTK Xbox: movies.
//
// Skipping: the game's movie table 0x179F30 ({name*, type} pairs, a NULL name ends it and its type is the default)
// sets which movies a button press may stop: type 0 never (LOGOS), 1 always (ATTRFMV1, the bonus videos), 2 only
// after it has been seen once (every story movie). Same table and rules as ROTK PC (0x572E00, see ROTK Fix).
// [graphics] skip_all_videos (off by default, as on the console) sets every entry to 1.
// The opening logos (copyright, THX, New Line, EA) are not a movie of their own on the Xbox, as logos.avi is on PC:
// they are the start of HELMFMV1.vid, the Helm's Deep intro, so they can only be skipped with the whole intro
// (skip_all_videos). LOGOS (logos.vid) is never played at startup. ROTK Fix's SkipOpeningLogos has no Xbox match.
//
// Movie timing: while a movie plays the renderer shows it by the movie settings (gpu::g_movie), which is what
// [graphics] cutscene_audio_delay needs to hold the picture back for Bluetooth sound. The player draws each frame
// with 0xC46A0 (LLVideoDivXWrap_XBox.c); the flag stays up while it keeps drawing.
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <cstring>

#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace rotk {
bool g_skip_all_videos = false;  // settings.toml [graphics] skip_all_videos

namespace {
constexpr uint32_t kMovieTable = 0x179F30, kMovieTableMax = 64;
std::atomic<int64_t> g_movie_drawn{0};  // steady clock ms of the last movie frame
int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

void HarnessLevel();  // rotk_harness_level.cpp, test build only

// GameInfo::loaded: the image is in memory, the game has not started
void Loaded() {
    HarnessLevel();
    if (!g_skip_all_videos) return;
    int changed = 0;
    for (uint32_t i = 0; i < kMovieTableMax; i++) {
        uint32_t e = kMovieTable + i * 8, name = ::kernel::R32(e), type = ::kernel::R32(e + 4);
        if (type > 2) {
            LOGR("rotk: movie table not recognised at entry %u, videos keep the game's rules", i);
            return;
        }
        if (type != 1) {
            ::kernel::W32(e + 4, 1);
            changed++;
        }
        if (!name) break;
    }
    LOGR("rotk: every video can be skipped (%d table entries changed)", changed);
}

}  // namespace rotk

// 0xC46A0 draws a movie frame
X86_EXTERN_FUNC(__imp__sub_000C46A0);
X86_FUNC(sub_000C46A0) {
    static std::once_flag watcher;
    std::call_once(watcher, [] {
        // the flag drops a quarter second after the last movie frame
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (gpu::g_movie && rotk::NowMs() - rotk::g_movie_drawn > 250) gpu::g_movie = false;
            }
        }).detach();
    });
    rotk::g_movie_drawn = rotk::NowMs();
    gpu::g_movie = true;
    __imp__sub_000C46A0(ctx, base);
}
