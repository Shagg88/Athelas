// FOTR Xbox: the frame rate. The console shows a frame every second vertical blank (CreateDevice's presentation
// interval two, 30 a second); the game steps by the real time since the last frame (the performance counter, its
// step at most a tenth of a second), so a higher rate plays at the same speed. 30 by default, as on the console;
// [graphics] fps in the settings file raises it.
#include <cstdint>
#include <string>

#include "app/app.h"
#include "core/log.h"

namespace fotr {
uint32_t g_fps = 30;  // settings.toml [graphics] fps
void InitLanguage();  // fotr_lang.cpp

// GameInfo::init: after the settings load
void Init() {
    InitLanguage();
    app::Get().fps_cap = g_fps;
    LOGR("fotr: frame rate %u%s", g_fps, g_fps == 30 ? " (the console's)" : " (the console's is 30)");
}
}  // namespace fotr
