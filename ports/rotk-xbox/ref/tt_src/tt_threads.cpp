// The Two Towers: the loading screen's thread against the main thread.
//
// While a level loads, a second thread (the loop at 0xE7AE0) draws the loading screen each frame under the critical
// section at 0x4BB400 (RtlTryEnterCriticalSection at 0xE7A2B, released at 0xE7AD3). Its draw, the callback 0x7C270
// (body 0xD0100), reads the current screen page through the pointer at 0x4A9118 more than once. The main thread sets
// that page in 0xCFE40 without the lock, clearing the pointer before it picks the next one. On the console's single
// CPU the two hardly ever interleave; on a PC both threads run at once, and the draw read the pointer as it was being
// cleared (seen: a read of address 8 just after 0xD0267). 0xCFE40 takes the same critical section here. It is
// recursive, and 0xCFE40 already enters it itself through the debug print 0xE3CA0, so this cannot deadlock against
// the loading thread.
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kScreenLock = 0x4BB400;
constexpr uint32_t kEnterImport = 0x16FFA4;  // RtlEnterCriticalSection, as the kernel thunks were bound at boot
constexpr uint32_t kLeaveImport = 0x16FFA0;  // RtlLeaveCriticalSection
}  // namespace

// the main thread's screen page change
X86_EXTERN_FUNC(__imp__sub_000CFE40);
X86_FUNC(sub_000CFE40) {
    ::kernel::CallGuest(ctx, ::kernel::R32(kEnterImport), {kScreenLock});
    __imp__sub_000CFE40(ctx, base);
    uint32_t eax = ctx.eax;
    ::kernel::CallGuest(ctx, ::kernel::R32(kLeaveImport), {kScreenLock});
    ctx.eax = eax;
}
