// FOTR Xbox: the frame rate. The console shows a frame every second vertical blank (CreateDevice's presentation
// interval two, 30 a second); the game steps by the real time since the last frame (the performance counter, its
// step at most a tenth of a second), so a higher rate plays at the same speed. 30 by default, as on the console;
// [graphics] fps in the settings file raises it.
#include <cstdint>
#include <cstdlib>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace dsound {
extern bool g_output_buffers;  // sdk/runtime/xdk/dsound.cpp
extern bool g_mixin_buffers;
}
namespace fotr {
uint32_t g_fps = 30;  // settings.toml [graphics] fps
void InitLanguage();  // fotr_lang.cpp

// GameInfo::init: after the settings load
void InitText();  // fotr_text.cpp
void Init() {
    InitLanguage();
    InitText();
    app::Get().fps_cap = g_fps;
    // DirectMusic's synth (0x16CF14 and the routines it calls) guards its voice list (0x2EAE30) by raising the IRQL
    // alone, which on the console's one processor kept every other thread out. Here two threads changed the list at
    // once as the opening film started: null writes at 0x16D2E9 / 0x16CF47, 2 of about 15 runs, in 1.8.4 too.
    // FOTR_DISPATCH_LOCK=0 turns it off to compare.
    const char* dl = std::getenv("FOTR_DISPATCH_LOCK");
    kernel::g_dispatch_lock = !(dl && *dl == '0');
    // DirectMusic plays its notes into buffers of its own (IDirectSoundBuffer_SetOutputBuffer) whose volumes set how
    // loud they are; played straight, Bag End's sounds came out far above its music (#90). FOTR_OUTPUT_BUFFERS=0
    // turns it off to compare.
    const char* ob = std::getenv("FOTR_OUTPUT_BUFFERS");
    dsound::g_output_buffers = !(ob && *ob == '0');
    // Bag End's crickets go to effect-send bins 27 and 28, which the game plays through mix-in buffers of its own
    // (0x8BB20) at their volumes; played straight they were too loud (#92). FOTR_MIXIN=0 turns it off to compare.
    const char* mi = std::getenv("FOTR_MIXIN");
    dsound::g_mixin_buffers = !(mi && *mi == '0');
    LOGR("fotr: frame rate %u%s", g_fps, g_fps == 30 ? " (the console's)" : " (the console's is 30)");
}
}  // namespace fotr
