// The Third Age (Xbox): [graphics] skip_logos. The opening logos are one movie, D:\Data\Movies\logo\logo.vid (EA, New Line
// and the others, about 34 seconds), which a button press doesn't end. With skip_logos on, the game is given that file
// cut down to its first frame (SDK kernel::SetFileProvider: served from memory, the file on disk untouched), so the
// game's own player ends it at once and goes on to the title screen as after the full movie.
//
// The .vid format (byte-reversed four-letter tags, each chunk's size after its tag, little-endian): VID1 (32 bytes),
// HEAD (64 bytes: "VIDH", then the size, a 1, the picture size 0x01C00200 = 512 x 448, the frame count at byte 0x14 of
// its payload, 0x3FE = 1022 for logo.vid, and the frame rate 30000 / 1001), then one FRAM chunk per frame.
// STATUS: the one-frame file is not used (test 095: the game stayed on a black screen, waiting for the movie's
// sound). The skips are done as the game's own A-button skip instead: see sub_001CB9F0 at the end.
#include <cstdint>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"

namespace ta {
bool g_skip_logos = false;  // settings.toml [graphics] skip_logos
bool g_skip_all_videos = false;  // [graphics] skip_all_videos
std::atomic<uint32_t> g_movie_slot{0};  // the slot the movie player's frame step last ran on (ta_state.cpp's movie test)
uint32_t g_movie_zoom = 115;     // [graphics] movie_zoom: the story films zoomed past Fit, their black bars cut down
}

namespace {
uint32_t Le32(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v = 0;
    if (at + 4 <= d.size()) std::memcpy(&v, &d[at], 4);
    return v;
}
bool Tag(const std::vector<uint8_t>& d, size_t at, const char* reversed) {
    return at + 4 <= d.size() && std::memcmp(&d[at], reversed, 4) == 0;
}

// the movie with only its first frame; null when the file isn't laid out as expected (then it plays as it is)
kernel::FileData FirstFrameOnly(const std::string& path) {
    std::vector<uint8_t> d;
    if (!kernel::ReadGameFile(path, d)) return nullptr;
    size_t vid1 = 0, head = Le32(d, 4);
    if (!Tag(d, vid1, "1DIV") || !Tag(d, head, "DAEH")) return nullptr;
    size_t fram = head + Le32(d, head + 4);
    if (!Tag(d, fram, "MARF")) return nullptr;
    size_t end = fram + Le32(d, fram + 4);
    if (end > d.size() || end <= fram) return nullptr;
    constexpr size_t kFrameCount = 8 + 0x14;  // HEAD chunk: tag and size, then the frame count at payload byte 0x14
    uint32_t frames = Le32(d, head + kFrameCount);
    if (frames < 2 || frames > 100000) return nullptr;
    d.resize(end);
    uint32_t one = 1;
    std::memcpy(&d[head + kFrameCount], &one, 4);
    LOGR("ta: opening logos skipped (logo.vid served with 1 of its %u frames)", frames);
    return std::make_shared<const std::vector<uint8_t>>(std::move(d));
}

// SDK file provider: asked at each file the game opens, the path lower case
kernel::FileData Provide(const std::string& path) {
    if (!ta::g_skip_logos) return nullptr;
    static const std::string kLogo = "\\movies\\logo\\logo.vid";
    if (path.size() < kLogo.size() || path.compare(path.size() - kLogo.size(), kLogo.size(), kLogo) != 0) return nullptr;
    return FirstFrameOnly(path);
}
}  // namespace

namespace ta {
// GameInfo::init (ta::Init): after the settings load
void InitMovieOptions() { kernel::SetFileProvider(Provide); }
// GameInfo::init: every movie fills the screen's width at any screen shape (no bars at the sides); the top and bottom
// are cut instead
void InitMovieShape() { gpu::g_movie_fit_max = 4.0f; }
}  // namespace ta

// The movie player (static research 2026-10-29): a movie slot (0x2FD2C0, stride 0x12A0) runs through states at +0xC0
// (3 audio started, 4 playing, 5 finished). 0x1CB9F0 is the playing state's frame step (eax: the slot). The A button
// (0x20DEA0) calls 0x1AD9A0, which stops the sound, runs the slot's callbacks and sets state 5, but only for a slot
// marked skippable (+0xBC), which the logo movie is not. With skip_logos (the logo movie, its name at +0x115) or
// skip_all_videos, that skip is run on the movie's first playing frame, so the game carries on exactly as after A.
X86_EXTERN_FUNC(__imp__sub_001CB9F0);
X86_FUNC(sub_001CB9F0) {
    uint32_t slot = ctx.eax;
    ta::g_movie_slot = slot;
    bool skip = ta::g_skip_all_videos;
    if (!skip && ta::g_skip_logos) {
        char name[16] = {};
        for (int i = 0; i < 15; i++) name[i] = char(::kernel::G<uint8_t>(slot + 0x115 + uint32_t(i)) | 0x20);
        skip = std::strncmp(name, "logo", 4) == 0;
    }
    if (!skip || ::kernel::R32(slot + 0xC0) != 4) {
        __imp__sub_001CB9F0(ctx, base);
        return;
    }
    static std::atomic<int> logged{0};
    if (logged++ < 20) LOGR("ta: movie skipped (%s)", ta::g_skip_all_videos ? "skip_all_videos" : "skip_logos");
    ctx.eax = slot;
    ::kernel::CallGuest(ctx, 0x1AD9A0, {});
    ctx.esp += 4;  // the frame step's own ret (plain)
}
