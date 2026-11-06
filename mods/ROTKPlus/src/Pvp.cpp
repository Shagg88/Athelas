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
// game); the mode ends when one player is left, or with teams when one team is left.
//
// The Versus entry of the main menu (left/right, Screens.cpp) chooses among 2, 3 or 4 players each on their own and
// "2 vs 2", with the level's enemies or with the heroes only. In a versus game:
//  - teams ([PvP] Team1, default 12: players 1 and 2 against 3 and 4): teammates do not hit each other (every rule
//    below asks SameTeam), and the game ends when every player of one team is eliminated;
//  - heroes only: the characters of the enemy side are not created (CreateActor below), the allies are;
//  - the map offers only the three arenas ([PvP] ArenasOnly, default 1; MapNodeOpen and the rest below);
//  - a sound when a player hits another one and a music at the start, from ROTKPlus.ini ([PvP] Cheer_<level>,
//    Music_<level>: audio commands of that level, see PlayAudio).
#include "ROTKPlus.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <map>

extern uint32_t g_players[8];       // Coop.cpp
void RequestEndLevel();             // Support.cpp

static bool g_allowed;   // [PvP] Enabled: the Versus menu entry and the patches are installed
static bool g_pvp;       // versus game in progress: turned on by confirming Versus in the main menu (Screens.cpp)
static int  g_lives = 2;
static bool g_teams;       // this versus game is 2 vs 2
static bool g_heroesOnly;  // this versus game has no enemies
static bool g_arenas = true;   // [PvP] ArenasOnly: the map offers only the arenas
static int  g_team[4] = { 0, 0, 1, 1 };   // team of each player ([PvP] Team1)
static bool g_timed;       // [PvP] Rule=1: the most knock-downs in [PvP] Time minutes win, no lives
static int  g_minutes = 3;
static int  g_rounds = 1;  // [PvP] Rounds: 1, or a tournament at the best of 3 or 5 rounds

// --- the choice on the Versus entry: number of players, teams, enemies -------------------------------------------
// Right goes forward, left back, round again. Kept in ROTKPlus.ini: [PvP] Players, Teams, Enemies.
struct Choice { int players; bool teams; bool enemies; };
static const Choice kChoices[] = {
    { 2, false, true }, { 3, false, true }, { 4, false, true }, { 4, true, true },
    { 2, false, false }, { 3, false, false }, { 4, false, false }, { 4, true, false },
};
static const int kNChoices = (int)(sizeof kChoices / sizeof kChoices[0]);
static int g_choice = -1;   // index in kChoices, read from the ini at the first use

static int ChoiceIndex()
{
    if (g_choice >= 0) return g_choice;
    const int n = IniInt("PvP", "Players", 2);
    const bool teams = IniInt("PvP", "Teams", 0) != 0, enemies = IniInt("PvP", "Enemies", 1) != 0;
    g_choice = 0;
    for (int i = 0; i < kNChoices; ++i)
        if (kChoices[i].teams == teams && kChoices[i].enemies == enemies && (teams || kChoices[i].players == n)) {
            g_choice = i;
            break;
        }
    return g_choice;
}

int PvpChoicePlayers() { return kChoices[ChoiceIndex()].players; }

void PvpChoiceStep(int dir)
{
    g_choice = (ChoiceIndex() + (dir > 0 ? 1 : kNChoices - 1)) % kNChoices;
    const Choice& c = kChoices[g_choice];
    IniWriteInt("PvP", "Players", c.players);
    IniWriteInt("PvP", "Teams", c.teams ? 1 : 0);
    IniWriteInt("PvP", "Enemies", c.enemies ? 1 : 0);
}

static void SetTeams(int team1);

// the Multiplayer page's choice (Players.cpp, through Screens.cpp): the game it describes becomes the current choice,
// with its rule, lives or time, rounds and teams, and stays in the ini
void PvpConfigure(const MultiChoice& m)
{
    g_choice = 0;
    for (int i = 0; i < kNChoices; ++i)
        if (kChoices[i].teams == m.teams && kChoices[i].enemies == m.enemies && (m.teams || kChoices[i].players == m.players)) {
            g_choice = i;
            break;
        }
    g_lives = m.lives < 1 ? 1 : m.lives;
    g_timed = m.timed;
    g_minutes = m.minutes < 1 ? 1 : m.minutes;
    g_rounds = m.rounds == 3 || m.rounds == 5 ? m.rounds : 1;
    SetTeams(m.team1);
    if (!m.teams) IniWriteInt("PvP", "Players", m.players);
    IniWriteInt("PvP", "Teams", m.teams ? 1 : 0);
    IniWriteInt("PvP", "Enemies", m.enemies ? 1 : 0);
    IniWriteInt("PvP", "Lives", g_lives);
    IniWriteInt("PvP", "Team1", m.team1);
    IniWriteInt("PvP", "Rule", g_timed ? 1 : 0);
    IniWriteInt("PvP", "Time", g_minutes);
    IniWriteInt("PvP", "Rounds", g_rounds);
}

// the entry's text: "Versus: 3 players", "Versus: 2 vs 2", and ", heroes only" without the enemies
std::string PvpChoiceLabel(const std::string& label)
{
    const Choice& c = kChoices[ChoiceIndex()];
    std::string t = c.teams ? LangText(6, 0, "", label.c_str()) : LangText(2, c.players, "", label.c_str());
    if (!c.enemies) t = LangText(7, 0, "", t.c_str());
    return t;
}

bool PvpAllowed() { return g_allowed; }
bool PvpActive() { return g_pvp; }
void PvpSetActive(bool on)
{
    if (on && !g_allowed) return;
    if (on) {
        const Choice& c = kChoices[ChoiceIndex()];
        g_teams = c.teams;
        g_heroesOnly = !c.enemies;
        // timed games and tournaments are chosen on the Multiplayer page: without it (the old Versus entry) a game of
        // one round with lives, whatever the ini says
        if (!PlayersMultiEnabled()) { g_timed = false; g_rounds = 1; }
        Log("pvp: versus mode on: %s, %s, %s, %d round(s)", c.teams ? "2 vs 2" : c.players == 2 ? "2 players" :
            c.players == 3 ? "3 players" : "4 players", c.enemies ? "with the enemies" : "heroes only",
            g_timed ? (std::to_string(g_minutes) + " minutes").c_str() : (std::to_string(g_lives) + " lives").c_str(), g_rounds);
        if (c.teams) {
            int a[2] = { 0, 0 }, b[2] = { 0, 0 }, na = 0, nb = 0;
            for (int i = 0; i < 4; ++i) {
                if (g_team[i] == 0 && na < 2) a[na++] = i + 1;
                else if (g_team[i] == 1 && nb < 2) b[nb++] = i + 1;
            }
            Log("pvp: teams: players %d and %d against %d and %d ([PvP] Team1)", a[0], a[1], b[0], b[1]);
        }
    } else if (g_pvp) Log("pvp: versus mode off");
    g_pvp = on;
}

static bool IsPlayer(uint32_t e) { return e && *(const int16_t*)(e + 0x7A) == 99; }
static int  PlayerNo(uint32_t e) { return *(const int*)(e + 0x690) + 1; }
static bool InCoop() { return *(const int*)0x562A98 == 1; }

// two players of the same team (2 vs 2): they do not hit each other
static bool SameTeam(uint32_t a, uint32_t b)
{
    if (!g_teams || !IsPlayer(a) || !IsPlayer(b)) return false;
    const int i = PlayerNo(a) - 1, k = PlayerNo(b) - 1;
    return i >= 0 && i < 4 && k >= 0 && k < 4 && g_team[i] == g_team[k];
}

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
    if (owner == shooter || owner == attacker || !IsPlayer(owner) || SameTeam(owner, shooter)) return t;
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
    if (!g_pvp || !InCoop() || !IsPlayer(c) || !ctx || !IsPlayer(ctx[0]) || ctx[0] == c || SameTeam(c, ctx[0]))
        return GameFn((Fn)orig)(c, ctx);
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
    if (!g_pvp || !InCoop() || !IsPlayer(c) || !ctx || !IsPlayer(ctx[0]) || ctx[0] == c || SameTeam(c, ctx[0]))
        return AsEnemyCall(0x43F3F0, c, ctx);
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
    if (!g_pvp || !InCoop() || !IsPlayer(c) || !ctx || !IsPlayer(ctx[0]) || ctx[0] == c || SameTeam(c, ctx[0]))
        return GameFn((uint32_t(__cdecl*)(uint32_t, uint32_t*))0x43E710)(c, ctx);
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
    if (!g_pvp || !InCoop() || !IsPlayer(shooter) || !IsPlayer(victim) || shooter == victim || SameTeam(shooter, victim))
        return 0;
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
    if (!IsPlayer(shooter) || !IsPlayer(cand) || cand == shooter || SameTeam(cand, shooter)) return r;
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
        if (spear && IsPlayer(cand) && IsPlayer(thrower) && cand != thrower && !SameTeam(cand, thrower) &&
            *(const uint8_t*)(spear + 0x178) &&
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
    if (g_pvp && InCoop() && IsPlayer(victim) && IsPlayer(attacker) && !SameTeam(victim, attacker) && data) {
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

// --- the rules: lives or time, rounds ------------------------------------------------------------------------------
// Lives: with three or four players whoever loses all their lives is eliminated and stays on the ground (the update of
// their "die" state, 0x434D80, entry +8 of the table at 0x561428, is skipped after the fall animation: no respawn and no
// defeat of the game); the round ends when one is left (or one team). Timed ([PvP] Rule=1, Time minutes): nobody is
// eliminated, every knock-down of an opponent is a point (the game's own respawn after a fall), and when the time is
// up the most points win, by player or by team; the time runs only while the HUD is on the screen (not during the
// opening cutscene or the pause). Rounds ([PvP] Rounds 1, 3 or 5): a round won counts towards the tournament and the
// first to win more than half takes the Cup; between two rounds the eliminated players get up again (their "die" state
// runs on: the game's own respawn) and those standing get their full life back. A drawn round counts for nobody (at
// most twice the rounds asked, then the leader takes it). At the end a banner in the middle (HudBanner, with a cup for
// a tournament), then the level closes.
static uint32_t g_seenPlayers[4];
static int      g_deaths[4];
static bool     g_out[4];        // eliminated
static bool     g_over;          // the game is over (the only round, or the tournament)
static int      g_phase;         // 0 a round is played, 1 between two rounds or over
static DWORD    g_outAt[4];      // when it was eliminated
static DWORD    g_endAt;         // when to close the level after the winner banner (0 = no)
static int      g_round = 1;     // the round being played
static int      g_roundWins[4];  // rounds won, by player (or by team, 0 and 1, in 2 vs 2)
static int      g_score[4];      // timed: knock-downs of opponents in this round
static DWORD    g_playedMs, g_lastTick, g_nextRoundAt;
static int      g_lastBy[4] = { -1, -1, -1, -1 };   // who last knocked each player down, and when
static DWORD    g_lastByAt[4];
// A fall with no knock-down by a player just before it is written as "falls" a moment later (the knock-down may come
// right after it), and an elimination with nobody to credit waits as long for its player
static DWORD    g_fellAt[4], g_outPendingAt[4];
// the HUD's texts, read once a round (LangText reads ROTKPlus.lang.ini: not at every frame); the counts keep a mark in
// place of the number ("1234567 lives"), replaced when drawn
static std::string g_txtRound, g_txtOut, g_txtSide[2], g_txtName[4], g_txtLife1, g_txtLives, g_txtPoint1, g_txtPoints;
static const char* const kCountMark = "1234567";
// timed: the respawn's time runs this much faster ([PvP] TimedRespawn, the seconds on the ground: 10 is the game's)
static float    g_respawnFast = 2.0f;
static bool     g_skipScore = true;   // [PvP] SkipScoreScreens
static bool     g_lastGame = true;    // [PvP] LastGame
static bool     g_suddenOn = true;    // [PvP] SuddenDeath
static bool     g_sudden;             // a timed round tied when the time was up: the next knock-down decides
static DWORD    g_suddenMs;           // how long the sudden death has lasted (with the HUD on the screen)
static DWORD    g_hudGoneAt;          // a timed round: since when the HUD is not drawn (the clock stands still)

static void StatusTexts()
{
    g_txtRound = LangText(18, g_round);
    g_txtOut = LangText(20);
    g_txtSide[0] = LangText(11);
    g_txtSide[1] = LangText(12);
    for (int k = 0; k < 4; ++k) g_txtName[k] = PlayerName(k);
    g_txtLife1 = LangText(24);
    g_txtLives = LangText(25, atoi(kCountMark));
    g_txtPoint1 = LangText(26);
    g_txtPoints = LangText(27, atoi(kCountMark));
}

// "1 life", "3 lives"; "1 point", "0 points"
static std::string Count(int n, const std::string& one, const std::string& many)
{
    if (n == 1) return one;
    std::string s = many;
    const size_t at = s.find(kCountMark);
    if (at != std::string::npos) s.replace(at, strlen(kCountMark), std::to_string(n));
    return s;
}

static void ResetRound()
{
    for (int i = 0; i < 4; ++i) {
        g_deaths[i] = 0; g_out[i] = false; g_score[i] = 0; g_lastBy[i] = -1; g_lastByAt[i] = 0; g_fellAt[i] = 0;
        g_outPendingAt[i] = 0;
    }
    g_playedMs = 0;
    g_lastTick = 0;
    g_phase = 0;
    g_sudden = false;
    g_suddenMs = 0;
    StatusTexts();
}

static void ResetGame()
{
    for (int i = 0; i < 4; ++i) { g_seenPlayers[i] = g_players[i]; g_roundWins[i] = 0; }
    ResetRound();
    g_round = 1;
    g_over = false;
    g_endAt = 0;
    g_nextRoundAt = 0;
    HudFeedClear();
    StatusTexts();
}

static void ResetIfNewLevel()
{
    if (g_seenPlayers[0] == g_players[0] && g_seenPlayers[1] == g_players[1] && g_seenPlayers[2] == g_players[2] &&
        g_seenPlayers[3] == g_players[3]) return;
    ResetGame();
}

bool PvpSkipScoreScreens() { return g_skipScore; }
bool PvpLastGame() { return g_lastGame && g_skipScore; }
bool PvpInGame() { return g_pvp && InCoop(); }
bool PvpHeroesOnlyGame() { return g_pvp && g_heroesOnly && InCoop(); }
int  PvpTeamOf(int slot) { return g_pvp && g_teams && slot >= 0 && slot < 4 ? g_team[slot] : -1; }

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

// "Alex and Robin": the players of a team, with their names ("Player N" without one)
static std::string TeamNames(int team)
{
    std::string names;
    for (int k = 0; k < 4; ++k) {
        if (g_team[k] != team || !g_players[k]) continue;
        names = names.empty() ? PlayerName(k) : LangText(10, 0, PlayerName(k).c_str(), names.c_str());
    }
    return names;
}

static int ToneOf(int k) { return g_teams ? (g_team[k] == 0 ? 1 : 2) : 0; }   // the feed's colours: the Sun gold, the Moon blue
static std::string SideName(int w) { return g_teams ? LangText(11 + w) : PlayerName(w); }   // a player, or a team

static unsigned SideMask(int w)   // the players of a winning side (a player, or a team)
{
    unsigned mask = 0;
    for (int k = 0; k < 4; ++k)
        if (g_players[k] && w >= 0 && (g_teams ? g_team[k] == w : k == w)) mask |= 1u << k;
    return mask;
}

static void Banner(const std::string& text, DWORD ms, bool cup)
{
    char banner[96];
    lstrcpynA(banner, ("@" + text).c_str(), sizeof banner);
    if (cup) HudBannerCup(banner, ms); else HudBanner(banner, ms);
    Log("pvp: banner \"%s\"", banner + 1);
}

// the game is over: w the winner (a player, or a team), -1 a draw
static void GameOver(int w)
{
    g_over = true;
    g_phase = 1;
    const unsigned mask = SideMask(w);
    const bool cup = g_rounds > 1 && w >= 0;
    std::string text;
    if (cup) text = LangText(g_teams ? 22 : 19, 0, "", SideName(w).c_str());   // "... wins the Cup!"
    else if (w >= 0 && g_teams) text = LangText(8, 0, "", LangText(11 + w).c_str()) + ": " + TeamNames(w);
    else if (w >= 0) {
        const std::string named = PlayersWinnerText(w, HeroName(g_players[w]));
        text = named.empty() ? LangText(4, w + 1, HeroName(g_players[w])) : named;
    } else text = LangText(5);
    Log("pvp: versus game over after %d round(s): %s", g_round, w >= 0 ? SideName(w).c_str() : "draw");
    HudFeedClear();   // (the cup and the banner take the middle of the screen)
    PvpResult r;
    r.winners = mask;
    r.teams = g_teams; r.heroesOnly = g_heroesOnly; r.timed = g_timed;
    r.lives = g_lives; r.minutes = g_minutes; r.rounds = g_rounds;
    r.text = text;
    if (cup) {   // the Cup's score: the winner's rounds, then the best of the others
        int other = 0;
        for (int k = 0; k < (g_teams ? 2 : 4); ++k) if (k != w && g_roundWins[k] > other) other = g_roundWins[k];
        r.text += " (" + std::to_string(g_roundWins[w]) + " - " + std::to_string(other) + ")";
    }
    PlayersPvpOver(r);
    for (int k = 0; k < 4; ++k)
        if ((mask >> k & 1) && !g_out[k]) Celebrate(g_players[k]);
    Banner(text, cup ? 8000 : 6000, cup);
    g_endAt = (GetTickCount() + (cup ? 8000 : 6000)) | 1;
}

// a round is over: w the winner (a player, or a team), -1 nobody
static void RoundOver(int w)
{
    g_phase = 1;
    if (g_rounds > 1) PlayersPvpRound(SideMask(w));   // (a single game is not a "round" in the statistics)
    if (w >= 0) ++g_roundWins[w];
    const int sides = g_teams ? 2 : 4, need = g_rounds / 2 + 1;
    int lead = -1, best = -1;
    bool tie = false;
    for (int k = 0; k < sides; ++k) {
        if (!g_teams && !g_players[k]) continue;
        if (g_roundWins[k] > best) { best = g_roundWins[k]; lead = k; tie = false; }
        else if (g_roundWins[k] == best) tie = true;
    }
    Log("pvp: round %d over: %s (round wins %d %d %d %d)", g_round, w >= 0 ? SideName(w).c_str() : "nobody", g_roundWins[0],
        g_roundWins[1], g_roundWins[2], g_roundWins[3]);
    if (g_rounds <= 1) { GameOver(w); return; }
    if (w >= 0 && g_roundWins[w] >= need) { GameOver(w); return; }
    if (g_round >= g_rounds * 2) { GameOver(tie ? -1 : lead); return; }   // too many drawn rounds: the leader
    std::string text;
    if (w < 0) text = LangText(17, g_round);
    else if (g_teams) text = LangText(18, g_round) + ": " + LangText(8, 0, "", LangText(11 + w).c_str());
    else text = LangText(16, g_round, "", PlayerName(w).c_str());
    Banner(text, 4000, false);
    g_nextRoundAt = (GetTickCount() + 5000) | 1;
}

// the next round: the eliminated players get up (DieUpdate lets them), those standing get their full life back
static void NextRound()
{
    ++g_round;
    ResetRound();   // (and the round's texts)
    for (int k = 0; k < 4; ++k) {
        const uint32_t c = g_players[k];
        if (!c) continue;
        Guard([&] {
            const int maxHp = *(const int16_t*)(*(const uint32_t*)(c + 0xD0) + 0x48);
            if (*(int*)(c + 0x90) > 0 && maxHp > 0) *(int*)(c + 0x90) = maxHp;
        });
    }
    Banner(LangText(18, g_round), 2500, false);
    std::string standing;   // the tournament so far, in the feed: "Alex 1 - Robin 0 - ..."
    for (int k = 0; k < (g_teams ? 2 : 4); ++k) {
        if (!g_teams && !g_players[k]) continue;
        standing += (standing.empty() ? "" : " - ") + SideName(k) + " " + std::to_string(g_roundWins[k]);
    }
    HudFeed(standing.c_str(), 0);
    Log("pvp: round %d starts", g_round);
}

// timed: who leads (a team, or a player), -1 a tie at the top
static int Leader()
{
    int w = -1;
    if (g_teams) {
        int t[2] = { 0, 0 };
        for (int k = 0; k < 4; ++k) if (g_players[k]) t[g_team[k]] += g_score[k];
        return t[0] > t[1] ? 0 : t[1] > t[0] ? 1 : -1;
    }
    int best = -1;
    for (int k = 0; k < 4; ++k) {
        if (!g_players[k]) continue;
        if (g_score[k] > best) { best = g_score[k]; w = k; }
        else if (g_score[k] == best) w = -1;
    }
    return w;
}

// Sudden death ([PvP] SuddenDeath=1, 05/11/2026): a timed round tied at the top when the time is up goes on, the clock
// at 0:00, until a knock-down leaves one side ahead (in everybody against everybody one player ahead of all the others);
// after two more minutes with nobody ahead it is a draw.
static void SuddenCheck()
{
    if (!g_sudden) return;
    const int w = Leader();
    if (w < 0) return;
    g_sudden = false;
    Log("pvp: sudden death decided after %.1f s: %s", g_suddenMs / 1000.0f, SideName(w).c_str());
    RoundOver(w);
}

// timed: the time is up, the most knock-downs win the round
static void TimeUp()
{
    if (g_suddenOn && Leader() < 0) {
        g_sudden = true;
        g_suddenMs = 0;
        Log("pvp: time is up with a tie: sudden death");
        Banner(LangText(28), 4000, false);
        return;
    }
    int w = -1;
    if (g_teams) {
        int t[2] = { 0, 0 };
        for (int k = 0; k < 4; ++k) if (g_players[k]) t[g_team[k]] += g_score[k];
        w = t[0] > t[1] ? 0 : t[1] > t[0] ? 1 : -1;
        Log("pvp: time is up, %d to %d", t[0], t[1]);
    } else {
        int best = -1;
        for (int k = 0; k < 4; ++k) {
            if (!g_players[k]) continue;
            if (g_score[k] > best) { best = g_score[k]; w = k; }
            else if (g_score[k] == best) w = -1;
        }
        Log("pvp: time is up: %d %d %d %d", g_score[0], g_score[1], g_score[2], g_score[3]);
    }
    RoundOver(w);
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
        if (i >= 0 && i < 4 && !g_over && g_phase == 0 && !g_out[i]) {
            const DWORD now = GetTickCount();
            const int by = g_lastBy[i] >= 0 && now - g_lastByAt[i] < 2000 ? g_lastBy[i] : -1;   // knocked down by a player
            if (by < 0) g_fellAt[i] = now | 1;   // fell by itself, or to the enemies (written a moment later, PvpFrame)
            if (!g_timed) {
                ++g_deaths[i];
                Log("pvp: player %d fell (%d of %d)", i + 1, g_deaths[i], g_lives);
                if (g_deaths[i] >= g_lives) {
                    g_out[i] = true;
                    g_outAt[i] = now;
                    if (by >= 0) PlayersPvpElim(by);
                    else g_outPendingAt[i] = now | 1;   // a knock-down may still come for it
                    int left = 0, last = -1, alive[2] = { 0, 0 };
                    for (int k = 0; k < 4; ++k)
                        if (g_players[k] && !g_out[k]) { ++left; last = k; ++alive[g_team[k]]; }
                    Log("pvp: player %d is eliminated, %d left", i + 1, left);
                    if (g_teams) { if (!alive[0] || !alive[1]) RoundOver(alive[0] ? 0 : alive[1] ? 1 : -1); }
                    else if (left <= 1) RoundOver(last);
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
// with the mark above its head, and 2.2 seconds froze a slower hero halfway through the animation. In the next round it
// runs on, and the game makes the player get up.
// In a timed game nobody is eliminated and the time on the ground counts twice ([PvP] TimedRespawn=5): the update adds
// its time (the argument, in seconds) to +0xD0 -> +0x104 and makes the player get up when that reaches 10 (0x434DE3,
// the constant 0x53618C, shared by dozens of other computations: it is not changed). The game's countdown on player 1's
// and 2's boxes (0x4ED110) goes down by two a second, like the extra players' one (Hud.cpp).
static void __cdecl DieUpdate(uint32_t x, uint32_t c, uint32_t dt)
{
    if (g_pvp && InCoop() && IsPlayer(c)) {
        const int i = PlayerNo(c) - 1;
        if (i >= 0 && i < 4 && g_out[i] && g_seenPlayers[i] == c && GetTickCount() - g_outAt[i] > 4000) return;
        if (g_timed && g_respawnFast > 1.0f) {
            float secs;
            memcpy(&secs, &dt, sizeof secs);
            if (secs > 0.0f && secs < 1.0f)
                Guard([&] { *(float*)(*(const uint32_t*)(c + 0xD0) + 0x104) += secs * (g_respawnFast - 1.0f); });
        }
    }
    ((void(__cdecl*)(uint32_t, uint32_t, uint32_t))0x434D80)(x, c, dt);
}

// --- the map: only the arenas --------------------------------------------------------------------------------------
// The map (mode 4, "SceneSelScreen") has 28 places (0x8E57B4 the selected one): 0-12 the levels, then the special
// features; names and paths in the table at 0x540D88 (two pointers per place), the four neighbours of each place at
// 0x540B80 (16 bytes per place: the place reached with each of the directions 0x100, 0x200, 0x40, 0x80, as 0x4F16A0
// reads them). The levels' places are the levels' numbers of 0x562400 (table 0x540D40). The map's script asks of every
// place whether it is open (0x4F11A0 -> 0x482230(level, co-op), the same answer the hero selection uses through
// 0x482530): a closed one is drawn closed and confirming it shows why it is closed. In a versus game the open places
// are only the arenas: the Southern Gate (min02, 3), the Courtyard of Minas Tirith (min04, 8) and the Black Gate (bla01,
// 5), whatever the player has unlocked, and the cursor jumps from an arena to the next one in the direction pressed
// (along the map's own neighbours), so a closed place is never selected.
static bool IsArena(int n) { return n == 3 || n == 5 || n == 8; }   // level numbers and map places alike
static int  g_arenaPlace = 3;   // the last arena selected on the map

static bool ArenasOnly() { return g_allowed && g_arenas && g_pvp && InCoop(); }

// replaces the calls of 0x4F11A0(place) (place open?) at 0x4F14D0, 0x4F1537, 0x4F17EA, 0x4F844E
static uint32_t __cdecl MapPlaceOpen(int place)
{
    if (ArenasOnly()) return IsArena(place) ? 1 : 0;
    return ((uint32_t(__cdecl*)(int))0x4F11A0)(place);
}

// replaces the calls of 0x482230(level, co-op) (level unlocked?) of the map's script (0x480A2A, 0x4F8505) and of the
// heroes' availability (0x4825AD)
static uint32_t __cdecl LevelOpen(int level, int coop)
{
    if (ArenasOnly()) return IsArena(level) ? 1 : 0;
    return ((uint32_t(__cdecl*)(int, int))0x482230)(level, coop);
}

// replaces the call of 0x4F16A0(input) at 0x4F8391: a direction moves to the next arena that way
static int __cdecl MapMove(int input)
{
    if (!ScreensMapFilter(input)) return -1;   // (the Last game over the map: the map's keys are held back)
    const int dir = input == 0x100 ? 0 : input == 0x200 ? 1 : input == 0x40 ? 2 : input == 0x80 ? 3 : -1;
    if (!ArenasOnly() || dir < 0) return ((int(__cdecl*)(int))0x4F16A0)(input);   // confirm, save: as the game does
    int& place = *(int*)0x8E57B4;
    const int from = place;
    int n = from, to = -1;
    for (int k = 0; k < 28 && to < 0; ++k) {
        const int next = *(const int*)(0x540B80 + n * 16 + dir * 4);
        if (next < 0 || next >= 28 || next == n) break;   // the map's edge
        n = next;
        if (IsArena(n) && n != from) to = n;
    }
    if (to < 0 && !IsArena(from)) to = g_arenaPlace;
    if (to >= 0) {
        place = to;
        g_arenaPlace = to;
        static int logged;
        if (logged++ < 20) Log("pvp: map, from place %d to the arena %d", from, to);
    }
    ((void(__cdecl*)())0x4F1500)();   // the place's name and path, as 0x4F16A0 does after a move
    return place;
}

// replaces the call of 0x4F1590 (the map is opened) at 0x4EA602: if the place the game chose is not an arena, the last
// arena, with the place's name and path written as 0x4F1590 writes them
static void __cdecl MapOpen()
{
    ((void(__cdecl*)())0x4F1590)();
    if (!ArenasOnly()) return;
    int& place = *(int*)0x8E57B4;
    if (IsArena(place)) { g_arenaPlace = place; return; }
    Log("pvp: map opened on place %d, moved to the arena %d", place, g_arenaPlace);
    place = g_arenaPlace;
    typedef void(__cdecl * ElemFn)(int, const char*, int);
    const char* const level = (const char*)0x540FB0;   // "SceneSelScreen.CurrLevelText", the game's own string
    const char* const path = (const char*)0x540FD0;    // "SceneSelScreen.pathText.CurrPathText"
    ((ElemFn)0x4F6990)(0, level, 1);
    ((ElemFn)0x4F69B0)(0, *(const char* const*)(0x540D88 + place * 8), 0);
    ((ElemFn)0x4F6A90)(0, level, 0);
    ((ElemFn)0x4F6990)(0, path, 1);
    ((ElemFn)0x4F69B0)(0, *(const char* const*)(0x540D8C + place * 8), 0);
    *(uint32_t*)0x8E2F70 = 0;
}

// --- heroes only: no characters of the enemy side --------------------------------------------------------------------
// Every character of a level's data, placed or made later by a spawner (EntitySpawner: list at 0x5C09FC, 0x48DFF0 every
// frame, 0x48DD70 one spawn), is created by 0x412140(template, spawned) through the entity types' table (0x565FA8, 0x38
// bytes per type, the characters 0..999 with 0x412240); the players are created by 0x4927A0, which calls it too. Its
// type (0..0x59, the template's +4 -> +4) has a row of 0x38 bytes at 0x55ADB8: the characters driven by the computer
// share the function at +0x10 (0x417760; the mumakil 0x4E have their own), and 0x4189F0 puts on the players' side the
// types listed in IsFriend. 0x412140 itself already answers 0 for the characters marked "single player only" in
// co-op, and every caller patched here checks the answer (not 0x44B735, which does not, nor the players' 0x492AA1).
static bool IsHeroType(int t)
{
    return t == 4 || t == 8 || t == 0xC || t == 0xD || t == 0x1E || t == 0x22 || t == 0x43 || t == 0x44 || t == 0x59;
}

static bool IsFriend(int t)   // as 0x4189F0 (bit 0 of +0x5E8)
{
    switch (t) {
    case 0x12: case 0x20: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3F: case 0x40: case 0x42:
    case 0x46: case 0x47: case 0x48: case 0x50: case 0x54: case 0x55: case 0x56: case 0x57: case 0x58: return true;
    }
    return false;
}

static bool IsEnemyType(int t)
{
    if (t < 0 || t >= 0x5A || IsHeroType(t) || IsFriend(t)) return false;
    return *(const uint32_t*)(0x55ADB8 + t * 0x38 + 0x10) == 0x417760 || t == 0x4E;
}

static bool HeroesOnly() { return g_allowed && g_pvp && g_heroesOnly && InCoop() && *(const int*)0x562A54 != 13; }

static int g_notCreated;   // enemies not created in this level

// --- [Diag] BossBones: the Black Gate's boss with the heroes only ------------------------------------------------------
// The boss of the Black Gate (type 0x53) shows Frodo's mithril and speaks before the fight; with the heroes only it is not
// created, and only its voice and the flying mithril are left. To find out whether it could be drawn with the model of
// a hero of the other team, with BossBones=1 it is created anyway and the log gets the bones of its model and of the
// heroes' (names and numbers, in the order of the model), then its state and position every second. The chain of the
// model: character +0x64 -> object to draw (first byte 1) +4 -> character control (0x45BA00) +4 -> definition: +0x18
// parts, +0x1C their table (0x18 bytes: +0 number of named bones, +0x14 the names, 0x14 bytes each: the name packed in 16
// bytes, decoded by 0x4954E0, and the bone's number at +0x10), +0x44 bones.
static bool     g_bossBones;   // [Diag] BossBones
static uint32_t g_boss;        // the boss created with BossBones=1
static bool     g_bossDumped;
static int      g_bossLines;
static DWORD    g_bossAt;

static void LogBones(const char* who, uint32_t c)
{
    Guard([&] {
        const uint8_t* ro = c ? *(const uint8_t* const*)(c + 0x64) : nullptr;
        if (!ro || ro[0] != 1) { Log("pvp: bones of %s (%08X): no model to draw", who, c); return; }
        const uint8_t* ctrl = *(const uint8_t* const*)(ro + 4);
        const uint8_t* def = ctrl ? *(const uint8_t* const*)(ctrl + 4) : nullptr;
        if (!def) { Log("pvp: bones of %s (%08X): no definition", who, c); return; }
        const int parts = *(const int*)(def + 0x18);
        Log("pvp: bones of %s (%08X): definition %08X, %d part(s), %d bones, part in use %d, +0x48 %.3f", who, c,
            (unsigned)(uintptr_t)def, parts, *(const int*)(def + 0x44), *(const short*)(ctrl + 8), *(const float*)(def + 0x48));
        const uint8_t* tab = *(const uint8_t* const*)(def + 0x1C);
        for (int p = 0; tab && p < parts && p < 6; ++p) {
            const uint8_t* part = tab + p * 0x18;
            const int n = *(const int*)part;
            const uint8_t* rec = *(const uint8_t* const*)(part + 0x14);
            std::string line;
            int shown = 0;
            for (int i = 0; rec && i < n && i < 200; ++i) {
                char name[32] = {};
                ((void(__cdecl*)(char*, const void*))0x4954E0)(name, rec + i * 0x14);
                char item[48];
                snprintf(item, sizeof item, "%s%s=%d", line.empty() ? "" : " ", name, *(const int*)(rec + i * 0x14 + 0x10));
                line += item;
                if (line.size() > 180 || i + 1 == n || i + 1 == 200) {
                    Log("pvp: bones of %s, part %d (%d named)%s: %s", who, p, n, shown ? ", more" : "", line.c_str());
                    line.clear();
                    ++shown;
                }
            }
        }
    });
}

// --- [PvP] BossScene: the Black Gate's boss only for its scene ---------------------------------------------------------
// Test of 05/11/2026 (BossBones=1): created during the loading, the boss plays the whole opening scene (Frodo's mithril,
// its lines), then fights; killed, the level makes a second one at once (the gate's scene, 26 s without the HUD). With
// BossScene=1 (the default) and the heroes only, the first boss of the Black Gate is created and, at the first frame of
// play with the HUD after it (the scene is over), taken away the game's own way: +0x70 = -1, and 0x410630 takes it out
// of the list of characters (0x579188, linked at +0x60) and calls its type's function at 0x55ADD4 (0x4196F0). Bit 30 of
// +0x8C (which there makes 0x48E0D0 tell its spawner) is cleared first: the level is not told it is gone, so the gate's
// scene and the second boss do not come; any other boss of the level is not created, as before.
static bool     g_bossScene = true;   // [PvP] BossScene
static uint32_t g_sceneBoss;          // the boss kept for its scene
static DWORD    g_sceneBossAt;
static bool     g_sceneBossUsed;      // this level's boss was already let through

static std::string LevelCode();
static bool BlackGate() { return LevelCode() == "bla01"; }

static void BossSceneFrame(DWORD now, int mode)
{
    if (!g_sceneBoss || mode != 0) return;
    const DWORD hud = HudDrawnAt();
    if (!hud || (int)(hud - g_sceneBossAt) < 0 || now - hud > 250) return;
    const uint32_t c = g_sceneBoss;
    g_sceneBoss = 0;
    Guard([&] {
        const uint8_t* ro = *(const uint8_t* const*)(c + 0x64);
        const bool kept = ro && ro[0] == 1 && *(const uint8_t* const*)(ro + 4) && (*(const uint8_t* const*)(ro + 4))[1] & 0x80;
        const int was = *(const int*)(c + 0x70);
        *(uint32_t*)(c + 0x8C) &= ~(1u << 30);
        *(int*)(c + 0x70) = -1;
        Log("pvp: the Black Gate's boss: its scene is over after %.1f s, taken away (%08X, +0x70 was %d%s)",
            (now - g_sceneBossAt) / 1000.0f, c, was, kept ? ", BUT marked to be kept by its model: it may stay" : "");
    });
}

static void BossFrame(DWORD now, int mode)
{
    if (!g_boss || mode != 0) return;
    if (!g_bossDumped) {
        g_bossDumped = true;
        LogBones("the boss (type 53)", g_boss);
        for (int k = 0; k < 4; ++k) if (g_players[k]) LogBones(HeroName(g_players[k]), g_players[k]);
    }
    if (g_bossLines >= 120 || now - g_bossAt < 1000) return;
    g_bossAt = now;
    ++g_bossLines;
    Guard([&] {
        const int st = ((int(__cdecl*)(const void*))0x42F570)((const void*)(uintptr_t)g_boss);
        Log("pvp: the boss: state %X at %.1f %.1f %.1f", st, *(const float*)(g_boss + 0x30), *(const float*)(g_boss + 0x34),
            *(const float*)(g_boss + 0x38));
    });
}

static uint32_t __cdecl CreateActor(uint32_t tmpl, int spawned)
{
    if (tmpl && HeroesOnly()) {
        int type = -1;
        Guard([&] {
            const uint32_t d = *(const uint32_t*)(tmpl + 4);
            if (d) type = *(const int*)(d + 4);
        });
        if (type == 0x53 && !g_bossBones && g_bossScene && !g_sceneBossUsed && BlackGate()) {
            g_sceneBossUsed = true;
            const uint32_t r = ((uint32_t(__cdecl*)(uint32_t, int))0x412140)(tmpl, spawned);
            if (r) { g_sceneBoss = r; g_sceneBossAt = GetTickCount(); }
            Log("pvp: heroes only, the Black Gate's boss created for its scene ([PvP] BossScene=1): %08X", r);
            return r;
        }
        if (type == 0x53 && g_bossBones) {
            const uint32_t r = ((uint32_t(__cdecl*)(uint32_t, int))0x412140)(tmpl, spawned);
            Log("pvp: heroes only, the boss (type 53) created all the same ([Diag] BossBones=1): %08X", r);
            if (r) { g_boss = r; g_bossDumped = false; g_bossLines = 0; }
            return r;
        }
        if (IsEnemyType(type)) {
            if (g_notCreated++ < 40)
                Log("pvp: heroes only, enemy of type %X not created (%s, caller %08X)", type, spawned & 0xFF ? "spawner" : "level",
                    (unsigned)(uintptr_t)_ReturnAddress());
            return 0;
        }
        static int kept;
        if (type >= 0 && !IsHeroType(type) && *(const uint32_t*)(0x55ADB8 + type * 0x38 + 0x10) == 0x417760 && kept++ < 20)
            Log("pvp: heroes only, ally of type %X created", type);
    }
    return ((uint32_t(__cdecl*)(uint32_t, int))0x412140)(tmpl, spawned);
}

// --- sound and music of the versus game -------------------------------------------------------------------------------
// The sounds of a level are in its own package and are played through the audio command 0x461930(emitter, command, n, a,
// b, c): the level scripts' "audio: play" (0x45DB30) gives command 1 with the emitter of the script's entity and n its
// sound (0..7), "play ig stream" (0x45DBB0) command 0xE. Which emitter and sound is the crowd of the Black Gate, or a
// music of a level, is not written anywhere readable: [Diag] AudioLog=1 writes them to the log (Audio.cpp), and the
// numbers go to ROTKPlus.ini: [PvP] Cheer_<level> (played when a player hits another, at most every CheerEvery ms) and
// Music_<level> (at the start of a versus game), each "command emitter n a b c", several separated by ';' (one is
// chosen at random each time). Without them nothing is played.
static std::string LevelCode()
{
    const int l = *(const int*)0x562A54;
    if (l < 0 || l > 17) return "";
    std::string s = *(const char* const*)(0x562400 + l * 4);
    for (char& ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

// The Black Gate's crowd (test of 05/11/2026 on SteamOS, the boss fight with AudioLog=1 and screenshots at the moments
// the soldiers cheer: 00:32:14.7, 00:32:17.6 and 00:32:31.5): when Aragorn hits the Mouth of Sauron the level's script
// plays the sound 2 (5, 255, 0) on ten emitters within half a second, 0 to 7, 12 and 13, the soldiers around the
// arena. That is the built-in Cheer_bla01; the ini's own line, if there is one, takes its place.
static const char* const kCheerBla01 = "1 0 2 5 255 0+1 1 2 5 255 0+1 2 2 5 255 0+1 3 2 5 255 0+1 4 2 5 255 0+"
                                       "1 5 2 5 255 0+1 6 2 5 255 0+1 7 2 5 255 0+1 12 2 5 255 0+1 13 2 5 255 0";

static bool PlayAudio(const char* key)
{
    // the ini is read once for each key (every hit between players asks for the cheer: not a file read each time)
    static std::map<std::string, std::string> known;
    auto it = known.find(key);
    if (it == known.end()) {
        char ini[MAX_PATH], line[1024];
        sprintf_s(ini, "%s\\ROTKPlus.ini", GameDir());
        GetPrivateProfileStringA("PvP", key, "", line, sizeof line, ini);
        if (!line[0] && !_stricmp(key, "Cheer_bla01")) strncpy_s(line, kCheerBla01, _TRUNCATE);
        it = known.emplace(key, std::string(line)).first;
    }
    if (it->second.empty()) return false;
    char buf[1024];
    strncpy_s(buf, it->second.c_str(), _TRUNCATE);
    // items separated by ';' (one at random each time), each one or more commands joined by '+' (played together)
    char* items[16];
    int n = 0;
    for (char* p = buf; p && *p && n < 16;) {
        items[n++] = p;
        p = strchr(p, ';');
        if (p) *p++ = 0;
    }
    bool ok = false;
    int played = 0;
    for (char* cmdText = items[GetTickCount() % (DWORD)n]; cmdText && *cmdText;) {
        char* next = strchr(cmdText, '+');
        if (next) *next++ = 0;
        int cmd = 0, emitter = 0, sound = 0;
        float a = 0.0f, b = 0.0f, c = 0.0f;
        if (sscanf_s(cmdText, "%d %d %d %f %f %f", &cmd, &emitter, &sound, &a, &b, &c) < 3) {
            static int bad;
            if (bad++ < 5) Log("pvp: [PvP] %s: \"%s\" is not \"command emitter n a b c\"", key, cmdText);
        } else {
            ok |= Guard([&] {
                ((void(__cdecl*)(uint32_t, int, int, float, float, float))0x461930)((uint32_t)emitter, cmd, sound, a, b, c);
            });
            ++played;
        }
        cmdText = next;
    }
    static int logged;
    if (played && logged++ < 30) Log("pvp: %s: %d audio command(s)%s", key, played, ok ? "" : " FAILED");
    return ok;
}

static int   g_cheerEvery = 1500;
static DWORD g_cheerAt;

// Players.cpp: a player was hit by another one (down: knocked down). A knock-down goes to the feed ("Alex downs Robin")
// and, in a timed game, is a point; the same one twice within 1.5 s (a hit on someone already falling) counts once.
void PvpOnPlayerHit(int attacker, int victim, bool down)
{
    if (!g_pvp || !InCoop() || attacker == victim || attacker < 0 || attacker > 3 || victim < 0 || victim > 3) return;
    const DWORD now = GetTickCount();
    if (down && !(g_lastBy[victim] >= 0 && now - g_lastByAt[victim] < 1500)) {   // (one knock-down a fall, whoever hit)
        g_lastBy[victim] = attacker;
        g_lastByAt[victim] = now | 1;
        const bool fallen = g_fellAt[victim] != 0;   // the fall came first and was counted
        g_fellAt[victim] = 0;   // not a fall of its own
        if (g_outPendingAt[victim] && now - g_outPendingAt[victim] < 600) { g_outPendingAt[victim] = 0; PlayersPvpElim(attacker); }
        if (!g_over && g_phase == 0) {
            // the last life: "knocks out" (the fall may already have been counted, a moment before: it was waiting in
            // g_fellAt)
            const bool last = !g_timed && (g_out[victim] || g_deaths[victim] + (fallen ? 0 : 1) >= g_lives);
            HudFeed(LangText(last ? 23 : 13, 0, PlayerName(victim).c_str(), PlayerName(attacker).c_str()).c_str(),
                    ToneOf(attacker));
            if (g_timed) { ++g_score[attacker]; SuddenCheck(); }
        }
    }
    if (g_cheerAt && now - g_cheerAt < (DWORD)g_cheerEvery && !down) return;
    if (PlayAudio(("Cheer_" + LevelCode()).c_str())) g_cheerAt = now | 1;
}

// The HUD of a versus game (test of 04/11/2026: a line of lives and the messages in the middle of the screen covered
// the names above the heads): each player's lives (or "out"), or the points of a timed game, in its own box, in place
// of the game's kills (after them when the level has its enemies); in the gap between the top boxes the clock of a
// timed game, the two teams' totals in 2 vs 2 and the round of a tournament. The feed is at the bottom (Hud.cpp).
static void StatusFrame()
{
    const DWORD now = GetTickCount();
    for (int i = 0; i < 4; ++i) {   // the falls with no knock-down by a player after them: "falls", "is out"
        if (g_fellAt[i] && now - g_fellAt[i] > 300) {
            g_fellAt[i] = 0;
            HudFeed(LangText(g_out[i] ? 15 : 14, 0, "", PlayerName(i).c_str()).c_str(), 3);
        }
        if (g_outPendingAt[i] && now - g_outPendingAt[i] > 600) g_outPendingAt[i] = 0;
    }
    for (int k = 0; k < 4; ++k) {
        if (!g_players[k]) { HudBoxText(k, nullptr, false); continue; }
        std::string t;
        if (g_timed) t = Count(g_score[k], g_txtPoint1, g_txtPoints);
        else if (g_out[k]) t = g_txtOut;
        else t = Count((std::max)(0, g_lives - g_deaths[k]), g_txtLife1, g_txtLives);
        HudBoxText(k, t.c_str(), !g_heroesOnly && !g_out[k], ToneOf(k));   // (the team's colour)
    }
    if (g_over) { HudStatus(0, nullptr, nullptr); HudClock(nullptr); HudRound(nullptr); return; }   // (the cup and the banner)
    std::string parts[3];
    int tones[3], n = 0;
    if (g_teams) {   // the Sun's total, the Moon's: lives left, or points
        int t[2] = { 0, 0 };
        for (int k = 0; k < 4; ++k)
            if (g_players[k]) t[g_team[k]] += g_timed ? g_score[k] : g_out[k] ? 0 : (std::max)(0, g_lives - g_deaths[k]);
        parts[0] = std::to_string(t[0]); tones[0] = 1;
        parts[1] = std::to_string(t[1]); tones[1] = 2;
        n = 2;   // (Hud.cpp puts the clock, or a dash, between them)
    }
    const char* texts[3];
    for (int i = 0; i < n; ++i) texts[i] = parts[i].c_str();
    HudStatus(n, texts, tones);
    HudRound(g_rounds > 1 ? g_txtRound.c_str() : nullptr);
    if (!g_timed) { HudClock(nullptr); return; }
    const DWORD limit = (DWORD)g_minutes * 60000u, left = g_playedMs >= limit ? 0 : limit - g_playedMs;
    char clock[16];
    sprintf_s(clock, "%u:%02u", (unsigned)(left / 60000), (unsigned)(left / 1000 % 60));
    HudClock(clock);
}

// --- every frame (Pads.cpp) ------------------------------------------------------------------------------------------
// Closes the level after the winner banner. At the start of a versus game (the first frame of play after a loading,
// mode 14, or with other characters): lives counted again, the teams for a few seconds each (2 vs 2) and the level's
// music from the ini.
static uint32_t g_startedFor[4];   // the players the start was done for
static bool     g_loaded;          // a loading was seen since the last start
static DWORD    g_startAt;         // when to show the teams and start the music (0 = done)
static int      g_startStep;

void PvpFrame()
{
    const DWORD now = GetTickCount();
    const int mode = *(const int*)0x562AC0;
    if (mode == 14) g_loaded = true;
    {   // a new loading: the boss of the level before is gone (the new one is made during the loading itself)
        static int lastMode;
        if (mode == 14 && lastMode != 14) { g_boss = 0; g_sceneBoss = 0; g_sceneBossUsed = false; }
        lastMode = mode;
    }
    BossFrame(now, mode);
    BossSceneFrame(now, mode);
    if (g_pvp && InCoop() && mode == 0 && g_players[0] && g_players[1] &&
        (g_loaded || memcmp(g_startedFor, g_players, sizeof g_startedFor))) {
        g_loaded = false;
        memcpy(g_startedFor, g_players, sizeof g_startedFor);
        ResetGame();
        g_startAt = (now + 1500) | 1;
        g_startStep = 0;
        g_notCreated = 0;
        Log("pvp: versus game on %s: %s, %s, %s, %s", LevelCode().c_str(), g_teams ? "2 vs 2" : "everybody against everybody",
            g_heroesOnly ? "heroes only" : "with the enemies", g_timed ? "timed" : "lives",
            g_rounds > 1 ? (g_rounds == 3 ? "best of 3" : "best of 5") : "one round");
    }
    const bool playing = g_pvp && InCoop() && mode == 0 && g_players[0] && g_players[1];
    if (playing) {
        // the time of a timed round runs while the HUD is on the screen
        const bool hud = HudDrawnAt() && now - HudDrawnAt() < 250;
        if (g_timed && g_phase == 0 && !g_over) {
            if (hud && g_lastTick) (g_sudden ? g_suddenMs : g_playedMs) += now - g_lastTick;
            g_lastTick = now;
            // the clock stands still without the HUD (test of 04/11/2026: a round of 2 minutes took 2:32): how long, and
            // when, in the log
            if (!hud && !g_hudGoneAt) g_hudGoneAt = now | 1;
            if (hud && g_hudGoneAt) {
                static int logged;
                if (now - g_hudGoneAt > 1000 && logged++ < 40)
                    Log("pvp: no HUD for %.1f s in round %d (the clock stood still at %u s played)",
                        (now - g_hudGoneAt) / 1000.0, g_round, (unsigned)(g_playedMs / 1000));
                g_hudGoneAt = 0;
            }
            if (g_sudden) {
                if (g_suddenMs >= 120000u) {   // two minutes more with nobody ahead: a draw
                    g_sudden = false;
                    Log("pvp: sudden death: nobody ahead after two minutes, a draw");
                    RoundOver(-1);
                }
            } else if (g_playedMs >= (DWORD)g_minutes * 60000u) TimeUp();
        }
        if (g_nextRoundAt && (int)(now - g_nextRoundAt) >= 0 && !g_over) { g_nextRoundAt = 0; NextRound(); }
        StatusFrame();
    } else {
        HudStatus(0, nullptr, nullptr);
        HudClock(nullptr);
        HudRound(nullptr);
        for (int k = 0; k < 4; ++k) HudBoxText(k, nullptr, false);
        g_lastTick = 0;   // (out of the game, the pause for one: that time does not count when it comes back)
        g_hudGoneAt = 0;
    }
    // the banners wait for the HUD (the opening cutscene hides it): the second team after the first one is gone
    if (g_startAt && (int)(now - g_startAt) >= 0 && !(g_startStep > 0 && HudBannerBusy())) {
        if (g_startStep == 0) PlayAudio(("Music_" + LevelCode()).c_str());
        if (g_teams && g_startStep < 2) {
            char banner[96];
            lstrcpynA(banner, ("@" + LangText(9, 0, TeamNames(g_startStep).c_str(), LangText(11 + g_startStep).c_str())).c_str(),
                      sizeof banner);
            HudBanner(banner, 3000);
            Log("pvp: banner \"%s\"", banner + 1);
            g_startAt = (now + 200) | 1;
            ++g_startStep;
        } else g_startAt = 0;
    }
    if (!g_endAt || (int)(now - g_endAt) < 0) return;
    g_endAt = 0;
    RequestEndLevel();
}

// [PvP] Team1: the two players of team 1 as digits (12, 13 or 14), the other two are team 2
static void SetTeams(int t)
{
    const int a = t / 10, b = t % 10;
    const bool good = t >= 12 && t <= 43 && a != b && a >= 1 && a <= 4 && b >= 1 && b <= 4;
    for (int i = 0; i < 4; ++i) g_team[i] = good ? (i + 1 == a || i + 1 == b ? 0 : 1) : i < 2 ? 0 : 1;
    if (!good) Log("pvp: [PvP] Team1=%d is not two players (12, 13, 14...): players 1 and 2 are team 1", t);
}

void PvpInstall()
{
    g_allowed = IniInt("PvP", "Enabled", 1) != 0;
    g_lives = IniInt("PvP", "Lives", 2);
    if (g_lives < 1) g_lives = 1;
    g_arenas = IniInt("PvP", "ArenasOnly", 1) != 0;
    g_cheerEvery = IniInt("PvP", "CheerEvery", 1500);
    g_timed = IniInt("PvP", "Rule", 0) == 1;
    g_minutes = IniInt("PvP", "Time", 3);
    if (g_minutes < 1 || g_minutes > 30) g_minutes = 3;
    g_rounds = IniInt("PvP", "Rounds", 1);
    if (g_rounds != 3 && g_rounds != 5) g_rounds = 1;
    SetTeams(IniInt("PvP", "Team1", 12));
    int respawn = IniInt("PvP", "TimedRespawn", 5);
    if (respawn < 1 || respawn > 10) respawn = 5;
    g_respawnFast = 10.0f / (float)respawn;
    g_skipScore = IniInt("PvP", "SkipScoreScreens", 1) != 0;
    g_lastGame = IniInt("PvP", "LastGame", 1) != 0;
    g_suddenOn = IniInt("PvP", "SuddenDeath", 1) != 0;
    g_bossBones = IniInt("Diag", "BossBones", 0) != 0;
    g_bossScene = IniInt("PvP", "BossScene", 1) != 0;
    if (g_bossBones) Log("pvp: [Diag] BossBones=1: the Black Gate's boss is created also with the heroes only, its bones in the log");
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
    Log("pvp: timed games get up after %d s; after a versus game %s", respawn,
        g_skipScore ? "straight to the map (no score screens)" : "the game's score screens");
    // the map's arenas
    bool map = true;
    for (uintptr_t at : { 0x4F14D0u, 0x4F1537u, 0x4F17EAu, 0x4F844Eu }) map &= PatchCall(at, 0x4F11A0, (const void*)&MapPlaceOpen);
    for (uintptr_t at : { 0x480A2Au, 0x4825ADu, 0x4F8505u }) map &= PatchCall(at, 0x482230, (const void*)&LevelOpen);
    map &= PatchCall(0x4F8391, 0x4F16A0, (const void*)&MapMove);
    map &= PatchCall(0x4EA602, 0x4F1590, (const void*)&MapOpen);
    Log("pvp: map with the arenas only (Southern Gate, Courtyard, Black Gate) in versus games: %s",
        !g_arenas ? "off ([PvP] ArenasOnly=0)" : map ? "active" : "NOT completely installed");
    // heroes only
    bool heroes = true;
    for (uintptr_t at : { 0x4122EFu, 0x44BA2Fu, 0x44F0E3u, 0x45413Eu, 0x48E8E5u })
        heroes &= PatchCall(at, 0x412140, (const void*)&CreateActor);
    Log("pvp: versus games with the heroes only (no enemies created): %s", heroes ? "available" : "NOT completely installed");
}
