// ROTK Xbox: movies.
//
// Skipping: the game's movie table 0x179F30 ({name*, type} pairs, a NULL name ends it and its type is the default)
// sets which movies a button press may stop: type 0 never (LOGOS), 1 always (ATTRFMV1, the bonus videos), 2 only
// after it has been seen once (every story movie). Same table and rules as ROTK PC (0x572E00, see ROTK Fix).
// [graphics] skip_all_videos (off by default, as on the console) sets every entry to 1.
// The opening logos: on a first start (no saved game) they are the start of HELMFMV1.vid, the Helm's Deep intro, so
// they can only be skipped with the whole intro (skip_all_videos). Once there is a save the game starts with LOGOS
// (logos.vid: EA, New Line) after the copyright page instead. [graphics] skip_logos (as the other ports have it)
// passes over LOGOS: the movie is made skippable as it starts (its table type answered 1) and the port presses A
// for the player, on and off, until it has ended (the game's own skip; at most 10 s). The intro of a first start
// keeps its logos (they are the film's own first seconds). ROTK_SKIP_MOVIE=<name> does the same to another movie
// in test builds.
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
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/texture.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace rotk {
bool g_skip_all_videos = false;  // settings.toml [graphics] skip_all_videos
bool g_skip_logos = false;       // settings.toml [graphics] skip_logos
std::atomic<int64_t> g_skip_until{0};  // steady clock ms: A pressed for the player until then (skip_logos)
std::atomic<bool> g_skip_seen{false};  // the skipped movie's frames have been drawn

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
void InitEowyn();     // rotk_eowyn.cpp

// GameInfo::loaded: the image is in memory, the game has not started
void Loaded() {
    HarnessLevel();
    InitFonts();
    InitEowyn();
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

// A movie frame is one draw. When the game draws much else as well, the picture is the game's: a skipped or ending
// movie fades out over the level for half a second or so. Those frames, and what follows, are shown as the game,
// not by the movie settings (cropped to the film's middle, held back by cutscene_audio_delay), so nothing on screen
// jumps when the movie ends; the flag drops at once instead of a quarter second after the last movie frame.
constexpr int kOtherDrawsMixed = 48;
std::atomic<int> g_other_draws{0};

void WatchMovie(const gpu::State& s) {
    gpu::TextureDesc d;
    bool movie = s.textures[2] && s.ps && gpu::Describe(s.textures[2], d) && d.width == 768 && d.height == 544 &&
                 d.linear && d.pitch;
    if (!movie) {
        if (++g_other_draws > kOtherDrawsMixed && gpu::g_movie.exchange(false))
            LOGI("rotk: movie over or fading into the game (%d ms after its last frame)", int(rotk::NowMs() - rotk::g_movie_drawn));
        return;
    }
    if (g_other_draws.exchange(0) > kOtherDrawsMixed) return;  // drawn over the game: the game's picture
    // a movie frame drawn: the movie settings apply while frames keep coming (the watcher drops the flag after them)
    StartWatcher();
    rotk::g_movie_drawn = rotk::NowMs();
    gpu::g_movie = true;
    rotk::g_skip_seen = true;
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
    std::string name;
    if (ctx.ebx) {
        std::lock_guard l(g_name_lock);
        g_movie_name = name = static_cast<const char*>(::kernel::HostPtr(ctx.ebx));
        g_movie_new = true;
    }
    __imp__sub_00111B90(ctx, base);
    app::MovieSubtitles(name);  // subtitles\<name>.srt, when the player has one
    for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
#ifndef _WIN32
    static const char* test = std::getenv("ROTK_SKIP_MOVIE");
    bool skip = rotk::g_skip_logos && name == "LOGOS";
    if (test && name == test) skip = true;
#else
    bool skip = rotk::g_skip_logos && name == "LOGOS";
#endif
    if (skip) {
        ctx.eax = 1;  // may be skipped
        rotk::g_skip_seen = false;
        rotk::g_skip_until = rotk::NowMs() + 10000;
        LOGR("rotk: movie %s skipped (skip_logos)", name.c_str());
    }
}

namespace rotk {
// GameInfo::pad (rotk_controls.cpp), player 1: A pressed for 100 ms in every 200 while a skipped movie plays; done
// once its frames have stopped coming (the movie flag down after they were seen)
void SkipPress(int port, input::Pad& pad) {
    if (port != 0 || g_skip_until == 0) return;
    int64_t now = NowMs();
    if (now > g_skip_until || (g_skip_seen && !gpu::g_movie)) {
        g_skip_until = 0;
        return;
    }
    if (now / 100 % 2 == 0) pad.analog[0] = 255;
}
}  // namespace rotk
