// Coop.cpp - third and fourth player in co-op
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// How players come to life in rotk.exe (all addresses are for `rotk.exe` 1.0):
//  - the level loader (0x4BE870) puts the packages to load in a list, among them Data/Player/char_xxx.scw for
//    player 1 and, in co-op, for player 2; the list has 8 slots;
//  - each char_ package creates a 0xE10 entity (the hero's model) registered by 0x492C40 in an array of 2 slots at
//    0x99CE40 (counter 0x5C0A44);
//  - the level's start entities 0xDAC/0xDAD call 0x492AC0(1) and, in co-op, 0x492AC0(2), which takes the player's
//    hero (0x47EF60 / 0x47EF80), looks for its model and spawns it there;
//  - 0x442ED0 initialises the character: index 0 if it is player 1's hero, otherwise 1, and registers it (0x410150)
//    in the players array of 2 slots at 0x57916C.
// Here: the arrays are moved to larger ones of our own, one package per extra player is added to the list, and a
// second and third spawn are made at player 2's start (offset by nothing: they appear on the same spot) with the
// extra players' heroes, indices 2 and 3. g_spawningP3 is 1 while the third player is being created or handled and
// 2 for the fourth: 0x47EF80 then answers with that player's hero, and the character index is 1 + g_spawningP3.
#include "ROTKPlus.h"
#include <string.h>
#include <stdio.h>
#include <string>
#include <math.h>

// --- addresses in rotk.exe ------------------------------------------------------
static const uintptr_t kPlayers      = 0x57916C;   // character* [2]
static const uintptr_t kModels       = 0x99CE40;   // model entity* [2]
static const uintptr_t kHeroP1       = 0x562A84;   // hero chosen by player 1 (0..8)
static const uintptr_t kHeroP2       = 0x562A88;   // hero chosen by player 2
static const uintptr_t kCoopMode     = 0x562A98;   // 1 = co-op game
static const uintptr_t kCharNames    = 0x562448;   // const char* [9]: "char_ara.sc" ...
static const uintptr_t kSpawnPlayer  = 0x492AC0;   // void __cdecl(int n), start entity in ebx
static const uintptr_t kLoadPackages = 0x4E0690;   // int __cdecl(list*)

// the loader's package list: a counter and 8 paths of 64 bytes, on the stack
struct PackageList { int count; char path[8][64]; };

// --- state ----------------------------------------------------------------------
uint32_t g_players[8];                   // replaces 0x57916C (2 slots)
static uint32_t g_models[16];            // replaces 0x99CE40 (2 slots, the game never checks the limit)
static uint8_t  g_inputSlot[4] = { 0, 1, 2, 3 };   // replaces the byte table at 0x561830
static int      g_hero[4] = { -1, -1, -1, -1 };    // heroes of the extra players (slot 2 = third, 3 = fourth) in this game
volatile int g_spawningP3;               // 0 none; 1 third, 2 fourth player: 0x47EF80 answers g_hero[value + 1]

static const char* const kHeroNames[9] = { "Aragorn", "Legolas", "Gimli", "Gandalf", "Sam", "Frodo", "Pippin", "Merry", "Faramir" };

static int ReadHero(uintptr_t a)
{
    int v = *(const int*)a;
    return (v < 0 || v > 8) ? -1 : v;
}

// Picks the hero of an extra player (slot 2 = third, 3 = fourth): the one chosen in the selection screen or, for the
// third player, the one in the ini file if it differs from the others, otherwise the first free one among Gimli,
// Legolas, Aragorn, Gandalf (Fellowship heroes, available in every co-op level). It excludes players 1 and 2, the
// level's computer-controlled allies (0x562A8C/90/94, chosen by the loader before the package list is built: the
// third player could end up as Gimli next to an ally Gimli) and, for the fourth player, the third player's hero.
static int PickHero(int slot)
{
    const int used[6] = { ReadHero(kHeroP1), ReadHero(kHeroP2), ReadHero(0x562A8C), ReadHero(0x562A90), ReadHero(0x562A94),
                          slot == 3 ? g_hero[2] : -1 };
    auto free = [&](int h) { for (int u : used) if (u == h) return false; return true; };
    const int chosen = CharSelChoice(slot);   // chosen in the hero selection screen
    if (chosen >= 0 && free(chosen)) return chosen;
    if (chosen >= 0) Log("co-op: the hero chosen by player %d (%d) is already in the game, picking one automatically", slot + 1, chosen);
    if (slot == 2 && g_cfg.hero3 >= 0 && free(g_cfg.hero3)) return g_cfg.hero3;
    static const int order[] = { 2, 1, 0, 3 };
    for (int h : order)
        if (free(h)) return h;
    return -1;
}

int HeroOfSlot(int slot) { return slot == 2 || slot == 3 ? g_hero[slot] : -1; }

// --- replacement for 0x47EF80 (hero of player 2) ------------------------------------
void ScreensSetHeroes(int hero3, int hero4);   // Screens.cpp
void HudNewLevel();                            // Hud.cpp

static int __cdecl Hero2()
{
    const int sp = g_spawningP3;
    return sp == 1 || sp == 2 ? g_hero[sp + 1] : ReadHero(kHeroP2);
}

// --- character index in 0x442ED0: replaces "mov dword [esi+690h], 1" ---
// 1 for player 2, 2 for the third player (g_spawningP3 = 1), 3 for the fourth (g_spawningP3 = 2)
static NAKED void IndexStub()
{
    ASM_BEGIN
    ASM(push eax)
    ASM(mov  eax, dword ptr [g_spawningP3])
    ASM(inc  eax)
    ASM(mov  dword ptr [esi + 0x690], eax)
    ASM(pop  eax)
    ASM(ret)
    ASM_END
}

// --- level allies: a hero chosen by an extra player is not also an ally ---------------------------
// The loader computes the three allies (0x562A8C, 0x562A90, 0x562A94) with 0x481A50(level, n, hero of player 1, hero
// of player 2, co-op): fixed per-level rules that, in co-op, remove the two players' heroes (calls at 0x4BEB36,
// 0x4BEBF2, 0x4BECBB). Found while testing at the South Gate (Aragorn, Legolas, ally Gimli): the third player had
// chosen Gimli, but PickHero avoids allies and took Gandalf. Here, if the computed ally is a hero chosen by an extra
// player, the ally is removed (-1: 0x47FDF0 then does not add its foll_ package).
static int __cdecl AllyFor3(int level, int n, int hero1, int hero2, int coop)
{
    const int r = ((int(__cdecl*)(int, int, int, int, int))0x481A50)(level, n, hero1, hero2, coop);
    if (coop == 1 && Coop3Wanted() && r >= 0)
        for (int slot = 2; slot <= 3; ++slot)
            if (CharSelChoice(slot) == r) {
                Log("co-op: ally %d of level %d (%s) was chosen by player %d: removed from the allies", n, level,
                    r <= 8 ? kHeroNames[r] : "?", slot + 1);
                return -1;
            }
    return r;
}

// --- shared lives of the co-op: one more per extra player, defeat only when no teammate is alive ---
// In co-op the lives are shared (0x579B28; 0x43FD40 reads them, 0x43FD50 writes them; the "1" box among the HUD
// elements shows them, 0x4ECF74). Each player's initialisation (0x442ED0, at 0x4432CB) writes them: 1, or 2 in
// level 10 (0x47F300). On entering the "die" state (0x434D80): if it is co-op, lives remain and "the other" player
// (0x445A40: player 2 for the first player, player 1 for everybody else) is not itself in the "die" state (0x28), a
// life is consumed and the character will respawn; otherwise the defeat sequence starts (0x43FE40, then
// 0x4F6E90(1)). With two players you can therefore die twice in total. Here: one more life per extra player
// (g_livesExtra, added to the write at 0x4432CB even if player 1 is created after the others) and, at death, "the
// other" is any teammate still alive among the four (OtherForDeath at 0x434DB9).
static int g_livesExtra ASM_NAME(g_livesExtra);   // extra lives in the current level (1 per extra player)

static NAKED void LivesInitStub()   // replaces "mov [579B28h], edx" at 0x4432CB (6 bytes)
{
    ASM_BEGIN
    ASM(add  edx, dword ptr [g_livesExtra])
    ASM(mov  dword ptr ds:[0x579B28], edx)
    ASM(ret)
    ASM_END
}

static int CharState(uint32_t c)
{
    const uint8_t* d = *(const uint8_t* const*)(c + 0xD0);
    const int* s = *(const int* const*)(d + 4);
    return s ? *s : *(const int*)(d + 0x10C);
}

static uint32_t __cdecl OtherForDeath(uint32_t c)
{
    const uint32_t def = ((uint32_t(__cdecl*)(uint32_t))0x445A40)(c);
    if (!g_players[2]) return def;
    for (int i = 0; i < 4; ++i) {
        const uint32_t p = g_players[i];
        if (p && p != c && CharState(p) != 0x28) return p;
    }
    Log("co-op: died with every teammate down, lives left %d", *(const int*)0x579B28);
    return def;
}

// --- loader: adds the extra players' char_xxx.scw before the last package (the level) -----
// The list has 8 slots (the loader copies 0x204 bytes of it, 0x4E0690): LoadOnce, GlobScen, two char_, the allies'
// foll_, one char_ per extra player and the level. With four players there is room for only one ally: the others are
// removed from the list and from the ally variables (0x562A8C/90/94 = -1, as AllyFor3 does).
static bool DropAlly(PackageList* list)
{
    for (int k = 2; k >= 0; --k) {
        const int ally = ReadHero(0x562A8C + 4 * k);
        if (ally < 0) continue;
        char path[64];
        sprintf_s(path, "Data/Player/foll_%sw", ((const char* const*)kCharNames)[ally] + 5);
        for (int i = 0; i < list->count && i < 8; ++i)
            if (!_stricmp(list->path[i], path)) {
                for (int j = i; j + 1 < list->count && j + 1 < 8; ++j) memcpy(list->path[j], list->path[j + 1], 64);
                list->count--;
                *(int*)(0x562A8C + 4 * k) = -1;
                Log("co-op: package list full, ally %d (%s) removed", k, kHeroNames[ally]);
                return true;
            }
    }
    return false;
}

static int __cdecl LoadPackages(PackageList* list)
{
    g_hero[2] = g_hero[3] = -1;
    HudNewLevel();
    AudioNewLevel();
    // game levels only: the list contains the char_ packages of the two players (the menu, which is also a level
    // loaded this way, does not: Gimli once ended up in it when returning to the menu after co-op)
    int chars = 0;
    for (int i = 0; i < list->count && i < 8; ++i)
        if (!_strnicmp(list->path[i], "Data/Player/char_", 17)) ++chars;
    if (chars >= 1) ScreensSetHeroes(-1, -1);   // new level: the extra players of the screens are decided at spawn time
    const bool coop = *(const int*)kCoopMode == 1 && chars >= 2;
    if (coop && !Coop3Wanted()) Log("co-op: 2 players chosen in the menu, no extra players");
    if (coop && Coop3Wanted()) {
        const int wanted = CoopPlayers() - 2;
        for (int slot = 2; slot < 2 + wanted; ++slot) {
            const int h = PickHero(slot);
            if (h < 0) { Log("co-op: no free hero for player %d", slot + 1); break; }
            while (list->count >= 8 && DropAlly(list)) {}
            if (list->count < 1 || list->count >= 8) {
                Log("co-op: package list full (%d), player %d not loaded", list->count, slot + 1);
                break;
            }
            char path[64];
            sprintf_s(path, "Data/Player/%sw", ((const char* const*)kCharNames)[h]);
            const int last = list->count - 1;
            memcpy(list->path[last + 1], list->path[last], 64);
            strcpy_s(list->path[last], path);
            list->count++;
            g_hero[slot] = h;
            Log("co-op: player %d is %s (%s), %d packages", slot + 1, kHeroNames[h], path, list->count);
        }
        if (g_hero[2] >= 0) {
            Log("co-op with %d players: %s and %s, allies %d %d %d", CoopPlayers(),
                ReadHero(kHeroP1) >= 0 ? kHeroNames[ReadHero(kHeroP1)] : "?",
                ReadHero(kHeroP2) >= 0 ? kHeroNames[ReadHero(kHeroP2)] : "?",
                ReadHero(0x562A8C), ReadHero(0x562A90), ReadHero(0x562A94));
            for (int i = 0; i < list->count; ++i) Log("    package %d: %s", i, list->path[i]);
        }
    }
    g_livesExtra = (g_hero[2] >= 0) + (g_hero[3] >= 0);
    if (chars >= 1 && list->count >= 1) {   // statistics of the players (Players.cpp): a game level, not the menu
        // the level is the last package: "Data/Game/Min/Min02.scw" -> "min02"
        char code[64];
        const char* name = strrchr(list->path[list->count - 1], '/');
        strcpy_s(code, name ? name + 1 : list->path[list->count - 1]);
        if (char* dot = strchr(code, '.')) *dot = 0;
        _strlwr_s(code);
        PlayersLevelStart(code);
    }
    return ((int(__cdecl*)(PackageList*))kLoadPackages)(list);
}

// --- player 2's start: spawns the extra players too -----------------------------------
// Called with the start entity (ebx in 0x492B40). The extra players are created on the same spot as player 2: an
// early version moved the entity's +0x30 field, which is not a position but a pointer (it caused a crash at 0x499184
// together with the loop in 0x410230). Characters' positions are at +0x30/+0x34/+0x38.
static void LogPos(const char* who, uint32_t c)
{
    if (!c) { Log("    %s: none", who); return; }
    const float* p = (const float*)(c + 0x30);
    Log("    %s: %08X type %d index %d position (%.2f, %.2f, %.2f)", who, c,
        *(const short*)(c + 0x78), *(const int*)(c + 0x690), p[0], p[1], p[2]);
}

// k = 1: the third player (slot 2), k = 2: the fourth (slot 3)
static int __cdecl BeginSpawn(int k) ASM_NAME(BeginSpawn);
static void __cdecl EndSpawn(int k) ASM_NAME(EndSpawn);

static int __cdecl BeginSpawn(int k)
{
    if (g_hero[k + 1] < 0) return 0;
    g_spawningP3 = k;
    Log("co-op: %s (player %d) is created next to player 2", kHeroNames[g_hero[k + 1]], k + 2);
    return 1;
}

static void __cdecl EndSpawn(int k)
{
    g_spawningP3 = 0;
    const int slot = k + 1;
    // Identifier by which the level's scripts recognise players (+0x70, searched by 0x4101B0): extra players are
    // created from player 2's start and inherit its identifier, so for the scripts the death of the third player was
    // the death of player 2 (defeat with Aragorn still alive).
    if (g_players[1] && g_players[slot]) {
        int* id2 = (int*)(g_players[1] + 0x70);
        int* idN = (int*)(g_players[slot] + 0x70);
        Log("co-op: player identifiers %d %d %d", g_players[0] ? *(int*)(g_players[0] + 0x70) : -1, *id2, *idN);
        bool dup = false;
        for (int i = 0; i < 4; ++i)
            if (i != slot && g_players[i] && *(int*)(g_players[i] + 0x70) == *idN) dup = true;
        if (dup) { *idN = *id2 + 0x1000 * k; Log("co-op: identifier of player %d changed to %d", slot + 1, *idN); }
        // Identifier at +0x74: the game's messages (0x48ED90 -> 0x412460, type 0) go to the first entity of the list
        // at 0x579188 (next one at +0x60) with that value. If an extra player has player 2's, the events of its
        // animations (e.g. 0x25D, spear release, from 0x45DAD0) reach player 2: Gimli did the throw but the spear
        // stayed in his hand. A free value is chosen.
        int* keyN = (int*)(g_players[slot] + 0x74);
        Log("co-op: message keys %d %d %d", g_players[0] ? *(int*)(g_players[0] + 0x74) : -1,
            *(int*)(g_players[1] + 0x74), *keyN);
        int maxKey = 0, same = 0;
        if (!Guard([&] {
                for (uint8_t* e = *(uint8_t**)0x579188; e; e = *(uint8_t**)(e + 0x60)) {
                    const int key = *(int*)(e + 0x74);
                    if (key > maxKey) maxKey = key;
                    if (key == *keyN && (uint32_t)(uintptr_t)e != g_players[slot]) ++same;
                }
            })) {
            Log("co-op: entity list unreadable");
            same = 0;
        }
        if (same) { *keyN = maxKey + 1; Log("co-op: key of player %d shared with %d entities, changed to %d", slot + 1, same, *keyN); }
    }
    // Zero the extra player's per-level statistics: the game does it for players 1 and 2 when a level starts, not for
    // the others (a third player could start a level with the kills and experience of the previous one). In-game
    // record 0x5BE05C + hero * 0x3C: dword 8 = experience gained in the level, 11-14 = kills rated Perfect ("Superbo"
    // in Italian), Excellent, Good and Fair (see Support.cpp). Dwords 6 (level) and 7 (total experience) are the hero's
    // own and are kept. Until 1.1.0-dev dword 11 was not cleared: an extra player started a level with the Perfect kills
    // of an earlier one (52 for Legolas after a reload of the South Gate).
    if (k == 1) LogStats("level start, before the extra players' statistics are reset");
    if (g_hero[slot] >= 0) {
        uint32_t* r = (uint32_t*)(0x5BE05C + g_hero[slot] * 0x3C);
        Log("co-op: level statistics of player %d reset (experience %g, kills %u/%u/%u/%u)", slot + 1,
            *(const float*)(r + 8), r[11], r[12], r[13], r[14]);
        r[8] = r[11] = r[12] = r[13] = r[14] = 0;
    }
    Log("co-op: %d players in the game, shared lives %d", *(const int*)0x579B2C, *(const int*)0x579B28);
    if (k == 2 || g_hero[3] < 0) {   // last spawn: tell the screens who the extra players are, log the picture
        ScreensSetHeroes(g_hero[2], g_hero[3]);
        LogPos("player 1", g_players[0]);
        LogPos("player 2", g_players[1]);
        LogPos("player 3", g_players[2]);
        LogPos("player 4", g_players[3]);
    }
}

// replaces "call 0x492AC0" at 0x492B87 ("push 2" already done, ebx = start entity)
static NAKED void SpawnP2Stub()
{
    ASM_BEGIN
    ASM(push dword ptr [esp + 4])
    ASM(mov  eax, 0x492AC0)            // kSpawnPlayer
    ASM(call eax)
    ASM(add  esp, 4)
    ASM(push 1)
    ASM(call BeginSpawn)
    ASM(add  esp, 4)
    ASM(test eax, eax)
    ASM(jz   SpawnP2Stub_done3)
    ASM(push 2)
    ASM(mov  eax, 0x492AC0)
    ASM(call eax)
    ASM(add  esp, 4)
    ASM(push 1)
    ASM(call EndSpawn)
    ASM(add  esp, 4)
ASM(SpawnP2Stub_done3:)
    ASM(push 2)
    ASM(call BeginSpawn)
    ASM(add  esp, 4)
    ASM(test eax, eax)
    ASM(jz   SpawnP2Stub_done)
    ASM(push 2)
    ASM(mov  eax, 0x492AC0)
    ASM(call eax)
    ASM(add  esp, 4)
    ASM(push 2)
    ASM(call EndSpawn)
    ASM(add  esp, 4)
ASM(SpawnP2Stub_done:)
    ASM(ret)
    ASM_END
}

// --- controls of the extra players -------------------------------------------------
// The game has control blocks for 4 players (0x8E7E60, 0x24690 bytes each), configured by the [inputN] sections of
// ROTK.ini (map, mode KB keyboard or JS pad) and [input_KB_BASE_N] / [input_JS_BASE_N], but initialises only two:
// 0x4AC890 calls 0x4AC670(0,1) and 0x4AC670(1,0). Here 0x4AC670(2,0) and 0x4AC670(3,0) are added. The extra players
// use the keyboard with player 1's keys (WASD, Q, E, space, mouse), which are free when the first two play with pads;
// a connected pad overrides them every frame (Pads.cpp).
static const uintptr_t kInitPlayerInput = 0x4AC670;   // char __cdecl(uint player, char first)
static const char*     kGameIni         = (const char*)0x5BE8F2;   // path of the ROTK.ini the game uses

static void EnsureExtraInputIni(int player)   // 2 = third, 3 = fourth
{
    char v[64], sect[32], base[32];
    sprintf_s(sect, "input%d", player);
    sprintf_s(base, "input_KB_BASE_%d", player);
    GetPrivateProfileStringA(sect, "map", "", v, sizeof v, kGameIni);
    if (!v[0]) {
        WritePrivateProfileStringA(sect, "map", "ROTKDefault", kGameIni);
        WritePrivateProfileStringA(sect, "mode", "KB", kGameIni);
        Log("co-op: wrote [%s] (keyboard) in %s", sect, kGameIni);
    }
    static char sec[0x2000];
    if (GetPrivateProfileSectionA(base, sec, sizeof sec, kGameIni) < 3) {
        const DWORD n = GetPrivateProfileSectionA("input_KB_BASE_1", sec, sizeof sec, kGameIni);
        if (n > 2 && WritePrivateProfileSectionA(base, sec, kGameIni))
            Log("co-op: [%s] copied from [input_KB_BASE_1] (%lu bytes)", base, n);
        else
            Log("co-op: [input_KB_BASE_1] missing, player %d has no keys", player + 1);
    }
}

// replaces "call 0x4AC670" for player 1 at 0x4AC9BF
static char __cdecl InitInputP2P3(unsigned player, char first)
{
    typedef char(__cdecl* Init_t)(unsigned, char);
    const char r = ((Init_t)kInitPlayerInput)(player, first);
    EnsureExtraInputIni(2);
    EnsureExtraInputIni(3);
    const char r3 = ((Init_t)kInitPlayerInput)(2, 0);
    const char r4 = ((Init_t)kInitPlayerInput)(3, 0);
    static int logged;   // the game rebuilds the controls at every screen change
    if (logged++ < 3) Log("co-op: controls of players 3 and 4 initialised (%d %d)", (int)r3, (int)r4);
    return r;
}

// --- game functions that deal with "player 1 and player 2" -----------------------------
// Rewritten for every player present (slots 0..3 of the moved array).
// 0x445960(lock): field +0x81C of each player = !lock (controls enabled). The game calls it around movies: without
// this the third player stayed with its controls locked (actions worked, movement did not).
static void __cdecl SetControlsAll(char lock)
{
    for (int i = 0; i < 4; ++i)
        if (uint8_t* c = (uint8_t*)g_players[i]) c[0x81C] = lock == 0;
}

// 0x488C60 / 0x488C20: freeze and unfreeze the players (bit 1 of +0x8C) with a counter at 0x5C078C.
static void __cdecl FreezeAll()
{
    int& n = *(int*)0x5C078C;
    if (n == 0)
        for (int i = 0; i < 4; ++i)
            if (uint8_t* c = (uint8_t*)g_players[i]) *(uint32_t*)(c + 0x8C) |= 2;
    ++n;
}

static void __cdecl UnfreezeAll()
{
    int& n = *(int*)0x5C078C;
    if (n > 0 && --n == 0)
        for (int i = 0; i < 4; ++i)
            if (uint8_t* c = (uint8_t*)g_players[i]) *(uint32_t*)(c + 0x8C) &= ~2u;
}

// --- camera: frames the extra players too ---------------------------------------------
// In co-op 0x46FB10 computes with 0x4402C0(character, vec4) the point to follow of player 1 (in 0x5A5BA0) and of
// player 2 (in 0x5A5B90), then 0x46F530 frames the pair (centre and distance). Before 0x46F530 the points of the
// extra players are computed too and the two points farthest apart among all of them are left in the two places:
// the camera frames the whole group. A dead or waiting player (+0x810) does not count.
static const uintptr_t kFocusPoint = 0x4402C0;   // void __cdecl(character, float out[4])
static const uintptr_t kFramePair  = 0x46F530;   // registers: edx = point A, ecx = point B, eax, esi

static void RecallStragglers();

static bool g_camera = true;   // [Coop] Camera: 0 = the game's own framing of players 1 and 2 (to compare, e.g. the music)

static void __cdecl AdjustCameraFocus() ASM_NAME(AdjustCameraFocus);

static void __cdecl AdjustCameraFocus()
{
    if (!g_players[2] || !g_camera) return;
    RecallStragglers();
    float* a = (float*)0x5A5BA0;
    float* b = (float*)0x5A5B90;
    float pts[4][4];
    memcpy(pts[0], a, sizeof pts[0]);
    memcpy(pts[1], b, sizeof pts[1]);
    int n = 2;
    for (int s = 2; s < 4; ++s) {
        const uint8_t* p = (const uint8_t*)g_players[s];
        if (!p || p[0x810]) continue;
        ((void(__cdecl*)(const void*, float*))kFocusPoint)(p, pts[n]);
        ++n;
    }
    if (n == 2) return;
    auto d2 = [](const float* u, const float* v) {
        const float x = u[0] - v[0], y = u[1] - v[1], z = u[2] - v[2];
        return x * x + y * y + z * z;
    };
    int bi = 0, bj = 1;
    float best = d2(pts[0], pts[1]);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            const float d = d2(pts[i], pts[j]);
            if (d > best) { best = d; bi = i; bj = j; }
        }
    memcpy(a, pts[bi], sizeof pts[bi]);
    memcpy(b, pts[bj], sizeof pts[bj]);
}

// replaces "call 0x46F530" at 0x46FBBA: registers left intact for 0x46F530
static NAKED void FramePairStub()
{
    ASM_BEGIN
    ASM(pushad)
    ASM(call AdjustCameraFocus)
    ASM(popad)
    ASM(push 0x46F530)                 // kFramePair
    ASM(ret)
    ASM_END
}

// --- bringing back whoever is stuck outside the camera frame -----------------------------
// In co-op 0x441454 calls 0x43DCB0(character, speed) at every step in which the character moves: it returns the
// edge of the frame (bits 0..5) it is pushing against, 0 if free. With three or more players, if the group spreads out
// more than the camera can follow, whoever is behind stays stuck.
// Rule: if a player touched an edge in the last 3 s, has not moved more than 0.3 for 1.5 s and the nearest teammate
// is farther than 8 units, it is teleported next to that teammate. The check runs every frame on the game's thread
// (from the co-op camera) and writes the position (+0x30) and the previous position (+0x6E0) from which the game
// derives the next step.
static const uintptr_t kViewEdge = 0x43DCB0;   // uint __cdecl(character, float vel[3])
static DWORD g_edgeAt[4];                       // last time an edge was touched
static DWORD g_stillSince[4];                   // since when it has not moved
static float g_stillPos[4][3];

static uint32_t __cdecl ViewEdge(uint8_t* c, float* vel)
{
    const uint32_t r = ((uint32_t(__cdecl*)(uint8_t*, float*))kViewEdge)(c, vel);
    const int idx = *(const int*)(c + 0x690);
    if (r && idx >= 0 && idx < 4) g_edgeAt[idx] = GetTickCount();
    return r;
}

static void RecallStragglers()
{
    const DWORD now = GetTickCount();
    for (int idx = 0; idx < 4; ++idx) {
        uint8_t* c = (uint8_t*)g_players[idx];
        if (!c || c[0x810]) { g_stillSince[idx] = 0; continue; }
        float* p = (float*)(c + 0x30);
        const float dx = p[0] - g_stillPos[idx][0], dz = p[2] - g_stillPos[idx][2];
        if (!g_stillSince[idx] || dx * dx + dz * dz > 0.09f) {
            g_stillSince[idx] = now; memcpy(g_stillPos[idx], p, 12); continue;
        }
        if (now - g_stillSince[idx] < 1500 || !g_edgeAt[idx] || now - g_edgeAt[idx] > 3000) continue;
        const uint8_t* best = nullptr; float bestD = 1e30f;
        for (int i = 0; i < 4; ++i) {
            const uint8_t* o = (const uint8_t*)g_players[i];
            if (!o || o == c || o[0x810]) continue;
            const float* q = (const float*)(o + 0x30);
            const float d = (p[0] - q[0]) * (p[0] - q[0]) + (p[2] - q[2]) * (p[2] - q[2]);
            if (d < bestD) { bestD = d; best = o; }
        }
        if (!best || bestD < 64.0f) continue;   // within 8 units of the nearest teammate
        // like the game's own teleport (event 0x47 at 0x442690..): position at +0x30, +0x40, +0x6C0 and +0x6E0, then
        // event 0x62 to the character (0x401780), which realigns it
        const float* q = (const float*)(best + 0x30);
        const float to[3] = { q[0] + 1.0f, q[1] + 0.5f, q[2] + 1.0f };
        for (int off : { 0x30, 0x40, 0x6C0, 0x6E0 }) memcpy(c + off, to, sizeof to);
        ((void(__cdecl*)(void*, int, int, int, int))0x401780)(c, 0x62, 0, 0, 0);
        g_edgeAt[idx] = 0; g_stillSince[idx] = 0;
        Log("co-op: player %d was stuck outside the camera frame (at %.1f), brought back next to a teammate",
            idx + 1, sqrtf(bestD));
    }
}

// --- victory: level completed for the extra players' heroes too -----------------------------
// The "end of level" script function (case 0x62C3F8B4 of 0x480830, parameter 1 = victory) records the level as
// completed for player 1's hero and, in co-op, repeats the same sequence for player 2 (0x480C3D..0x480C70: 0x47EF80,
// 0x47F300, 0x481710, 0x47EF80, 0x480140, 0x47F300, 0x47EF80, 0x47F590). The last call (0x47F590 at 0x480C6B) is
// hooked and the identical sequence is repeated for each extra player, with 0x47EF80 answering that player's hero.
static uint32_t g_creditRet ASM_NAME(g_creditRet);
static int g_creditSlot ASM_NAME(g_creditSlot);

// k = 1 (third) or 2 (fourth): 1 if the player is in the level
static int __cdecl ExtraInLevel(int k) ASM_NAME(ExtraInLevel);
static int __cdecl ExtraInLevel(int k) { return g_hero[k + 1] >= 0 && g_players[k + 1] != 0; }

static NAKED void CreditExtraStub()
{
    ASM_BEGIN
    ASM(pop  ecx)
    ASM(mov  dword ptr [g_creditRet], ecx)
    ASM(mov  eax, 0x47F590)
    ASM(call eax)                    // the original call, with its arguments on the stack
    ASM(mov  dword ptr [g_creditSlot], 1)
ASM(CreditExtraStub_next:)
    ASM(push dword ptr [g_creditSlot])
    ASM(call ExtraInLevel)
    ASM(add  esp, 4)
    ASM(test eax, eax)
    ASM(jz   CreditExtraStub_skip)
    ASM(mov  eax, dword ptr [g_creditSlot])
    ASM(mov  dword ptr [g_spawningP3], eax)
    ASM(push 1)
    ASM(mov  eax, 0x47EF80)
    ASM(call eax)
    ASM(push eax)
    ASM(mov  eax, 0x47F300)
    ASM(call eax)
    ASM(push eax)
    ASM(mov  eax, 0x481710)
    ASM(call eax)
    ASM(mov  eax, 0x47EF80)
    ASM(call eax)
    ASM(push eax)
    ASM(mov  eax, 0x480140)
    ASM(call eax)
    ASM(add  esp, 0x10)
    ASM(push eax)
    ASM(mov  eax, 0x47F300)
    ASM(call eax)
    ASM(push eax)
    ASM(mov  eax, 0x47EF80)
    ASM(call eax)
    ASM(push eax)
    ASM(mov  eax, 0x47F590)
    ASM(call eax)
    ASM(add  esp, 0x0C)
    ASM(mov  dword ptr [g_spawningP3], 0)
ASM(CreditExtraStub_skip:)
    ASM(inc  dword ptr [g_creditSlot])
    ASM(cmp  dword ptr [g_creditSlot], 2)
    ASM(jle  CreditExtraStub_next)
    ASM(push dword ptr [g_creditRet])
    ASM(ret)
    ASM_END
}

// --- installation ----------------------------------------------------------------
struct Reloc { uintptr_t at; uint32_t old; };

// instructions that contain the address of the players array (the arrays are found by searching the exe for the
// address: 57916C .. 579174)
static const Reloc kPlayerRefs[] = {
    { 0x41015B, 0x57916C }, { 0x410183, 0x57916C }, { 0x4101A7, 0x57916C }, { 0x4101C3, 0x57916C },
    { 0x4101DC, 0x57916C }, { 0x4101F7, 0x57916C }, { 0x41026F, 0x57916C }, { 0x4102DB, 0x579170 },
    { 0x410348, 0x579174 }, { 0x4103B5, 0x579178 },   // unrolled loop of 0x410230: slots 2 and 3
    { 0x410433, 0x57916C }, { 0x4104B1, 0x57916C }, { 0x410BF2, 0x57916C }, { 0x410C30, 0x579170 },
    { 0x410D14, 0x57916C }, { 0x410D79, 0x57916C }, { 0x410D7E, 0x579170 }, { 0x410DD9, 0x57916C },
    { 0x410DF1, 0x579170 }, { 0x4116E3, 0x57916C }, { 0x4124B4, 0x57916C },
};
// instructions that contain the address of the models array (99CE40 .. 99CE48)
static const Reloc kModelRefs[] = {
    { 0x492AE3, 0x99CE40 }, { 0x492B2D, 0x99CE40 }, { 0x492BB3, 0x99CE40 }, { 0x492BC2, 0x99CE44 },
    { 0x492BE3, 0x99CE40 }, { 0x492BF7, 0x99CE40 }, { 0x492C28, 0x99CE40 }, { 0x492C61, 0x99CE40 },
};

// Other per-player tables with 2 rows. With index 2 the third player's row landed on the data that follows: the
// table at 0x578E80 overwrote the difficulty (0x578F48) and the memory pool pointer at 0x578F84 (crash at 0x499184).
// They are moved to spaces of our own with 4 rows and the same stride; rows 2 and 3 are initialised like row 1.
struct PerPlayer { uintptr_t base; uint32_t stride; uint8_t* copy; };
static uint8_t g_ppA[4 * 4], g_ppB[4 * 4], g_ppC[4 * 8], g_ppD[4 * 100], g_ppE[4 * 0x144], g_ppF[4 * 4], g_ppG[4 * 4];
static const PerPlayer kPerPlayer[] = {
    { 0x578E60, 4,     g_ppA },   // float per player
    { 0x578E68, 4,     g_ppB },   // float per player
    { 0x578E70, 8,     g_ppC },   // 2 dwords per player (0x40CF3A)
    { 0x578E80, 100,   g_ppD },   // 100-byte structure (0x40C600, initialised by 0x40C8F0)
    { 0x99DB80, 0x144, g_ppE },   // recorded commands and combos (0x445A70, counter at +0x140 = 0x99DCC0)
    { 0x579B30, 4,     g_ppF },   // dword per player (0x441466)
    { 0x579B38, 4,     g_ppG },   // dword per player (0x43CFD2)
};
static const uintptr_t kPerPlayerRefs[] = {   // address of the 32-bit value inside the instruction
    0x445A81, 0x445A87, 0x445A9A, 0x445AAC, 0x445ABB, 0x445ACA, 0x445AD9, 0x445AE8, 0x445AF7, 0x445B06,
    0x445BE1, 0x445C0E, 0x445C4D, 0x445C60, 0x445D5C, 0x445EA6, 0x4462E5, 0x446310, 0x446354, 0x446374,
    0x43CFD5, 0x441469, 0x40C608, 0x40C617, 0x40C849, 0x40C86D, 0x40C873, 0x40C88D, 0x40C8A9, 0x40CAE8,
    0x40CB49, 0x40CC2D, 0x40CD67, 0x40CD74, 0x40CD83, 0x40CDA5, 0x40CE20, 0x40CEDC, 0x40CF2B, 0x40CF3D,
    0x40CF44, 0x40D0B0, 0x40D156, 0x40D2AE, 0x40D315, 0x40D349,
};

static const PerPlayer* PerPlayerOf(uint32_t v)
{
    for (const PerPlayer& p : kPerPlayer)
        if (v >= p.base && v < p.base + 2 * p.stride) return &p;
    return nullptr;
}

bool CoopInstall()
{
    // check every place first, then modify: all or nothing
    bool ok = true;
    for (const Reloc& r : kPlayerRefs) ok &= *(const uint32_t*)r.at == r.old;
    for (const Reloc& r : kModelRefs)  ok &= *(const uint32_t*)r.at == r.old;
    for (uintptr_t at : kPerPlayerRefs) ok &= PerPlayerOf(*(const uint32_t*)at) != nullptr;
    static const uint8_t hero2[] = { 0xA1, 0x88, 0x2A, 0x56, 0x00 };
    static const uint8_t index[]  = { 0xC7, 0x86, 0x90, 0x06, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 };
    static const uint8_t input[]  = { 0x8A, 0x91, 0x30, 0x18, 0x56, 0x00 };
    static const uint8_t loopP[]  = { 0x83, 0xF9, 0x02 };
    static const uint8_t loopM[]  = { 0x83, 0xFE, 0x02 };
    ok &= !memcmp((const void*)0x47EF80, hero2, sizeof hero2);
    ok &= !memcmp((const void*)0x442F25, index, sizeof index);
    ok &= !memcmp((const void*)0x443014, input, sizeof input);
    ok &= !memcmp((const void*)0x4101D1, loopP, sizeof loopP);
    ok &= !memcmp((const void*)0x492AFB, loopM, sizeof loopM);
    ok &= *(const uint8_t*)0x492B87 == 0xE8 && *(const uint8_t*)0x4BEDA1 == 0xE8 && *(const uint8_t*)0x4AC9BF == 0xE8;
    if (!ok) {
        Log("co-op: the game's code is not what was expected, nothing was changed");
        return false;
    }

    memcpy(g_players, (const void*)kPlayers, 8);
    memcpy(g_models, (const void*)kModels, 8);
    g_inputSlot[2] = (uint8_t)g_cfg.input3;
    for (const Reloc& r : kPlayerRefs) ok &= PatchDword(r.at, r.old, (uint32_t)(uintptr_t)g_players + (r.old - kPlayers));
    for (const Reloc& r : kModelRefs)  ok &= PatchDword(r.at, r.old, (uint32_t)(uintptr_t)g_models + (r.old - kModels));
    for (const PerPlayer& p : kPerPlayer) {   // rows 0 and 1 as they are, rows 2 and 3 copies of row 1
        memcpy(p.copy, (const void*)p.base, 2 * p.stride);
        memcpy(p.copy + 2 * p.stride, (const void*)(p.base + p.stride), p.stride);
        memcpy(p.copy + 3 * p.stride, (const void*)(p.base + p.stride), p.stride);
    }
    for (uintptr_t at : kPerPlayerRefs) {
        const uint32_t v = *(const uint32_t*)at;
        const PerPlayer* p = PerPlayerOf(v);
        ok &= PatchDword(at, v, (uint32_t)(uintptr_t)p->copy + (v - p->base));
    }

    // loops: search for the player by id up to 4 slots, search for the model up to 4
    const uint8_t four = 4;
    ok &= Patch(0x4101D3, loopP + 2, &four, 1);
    ok &= Patch(0x492AFD, loopM + 2, &four, 1);
    // messages addressed by player index (0x412460, type 7): "cmp eax,2" at 0x4124A8 rejected the third player
    static const uint8_t msgOld[] = { 0x83, 0xF8, 0x02 };
    const uint8_t msgNew[] = { 0x83, 0xF8, 0x04 };
    ok &= Patch(0x4124A8, msgOld, msgNew, sizeof msgNew);
    // input slot by index: our own 4-byte table
    ok &= PatchDword(0x443016, 0x561830, (uint32_t)(uintptr_t)g_inputSlot);
    // memory pool of the large characters (a UMemPool created at every level by 0x410BE0, pointer at 0x579178):
    // 2 slots, one per player; the element count "push 2" at 0x410C65 becomes 4
    static const uint8_t poolOld[] = { 0x6A, 0x02, 0xE8 };
    const uint8_t poolNew[] = { 0x6A, 0x04, 0xE8 };
    ok &= Patch(0x410C65, poolOld, poolNew, sizeof poolNew);
    // index 2 for the third player, 3 for the fourth
    uint8_t call[10] = { 0xE8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90 };
    *(int32_t*)(call + 1) = (int32_t)((uintptr_t)&IndexStub - (0x442F25 + 5));
    ok &= Patch(0x442F25, index, call, sizeof call);
    // hero of player 2 (answers the extra player's hero while it is being created)
    ok &= PatchJmp(0x47EF80, hero2, sizeof hero2, (const void*)&Hero2);
    // creation of the extra players at player 2's start, and package loading
    ok &= PatchCall(0x492B87, kSpawnPlayer, (const void*)&SpawnP2Stub);
    // shared lives: one more per extra player; at death look for a living teammate among the four
    {
        static const uint8_t livesOld[] = { 0x89, 0x15, 0x28, 0x9B, 0x57, 0x00 };
        uint8_t livesNew[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
        *(int32_t*)(livesNew + 1) = (int32_t)((uintptr_t)&LivesInitStub - (0x4432CB + 5));
        ok &= Patch(0x4432CB, livesOld, livesNew, sizeof livesNew);
        ok &= PatchCall(0x434DB9, 0x445A40, (const void*)&OtherForDeath);
    }
    // level allies without the heroes chosen by the extra players
    for (uintptr_t site : { (uintptr_t)0x4BEB36, (uintptr_t)0x4BEBF2, (uintptr_t)0x4BECBB })
        ok &= PatchCall(site, 0x481A50, (const void*)&AllyFor3);
    ok &= PatchCall(0x4BEDA1, kLoadPackages, (const void*)&LoadPackages);
    // "for both players" functions extended to everybody
    static const uint8_t lockOld[]   = { 0x56, 0x6A, 0x00, 0xE8, 0x38 };
    static const uint8_t unfrzOld[]  = { 0xA1, 0x8C, 0x07, 0x5C, 0x00 };
    ok &= PatchJmp(0x445960, lockOld, sizeof lockOld, (const void*)&SetControlsAll);
    ok &= PatchJmp(0x488C20, unfrzOld, sizeof unfrzOld, (const void*)&UnfreezeAll);
    ok &= PatchJmp(0x488C60, unfrzOld, sizeof unfrzOld, (const void*)&FreezeAll);
    // victory: level completed for the extra players' heroes too
    ok &= PatchCall(0x480C6B, 0x47F590, (const void*)&CreditExtraStub);
    // co-op camera that frames the extra players (and brings back whoever is stuck outside the frame); [Coop] Camera=0
    // leaves the game's own framing of players 1 and 2
    g_camera = IniInt("Coop", "Camera", 1) != 0;
    if (!g_camera) Log("co-op: camera frames only players 1 and 2 ([Coop] Camera=0)");
    ok &= PatchCall(0x46FBBA, kFramePair, (const void*)&FramePairStub);
    // bring back whoever is stuck outside the camera frame
    ok &= PatchCall(0x441454, kViewEdge, (const void*)&ViewEdge);
    // controls of players 3 and 4 (keyboard if no pad)
    ok &= PatchCall(0x4AC9BF, kInitPlayerInput, (const void*)&InitInputP2P3);

    if (g_cfg.pads) ok &= PadsInstall(g_cfg.pad3Name);
    if (g_cfg.hud) ok &= HudInstall(g_cfg.hudDx, g_cfg.hudDy, g_cfg.hudRow, g_cfg.hudScale);
    ok &= HeroInstall();
    ok &= ScreensInstall();
    PlayersInstall();   // names and statistics: reported on a line of its own
    Log("co-op with 3 or 4 players: %s (players at %p, models at %p, third player's controls from slot %d)",
        ok ? "installed" : "installed IN PART, see errors above", g_players, g_models, g_cfg.input3);
    return ok;
}
