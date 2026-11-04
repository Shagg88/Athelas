// Support.cpp - small hooks the other modules depend on: hero choice in the selection screen, script calls, ending
// a level on demand
// Language: C++17, MSVC or mingw-w64 g++, Win32.
#include "ROTKPlus.h"
#include <stdio.h>
#include <string.h>
#include <string>

void ScreensFrame();   // Screens.cpp

static int Mode() { return *(const int*)0x562AC0; }
static bool InLevel() { return Mode() == 0; }   // 0 = playing (1 is the hero selection screen)

// --- 0x47DF00 / 0x47DF20: hero choice of player 1 / player 2 (selection screen) ---------------------------------
// The selection screen is driven by the screen's script, which calls these two functions. Screens.cpp decides when a
// choice belongs to the third or fourth player instead (CharSelOnChoice).
static void __cdecl SetChoice1(int h)
{
    Log("selection: hero choice of player 1 = %d (mode %d)", h, Mode());
    if (CharSelOnChoice(1, h)) return;
    if (h > -1 && h < 9) *(int*)0x562A84 = h;
}

static void __cdecl SetChoice2(int h)
{
    Log("selection: hero choice of player 2 = %d (mode %d)", h, Mode());
    if (CharSelOnChoice(2, h)) return;
    if (h > -1 && h < 9) *(int*)0x562A88 = h;
}

// --- 0x48ED90: calls made by the screens' scripts ----------------------------------------------------------------
// 0x48ED90(type, x, code, args, result, ...) dispatches script calls. Outside the main per-frame update this is the
// only regular place where the selection screen runs, so the deferred actions of Screens.cpp (ScreensFrame) are also
// given a chance after every script call.
static NAKED void ScriptCallOrig()   // mov edx,[esp+4]; sub esp,24h
{
    ASM_BEGIN
    ASM(mov  edx, dword ptr [esp + 4])
    ASM(sub  esp, 0x24)
    ASM(push 0x48ED97)
    ASM(ret)
    ASM_END
}

static uint32_t __cdecl ScriptCall(int type, int x, uint32_t code, float* args, float* res, int a6, int a7)
{
    const uint32_t r = GameFn((uint32_t(__cdecl*)(int, int, uint32_t, float*, float*, int, int))&ScriptCallOrig)(
        type, x, code, args, res, a6, a7);
    PlayersScriptCall(type, code, args);
    ScreensFrame();
    return r;
}

// --- ending a level on demand ----------------------------------------------------------------------------------
// The "end of level" case (0x62C3F8B4) of the script dispatcher 0x480830 starts at 0x480B68 and uses: esi = script
// message ([esi+0xC] -> float parameter, 1 victory / 2 defeat), ebx = 0, the frame of 0x480830's prologue.
// EndLevelNow rebuilds the prologue and jumps there: the case ends with the original epilogue and returns here. Used
// by the versus mode (RequestEndLevel) and, with [Diag] VictoryKey=1, by F9 (instant victory, handy for looking at the
// end-of-level screens without playing the level).
static float g_endParam;
static int   g_endMsg[8];

static NAKED void __cdecl EndLevelNow(int* /*msg*/)
{
    ASM_BEGIN
    ASM(push ebp)
    ASM(mov  ebp, esp)
    ASM(and  esp, 0xFFFFFFF8)
    ASM(sub  esp, 0x0C)
    ASM(push ebx)
    ASM(push esi)
    ASM(mov  esi, dword ptr [ebp + 8])
    ASM(xor  ebx, ebx)
    ASM(push edi)
    ASM(push 0x480B68)
    ASM(ret)
    ASM_END
}

static bool g_victoryKey;
static volatile bool g_endPending;   // end of level requested by other modules (Pvp.cpp), run on the next frame

void RequestEndLevel() { g_endPending = true; }

static void EndLevel(const char* why)
{
    g_endParam = 1.0f;
    g_endMsg[0] = 2000;
    g_endMsg[3] = (int)(uintptr_t)&g_endParam;
    Log("end of level: %s", why);
    GameFn(&EndLevelNow)(g_endMsg);
}

// --- detailed log: statistics and snapshots -------------------------------------------------------------------------
// The per-hero statistics records are at 0x5BE05C + hero * 0x3C (15 dwords). Compared with the score screen: dword 6 is
// the hero's level (float), 7 the total experience (float), 8 the experience of the current level (float), 11-14 the
// kills of the level by rating: Perfect, Excellent, Good, Fair ("Superbo", "Ottimo", "Buono", "Medio" in Italian). The
// other dwords are not understood yet: they are dumped as they are, so that a problem with the counters can be studied
// from the log.
extern uint32_t g_players[8];   // Coop.cpp
static const char* const kStatNames[9] = { "Aragorn", "Legolas", "Gimli", "Gandalf", "Sam", "Frodo", "Pippin", "Merry", "Faramir" };

// copies the record of a hero, false if it cannot be read
static bool ReadRecord(int hero, uint32_t out[15])
{
    return Guard([&] { memcpy(out, (const void*)(0x5BE05C + hero * 0x3C), 15 * sizeof(uint32_t)); });
}

void LogStats(const char* when)
{
    if (!LogDetail()) return;
    // the records are built first, so that a periodic snapshot with nothing new does not repeat them
    static std::string last;
    std::string text;
    for (int h = 0; h < 9; ++h) {
        uint32_t r[15];
        if (!ReadRecord(h, r)) { Log("stats (%s): unreadable", when); return; }
        bool any = false;
        char buf[256]; int n = 0;
        for (int i = 0; i < 15; ++i) {
            if (r[i]) any = true;
            n += sprintf_s(buf + n, sizeof buf - n, " %X", r[i]);
        }
        if (!any) continue;
        char line[400];
        sprintf_s(line, "    %s: experience %g kills %u/%u/%u/%u raw%s\n", kStatNames[h], *(const float*)(r + 8), r[11], r[12], r[13],
                  r[14], buf);
        text += line;
    }
    const bool snapshot = !strcmp(when, "snapshot");
    if (snapshot && text == last) { Log("stats (snapshot): unchanged"); return; }
    last = text;
    Log("stats (%s):", when);
    for (size_t p = 0; p < text.size();) {
        const size_t e = text.find('\n', p);
        Log("%s", text.substr(p, e - p).c_str());
        p = e + 1;
    }
}

// Every 60 s in a level: where the players are and their statistics. Also notes when a player's "down" flag (+0x810:
// dead or waiting to respawn) changes, with the position, to help with deaths in odd places.
static void LogSnapshots()
{
    static DWORD last;
    static uint32_t seen[4];
    static uint8_t down[4];
    const DWORD now = GetTickCount();
    bool changed = false;
    for (int i = 0; i < 4; ++i) {
        const uint32_t c = g_players[i];
        if (c != seen[i]) { seen[i] = c; down[i] = 0; changed = true; }
        if (!c) continue;
        Guard([&] {
            const uint8_t d = *(const uint8_t*)(c + 0x810);
            if (d != down[i]) {
                const float* p = (const float*)(c + 0x30);
                Log("player %d: down flag %d -> %d at (%.1f, %.1f, %.1f)", i + 1, down[i], d, p[0], p[1], p[2]);
                down[i] = d;
            }
        });
    }
    if (changed) last = now;
    if (now - last < 60000) return;
    last = now;
    char line[256]; int n = 0;
    for (int i = 0; i < 4; ++i) {
        if (!g_players[i]) continue;
        Guard([&] {
            const float* p = (const float*)(g_players[i] + 0x30);
            n += sprintf_s(line + n, sizeof line - n, " P%d(%.0f,%.0f,%.0f)%s", i + 1, p[0], p[1], p[2], down[i] ? "*" : "");
        });
    }
    if (!n) return;
    Log("snapshot:%s  (* = down)", line);
    LogStats("snapshot");
}

void SupportFrame()   // from Pads.cpp, every frame, on the game's thread
{
    static bool down;
    if (LogDetail() && InLevel()) LogSnapshots();
    if (g_endPending && InLevel()) {
        g_endPending = false;
        EndLevel("requested");
    }
    if (!g_victoryKey) return;
    const bool now = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (now && !down && InLevel()) { PlayersVictoryKey(); EndLevel("F9, instant victory"); }
    down = now;
}

void SupportInstall(bool victoryKey)
{
    static const uint8_t choiceOld[] = { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xF8, 0xFF };
    static const uint8_t scriptOld[] = { 0x8B, 0x54, 0x24, 0x04, 0x83, 0xEC, 0x24 };
    bool ok = PatchJmp(0x47DF00, choiceOld, sizeof choiceOld, (const void*)&SetChoice1);
    ok &= PatchJmp(0x47DF20, choiceOld, sizeof choiceOld, (const void*)&SetChoice2);
    ok &= PatchJmp(0x48ED90, scriptOld, sizeof scriptOld, (const void*)&ScriptCall);
    g_victoryKey = victoryKey;
    Log("hero choice and script hooks: %s%s", ok ? "active" : "NOT installed", victoryKey ? " (F9 = instant victory)" : "");
}
