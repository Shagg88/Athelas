// The Third Age (Xbox): the D3D8LTCG library's internals the game calls directly (XDK 5849, link-time code generated),
// where the host renderer keeps the state itself.
#include <cstdint>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "xdk/d3d8.h"

// The NV2A tile registers. D3DDevice_SetTileNoWait 0x228EE0 (index in ecx, the tile in eax; 3 callers, one the game's
// own tile set-up at 0xD8900) programs a tile through 0x23150F, and clearing one goes through 0x2316FC (5 callers):
// both take the GPU's register block from [device+0x1C28] and write PFB/PGRAPH registers (+0x100300, +0x400750...).
// The host has no such registers (job 030: a read at null+0x3214 inside 0x2317C3, the wait for the GPU to go idle).
// Tiles only arrange the console's video memory, so both report success and do nothing, as D3DDevice_SetTile does in
// the SDK for the other three games.
X86_FUNC(sub_0023150F) {   // set a tile's registers: 7 stack arguments (ret 0x1C), returns 1
    (void)base;
    LOG_FIRST(1, "ta: tile registers left to the host (0x23150F)");
    ::kernel::Return(ctx, 1, 7);
}
X86_FUNC(sub_002316FC) {   // clear a tile's registers: 1 stack argument (ret 4), returns 1
    (void)base;
    LOG_FIRST(1, "ta: tile clear left to the host (0x2316FC)");
    ::kernel::Return(ctx, 1, 1);
}

// Loads the viewport's scale and offset into the vertex shader constants (pushes 0x41EA4 / 0x200B80, the slots below
// c0) when the current vertex shader asks for them. It finds that shader at device+0x794, a field only the console's
// library fills (job 031: a read at null+4 here, from 0x22D8E0). The host renderer sets the viewport transform for
// every shader itself at each draw, so this does nothing; the device is in esi, no stack arguments.
X86_FUNC(sub_0022A530) {
    (void)base;
    LOG_FIRST(1, "ta: viewport shader constants left to the host (0x22A530)");
    ::kernel::Return(ctx, ctx.eax, 0);
}

// The lazy state flush 0x22E880 (8 callers: the draws in D3D and four in the game): with D3D_DirtyFlags 0x238940 it
// rebuilds the console library's state into the push buffer through eight helpers (0x22D200, 0x233260, 0x22D360,
// 0x22D6E0, 0x22D8E0, 0x22DAD0, 0x22E300, 0x22E670), which read fields of the device only that library fills, the
// current vertex shader at device+0x794 among them (jobs 031/032: null+4 in two of them). FOTR's CDevice_SetStateVB,
// the same flush, is left to the host renderer, which applies its state at each draw. This keeps the flush's own
// result: the flags it handles cleared, the ones it leaves (0xC0000070) kept.
X86_FUNC(sub_0022E880) {
    (void)base;
    LOG_FIRST(1, "ta: the state flush left to the host renderer (0x22E880)");
    // the transforms the game stored in the device since the last flush reach the renderer now, as they would
    // reach the GPU here on the console (the fade after Start drew with the previous ones)
    ::d3d::TakeDeviceTransforms();
    ::kernel::W32(0x238940, ::kernel::R32(0x238940) & 0xC0000070u);
    ::kernel::Return(ctx, ctx.eax, 0);
}

// A GPU register flush and wait (6 callers: D3D_BlockOnTime and CDevice_KickOff, which the host replaces, and two in
// the library's interrupt code at 0x2321C8 / 0x2321E3): sets bit 16 of PFIFO +0x100410 in the register block at
// [device+0x1C28] and spins until the GPU clears it. The host has no such registers (the read would be at
// null+0x100410), and its renderer has nothing queued to flush, so this returns at once.
X86_FUNC(sub_0022C360) {
    (void)base;
    LOG_FIRST(1, "ta: GPU register flush left to the host (0x22C360)");
    ::kernel::Return(ctx, ctx.eax, 0);
}
