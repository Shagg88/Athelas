/* ROTK Fix - [Gameplay]: damage taken, damage enemies take, every level and character open.
 *
 * The Return of the King keeps these in two small functions, the same on PC and Xbox (the Xbox port does the same
 * through its own hooks, see the ROTK Xbox port's rotk_secrets.cpp):
 *
 * - 0x40CF80(character, amount, attacker): every hit. A player (word +0x7A = 0x63) takes the amount through the
 *   difficulty table's multipliers (0x578F48.., loaded from the "DIFF" table by 0x40C6E0); anyone else loses it from
 *   health (+0x90) as it is. Its first instructions jump here: DamageTaken scales hits on a player, EnemyDamage the
 *   hits a player lands on anyone else. Hits between other characters (allies and enemies fight each other) are
 *   left alone. A hit that did damage still does at least 1.
 * - 0x4F6F60: returns the developer "unlock all" byte (0x8E5780), which the level map and character select ask.
 *   UnlockAll makes it answer 1: every level (the secret one too) and character can be chosen.
 *
 * All three are off (100, 100, 0) by default. */
#include "rotkfix.h"
#include <stdio.h>
#include <string.h>

static int g_damageTaken = 100, g_enemyDamage = 100, g_unlockAll = 0;

static int clamp_pct(int v) { return v < 10 ? 10 : v > 500 ? 500 : v; }

void gameplay_load_settings(const char *ini)
{
    g_damageTaken = clamp_pct(GetPrivateProfileIntA("Gameplay", "DamageTaken", 100, ini));
    g_enemyDamage = clamp_pct(GetPrivateProfileIntA("Gameplay", "EnemyDamage", 100, ini));
    g_unlockAll = GetPrivateProfileIntA("Gameplay", "UnlockAll", 0, ini) != 0;
}

#define ADDR_HIT    0x0040CF80u
#define ADDR_UNLOCK 0x004F6F60u
/* sub esp,8; push esi; mov esi,[esp+10h] - the 8 bytes the jump replaces; then the player test at +0x1A */
static const BYTE SIG_HIT[8] = { 0x83, 0xEC, 0x08, 0x56, 0x8B, 0x74, 0x24, 0x10 };
static const BYTE SIG_HIT_PLAYER[5] = { 0x66, 0x83, 0x7E, 0x7A, 0x63 };   /* cmp word [esi+7Ah], 63h */
/* mov al,[0x8E5780]; ret */
static const BYTE SIG_UNLOCK[6] = { 0xA0, 0x80, 0x57, 0x8E, 0x00, 0xC3 };

typedef int (__cdecl *Hit_t)(void *who, int amount, void *by);
static Hit_t g_realHit;

static int is_player(const BYTE *c)
{
    return c && readable(c + 0x7A, 2) && *(const WORD *)(c + 0x7A) == 0x63;
}

static int scale(int amount, int pct)
{
    long long v = ((long long)amount * pct + 50) / 100;
    return v < 1 ? 1 : (int)v;
}

static int __cdecl hook_hit_scaled(void *who, int amount, void *by);
static int __cdecl hook_hit(void *who, int amount, void *by)
{
    /* a player's hit that takes an enemy's health (+0x90) to 0: one defeated (achieve.c) */
    const BYTE *w = (const BYTE *)who;
    int counts = w && !is_player(w) && is_player((const BYTE *)by) && readable(w + 0x90, 4);
    int before = counts ? *(const int *)(w + 0x90) : 0;
    int r = hook_hit_scaled(who, amount, by);
    if (counts && before > 0 && *(const int *)(w + 0x90) <= 0) achieve_enemy_defeated();
    return r;
}

static int __cdecl hook_hit_scaled(void *who, int amount, void *by)
{
    if (amount > 0 && who) {
        int pct = is_player((const BYTE *)who) ? g_damageTaken : is_player((const BYTE *)by) ? g_enemyDamage : 100;
        if (pct != 100) {
            static volatile LONG logged[2];
            int on_player = pct == g_damageTaken && is_player((const BYTE *)who);
            int was = amount;
            amount = scale(amount, pct);
            if (!InterlockedExchange(&logged[on_player], 1))
                logf_("gameplay: first %s %d scaled to %d", on_player ? "hit on a player" : "hit by a player", was, amount);
        }
    }
    return g_realHit(who, amount, by);
}

void gameplay_patch(void)
{
    {   /* always: enemies defeated count for the achievements; the damage settings at 100 change nothing */
        BYTE *fn = (BYTE *)ADDR_HIT;
        BYTE *tr = (BYTE *)code_alloc(16);
        if (!readable(fn, 0x20) || memcmp(fn, SIG_HIT, 8) || memcmp(fn + 0x1A, SIG_HIT_PLAYER, 5) || !tr) {
            logf_("gameplay: damage code not recognised; damage left as the game's");
        } else {
            memcpy(tr, SIG_HIT, 8);                                  /* the original first instructions */
            tr[8] = 0xE9;                                            /* jmp back into the function */
            *(LONG *)(tr + 9) = (LONG)((DWORD_PTR)(fn + 8) - ((DWORD_PTR)tr + 13));
            g_realHit = (Hit_t)(void *)tr;
            BYTE jmp[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
            *(LONG *)(jmp + 1) = (LONG)((DWORD_PTR)&hook_hit - ((DWORD_PTR)fn + 5));
            if (write_code(fn, jmp, 8)) {
                if (g_damageTaken != 100 || g_enemyDamage != 100)
                    logf_("gameplay: damage taken %d%%, damage enemies take %d%%", g_damageTaken, g_enemyDamage);
                else logf_("gameplay: hits followed for the achievements (damage as the game's)");
            }
        }
    }
    if (g_unlockAll) {
        static const BYTE yes[3] = { 0xB0, 0x01, 0xC3 };            /* mov al,1; ret */
        BYTE *fn = (BYTE *)ADDR_UNLOCK;
        if (!readable(fn, 6) || memcmp(fn, SIG_UNLOCK, 6)) logf_("gameplay: unlock switch not recognised; levels stay as earned");
        else if (write_code(fn, yes, 3)) logf_("gameplay: every level and character unlocked (UnlockAll)");
    }
}
