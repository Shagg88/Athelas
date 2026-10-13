// ROTK Xbox: host versions of XDK 5558 D3D calls the SDK does not replace.
#include <cstdint>

#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using kernel::R32;
using kernel::W32;

// (push buffer, fixup): the game's draw-list player 0xBD462 runs the meshes' push buffers. Interpreted by
// rotk_pushbuffer.cpp once their contents are known; for now it only reports what it is given.
HLE_STDCALL(D3DDevice_RunPushBuffer, 2) {
    LOG_FIRST(8, "rotk: RunPushBuffer %08X fixup %08X (data %08X size %08X)", a[0], a[1], a[0] ? R32(a[0] + 4) : 0,
              a[0] ? R32(a[0] + 8) : 0);
    return 0;
}
HLE_STDCALL(D3DDevice_KickPushBuffer, 0) { return 0; }

// (type, callback, context): run when the GPU reaches it. Nothing runs behind the host renderer, so it runs now.
HLE_STDCALL(D3DDevice_InsertCallback, 3) {
    if (a[1]) ::kernel::CallGuest(ctx, a[1], {a[2]});  // void __cdecl callback(DWORD context)
    return 0;
}

// (D3DPIXELSHADERDEF*): a shader set straight from its definition (as in The Two Towers' port)
HLE_STDCALL(D3DDevice_SetPixelShaderProgram, 1) {
    static uint32_t holder = 0;
    if (!a[0]) return ::kernel::CallGuest(ctx, SYM(D3DDevice_SetPixelShader), {0u});
    if (!holder) holder = mem::AllocSystem(16);
    W32(holder + XDK_PixelShader_Definition, a[0]);
    return ::kernel::CallGuest(ctx, SYM(D3DDevice_SetPixelShader), {holder});
}
