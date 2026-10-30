// TA_LEVEL=e02c03 (or any chapter scene in the table, or e98c02 / e99c01) boots that scene where a new game starts
// the first one (e01c01), so every chapter can be loaded and tested without playing up to it.
// The scene table: 0x26AFDC, 14 pointers to "eNNc/eNNcNN" (the loader builds "Data/Game/<that>.scx"):
//   e98c03 e98c02 e01c01 e01c02 e01c04 e02c01 e99c01 e02c03 e02c04 e02c05 e03c01 e03c02 e03c03 e03c04
// (e01c03, e02c02, e02c04, e03c04 and e99c01 have no files on the disc.) Without TA_LEVEL nothing changes.
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/log.h"
#include "kernel/kernel.h"

namespace ta {
namespace {
constexpr uint32_t kTable = 0x26AFDC, kScenes = 14, kFirst = 2;  // e01c01's entry
}

void HarnessLevel() {
    const char* want = std::getenv("TA_LEVEL");
    if (!want || !*want) return;
    for (uint32_t i = 0; i < kScenes; i++) {
        uint32_t p = ::kernel::R32(kTable + i * 4);
        const char* s = static_cast<const char*>(::kernel::HostPtr(p));
        const char* slash = s ? std::strchr(s, '/') : nullptr;
        if (slash && !std::strcmp(slash + 1, want)) {
            ::kernel::W32(kTable + kFirst * 4, p);
            LOGR("ta: test scene %s in e01c01's place", want);
            return;
        }
    }
    LOGR("ta: test scene %s is not in the scene table", want);
}

// GameInfo::loaded: the image is in memory, the game has not started
void Loaded() {
    // (a check that this is the table: the first new-game scene)
    const char* s = static_cast<const char*>(::kernel::HostPtr(::kernel::R32(kTable + kFirst * 4)));
    if (!s || std::strcmp(s, "e01c/e01c01")) {
        LOGR("ta: the scene table isn't where it was expected (0x%08X)", kTable);
        return;
    }
    HarnessLevel();
}
}  // namespace ta
