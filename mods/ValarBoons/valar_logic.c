/* Valar Boons - Combat & Gameplay Logic
 *
 * Implements boon activation/deactivation state machine, duration countdowns,
 * dynamic cooldown progression curves, group mutual exclusivity,
 * and combat hit damage filtering.
 */

#include "valar_boons.h"

float get_cooldown_duration(BoonConfig *b)
{
    if (b->cooldownCurveCount <= 0) return 0.0f;
    int idx = b->useCount;
    if (b->group > 0) {
        idx = g_groupUseCount[b->group];
    }
    if (idx >= b->cooldownCurveCount) {
        idx = b->cooldownCurveCount - 1; /* Last number is the final max wait seconds */
    }
    return b->cooldownCurve[idx];
}

void activate_boon(int idx, int viaEnableAll)
{
    if (idx < 0 || idx >= g_boonCount) return;
    BoonConfig *b = &g_boons[idx];
    DWORD now = GetTickCount();

    /* 1. Toggle off if already active and not called via EnableAll */
    if (b->active) {
        if (viaEnableAll) return; /* Keep active */

        b->active = 0;
        b->activeUntil = 0;
        float cd = get_cooldown_duration(b);
        DWORD cdMs = (DWORD)(cd * 1000.0f);
        b->cooldownUntil = now + cdMs;
        if (b->group > 0) {
            g_groupCooldownUntil[b->group] = now + cdMs;
            g_groupUseCount[b->group]++;
        }
        b->useCount++;

        valar_log("mods [ValarBoons]: Boon '%s' manually toggled OFF (Cooldown: %.1fs)", b->name, cd);
        notify_ex("Valar Boon", b->name, "Blessing withdrawn");
        return;
    }

    /* 2. Check Cooldowns */
    if (now < b->cooldownUntil) {
        float waitSec = (float)(b->cooldownUntil - now) / 1000.0f;
        valar_log("mods [ValarBoons]: Cannot activate '%s' (Boon Cooldown: %.1fs remaining)", b->name, waitSec);
        char desc[64];
        snprintf(desc, sizeof desc, "Recharging: wait %.1fs before activating again", waitSec);
        notify_ex("Cooldown", b->name, desc);
        return;
    }

    if (b->group > 0 && now < g_groupCooldownUntil[b->group]) {
        float waitSec = (float)(g_groupCooldownUntil[b->group] - now) / 1000.0f;
        valar_log("mods [ValarBoons]: Cannot activate '%s' (Group %d Cooldown: %.1fs remaining)", b->name, b->group, waitSec);
        char desc[64];
        snprintf(desc, sizeof desc, "Group Cooldown: wait %.1fs before activating", waitSec);
        notify_ex("Cooldown", b->name, desc);
        return;
    }

    /* 3. Group Mutual Exclusivity: Deactivate any other currently active boon in the same group */
    if (b->group > 0) {
        for (int i = 0; i < g_boonCount; i++) {
            if (i != idx && g_boons[i].group == b->group && g_boons[i].active) {
                g_boons[i].active = 0;
                g_boons[i].activeUntil = 0;
                valar_log("mods [ValarBoons]: Group %d mutual switch: deactivated '%s'", b->group, g_boons[i].name);
            }
        }
    }

    /* 4. Activate */
    b->active = 1;
    if (b->duration > 0.0f) {
        b->activeUntil = now + (DWORD)(b->duration * 1000.0f);
    } else {
        b->activeUntil = 0; /* Permanent toggle */
    }

    float cd = get_cooldown_duration(b);
    DWORD cdMs = (DWORD)(cd * 1000.0f);

    /* If boon has no duration (toggle), start cooldown upon activation */
    if (b->duration <= 0.0f) {
        b->cooldownUntil = now + cdMs;
        if (b->group > 0) {
            g_groupCooldownUntil[b->group] = now + cdMs;
            g_groupUseCount[b->group]++;
        }
        b->useCount++;
    }

    valar_log("mods [ValarBoons]: Boon '%s' ACTIVATED (Duration=%.1fs, Cooldown=%.1fs, Group=%d)",
              b->name, b->duration, cd, b->group);

    char title[64];
    snprintf(title, sizeof title, "%s [Active]", b->name);
    char desc[128];
    if (b->duration > 0.0f) {
        snprintf(desc, sizeof desc, "%s: %s (Active for %.0fs)", b->title, b->desc, b->duration);
    } else {
        snprintf(desc, sizeof desc, "%s: %s", b->title, b->desc);
    }
    notify_ex("Valar Boon", title, desc);
}

void trigger_enable_all(void)
{
    valar_log("mods [ValarBoons]: 'Enable All' triggered");
    int activatedGroups[MAX_GROUPS] = {0};

    for (int i = 0; i < g_boonCount; i++) {
        BoonConfig *b = &g_boons[i];
        if (b->group == 0) {
            activate_boon(i, 1);
        } else if (b->group > 0 && b->group < MAX_GROUPS) {
            /* If this group hasn't had a boon selected yet, select this first one of the group! */
            if (!activatedGroups[b->group]) {
                activate_boon(i, 1);
                activatedGroups[b->group] = 1;
            }
        }
    }
    notify_ex("Valar Boon", "Awakened Blessings", "Active boons awakened (Group first selected)");
}

void trigger_reset(void)
{
    valar_log("mods [ValarBoons]: 'Reset' triggered");
    for (int i = 0; i < g_boonCount; i++) {
        g_boons[i].active = 0;
        g_boons[i].activeUntil = 0;
        g_boons[i].cooldownUntil = 0;
        g_boons[i].useCount = 0;
    }
    for (int g = 0; g < MAX_GROUPS; g++) {
        g_groupCooldownUntil[g] = 0;
        g_groupUseCount[g] = 0;
    }
    notify_ex("Valar Boon", "Blessings Relinquished", "All active boons, groups, and cooldowns cleared");
}

int valar_hit_filter(void *who, int *amount, void *by)
{
    if (!who || !amount || *amount <= 0 || !g_IsPlayer) return 1;

    int player_taking_hit  = g_IsPlayer(who);
    int player_dealing_hit = by ? g_IsPlayer(by) : 0;

    /* 1. Defensive Blessings: When player takes damage */
    if (player_taking_hit) {
        int orig = *amount;
        int retributionTotal = 0;

        for (int i = 0; i < g_boonCount; i++) {
            BoonConfig *b = &g_boons[i];
            if (!b->active) continue;

            if (b->damageReduction > 0) {
                int pct = 100 - b->damageReduction;
                if (pct < 1) pct = 1;
                *amount = scale_val(*amount, pct);
                g_defendFlashUntil = GetTickCount() + 320;
            }

            if (b->maxDamageClamped > 0) {
                if (*amount > b->maxDamageClamped) {
                    *amount = b->maxDamageClamped;
                }
            }

            if (b->holyShockDamage > 0) {
                retributionTotal += b->holyShockDamage;
            }
        }

        if (retributionTotal > 0 && by && !g_IsPlayer(by) && g_DealHit) {
            g_DealHit(by, retributionTotal, who);
        }

        if (*amount != orig) {
            valar_log("mods [ValarBoons]: [DEFENSE] Hit %d dmg reduced to %d (Retribution: %d)",
                      orig, *amount, retributionTotal);
        }
    }

    /* 2. Offensive Blessings: When player attacks enemies */
    if (player_dealing_hit && !player_taking_hit) {
        int orig = *amount;

        for (int i = 0; i < g_boonCount; i++) {
            BoonConfig *b = &g_boons[i];
            if (!b->active) continue;

            if (b->damageMultiplier > 0 && b->damageMultiplier != 100) {
                *amount = scale_val(*amount, b->damageMultiplier);
                g_attackFlashUntil = GetTickCount() + 200;
            }

            if (b->flatDamage > 0) {
                *amount += b->flatDamage;
                g_attackFlashUntil = GetTickCount() + 200;
            }
        }

        if (*amount != orig) {
            valar_log("mods [ValarBoons]: [OFFENSE] Player attack %d dmg amplified to %d", orig, *amount);
        }
    }

    return 1;
}

void valar_frame_hook(void)
{
    static int s_firstFrame = 1;
    DWORD now = GetTickCount();

    if (s_firstFrame) {
        s_firstFrame = 0;
        valar_log("mods [ValarBoons]: In-engine frame hook active! %d dynamic boons configured.", g_boonCount);
        notify_ex("Mod Loaded", "Valar Boons (Blessings of Arda)", "Configurable Boons Active. Press configured hotkeys to toggle.");
    }

    /* 1. Check durations & expirations */
    for (int i = 0; i < g_boonCount; i++) {
        BoonConfig *b = &g_boons[i];
        if (b->active && b->activeUntil > 0 && now >= b->activeUntil) {
            b->active = 0;
            b->activeUntil = 0;
            float cd = get_cooldown_duration(b);
            DWORD cdMs = (DWORD)(cd * 1000.0f);
            b->cooldownUntil = now + cdMs;
            if (b->group > 0) {
                g_groupCooldownUntil[b->group] = now + cdMs;
                g_groupUseCount[b->group]++;
            }
            b->useCount++;

            valar_log("mods [ValarBoons]: Boon '%s' EXPIRED (Cooldown: %.1fs started)", b->name, cd);
            char desc[64];
            snprintf(desc, sizeof desc, "Blessing faded (Cooldown: %.0fs started)", cd);
            notify_ex("Valar Boon", b->name, desc);
        }
    }

    if (!g_TakeKeyPress) return;

    /* 2. Check Enable All */
    int pressedAll = 0;
    for (int k = 0; k < g_enableAllDikCount; k++) {
        if (g_TakeKeyPress(g_enableAllDiks[k]) > 0) { pressedAll = 1; break; }
    }
    if (pressedAll) {
        trigger_enable_all();
    }

    /* 3. Check Reset */
    int pressedReset = 0;
    for (int k = 0; k < g_resetDikCount; k++) {
        if (g_TakeKeyPress(g_resetDiks[k]) > 0) { pressedReset = 1; break; }
    }
    if (pressedReset) {
        trigger_reset();
    }

    /* 4. Check Individual Boon Keys */
    for (int i = 0; i < g_boonCount; i++) {
        int pressed = 0;
        for (int k = 0; k < g_boons[i].dikCount; k++) {
            if (g_TakeKeyPress(g_boons[i].diks[k]) > 0) { pressed = 1; break; }
        }
        if (pressed) {
            activate_boon(i, 0);
        }
    }
}
