// The Two Towers: host versions of XDK 4928 D3D calls the SDK does not replace.
// Argument counts are from the 4928 code's ret imm16 (see notes/04-xdk4928-support.md).
#include <cstdint>

#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using kernel::R32;
using kernel::W32;

// The game records each frame's draws into one of two push buffers (frame begin 0xE01E0, frame end 0xE0280)
// and replays the previous one a frame later. The host renderer draws as the calls arrive, so recording and
// replay become no-ops: the frame is drawn while it is "recorded" and shown at that frame's Swap, a frame
// earlier than on the console. The console's device redirect (CDevice +0x350..+0x35C) never happens.
HLE_STDCALL(D3DDevice_BeginPushBuffer, 1) {
    LOG_FIRST(1, "tt: BeginPushBuffer %08X (recording replaced by direct drawing)", a[0]);
    return 0;
}
HLE_STDCALL(D3DDevice_EndPushBuffer, 0) { return 0; }
// (push buffer, fixup)
HLE_STDCALL(D3DDevice_RunPushBuffer, 2) { return 0; }
HLE_STDCALL(D3DDevice_KickPushBuffer, 0) { return 0; }

// (type, callback, context): D3DCALLBACK_READ (0) or WRITE (1), run when the GPU reaches it. Nothing runs
// behind the host renderer, so the callback runs now. The game's GPU timer callback 0xDFFA0 stores the context
// in [0x1BB3BC], and 0xE08E3 waits for it to become 2, so it must run.
HLE_STDCALL(D3DDevice_InsertCallback, 3) {
    if (a[1]) ::kernel::CallGuest(ctx, a[1], {a[2]});  // void __cdecl callback(DWORD context)
    return 0;
}

// Visibility tests (LightGlowEffect / LensFlareEffect, drawn at 0x23890) are counted by the SDK:
// D3DDevice_*VisibilityTest in xdk/d3d8.cpp.

// (D3DPIXELSHADERDEF*): a shader set straight from its definition. The SDK's SetPixelShader accepts an object it
// did not create when +PixelShader_Definition points at a definition, and refreshes its copy when that changes.
HLE_STDCALL(D3DDevice_SetPixelShaderProgram, 1) {
    static uint32_t holder = 0;
    if (!a[0]) return ::kernel::CallGuest(ctx, SYM(D3DDevice_SetPixelShader), {0u});
    if (!holder) holder = mem::AllocSystem(16);
    W32(holder + XDK_PixelShader_Definition, a[0]);
    return ::kernel::CallGuest(ctx, SYM(D3DDevice_SetPixelShader), {holder});
}
