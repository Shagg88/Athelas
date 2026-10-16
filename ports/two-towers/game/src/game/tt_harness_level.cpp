// testing only: TT_LEVEL=04BALIN1 in the environment boots that level in the prologue's place (capture_level.bat).
// Without TT_LEVEL nothing changes.
// The level name tables (.rdata 0x17B7A4: world names, .data 0x1D6378: stream wave banks) get the named level's
// pointer in the prologue's entry.
#include <cstdlib>
#include <cstring>
#include <cctype>
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"
namespace {
bool SameName(const char* a, const char* b) {
    for (; *a && *b; a++, b++)
        if (std::toupper(static_cast<unsigned char>(*a)) != std::toupper(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000F1110);
X86_FUNC(sub_000F1110) {
    if (const char* l = std::getenv("TT_LEVEL")) {
        // TT_STREAM: another level's stream wave bank, for a level whose own is not here
        const char* streams = std::getenv("TT_STREAM");
        for (uint32_t slot : {0x17B7A4u, 0x1D6378u}) {
            const char* want = slot == 0x1D6378u && streams ? streams : l;
            const char* cur = static_cast<const char*>(kernel::HostPtr(kernel::R32(slot)));
            size_t prefix = std::strlen(cur) - 7;  // "S_WB_" before 00ISUL1
            bool done = false;
            for (int k = -64; k <= 64 && !done; k++) {
                uint32_t p = kernel::R32(slot + uint32_t(k * 4));
                if (p < 0x10000 || p > 0x2000000) continue;
                const char* s = static_cast<const char*>(kernel::HostPtr(p));
                if (std::strlen(s) > prefix && SameName(s + prefix, want)) {
                    LOGI("probe: level slot %08X %s -> %s", slot, cur, s);
                    kernel::W32(slot, p);
                    done = true;
                }
            }
            if (!done) LOGI("probe: level %s not found near %08X", want, slot);
        }
    }
    __imp__sub_000F1110(ctx, base);
}
