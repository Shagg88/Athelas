// FOTR Xbox: the D3D8LTCG library's internals the game calls directly (XDK 4831, link-time code generated), where the
// host renderer keeps the state itself.
#include <cstdint>
#include <cstring>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

// CDevice_SetStateVB(device, flag) 0x199C70 and CDevice_SetStateUP (device in esi) 0x199E20 write the dirty state
// into the push buffer before a draw the caller then pushes itself. The host renderer applies its state at each draw
// it is given, so these do nothing (they read device fields only the console's library keeps, +0x380 the vertex
// shader among them).
X86_FUNC(sub_00199C70) {
    (void)base;
    LOG_FIRST(1, "fotr: CDevice_SetStateVB left to the host renderer");
    ::kernel::Return(ctx, 0, 2);
}
X86_FUNC(sub_00199E20) {
    (void)base;
    LOG_FIRST(1, "fotr: CDevice_SetStateUP left to the host renderer");
    ::kernel::Return(ctx, 0, 0);
}

// RunPushBuffer(push buffer, fixup in eax) 0x190610: a recorded or prebuilt (.xpb) push buffer the console jumps to.
// Its commands are copied into the device's push buffer as if the game had pushed them, with the fixup's patches
// applied to the copy, so the host's push walk runs them (XBE_PUSH_LOG lists what it does not). The push buffer:
// +4 its data, +0xC its size (the last dword left for the return); the fixup: +4 its data, +0xC where its entries
// start (each: a size in bytes, an offset into the push buffer, the bytes; a size of 0xFFFFFFFF ends them).
namespace {
constexpr uint32_t kDevicePtr = 0x19E01C, kMakeRequestedSpace = 0x190C40;
}
X86_FUNC(sub_00190610) {
    (void)base;
    uint32_t pb = ::kernel::R32(ctx.esp + 4), fixup = ctx.eax;
    uint32_t data = pb ? ::kernel::R32(pb + 4) : 0, size = pb ? ::kernel::R32(pb + 0xC) : 0;
    static int calls = 0;
    if (calls++ < 4) LOGI("fotr: RunPushBuffer %08X: %u bytes at %08X, fixup %08X", pb, size, data, fixup);
    if (data && size > 4 && size < (16u << 20)) {
        uint32_t n = size - 4;
        uint32_t at = ::kernel::CallGuest(ctx, kMakeRequestedSpace, {n + 64, n + 64});
        uint32_t dev = ::kernel::R32(kDevicePtr);
        if (at && dev) {
            std::memcpy(::kernel::HostPtr(at), ::kernel::HostPtr(data), n);
            if (fixup) {
                uint32_t p = ::kernel::R32(fixup + 4) + ::kernel::R32(fixup + 0xC);
                for (int guard = 0; guard < 100000; guard++) {
                    uint32_t bytes = ::kernel::R32(p), off = ::kernel::R32(p + 4);
                    if (bytes == 0xFFFFFFFF || off + bytes > n) break;
                    std::memcpy(::kernel::HostPtr(at + off), ::kernel::HostPtr(p + 8), bytes);
                    p += 8 + bytes;
                }
            }
            ::kernel::W32(dev, at + n);
        }
    }
    ::kernel::Return(ctx, 0, 1);
}
