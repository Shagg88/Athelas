// Testing only, in the Linux test build (never in the Windows exe): FOTR_SCENE=Hobbiton (a scene folder's name, as in
// Scenes\<area>\<scene>) makes a new game start there instead of Bag End: the name the new game copies into the
// current scene (0x129CB9 loads "Bag_End" into eax for the copy 0x11E800) is replaced. Without FOTR_SCENE nothing
// changes.
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

void FOTR_StartScene(X86Context& ctx, uint8_t* base) {
    (void)base;
#ifndef _WIN32
    static const char* want = std::getenv("FOTR_SCENE");
    if (!want || !*want) return;
    static uint32_t text = 0;
    if (!text && (text = mem::AllocSystem(64, 16)) != 0) {
        std::strncpy(static_cast<char*>(kernel::HostPtr(text)), want, 63);
        LOGR("fotr: test scene %s instead of Bag_End", want);
    }
    if (text) ctx.eax = text;
#else
    (void)ctx;
#endif
}
