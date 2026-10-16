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
#include "core/log.h"
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

// The loading screen ends in 0x7C1D0 (main thread): after its fade-out loop it calls 0xE7940, which tells the
// loading thread to stop and lowers it to idle priority, then 0xE73F0, then clears the page pointer (0xCFFA0, inlined
// at the end). On the console an idle thread never runs while the game is busy; on a PC it can still be inside a
// draw, which read the pointer as null (seen: a read of address 0xC just after 0xD020B, entering Moria). The lock is
// taken from 0xE73F0 to the clear: not across the fade loop (the loading thread's draw advances that fade) nor across
// 0xE7940 (it waits for the loading thread). A draw after the clear sees null and stops.
namespace {
thread_local bool t_ending = false, t_held = false;
}

X86_EXTERN_FUNC(__imp__sub_0007C1D0);
X86_FUNC(sub_0007C1D0) {
    t_ending = true;
    __imp__sub_0007C1D0(ctx, base);
    uint32_t eax = ctx.eax;
    if (t_held) ::kernel::CallGuest(ctx, ::kernel::R32(kLeaveImport), {kScreenLock});
    t_held = t_ending = false;
    ctx.eax = eax;
}

X86_EXTERN_FUNC(__imp__sub_000E73F0);
X86_FUNC(sub_000E73F0) {
    if (t_ending && !t_held) {
        uint32_t arg = ::kernel::R32(ctx.esp + 4);
        ::kernel::CallGuest(ctx, ::kernel::R32(kEnterImport), {kScreenLock});
        ::kernel::W32(ctx.esp + 4, arg);
        t_held = true;
        static bool logged = false;
        if (!logged) LOGI("tt: loading screen ends under its lock");
        logged = true;
    }
    __imp__sub_000E73F0(ctx, base);
}
