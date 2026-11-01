// The Third Age (Xbox): [graphics] skip_logos. The opening logos are one movie, D:\Data\Movies\logo\logo.vid (EA, New Line
// and the others, about 34 seconds), which a button press doesn't end. With skip_logos on, it is
// skipped as the game's own A-button skip: see sub_001CB9F0 at the end. (Serving logo.vid cut down to one frame, through
// kernel::SetFileProvider, was tried and dropped: test 095, the game stayed on a black screen waiting for the movie's
// sound.)
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

namespace ta {
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
