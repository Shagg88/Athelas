// FOTR Xbox: [gameplay] damage_taken and enemy_damage, the damage the player takes and deals as a percent of the
// game's own.
//
// Every character takes a hit through 0xBDB10 (thiscall: ecx the character hit, one argument: the hit, a message;
// +0x04 its damage, float, which 0xBE115 takes from the character's health at +0x450). The player is the character
// the player update 0xEB360 runs for (ecx; it is also where the health cheat fills +0x450). A hit on the player is
// scaled by damage_taken; a hit from the player (the message's +0x50, the one who dealt it) by enemy_damage. The
// message is put back after the call, as the game may pass the same one to several characters (radius damage).
// FOTR_DMG_LOG=1 (test build): each hit logged (who, from whom, how much, health before and after).
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

namespace fotr {
uint32_t g_damage_taken = 100;  // [gameplay] damage_taken, percent
uint32_t g_enemy_damage = 100;  // [gameplay] enemy_damage, percent
}  // namespace fotr

namespace {
uint32_t g_player = 0;  // the character 0xEB360 last ran for

float F(uint32_t a) {
    float f;
    std::memcpy(&f, ::kernel::HostPtr(a), 4);
    return f;
}
void SetF(uint32_t a, float f) { std::memcpy(::kernel::HostPtr(a), &f, 4); }

bool Log() {
#ifndef _WIN32
    static const bool on = [] {
        const char* e = std::getenv("FOTR_DMG_LOG");
        return e && *e == '1';
    }();
    return on;
#else
    return false;
#endif
}
}  // namespace

namespace fotr {
uint32_t Player() { return g_player; }
}

// the player's update
X86_FUNC(__imp__sub_000EB360);
X86_FUNC(sub_000EB360) {
    if (ctx.ecx != g_player) {
        g_player = ctx.ecx;
        LOGI("fotr: player %08X", g_player);
    }
    __imp__sub_000EB360(ctx, base);
}

// a character hit
X86_FUNC(__imp__sub_000BDB10);
X86_FUNC(sub_000BDB10) {
    uint32_t who = ctx.ecx, msg = ::kernel::R32(ctx.esp + 4);
    uint32_t from = msg ? ::kernel::R32(msg + 0x50) : 0;
    float damage = msg ? F(msg + 4) : 0;
    uint32_t pct = 100;
    if (who && who == g_player) pct = fotr::g_damage_taken;
    else if (from && from == g_player) pct = fotr::g_enemy_damage;
    if (pct > 500) pct = 500;
    if (pct < 10) pct = 10;
    float before = who ? F(who + 0x450) : 0;
    if (msg && pct != 100) SetF(msg + 4, damage * float(pct) / 100.0f);
    __imp__sub_000BDB10(ctx, base);
    if (msg && pct != 100) SetF(msg + 4, damage);
    if (Log())
        LOGI("fotr: hit %08X%s from %08X%s: %g x %u%%, health %g -> %g", who, who == g_player ? " (player)" : "", from,
             from == g_player ? " (player)" : "", damage, pct, before, who ? F(who + 0x450) : 0.0f);
}
