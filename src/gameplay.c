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
 * - 0x482530(hero, level, co-op): the character select's question about each hero (0 Aragorn ... 8 Faramir; the
 *   level an index, 0 Helm's Deep to 12 the Crack of Doom): 0 not offered, 2 to be chosen, 1 otherwise. The Xbox
 *   0x9E870, the same code. AllHeroes makes it 2 for every hero in a level some hero has completed (the profile in
 *   play, SLOT in achieve.c: +0x244 + hero * 0x3C, a bit a level, alone and at +4 in co-op), as the game does once it
 *   is beaten. Helm's Deep stays Gandalf's, the levels with no co-op (9 to 12) stay solo, and a level not completed
 *   yet keeps the game's own answer. Gimli is never offered Helm's Deep (on Xbox it crashes in its opening).
 *
 * All four are off (100, 100, 0, 0) by default. */
#include "rotkfix.h"

static int g_damageTaken = 100, g_enemyDamage = 100, g_unlockAll = 0, g_allHeroes = 0;

static int clamp_pct(int v) { return v < 10 ? 10 : v > 500 ? 500 : v; }

void gameplay_load_settings(const char *ini)
{
    g_damageTaken = clamp_pct(GetPrivateProfileIntA("Gameplay", "DamageTaken", 100, ini));
    g_enemyDamage = clamp_pct(GetPrivateProfileIntA("Gameplay", "EnemyDamage", 100, ini));
    g_unlockAll = GetPrivateProfileIntA("Gameplay", "UnlockAll", 0, ini) != 0;
    g_allHeroes = GetPrivateProfileIntA("Gameplay", "AllHeroes", 0, ini) != 0;
}

#define ADDR_HIT    0x0040CF80u
#define ADDR_UNLOCK 0x004F6F60u
#define ADDR_HEROES 0x00482530u
#define HERO_SLOT   (0x5BCB08u + 2u * 0x988u)   /* the profile in play, as achieve.c */
/* call 0x4F6F60; test al,al; je +6; mov eax,2; ret - the hero question's start (the call is the 5 bytes replaced) */
static const BYTE SIG_HEROES[15] = { 0xE8, 0x2B, 0x4A, 0x07, 0x00, 0x84, 0xC0, 0x74, 0x06, 0xB8, 0x02, 0x00, 0x00, 0x00, 0xC3 };
/* sub esp,8; push esi; mov esi,[esp+10h] - the 8 bytes the jump replaces; then the player test at +0x1A */
static const BYTE SIG_HIT[8] = { 0x83, 0xEC, 0x08, 0x56, 0x8B, 0x74, 0x24, 0x10 };
static const BYTE SIG_HIT_PLAYER[5] = { 0x66, 0x83, 0x7E, 0x7A, 0x63 };   /* cmp word [esi+7Ah], 63h */
/* mov al,[0x8E5780]; ret */
static const BYTE SIG_UNLOCK[6] = { 0xA0, 0x80, 0x57, 0x8E, 0x00, 0xC3 };

typedef int (__cdecl *Hit_t)(void *who, int amount, void *by);
static Hit_t g_realHit;

#define MAX_HIT_FILTERS 16
static RF_HitFilter_t g_hitFilters[MAX_HIT_FILTERS];
static int g_hitFilterCount = 0;

void RF_RegisterHitFilter(RF_HitFilter_t fn)
{
    if (fn && g_hitFilterCount < MAX_HIT_FILTERS) {
        g_hitFilters[g_hitFilterCount++] = fn;
        logf_("gameplay: registered external hit filter #%d (%p)", g_hitFilterCount, fn);
    }
}

int RF_IsPlayer(const void *entity)
{
    const BYTE *c = (const BYTE *)entity;
    return c && readable(c + 0x7A, 2) && *(const WORD *)(c + 0x7A) == 0x63;
}

int RF_DealHit(void *who, int amount, void *by)
{
    return g_realHit ? g_realHit(who, amount, by) : 0;
}

void RF_ShowNotification(const char *title, const char *desc)
{
    achieve_push_note(title, desc);
}

void RF_ShowNotificationEx(const char *category, const char *title, const char *desc)
{
    achieve_push_note_ex(category, title, desc);
}

#define MAX_FRAME_HOOKS 16
static RF_FrameHook_t g_frameHooks[MAX_FRAME_HOOKS];
static int g_frameHookCount = 0;

void RF_RegisterFrameHook(RF_FrameHook_t fn)
{
    if (fn && g_frameHookCount < MAX_FRAME_HOOKS) {
        g_frameHooks[g_frameHookCount++] = fn;
        logf_("gameplay: registered external frame hook #%d (%p)", g_frameHookCount, fn);
    }
}

void RF_RunFrameHooks(void)
{
    for (int i = 0; i < g_frameHookCount; i++) {
        if (g_frameHooks[i]) g_frameHooks[i]();
    }
}

#define MAX_RENDER_HOOKS 16
static RF_RenderHook_t g_renderHooks[MAX_RENDER_HOOKS];
static int g_renderHookCount = 0;
static RF_RenderHook_t g_resetHooks[MAX_RENDER_HOOKS];
static int g_resetHookCount = 0;

void RF_RegisterRenderHook(RF_RenderHook_t fn)
{
    if (fn && g_renderHookCount < MAX_RENDER_HOOKS) {
        g_renderHooks[g_renderHookCount++] = fn;
        logf_("gameplay: registered external render hook #%d (%p)", g_renderHookCount, fn);
    }
}

void RF_RunRenderHooks(void *dev)
{
    for (int i = 0; i < g_renderHookCount; i++) {
        if (g_renderHooks[i]) g_renderHooks[i](dev);
    }
}

void RF_RegisterResetHook(RF_RenderHook_t fn)
{
    if (fn && g_resetHookCount < MAX_RENDER_HOOKS) {
        g_resetHooks[g_resetHookCount++] = fn;
        logf_("gameplay: registered external reset hook #%d (%p)", g_resetHookCount, fn);
    }
}

void RF_RunResetHooks(void *dev)
{
    for (int i = 0; i < g_resetHookCount; i++) {
        if (g_resetHooks[i]) g_resetHooks[i](dev);
    }
}

int RF_TakeKeyPress(int dik)
{
    return input_take_key_presses(dik);
}

void RF_SimulateKeyPress(int dik)
{
    input_simulate_key_press(dik);
}

int RF_IsKeyDown(int dik)
{
    return input_is_key_down(dik);
}

void RF_RegisterHotkey(int dik)
{
    input_register_hotkey(dik);
}

static int is_player(const BYTE *c)
{
    return RF_IsPlayer(c);
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
    if (counts && before > 0 && *(const int *)(w + 0x90) <= 0) {
        achieve_enemy_defeated();
    }
    return r;
}

static int __cdecl hook_hit_scaled(void *who, int amount, void *by)
{
    if (amount > 0 && who) {
        /* External mod filters (run before Athelas difficulty scaling) */
        for (int i = 0; i < g_hitFilterCount; i++) {
            if (g_hitFilters[i]) {
                if (!g_hitFilters[i](who, &amount, by)) {
                    return 0; /* filter suppressed the hit */
                }
            }
        }

        int player_taking_hit  = is_player((const BYTE *)who);
        int player_dealing_hit = is_player((const BYTE *)by);

        int pct = player_taking_hit ? g_damageTaken : player_dealing_hit ? g_enemyDamage : 100;
        if (pct != 100) {
            static volatile LONG logged[2];
            int on_player = pct == g_damageTaken && player_taking_hit;
            int was = amount;
            amount = scale(amount, pct);
            if (!InterlockedExchange(&logged[on_player], 1))
                logf_("gameplay: first %s %d scaled to %d", on_player ? "hit on a player" : "hit by a player", was, amount);
        }
    }
    return g_realHit(who, amount, by);
}

typedef int (__cdecl *Heroes_t)(int hero, int level, int coop);
static Heroes_t g_realHeroes;

static int level_completed(int level)
{
    DWORD any = 0;
    if (!readable((const void *)(DWORD_PTR)HERO_SLOT, 0x988)) return 0;
    for (int h = 0; h < 9; h++) {
        const DWORD *r = (const DWORD *)(DWORD_PTR)(HERO_SLOT + 0x244u + (DWORD)h * 0x3Cu);
        any |= r[0] | r[1];
    }
    any &= 0x1FFF;
    return level < 13 ? (int)((any >> level) & 1) : any == 0x1FFF;
}

static int __cdecl hook_heroes(int hero, int level, int coop)
{
    int r = g_realHeroes(hero, level, coop);
    if (level == 0 && hero == 2) return 0;   /* Gimli at Helm's Deep */
    if (r != 2 && g_allHeroes && hero >= 0 && hero < 9 && level > 0 && !(coop && level >= 9) && level_completed(level)) {
        static volatile LONG logged;
        if (InterlockedIncrement(&logged) <= 20) logf_("gameplay: hero %d offered for level %d (AllHeroes; the game said %d)", hero, level, r);
        return 2;
    }
    return r;
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
    {   /* AllHeroes, and Gimli kept out of Helm's Deep */
        BYTE *fn = (BYTE *)ADDR_HEROES;
        BYTE *tr = (BYTE *)code_alloc(16);
        if (!readable(fn, 15) || memcmp(fn, SIG_HEROES, 15) || !tr) {
            logf_("gameplay: character select not recognised; heroes as the game has them");
        } else {
            tr[0] = 0xE8;                                            /* call 0x4F6F60, from the trampoline */
            *(LONG *)(tr + 1) = (LONG)((DWORD_PTR)ADDR_UNLOCK - ((DWORD_PTR)tr + 5));
            tr[5] = 0xE9;                                            /* jmp back after the replaced call */
            *(LONG *)(tr + 6) = (LONG)((DWORD_PTR)(fn + 5) - ((DWORD_PTR)tr + 10));
            g_realHeroes = (Heroes_t)(void *)tr;
            BYTE jmp[5] = { 0xE9, 0, 0, 0, 0 };
            *(LONG *)(jmp + 1) = (LONG)((DWORD_PTR)&hook_heroes - ((DWORD_PTR)fn + 5));
            if (write_code(fn, jmp, 5) && g_allHeroes) logf_("gameplay: every hero offered in completed levels (AllHeroes)");
        }
    }
    if (g_unlockAll) {
        static const BYTE yes[3] = { 0xB0, 0x01, 0xC3 };            /* mov al,1; ret */
        BYTE *fn = (BYTE *)ADDR_UNLOCK;
        if (!readable(fn, 6) || memcmp(fn, SIG_UNLOCK, 6)) logf_("gameplay: unlock switch not recognised; levels stay as earned");
        else if (write_code(fn, yes, 3)) logf_("gameplay: every level and character unlocked (UnlockAll)");
    }
}
