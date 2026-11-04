// Pvp.cpp - versus mode (player against player, 2 to 4 players)
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// Hits. Melee attacks look for targets with 0x40EF50(attacker, centre, radius): the "sweep and prune" collision system
// (0x4795F0 -> 0x479CA0, volumes in 0x5BB9E4, stride 0xE0: +0 shape, +2 type, +3 state, +4 check function, +0x10 -> owner)
// with the check function 0x40EDC0 (the side filter, constant at 0x40EF95). The three hit shapes (0x415070, 0x4152C0,
// 0x4154F0) keep only volumes of type 5 (hittable) and 0x4185C0 sends the victim the event 0x259. While another player is
// the target, the search receives the other player's volumes of type 1 (body, already accepted by 0x40EDC0), 2, 3 and 6,
// never type 5 (same state 0x84 and same shape 1 as enemies, and the shape test table 0x5BB980 allows it: the reason is
// still unknown). So in versus mode the three functions count as type 5 the body volume (type 1) of another player
// (PvpVolType, replacing "mov r8,[vol+2]; cmp r8,5" at 0x415274, 0x415474, 0x4157A6).
//
// Rules: every player has [PvP] Lives lives (default 2). On entering the "die" state (0x28, the player states table is at
// 0x561420: +8 update 0x434D80, +0xC enter 0x434ED0, +0x10 leave) a death is counted; while lives remain the co-op's shared
// lives (0x579B28) are kept above zero so the game respawns the character; whoever loses the last life is eliminated and
// stays on the ground (the update of its "die" state is skipped after a few seconds: no respawn and no defeat of the
// game); the mode ends when one player is left.
#include "ROTKPlus.h"

extern uint32_t g_players[8];       // Coop.cpp
void RequestEndLevel();             // Support.cpp
void HudBanner(const char* text, DWORD ms);   // Hud.cpp

static bool g_allowed;   // [PvP] Enabled: the Versus menu entry and the patches are installed
static bool g_pvp;       // versus game in progress: turned on by confirming Versus in the main menu (Screens.cpp)
static int  g_lives = 2;

bool PvpAllowed() { return g_allowed; }
bool PvpActive() { return g_pvp; }
void PvpSetActive(bool on)
{
    if (on && !g_allowed) return;
    if (on != g_pvp) Log("pvp: versus mode %s", on ? "on" : "off");
    g_pvp = on;
}

static bool IsPlayer(uint32_t e) { return e && *(const int16_t*)(e + 0x7A) == 99; }
static int  PlayerNo(uint32_t e) { return *(const int*)(e + 0x690) + 1; }
static bool InCoop() { return *(const int*)0x562A98 == 1; }

// the player an object belongs to (0x4019E0, ECX = entity: type 0x13 through +0x130 and +0x12C, type 0xF through +0x188,
// for example a thrown spear), 0 if none. Nothing on the stack: called as __fastcall.
static uint32_t ObjectOwner(uint32_t e)
{
    return ((uint32_t(__fastcall*)(uint32_t))0x4019E0)(e);
}

// --- hits: another player's body counts as a hittable volume --------------------------------------------
// Also when the attacker is a player's object (arrows, spear), if they pass through here.
static uint8_t __cdecl PvpVolType(const uint8_t* vol, uint32_t attacker) ASM_NAME(PvpVolType);

static uint8_t __cdecl PvpVolType(const uint8_t* vol, uint32_t attacker)
{
    const uint8_t t = vol[2];
    if (!g_pvp || t != 1 || !InCoop() || !attacker) return t;
    const uint32_t shooter = IsPlayer(attacker) ? attacker : ObjectOwner(attacker);
    if (!shooter) return t;
    const uint32_t owner = **(const uint32_t* const*)(vol + 0x10);
    if (owner == shooter || owner == attacker || !IsPlayer(owner)) return t;
    static int logged;
    if (logged++ < 30)
        Log("pvp: player %d hits player %d%s", PlayerNo(shooter), PlayerNo(owner),
            shooter == attacker ? "" : " (with an object)");
    return 5;
}

// --- R2 and area magic: the other player as an enemy-side character --------------------------------------
// Radius searches 0x411890(centre, ..., function, context) call a check function for every character; those of the R2
// execution (0x435E20 from 0x435F00, which picks the victim on the ground, and 0x435F50) and of Gandalf's area magic
// (0x439E40, "GAN_ShieldSpark", which sends the victim the event 0x259) accept only whoever has bit 27 of +0x8C and the
// enemy side (bit 0 of +0x5E8 clear). In versus mode, if the candidate is another player (attacker in context[0]), for
// the duration of the call it is given those two bits and then they are put back: all of the original logic stays valid
// (on the ground, life, distance, angle, recharge of the hits).
static uint32_t AsEnemyCall(uintptr_t orig, uint32_t c, uint32_t* ctx)
{
    typedef uint32_t(__cdecl * Fn)(uint32_t, uint32_t*);
    if (!g_pvp || !InCoop() || !IsPlayer(c) || !ctx || !IsPlayer(ctx[0]) || ctx[0] == c) return GameFn((Fn)orig)(c, ctx);
    uint32_t* flags = (uint32_t*)(c + 0x8C);
    uint8_t* side = (uint8_t*)(c + 0x5E8);
    const uint32_t f0 = *flags;
    const uint8_t s0 = *side;
    *flags = f0 | (1u << 27);
    *side = s0 & ~1u;
    const uint32_t r = GameFn((Fn)orig)(c, ctx);
    *flags = (*flags & ~(1u << 27)) | (f0 & (1u << 27));
    *side = (*side & ~1u) | (s0 & 1u);
    return r;
}

static uint32_t __cdecl KillPick(uint32_t c, uint32_t* ctx) { return AsEnemyCall(0x435E20, c, ctx); }
static uint32_t __cdecl KillNear(uint32_t c, uint32_t* ctx) { return AsEnemyCall(0x435F50, c, ctx); }
static uint32_t __cdecl AreaMagic(uint32_t c, uint32_t* ctx) { return AsEnemyCall(0x439E40, c, ctx); }
// Locked-on aim (the green mark on whoever is about to be hit, used by arrows and the spear): 0x43F4A0 runs over the
// characters with 0x43F3F0, which rates them with 0x43EA10/0x43EBC0 (enemy side only) and writes the target to +0xD4. In
// long-range aiming (+0x7B0 of the attacker = 2, 0x43EBC0) the candidate's class counts (0x412A00, from the type +0x78):
// only the enemy classes pass, the heroes' types (4, 8, 0xC, 0xD, 0x1E, 0x22, 0x43, 0x44, 0x59) are excluded one by one.
// The locked target (+0xD4) is also the one of the thrown spear (+0x180, which hits only it). Here, for the duration of
// the rating, the candidate player also has the type of an orc (3).
static uint32_t __cdecl LockOn(uint32_t c, uint32_t* ctx)
{
    if (!g_pvp || !InCoop() || !IsPlayer(c) || !ctx || !IsPlayer(ctx[0]) || ctx[0] == c) return AsEnemyCall(0x43F3F0, c, ctx);
    int16_t* type = (int16_t*)(c + 0x78);
    const int16_t t0 = *type;
    *type = 3;
    const uint32_t r = AsEnemyCall(0x43F3F0, c, ctx);
    *type = t0;
    static int logged;
    if (*(const uint32_t*)(ctx[0] + 0xD4) == c && logged++ < 10)
        Log("pvp: player %d locks on player %d", PlayerNo(ctx[0]), PlayerNo(c));
    return r;
}
// area hit around the attacker (0x419020 from 0x419241: sends 0x259 to everybody in the radius; with attacker and
// candidate both players it always ended in "no"; it is Gandalf's special)
static uint32_t __cdecl AreaBlast(uint32_t c, uint32_t* ctx) { return AsEnemyCall(0x419020, c, ctx); }

// 0x43E710 (from 0x43E850, the target the player looks at and aims at, +0x824/+0x4D0, 0x43E8C0) explicitly excludes two
// players ("+0x7A of the attacker != 99 or +0x7A of the candidate != 99"): for the duration of the call the candidate also
// has +0x7A different from 99 (besides bit 27 and the enemy side)
static uint32_t __cdecl AimPick(uint32_t c, uint32_t* ctx)
{
    if (!g_pvp || !InCoop() || !IsPlayer(c) || !ctx || !IsPlayer(ctx[0]) || ctx[0] == c) return GameFn((uint32_t(__cdecl*)(uint32_t, uint32_t*))0x43E710)(c, ctx);
    int16_t* kind = (int16_t*)(c + 0x7A);
    uint32_t* flags = (uint32_t*)(c + 0x8C);
    uint8_t* side = (uint8_t*)(c + 0x5E8);
    const int16_t k0 = *kind;
    const uint32_t f0 = *flags;
    const uint8_t s0 = *side;
    *kind = 98;
    *flags = f0 | (1u << 27);
    *side = s0 & ~1u;
    const uint32_t r = GameFn((uint32_t(__cdecl*)(uint32_t, uint32_t*))0x43E710)(c, ctx);
    *kind = k0;
    *flags = (*flags & ~(1u << 27)) | (f0 & (1u << 27));
    *side = (*side & ~1u) | (s0 & 1u);
    return r;
}

// --- projectiles (arrows, Gandalf's special and the like) -------------------------------------------------
// Projectiles thrown by a player hit enemies through the collision loop 0x479CA0 -> the projectile's contact function ->
// 0x4547C0(victim; ESI = projectile, thrower at +0x130, damage doubled for Gandalf) -> event 0x259. Before it, at 0x454C12
// (EBP = projectile, EAX = thrower, EDI = victim, CX = +0x7A of the victim, BX = 99) come the side rules: with a player
// as victim it goes to 0x454C55, which asks for bit 27 of the thrower (players do not have it) and does not hit. In versus
// mode, at 0x454C39 ("cmp cx,bx; je 0x454C55"), if thrower and victim are different players the code jumps to the hit
// (0x454C78).
static int __cdecl ProjPvp(uint32_t shooter, uint32_t victim) ASM_NAME(ProjPvp);

static int __cdecl ProjPvp(uint32_t shooter, uint32_t victim)
{
    if (!g_pvp || !InCoop() || !IsPlayer(shooter) || !IsPlayer(victim) || shooter == victim) return 0;
    static int logged;
    if (logged++ < 30) Log("pvp: the projectile of player %d hits player %d", PlayerNo(shooter), PlayerNo(victim));
    return 1;
}

// Arrows and spears did not even reach 0x454C12: the projectile's volume has the check function 0x4543A0 (set at
// 0x454EF5/0x454F21 together with the contact 0x454940): it accepts everything except friendly fire, i.e. when the
// projectile has +0x148 > 0 and both the thrower (+0x130) and the candidate are "friends" (players, or bit 27 with bit 0
// of +0x5E8). In versus mode, if it refuses another player for that reason, it is accepted.
static uint32_t __cdecl ProjFilter(uint8_t* pv, uint8_t* cv)
{
    const uint32_t r = ((uint32_t(__cdecl*)(uint8_t*, uint8_t*))0x4543A0)(pv, cv);
    if ((r & 0xFF) || !g_pvp || !InCoop() || !pv || !cv || cv[1] != 0) return r;
    const uint32_t proj = **(const uint32_t* const*)(pv + 0x10);
    const uint32_t cand = **(const uint32_t* const*)(cv + 0x10);
    if (!proj || !cand || cand == proj) return r;
    const uint32_t shooter = *(const uint32_t*)(proj + 0x130);
    if (!IsPlayer(shooter) || !IsPlayer(cand) || cand == shooter) return r;
    if (*(const uint32_t*)(proj + 500) >= *(const uint32_t*)0x99CE48 || *(const uint32_t*)(proj + 0x1DC) ||
        *(const uint32_t*)(proj + 0xD0)) return r;
    if (!((char(__cdecl*)(uint8_t*, uint8_t*))0x412E30)(pv, cv)) return r;
    return (r & 0xFFFFFF00) | 1;
}

static NAKED void ProjStub()
{
    ASM_BEGIN
    ASM(cmp  cx, bx)
    ASM(jne  ProjStub_notPlayer)
    ASM(push eax)
    ASM(push ecx)
    ASM(push edx)
    ASM(push edi)                    // victim
    ASM(push eax)                    // thrower
    ASM(call ProjPvp)
    ASM(add  esp, 8)
    ASM(test al, al)
    ASM(pop  edx)
    ASM(pop  ecx)
    ASM(pop  eax)
    ASM(jnz  ProjStub_hit)
    ASM(push 0x454C55)
    ASM(ret)
ASM(ProjStub_hit:)
    ASM(push 0x454C78)
    ASM(ret)
ASM(ProjStub_notPlayer:)
    ASM(push 0x454C3E)
    ASM(ret)
    ASM_END
}

// --- spear against a player ------------------------------------------------------------------------------------
// The spear in flight (an object with check function 0x4524C0 and contact 0x452FE0, set with 0x40FCB0 at 0x4534B4) hits
// only the target locked at the throw (+0x180; +0x178 = in flight; thrower at +0x188). 0x452FE0 hits if the target is
// not a player, has bit 27 and +0x160 > +0x164; it sends the event 0x259 with {thrower, 0x12, 1000, -1, spear}. With the
// lock-on the spear went through the other player. In versus mode, for a player hit by another player's spear the hit
// is done here with half of the maximum life (like R2: 1000 would be too much) and the spear falls (0x452530(spear, 4),
// as when it bounces).
static void __cdecl SpearContact(uint8_t* sv, uint8_t* cv)
{
    typedef void(__cdecl * Fn)(uint8_t*, uint8_t*);
    if (g_pvp && InCoop() && sv && cv && cv[1] == 0 && (cv[2] == 5 || cv[2] == 1)) {
        const uint32_t spear = **(const uint32_t* const*)(sv + 0x10);
        const uint32_t cand = **(const uint32_t* const*)(cv + 0x10);
        const uint32_t thrower = spear ? *(const uint32_t*)(spear + 0x188) : 0;
        if (spear && IsPlayer(cand) && IsPlayer(thrower) && cand != thrower && *(const uint8_t*)(spear + 0x178) &&
            cand == *(const uint32_t*)(spear + 0x180) && *(const float*)(spear + 0x160) > *(const float*)(spear + 0x164)) {
            const int maxHp = *(const int16_t*)(*(const uint32_t*)(cand + 0xD0) + 0x48);
            int data[5] = { (int)thrower, 0x12, maxHp / 2 > 0 ? maxHp / 2 : 1, -1, (int)spear };
            Log("pvp: the spear of player %d hits player %d, damage %d", PlayerNo(thrower), PlayerNo(cand), data[2]);
            ((uint32_t(__cdecl*)(uint32_t, int, int*, char*, uint32_t))0x401780)(cand, 0x259, data, nullptr, 0);
            *(uint8_t*)(spear + 0x178) = 0;
            ((void(__cdecl*)(uint32_t, int))0x452530)(spear, 4);
            *(uint32_t*)(spear + 0x184) = 0;
            return;
        }
    }
    GameFn((Fn)0x452FE0)(sv, cv);
}

// --- R2 against a player: half of the maximum life instead of the 10000 hit -------------------------------------
// The "kill" state (0x43C410) sends the victim 0x401780(victim, 0x259, data, &result, attacker) at 0x43C697 with
// data = {attacker, 0xD, 10000, 1, 0}: a player died at once. In versus mode the damage becomes half of the maximum life
// (+0xD0 -> +0x48); it still goes through the difficulty multipliers of 0x40CF80. Which field holds the damage varies
// (integer or float), so the first value that is 10000 is replaced.
static uint32_t __cdecl KillSend(uint32_t victim, int ev, int* data, char* result, uint32_t attacker)
{
    if (g_pvp && InCoop() && IsPlayer(victim) && IsPlayer(attacker) && data) {
        const int maxHp = *(const int16_t*)(*(const uint32_t*)(victim + 0xD0) + 0x48);
        const int half = maxHp / 2 > 0 ? maxHp / 2 : 1;
        for (int i = 0; i < 8; ++i) {
            if (data[i] >= 9999 && data[i] <= 10000) { data[i] = half; Log("pvp: execution, damage (int %d) -> %d", i, half); break; }
            const float f = *(const float*)&data[i];
            if (f >= 9999.0f && f <= 10000.0f) { *(float*)&data[i] = (float)half; Log("pvp: execution, damage (float %d) -> %d", i, half); break; }
        }
    }
    return ((uint32_t(__cdecl*)(uint32_t, int, int*, char*, uint32_t))0x401780)(victim, ev, data, result, attacker);
}

// 0x415274 (8A 51 02 80 FA 05: mov dl,[ecx+2]; cmp dl,5), attacker at [ebp+8]
static NAKED void TypeStubA()
{
    ASM_BEGIN
    ASM(push eax)
    ASM(push ecx)
    ASM(push edx)
    ASM(push dword ptr [ebp + 8])
    ASM(push ecx)
    ASM(call PvpVolType)
    ASM(add  esp, 8)
    ASM(mov  byte ptr [esp], al)     // saved dl
    ASM(pop  edx)
    ASM(pop  ecx)
    ASM(pop  eax)
    ASM(cmp  dl, 5)
    ASM(ret)
    ASM_END
}

// 0x415474 (8A 48 02 80 F9 05: mov cl,[eax+2]; cmp cl,5)
static NAKED void TypeStubB()
{
    ASM_BEGIN
    ASM(push eax)
    ASM(push ecx)
    ASM(push edx)
    ASM(push dword ptr [ebp + 8])
    ASM(push eax)
    ASM(call PvpVolType)
    ASM(add  esp, 8)
    ASM(mov  byte ptr [esp + 4], al) // saved cl
    ASM(pop  edx)
    ASM(pop  ecx)
    ASM(pop  eax)
    ASM(cmp  cl, 5)
    ASM(ret)
    ASM_END
}

// 0x4157A6 (8A 41 02 3C 05: mov al,[ecx+2]; cmp al,5)
static NAKED void TypeStubC()
{
    ASM_BEGIN
    ASM(push eax)
    ASM(push ecx)
    ASM(push edx)
    ASM(push dword ptr [ebp + 8])
    ASM(push ecx)
    ASM(call PvpVolType)
    ASM(add  esp, 8)
    ASM(mov  byte ptr [esp + 8], al) // saved al
    ASM(pop  edx)
    ASM(pop  ecx)
    ASM(pop  eax)
    ASM(cmp  al, 5)
    ASM(ret)
    ASM_END
}

static bool PatchCallSite(uintptr_t at, const uint8_t* expect, size_t n, const void* stub)
{
    uint8_t code[8] = { 0xE8 };
    *(int32_t*)(code + 1) = (int32_t)((uintptr_t)stub - (at + 5));
    for (size_t i = 5; i < n; ++i) code[i] = 0x90;
    return Patch(at, expect, code, n);
}

// --- lives per player ----------------------------------------------------------------------------------------
// With three or four players whoever loses all their lives is eliminated and stays on the ground (the update of their
// "die" state, 0x434D80, entry +8 of the table at 0x561428, is skipped after the fall animation: no respawn and no defeat
// of the game); the mode ends when one is left. At the end a banner in the middle (HudBanner) for 6 s, then the level
// closes.
static uint32_t g_seenPlayers[4];
static int      g_deaths[4];
static bool     g_out[4];       // eliminated
static bool     g_over;
static DWORD    g_outAt[4];     // when it was eliminated
static DWORD    g_endAt;        // when to close the level after the winner banner (0 = no)

static void ResetIfNewLevel()
{
    if (g_seenPlayers[0] == g_players[0] && g_seenPlayers[1] == g_players[1] && g_seenPlayers[2] == g_players[2] &&
        g_seenPlayers[3] == g_players[3]) return;
    for (int i = 0; i < 4; ++i) { g_seenPlayers[i] = g_players[i]; g_deaths[i] = 0; g_out[i] = false; }
    g_over = false;
    g_endAt = 0;
}

// eliminated in the versus mode (for the HUD: no respawn countdown)
bool PvpIsOut(uint32_t c)
{
    if (!g_pvp || !InCoop() || !IsPlayer(c)) return false;
    const int i = PlayerNo(c) - 1;
    return i >= 0 && i < 4 && g_out[i] && g_seenPlayers[i] == c;
}

static const char* HeroName(uint32_t c)
{
    switch (*(const int16_t*)(c + 0x78)) {
    case 8: return "Aragorn";    case 0x22: return "Legolas"; case 0xD: return "Gimli";
    case 0x1E: return "Gandalf"; case 4: return "Frodo";      case 0xC: return "Sam";
    case 0x43: return "Merry";   case 0x44: return "Pippin";  case 0x59: return "Faramir";
    default: return "?";
    }
}

// The level-up effect on the winner. Like 0x43FDB0 (level-up: the model's "JO_root" bone with 0x45ECE0, then
// 0x401650("CFX_LevelUp", bone, character, 0)). A spoken line of the hero and a close-up were not found: the voices have
// no names in clear in the executable, the defeat sequence 0x43FE40 only does screen effects (0x4CD270 colour, 0x4CD2C0,
// 0x4CD750, ...) and the cameras are tracks in the levels' data.
static void Celebrate(uint32_t c)
{
    const uint8_t* pc = *(const uint8_t* const*)(c + 0x64);
    const uint32_t model = pc && pc[0] == 1 ? *(const uint32_t*)(pc + 4) : 0;
    const uint32_t bone = ((uint32_t(__cdecl*)(uint32_t, const char*))0x45ECE0)(model, "JO_root");
    ((uint32_t(__cdecl*)(const char*, uint32_t, uint32_t, int))0x401650)("CFX_LevelUp", bone, c, 0);
}

static void __cdecl DieEnter(uint32_t x, uint32_t c, uint32_t y)
{
    if (IsPlayer(c)) PlayersOnFall(c);   // statistics of the players (Players.cpp), every mode
    if (InCoop() && IsPlayer(c)) {   // log of falls (co-op and versus)
        static int n;
        if (n++ < 200)
            Log("fall: player %d (%08X) at (%.1f, %.1f, %.1f), shared lives %d, players in the game %d", PlayerNo(c), c,
                *(const float*)(c + 0x30), *(const float*)(c + 0x34), *(const float*)(c + 0x38), *(const int*)0x579B28,
                *(const int*)0x579B2C);
    }
    if (g_pvp && InCoop() && IsPlayer(c)) {
        ResetIfNewLevel();
        const int i = PlayerNo(c) - 1;
        if (i >= 0 && i < 4 && !g_over && !g_out[i]) {
            ++g_deaths[i];
            Log("pvp: player %d fell (%d of %d)", i + 1, g_deaths[i], g_lives);
            if (g_deaths[i] >= g_lives) {
                g_out[i] = true;
                g_outAt[i] = GetTickCount();
                int left = 0, last = -1;
                for (int k = 0; k < 4; ++k)
                    if (g_players[k] && !g_out[k]) { ++left; last = k; }
                Log("pvp: player %d is eliminated, %d left", i + 1, left);
                if (left <= 1) {
                    g_over = true;
                    char text[96];
                    if (last >= 0) {
                        const std::string named = PlayersWinnerText(last, HeroName(g_players[last]));
                        lstrcpynA(text, ("@" + (named.empty() ? LangText(4, last + 1, HeroName(g_players[last])) : named)).c_str(),
                                  sizeof text);
                        Log("pvp: versus game over, player %d (%s) wins", last + 1, HeroName(g_players[last]));
                    } else {
                        lstrcpynA(text, ("@" + LangText(5)).c_str(), sizeof text);
                        Log("pvp: versus game over, draw");
                    }
                    PlayersPvpEnd(last);
                    if (last >= 0) Celebrate(g_players[last]);
                    HudBanner(text, 6000);
                    g_endAt = GetTickCount() + 6000;
                    if (!g_endAt) g_endAt = 1;
                }
            }
        }
        // the co-op's shared lives always above zero: the game respawns whoever falls (and does not end the game before
        // our own end of the level)
        if (*(int*)0x579B28 < 5) *(int*)0x579B28 = 5;
    }
    ((void(__cdecl*)(uint32_t, uint32_t, uint32_t))0x434ED0)(x, c, y);
}

// update of the "die" state: for an eliminated player nothing is done after the fall animation (it stays on the ground).
// It keeps playing for 4 seconds (the game's own respawn is at 10): stopping it at once left the player standing still
// with the mark above its head, and 2.2 seconds froze a slower hero halfway through the animation.
static void __cdecl DieUpdate(uint32_t x, uint32_t c, uint32_t dt)
{
    if (g_pvp && InCoop() && IsPlayer(c)) {
        const int i = PlayerNo(c) - 1;
        if (i >= 0 && i < 4 && g_out[i] && g_seenPlayers[i] == c && GetTickCount() - g_outAt[i] > 4000) return;
    }
    ((void(__cdecl*)(uint32_t, uint32_t, uint32_t))0x434D80)(x, c, dt);
}

// every frame (Pads.cpp): closes the level after the winner banner
void PvpFrame()
{
    if (!g_endAt || (int)(GetTickCount() - g_endAt) < 0) return;
    g_endAt = 0;
    RequestEndLevel();
}

void PvpInstall()
{
    g_allowed = IniInt("PvP", "Enabled", 1) != 0;
    g_lives = IniInt("PvP", "Lives", 2);
    if (g_lives < 1) g_lives = 1;
    // the "die" state of the players: log of the falls and deaths in the players' statistics also without the versus
    // mode (its own rules act only while a versus game is in progress)
    bool ok = PatchDword(0x56142C, 0x434ED0, (uint32_t)(uintptr_t)&DieEnter);
    ok &= PatchDword(0x561428, 0x434D80, (uint32_t)(uintptr_t)&DieUpdate);
    if (!g_allowed) { Log("pvp: off ([PvP] Enabled=0)"); return; }
    static const uint8_t a[] = { 0x8A, 0x51, 0x02, 0x80, 0xFA, 0x05 };
    static const uint8_t b[] = { 0x8A, 0x48, 0x02, 0x80, 0xF9, 0x05 };
    static const uint8_t c[] = { 0x8A, 0x41, 0x02, 0x3C, 0x05 };
    ok &= PatchCallSite(0x415274, a, sizeof a, (const void*)&TypeStubA);
    ok &= PatchCallSite(0x415474, b, sizeof b, (const void*)&TypeStubB);
    ok &= PatchCallSite(0x4157A6, c, sizeof c, (const void*)&TypeStubC);
    // R2 (victim on the ground) and Gandalf's area magic
    ok &= PatchDword(0x435F0D, 0x435E20, (uint32_t)(uintptr_t)&KillPick);
    ok &= PatchDword(0x4360ED, 0x435F50, (uint32_t)(uintptr_t)&KillNear);
    ok &= PatchDword(0x43A057, 0x439E40, (uint32_t)(uintptr_t)&AreaMagic);
    ok &= PatchDword(0x43B4B4, 0x439E40, (uint32_t)(uintptr_t)&AreaMagic);
    // locked-on aim of arrows and spear, damage of the R2 execution
    ok &= PatchDword(0x43F52E, 0x43F3F0, (uint32_t)(uintptr_t)&LockOn);
    ok &= PatchDword(0x41920F, 0x419020, (uint32_t)(uintptr_t)&AreaBlast);
    ok &= PatchDword(0x43E871, 0x43E710, (uint32_t)(uintptr_t)&AimPick);
    ok &= PatchCall(0x43C697, 0x401780, (const void*)&KillSend);
    static const uint8_t projOld[] = { 0x66, 0x3B, 0xCB, 0x74, 0x17 };
    ok &= PatchJmp(0x454C39, projOld, sizeof projOld, (const void*)&ProjStub);
    ok &= PatchDword(0x454EF6, 0x4543A0, (uint32_t)(uintptr_t)&ProjFilter);
    ok &= PatchDword(0x454F25, 0x4543A0, (uint32_t)(uintptr_t)&ProjFilter);
    ok &= PatchDword(0x4534B5, 0x452FE0, (uint32_t)(uintptr_t)&SpearContact);
    Log("pvp: versus mode patches (Versus entry in the main menu), %d lives each: %s", g_lives, ok ? "active" : "NOT completely installed");
}
