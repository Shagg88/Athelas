// FOTR Xbox: [gameplay] combat_test, the developers' Combat Test, left in the released game but never switched on.
//
// Bilbo's room in Rivendell (Scenes\Rivendell\Bilbos_room) asks the script flag /Global/CombatTest/Enabled; when it is
// set, a dialogue offers FRODO, ARAGORN or GANDALF (TransCombatTest, TransCombatTest1, TransCombatTest2) and takes the
// player to one of three arenas: CombatRange, CombatRange1, CombatRange2 (Scenes\Rivendell, each with its own
// enemies; their exits lead back to Bilbo's room). Gloin in the House of Elrond reads the same flag. No script of the
// 64 scenes sets it, so the arenas are never reached in play.
//
// The scripts read flags through GetVar (0xA04C0, from the script function table at 0x1E3198), which looks the name
// up with 0x121460 (eax the name, ecx the result: +0 its type, +8 its value; returns 0, or 0x80070490 when no such
// flag exists, which the script takes as off). With combat_test on, that lookup answers this one flag as the whole
// number 1 (type 0x16, as a SetVar of 1 leaves it); every other name goes to the game's own lookup.
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace fotr {
bool g_combat_test = false;  // [gameplay] combat_test
}

X86_EXTERN_FUNC(__imp__sub_00121460);
X86_FUNC(sub_00121460) {
    static const char kFlag[] = "/Global/CombatTest/Enabled";
    if (fotr::g_combat_test && ctx.eax >= 0x10000 && ctx.ecx >= 0x10000 && mem::IsCommitted(ctx.eax) &&
        mem::IsCommitted(ctx.eax + sizeof kFlag - 1) && mem::IsCommitted(ctx.ecx + 15) &&
        std::memcmp(::kernel::HostPtr(ctx.eax), kFlag, sizeof kFlag) == 0) {
        ::kernel::W32(ctx.ecx + 0, 0x16);  // a whole number
        ::kernel::W32(ctx.ecx + 8, 1);
        ctx.eax = 0;
        ctx.esp += 4;  // its own ret
        static bool logged = false;
        if (!logged) logged = true, LOGR("fotr: Combat Test on (the game asked for it)");
        return;
    }
#ifndef _WIN32
    // test builds: FOTR_TEST_FLAG=<name> answers that flag as 1 too (Bilbo's room: /Rivendell/BilbosRoom/Started, so
    // the room is entered as on a later visit, where the Combat Test is offered)
    static const char* test = std::getenv("FOTR_TEST_FLAG");
    if (test && ctx.eax >= 0x10000 && ctx.ecx >= 0x10000 && mem::IsCommitted(ctx.eax) && mem::IsCommitted(ctx.ecx + 15) &&
        std::strncmp(static_cast<const char*>(::kernel::HostPtr(ctx.eax)), test, std::strlen(test) + 1) == 0) {
        ::kernel::W32(ctx.ecx + 0, 0x16);
        ::kernel::W32(ctx.ecx + 8, 1);
        ctx.eax = 0;
        ctx.esp += 4;
        return;
    }
#endif
    __imp__sub_00121460(ctx, base);
}
