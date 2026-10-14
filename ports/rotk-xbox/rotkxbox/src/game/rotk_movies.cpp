// ROTK Xbox: movies.
//
// Skipping: the game's movie table 0x179F30 ({name*, type} pairs, a NULL name ends it and its type is the default)
// sets which movies a button press may stop: type 0 never (LOGOS), 1 always (ATTRFMV1, the bonus videos), 2 only
// after it has been seen once (every story movie). Same table and rules as ROTK PC (0x572E00, see ROTK Fix).
// [graphics] skip_all_videos (off by default, as on the console) sets every entry to 1.
// The opening logos: on a first start (no saved game) they are the start of HELMFMV1.vid, the Helm's Deep intro, so
// they can only be skipped with the whole intro (skip_all_videos). Once there is a save the game starts with LOGOS
// (logos.vid: EA, New Line) after the copyright page instead. ROTK Fix's SkipOpeningLogos has no Xbox match.
//
// Movie timing: while a movie plays the renderer shows it by the movie settings (gpu::g_movie), which is what
// [graphics] cutscene_audio_delay needs to hold the picture back for Bluetooth sound. A movie frame is a draw from
// the player's three planes (Y 768x544 in stage 2), seen through the SDK's draw observer; the flag stays up while
// such frames keep coming. (The reader 0xC46A0 decodes ahead in bursts, so it could not keep the flag up.)
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <cctype>
#include <cstring>
#include <string>

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
void InitFonts();     // rotk_fonts.cpp

// GameInfo::loaded: the image is in memory, the game has not started
void Loaded() {
    HarnessLevel();
    InitFonts();
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

// movie_display "fit" (ROTK Fix's auto), by the movie's name as ROTK Fix does it: the story films (names with FMV,
// HELMFMV1 with the opening logos at its start) are letterboxed in the 640x480 frame at rows 65 to 413, as the PC's
// .avi files are (66 to 416), so their middle 16:9 is shown and they fill the width (gpu::kMovieFit). LOGOS fills
// the width too: EA's logo sits in rows 94 to 433 of a blue glow and New Line's in film bars (65 to 413), so the
// window ends at row 440 to keep EA's bottom line. ATTRFMV1, the *SPC* interviews and the rest are 4:3 pictures,
// shown 4:3. The name comes from the movie table lookup 0x111B90;
// the player draws each frame from three planes, Y the 768x544 one (stage 2, LIN_AL8).
std::string g_movie_name;  // the movie starting, from the type lookup 0x111B90
std::atomic<bool> g_movie_new{false};
std::mutex g_name_lock;

void WatchMovie(const gpu::State& s) {
    if (!s.textures[2] || !s.ps) return;
    gpu::TextureDesc d;
    if (!gpu::Describe(s.textures[2], d) || d.width != 768 || d.height != 544 || !d.linear || !d.pitch) return;
    // a movie frame drawn: the movie settings apply while frames keep coming (the watcher drops the flag after them)
    StartWatcher();
    rotk::g_movie_drawn = rotk::NowMs();
    gpu::g_movie = true;
    if (!g_movie_new.exchange(false)) return;
    std::string n;
    {
        std::lock_guard l(g_name_lock);
        n = g_movie_name;
    }
    for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
    bool film = n.find("FMV") != std::string::npos && n != "ATTRFMV1";
    bool logos = n == "LOGOS";
    gpu::g_movie_fit_bottom = logos ? 440.0f / 480.0f : 0.0f;
    gpu::g_movie_shape = film || logos ? gpu::kMovieFit : gpu::kMovie43;
    LOGR("rotk: movie %s (%s)", n.c_str(),
         logos  ? "logos, fill the width"
         : film ? "letterboxed film, fills the width"
                : "4:3");
}
const int kWatch = (gpu::g_draw_observer = WatchMovie, 0);
}  // namespace

// 0x111B90 (name in ebx): the movie table's type for a movie, asked once as each movie starts (0x111C89)
X86_EXTERN_FUNC(__imp__sub_00111B90);
X86_FUNC(sub_00111B90) {
    if (ctx.ebx) {
        std::lock_guard l(g_name_lock);
        g_movie_name = static_cast<const char*>(::kernel::HostPtr(ctx.ebx));
        g_movie_new = true;
    }
    __imp__sub_00111B90(ctx, base);
}
