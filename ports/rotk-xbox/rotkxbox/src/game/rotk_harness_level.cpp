// Testing: ROTK_LEVEL=Pat01 in the environment (every build: the scene test runs jump straight to a level with it;
// players never set it, and without it nothing changes) boots that
// level where the game starts Helm's Deep, so every level can be loaded and run headless. The level name table
// (0x16CF00, read by the scene loader 0x9AA00: "Data/Game/<abc>/<Abc01>.scx") gets the named level's pointer in
// Hel01's entry. Without ROTK_LEVEL nothing changes.
#include <cstdlib>
#ifdef _WIN32
#include <string.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#include "core/log.h"
#include "kernel/kernel.h"

namespace rotk {
void HarnessLevel() {
    const char* want = std::getenv("ROTK_LEVEL");
    if (!want) return;
    constexpr uint32_t kTable = 0x16CF00, kLevels = 18;
    for (uint32_t i = 0; i < kLevels; i++) {
        uint32_t p = kernel::R32(kTable + i * 4);
        if (!strcasecmp(static_cast<const char*>(kernel::HostPtr(p)), want)) {
            kernel::W32(kTable, p);
            LOGR("rotk: test level %s in Helm's Deep's place", want);
            return;
        }
    }
    LOGR("rotk: test level %s is not in the level table", want);
}
}  // namespace rotk

