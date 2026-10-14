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
// [graphics] cutscene_audio_delay needs to hold the picture back for Bluetooth sound. A movie frame is a draw from
// the player's three planes (Y 768x544 in stage 2), seen through the SDK's draw observer; the flag stays up while
// such frames keep coming. (The reader 0xC46A0 decodes ahead in bursts, so it could not keep the flag up.)
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <cstring>

#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/texture.h"
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

namespace {
void StartWatcher() {
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
}

// movie_display "fit" (ROTK Fix's auto): the story films carry black bars above and below (rows 66 to 416 of 480, as
// on PC), so their middle 16:9 is shown; the opening logos fill the 4:3 frame and are shown whole. The player draws
// each frame from three planes, Y the 768x544 one (stage 2, LIN_AL8, the picture in rows 16 to 528); a row inside
// each bar is looked at: black in both (Y 16) is a film. Bars bright switches to the whole frame at once; back to
// the film's 16:9 after six seconds of black bars (the logos have dark moments).
void WatchMovie(const gpu::State& s) {
    if (!s.textures[2] || !s.ps) return;
    gpu::TextureDesc d;
    if (!gpu::Describe(s.textures[2], d) || d.width != 768 || d.height != 544 || !d.linear || !d.pitch) return;
    // a movie frame drawn: the movie settings apply while frames keep coming (the watcher drops the flag after them)
    StartWatcher();
    rotk::g_movie_drawn = rotk::NowMs();
    gpu::g_movie = true;
    auto dark = [&](uint32_t row) {
        const uint8_t* p = static_cast<const uint8_t*>(::kernel::HostPtr(d.data + row * d.pitch));
        for (uint32_t x = 40; x < 728; x += 8)
            if (p[x] > 40) return false;
        return true;
    };
    static int64_t black_since = 0;
    int64_t now = rotk::NowMs();
    bool bars = dark(69) && dark(475);  // screen rows 50 and 430: inside the bars (they end at 65 and 415), just outside the 16:9 shown
    bool was = gpu::g_movie_letterbox;
    if (!bars) {
        black_since = 0;
        gpu::g_movie_letterbox = false;
    } else {
        if (!black_since) black_since = now;
        if (now - black_since >= 6000) gpu::g_movie_letterbox = true;
    }
    static std::atomic<int> logged{0};
    if (was != gpu::g_movie_letterbox && logged++ < 20)
        LOGI("rotk: movie %s", gpu::g_movie_letterbox ? "has film bars: its middle 16:9 shown" : "fills the frame: shown whole");
}
const int kWatch = (gpu::g_draw_observer = WatchMovie, 0);
}  // namespace

