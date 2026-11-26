// The Third Age: [gameplay] damage_taken, enemy_damage, unlock_all and [cheats] invulnerable, infinite_ap, as the other
// ports have them (static research 2026-10-29).
//
// Every HP change goes through 0x1F4E30 (eax: the source, edx: the target, ecx: the signed change, negative for
// damage; [esp+4]: a byte, "kill"). The party is the roster (count [0x3197D8], actor pointers at 0x31A374 + i*0x20);
// any other target is an enemy. AP changes go through 0x1EEC40 (esi: the actor, edi: the signed change). Evil Mode's
// lock is the save list's check at 0x1EAD68 (jne to 0x1EAD96 when the save has earned it); with unlock_all the jump is
// always taken, so nothing in the save is changed.
#include <atomic>
#include <cstdint>

#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"

namespace ta {
uint32_t g_damage_percent = 100;        // [gameplay] damage_taken
uint32_t g_enemy_damage_percent = 100;  // [gameplay] enemy_damage
bool g_unlock_all = false;              // [gameplay] unlock_all
bool g_invulnerable = false;            // [cheats] invulnerable
bool g_infinite_ap = false;             // [cheats] infinite_ap
}

namespace ta {
void EnemyDefeated();  // ta_achievements.cpp
void BattleBlow(uint32_t target, bool party, uint32_t source, bool by_party, bool kill);  // ta_battle.cpp
void BattleAp();                                                                          // ta_battle.cpp
}

namespace {
bool InParty(uint32_t actor) {
    if (!actor) return false;
    uint32_t n = ::kernel::R32(0x3197D8);
    if (n > 16) n = 16;
    for (uint32_t i = 0; i < n; i++)
        if (::kernel::R32(0x31A374 + i * 0x20) == actor) return true;
    return false;
}
int32_t Scale(int32_t v, uint32_t pct) {
    if (pct == 100 || v >= 0) return v;
    int64_t r = int64_t(v) * pct / 100;
    if (r == 0 && pct) r = -1;  // a hit still lands
    return int32_t(r);
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_001F4E30);
namespace {
// Logging Detailed: each blow, the target's HP before and after (#118: with damage_taken or enemy_damage away from
// 100%, an enemy got up from what looked like a killing blow, a hero staggered and then fell; the log shows which
// blows change their outcome)
void TraceBlow(uint32_t target, bool party, int32_t hp, int32_t raw, int32_t sent, uint8_t kill) {
    LOGI("ta: blow to %s %08X: hp %d, change %d (sent %d), kill byte %u, hp after %d", party ? "hero" : "enemy", target, hp,
         raw, sent, kill, int32_t(::kernel::R32(target + 0x8C)));
}
}  // namespace

X86_FUNC(sub_001F4E30) {
    int32_t change = int32_t(ctx.ecx);
    if (change < 0) {
        const uint32_t target = ctx.edx;
        bool party = InParty(target);
        const int32_t hp = int32_t(::kernel::R32(target + 0x8C));  // before this blow
        const uint8_t kill = ::kernel::G<uint8_t>(ctx.esp + 4);
        // [gameplay] battle_speed: blows tell when a battle is on (read before invulnerable clears the kill byte)
        ta::BattleBlow(target, party, ctx.eax, InParty(ctx.eax), kill != 0);
        if (party && ta::g_invulnerable) {
            ctx.ecx = 0;
            ::kernel::G<uint8_t>(ctx.esp + 4) = 0;  // no "kill" either
        } else {
            // the Journey's Virtues and Fate (ATHELAS_VIRTUES, 100 each when unset) on top of the damage settings
            uint32_t pct = party ? ta::g_damage_percent * ach::VirtueTaken() / 100
                                 : ta::g_enemy_damage_percent * ach::VirtueDealt() / 100;
            pct = pct < 10 ? 10 : pct > 500 ? 500 : pct;
            // the caller already decided whether this blow kills (the byte at esp+4): a killing blow is never made
            // smaller, or the target would be "killed" with HP left
            if (!(kill && pct < 100)) {
                int32_t scaled = Scale(change, pct);
                // and the game chose the blow's animation from the damage as it was (#118): a blow that would fell the
                // target still fells it when made smaller (an enemy got up from its death fall), and one it would
                // survive still leaves 1 HP when made bigger (a hero staggered, then fell). The settings change how
                // many blows a fight takes, not how one ends. (A shield's absorbing, done after this, only lowers the
                // damage, so the outcome stays the game's own.)
                if (hp > 0 && pct != 100) {
                    bool falls = int64_t(hp) + change <= 0;
                    // a felling blow made smaller is sent as it was (not just -hp: a shield, taken off after this,
                    // could then leave HP; the game's own 100% outcome either way)
                    if (falls && scaled > change) scaled = change;
                    else if (!falls && scaled <= -hp) scaled = -(hp - 1);
                }
                ctx.ecx = uint32_t(scaled);
            }
        }
        const int32_t sent = int32_t(ctx.ecx);
        // an enemy the party felled: its HP going from above 0 to 0 (the kill byte forces 0, so a marked kill is
        // counted too; once per enemy, not again for a later blow to one already down)
        const bool by_party = !party && InParty(ctx.eax);
        __imp__sub_001F4E30(ctx, base);
        if (by_party && hp > 0 && int32_t(::kernel::R32(target + 0x8C)) <= 0) ta::EnemyDefeated();
        if (logx::GetLevel() >= 1) TraceBlow(target, party, hp, change, sent, kill);
        return;
    }
    __imp__sub_001F4E30(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_001EEC40);
X86_FUNC(sub_001EEC40) {
    if (int32_t(ctx.edi) < 0 && InParty(ctx.esi)) ta::BattleAp();  // [gameplay] battle_speed: still fighting
    if (ta::g_infinite_ap && int32_t(ctx.edi) < 0 && InParty(ctx.esi)) {
        uint32_t saved = ctx.edi;
        ctx.edi = 0;
        __imp__sub_001EEC40(ctx, base);
        ctx.edi = saved;
        return;
    }
    __imp__sub_001EEC40(ctx, base);
}

// [[hook]] 0x1EAD68: true takes the "Evil Mode earned" jump
bool TA_EvilUnlocked(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    (void)base;
    if (!ta::g_unlock_all) return false;  // the game's own jne runs as it is
    static std::atomic<int> logged{0};
    if (!logged++) LOGR("ta: unlock_all: Evil Mode open for every save");
    return true;
}
