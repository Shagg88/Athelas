// Screens.cpp - turns of the extra players in the end-of-level screens, hero selection, main menu entries
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// The screens change with 0x47FA20(mode): previous (0x562AC4) = current (0x562AC0), current = new. After a victory in
// co-op: the score screen (mode 2) twice, first for player 1 (previous 0) and then for player 2 (mode 2 is requested
// again, previous 2); the co-op statistics (16) once in two columns; the shop (3) twice, player 1 (previous 16) and
// player 2 (previous 3); then saving (6). The screens pick the hero with 0x47EF60/0x47EF80 or by reading 0x562A84/0x562A88
// directly.
//
// Extra players' turn: when the game is about to leave player 2's turn of one of these screens, another round of the
// same mode is requested and, for its whole duration, player 2's choice (0x562A88) is the extra player's hero and
// "player 2's own profile" (0x562A9C) is off, so the shop does not swap the records. At the end of the round
// everything is restored. The co-op statistics are shown once more with the extra player in the second column.
// Fourth player: same road, one more round right after the third player's (input slot 3, the fourth player's hero in
// place of player 2's). Score and statistics: leaving the third player's round repeats the same mode for the fourth.
// Shop: the third player's "continue" (UpgradeContinue) passes to the fourth.
//
// Per-turn pads: 0x4EA200(slot) writes to 0x56DC75 the EXCLUDED slot, not the hooked one: the menu loop 0x4EAA60 runs
// over slots 0..3 and for the slot equal to 0x56DC75 takes only a reduced branch (4 or 6 = no slot excluded). With two
// players it is enough to exclude the other player's pad: on confirming a selection (0x457FAA) the one who just chose
// is excluded, score and shop exclude the other player's slot (0x8E2095 pad slot of player 1, written by 0x4E8180;
// 0x8E2094 of player 2, by 0x4E81A0; read by 0x4E81E0/0x4E81F0). The extra players' slots (2 and 3, Pads.cpp) are never
// excluded, so their pads used to command in every turn. Here a filter at the start of the loop (0x4EAAA0,
// MenuSlotStub): in an extra player's turn only that slot passes, in the turns of players 1 and 2 every slot but 2 and 3
// passes and the game decides the rest. (Letting only "the turn's slot" pass for players 1 and 2 did not work: in the
// turns the game changes which pad feeds records 0 and 1, so for players 1 and 2 the slot is not chosen, only the extra
// players' slots are taken out.)
//
// Hero selection (mode 1, CharSelScreen driven by the menu script): turns of player 1 and then 2 (player 2's circle goes
// back to player 1's turn); the script calls 0x47DF00 (player 1's choice) and 0x47DF20 (player 2's), then requests
// loading (0x47FA20(14), about three seconds later). Extra players' turn: that request reopens the selection
// (0x47FA20(1)); in the turn "of player 1" the extra player chooses, the choice does not touch 0x562A84 but becomes the
// extra player's hero (CharSelChoice, used by Coop.cpp at creation) and the next extra player's turn comes, or loading.
// A hero already chosen by the others reopens the selection.
//
// Selection scene object (0x456740 creates it, 0x456B40 reopens it clearing 0x562A84/0x562A88): the state of each hero
// is at +0x124 + hero*4, computed by 0x456210(object, hero): 4 if the hero is player 1's choice (+0x254) or player 2's
// (+0x258), i.e. already taken and not selectable (0x456610 answers 1); 5 available; 1, 2, 3 locked or completed.
// 0x4559E0 puts on the hero in state 4 the element Player1Number (+0x2F4, value +0x88 = 1.0) if it is player 1's choice,
// otherwise Player2Number (+0x2F8, 2.0). On confirm (0x457FAA) the scene excludes with 0x4EA200 the pad of the one who
// chose (0 for player 1's choice, 1 for player 2's). In an extra player's turn: 0x456210 answers 4 for the heroes already
// taken; Player1Number holds the extra player's number; the label of player 1 becomes "Player N".
#include "ROTKPlus.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <intrin.h>
#include <string>

static int* const kMode      = (int*)0x562AC0;
static int* const kPrevMode  = (int*)0x562AC4;
static int* const kModeExtra = (int*)0x562ABC;
static int* const kChoice2   = (int*)0x562A88;
static char* const kOwnProfile2 = (char*)0x562A9C;
static int* const kCoop      = (int*)0x562A98;
static float* const kOriginX = (float*)0x8E2F40;   // the drawing origin (Hud.cpp)
static float* const kOriginY = (float*)0x8E2F44;

enum { MODE_CHARSEL = 1, MODE_SCORING = 2, MODE_UPGRADE = 3, MODE_SCENESEL = 4, MODE_SAVE = 6, MODE_LOADING = 14,
       MODE_COOPSTATS = 16, MODE_MAINMENU = 17 };

static const uint8_t kAnySlot = 0xFF;

static int  g_lastHero[4] = { -1, -1, -1, -1 };   // heroes of the extra players (slots 2 and 3) in the last level played (valid in the screens after it)
static int  g_turn;             // 0 none, otherwise the mode of the extra player's round in progress
static int  g_turnSlot = 2;     // extra player of the round in progress (2 third, 3 fourth)
static bool g_done[4][32];      // round of the extra player already done, per slot and mode
static int  g_savedChoice2;
static char g_savedOwn2;

// hero selection
static bool g_csTurn;           // an extra player's turn in the current selection
static int  g_csSlot = 2;       // extra player of that turn (2 third, 3 fourth)
static bool g_csDone;           // every extra player has chosen, for this selection
static int  g_csChoice[4] = { -1, -1, -1, -1 };   // heroes chosen by the extra players (slots 2 and 3)
static int  g_csSaved1, g_csSaved2;
static uint8_t* g_csScene;      // selection scene object (from 0x456210), valid in mode 1

// slot filter in the menu loop 0x4EAA60, recomputed every frame: kAnySlot = all,
// kOnly | s = only slot s, kExcept2 = all but slots 2 and 3 (the extra players' pads)
static const uint8_t kOnly = 0x80, kExcept2 = 0x20;
static uint8_t g_menuSlot ASM_NAME(g_menuSlot) = kAnySlot;
static uint8_t* g_csLabel;          // element of player 1's label that was changed, nullptr = not changed
static uint32_t g_csLabelOld;       // its original text (+0x8C)
static uint8_t* g_csNumber;         // Player1Number that was changed
static char     g_csLabelText[128] = "@Player 3";  // of the extra player whose turn it is
static DWORD    g_csLoadAt;         // next step deferred after an extra player's choice (GetTickCount), 0 = none

void ScreensSetHeroes(int hero3, int hero4)   // from Coop.cpp when the extra players are created (and -1 -1 for levels without)
{
    g_lastHero[2] = hero3;
    g_lastHero[3] = hero4;
    for (auto& row : g_done) for (bool& d : row) d = false;
}

static void BeginTurn(int mode, int slot)
{
    g_turn = mode;
    g_turnSlot = slot;
    g_done[slot][mode] = true;
    g_savedChoice2 = *kChoice2;
    g_savedOwn2 = *kOwnProfile2;
    *kChoice2 = g_lastHero[slot];
    *kOwnProfile2 = 0;
    if (mode == 2) LogStats("end-of-level screens");
    Log("screens: turn of player %d (hero %d) in mode %d, pad %s", slot + 1, g_lastHero[slot], mode,
        PadConnected(slot) ? "its own" : "player 2's (pad not connected)");
}

static void EndTurn()
{
    if (!g_turn) return;
    *kChoice2 = g_savedChoice2;
    *kOwnProfile2 = g_savedOwn2;
    Log("screens: end of player %d's turn in mode %d", g_turnSlot + 1, g_turn);
    g_turn = 0;
}

static void RawSetMode(int m)
{
    *kPrevMode = *kMode;
    *kMode = m;
    *kModeExtra = -1;
    if (m == 0x12) ((void(__cdecl*)(int))0x4BCE50)(m);
}

// --- hero selection -----------------------------------------------------------------------------
int CharSelChoice(int slot) { return slot >= 2 && slot <= 3 ? g_csChoice[slot] : -1; }

// 0x4ED940(0, name): looks an element up by name (0x4F6A90 uses it and then also sets the visibility +0x74)
static uint8_t* FindElement(const char* name)
{
    return ((uint8_t*(__cdecl*)(int, const char*))0x4ED940)(0, name);
}

// --- names of the players in the hero selection (Players.cpp) -------------------------------------------------------
// The labels CharSelScreen.Player1Label / Player2Label hold the name of a game text ("charSelPlayer1Sel": "Player 1,
// select your character"). With a name of its own the name is added after the player's number: "Player 1 - Alex,
// select your character". The original pointer is put back when the selection is left (SetMode) or when an extra player's turn
// starts, and only if the element is still the one of the current screen.
struct NameLabel { uint8_t* e; uint32_t old; char text[128]; };
static NameLabel g_nameLabel[2];
static const char* const kLabelNames[2] = { "CharSelScreen.Player1Label", "CharSelScreen.Player2Label" };

// "Player <slot+1> - <name>, choose your hero" ("Giocatore 3 - Robin, scegli l'eroe"): the number stays, because it
// tells which controller's turn it is. The game's own ending ("seleziona il personaggio") made the line too long: with
// a longer name it went on two lines over the heroes' levels (test of 30/10/2026), so the ending is the mod's shorter
// text (Players.cpp, "PlayersChoose"). "" if the text does not look like "Player N ..."
static std::string NamedLabel(int slot, const std::string& base, char digit)
{
    const size_t at = base.find(digit);
    if (at == std::string::npos || at == 0 || at > 24) return "";
    return "@" + base.substr(0, at) + std::to_string(slot + 1) + " - " + PlayerName(slot) + ", " + PlayersChooseText();
}

static void RestoreNameLabels()
{
    for (int p = 0; p < 2; ++p) {
        NameLabel& L = g_nameLabel[p];
        if (L.e && FindElement(kLabelNames[p]) == L.e && *(const char**)(L.e + 0x8C) == L.text)
            *(uint32_t*)(L.e + 0x8C) = L.old;
        L.e = nullptr;
    }
}

static void NameLabelsFrame()
{
    if (*kMode != MODE_CHARSEL) { g_nameLabel[0].e = g_nameLabel[1].e = nullptr; return; }   // already put back by SetMode
    for (int p = 0; p < 2; ++p) {
        if (p == 0 && g_csTurn) continue;   // an extra player's turn: the label is theirs
        if (!PlayerHasName(p)) continue;
        uint8_t* e = FindElement(kLabelNames[p]);
        NameLabel& L = g_nameLabel[p];
        if (!e) continue;
        const char* cur = *(const char**)(e + 0x8C);
        if (L.e == e && cur == L.text) continue;            // already ours
        if (!cur || IsBadStringPtrA(cur, 64)) continue;
        const std::string base = cur[0] == '@' ? std::string(cur + 1) : LangGameText(cur);
        if (cur[0] != '@' && base == cur) continue;          // the game's texts are not there yet (it answers the key)
        const std::string text = NamedLabel(p, base, (char)('1' + p));
        if (text.empty()) continue;
        L.e = e;
        L.old = (uint32_t)(uintptr_t)cur;
        strncpy_s(L.text, text.c_str(), _TRUNCATE);
        *(const char**)(e + 0x8C) = L.text;
        static int logged;
        if (logged++ < 10) Log("selection: label of player %d -> \"%s\"", p + 1, L.text);
    }
}

static void CharSelRestoreScreen()
{
    if (g_csLabel) { *(uint32_t*)(g_csLabel + 0x8C) = g_csLabelOld; g_csLabel = nullptr; }
    if (g_csNumber) { *(float*)(g_csNumber + 0x88) = 1.0f; g_csNumber = nullptr; }
}

// label and number of player 1 -> the extra player's, when the scene is (re)opened during its turn
static void CharSelMarkScreen(uint8_t* scene)
{
    if (uint8_t* num = *(uint8_t**)(scene + 0x2F4)) {
        *(float*)(num + 0x88) = (float)(g_csSlot + 1);
        g_csNumber = num;
    }
    if (g_csLabel) return;
    static const char* const names[] = { "CharSelScreen.Player1Label", "CharSelScreen.player1label",
                                         "CharSelScreen.Player1", "CharSelScreen.P1Label" };
    for (const char* n : names) {
        uint8_t* e = FindElement(n);
        if (!e) continue;
        g_csLabel = e;
        g_csLabelOld = *(uint32_t*)(e + 0x8C);
        const char* old = (const char*)(uintptr_t)g_csLabelOld;
        char shown[64] = "?";
        if (old && !IsBadStringPtrA(old, 64)) {
            strncpy_s(shown, old, _TRUNCATE);
            // literal text ("@Player 1"): the same text with the extra player's number in place of the last 1;
            // otherwise (the label is the name of a game text, "charSelPlayer1Sel") the text in the game's language
            // with the number of the extra player whose turn it is (Lang.cpp)
            if (old[0] == '@') {
                strncpy_s(g_csLabelText, old, _TRUNCATE);
                if (char* d = strrchr(g_csLabelText, '1')) *d = (char)('0' + g_csSlot + 1);
            } else {
                sprintf_s(g_csLabelText, "@%s", LangText(3, g_csSlot + 1).c_str());
            }
            // the extra player's own name, if it has one ("Alex, select your character")
            if (PlayerHasName(g_csSlot)) {
                const std::string base = old[0] == '@' ? std::string(old + 1) : LangGameText(old);
                const std::string named = old[0] != '@' && base == old ? "" : NamedLabel(g_csSlot, base, '1');
                if (!named.empty()) strncpy_s(g_csLabelText, named.c_str(), _TRUNCATE);
            }
        }
        *(const char**)(e + 0x8C) = g_csLabelText;
        Log("selection: label %s (\"%s\") -> \"%s\"", n, shown, g_csLabelText);
        return;
    }
    Log("selection: element of player 1's label not found");
}

// replaces 0x456210(scene, hero): state of the hero in the scene
static NAKED void HeroStateOrig()   // push esi; mov esi,[esp+0Ch]; push edi
{
    ASM_BEGIN
    ASM(push esi)
    ASM(mov  esi, dword ptr [esp + 0x0C])
    ASM(push edi)
    ASM(push 0x456216)
    ASM(ret)
    ASM_END
}

static int __cdecl HeroState(uint8_t* scene, int hero)
{
    if (*kMode == MODE_CHARSEL) g_csScene = scene;
    if (g_csTurn && *kMode == MODE_CHARSEL) {
        if (hero == 0) CharSelMarkScreen(scene);
        if (hero == g_csSaved1 || hero == g_csSaved2 || (g_csSlot == 3 && hero == g_csChoice[2])) return 4;   // already taken: like the other players' choice
    }
    return GameFn((int(__cdecl*)(uint8_t*, int))&HeroStateOrig)(scene, hero);
}

// --- per-turn pads in the menus -----------------------------------------------------------------
// Filter: only in co-op with extra players, with at least one extra pad connected, and in the turn-based screens.
static uint8_t TurnSlot()
{
    if (!g_cfg.coop || *kCoop != 1 || (!PadConnected(2) && !PadConnected(3))) return kAnySlot;
    const int cur = *kMode;
    // 2 players chosen in the menu: the extra players' pads do not command in the turn-based screens
    if (!Coop3Wanted())
        return cur == MODE_CHARSEL || cur == MODE_SCORING || cur == MODE_UPGRADE || cur == MODE_COOPSTATS
                   ? kExcept2 : kAnySlot;
    if (cur == MODE_CHARSEL) {
        if (g_csTurn) return PadConnected(g_csSlot) ? kOnly | (uint8_t)g_csSlot : kAnySlot;
        if (g_csScene && *(const int*)(g_csScene + 0x254) != -1 && *(const int*)(g_csScene + 0x258) != -1)
            return kAnySlot;                                     // choices made, waiting for loading
        return kExcept2;                                         // turns of players 1 and 2
    }
    if (g_lastHero[2] < 0) return kAnySlot;
    if (cur == MODE_SCORING || cur == MODE_UPGRADE || cur == MODE_COOPSTATS) {
        if (g_turn != cur) return kExcept2;
        return PadConnected(g_turnSlot) ? kOnly | (uint8_t)g_turnSlot : kAnySlot;
    }
    return kAnySlot;
}

// the slot that alone commands the menus right now (an extra player's turn), -1 if none (used by Sim.cpp)
int ScreensOnlySlot() { return (g_menuSlot != kAnySlot && (g_menuSlot & kOnly)) ? (g_menuSlot & 0x0F) : -1; }

static void UpdateMenuSlot()
{
    const uint8_t s = TurnSlot();
    if (s != g_menuSlot) {
        if (s == kAnySlot) Log("menu: pad of any player (mode %d)", *kMode);
        else if (s & kOnly) Log("menu: only slot %d (mode %d)", s & 0x0F, *kMode);
        else Log("menu: all but slots 2 and 3 (mode %d)", *kMode);
    }
    g_menuSlot = s;
}

// at the start of every round of the menu loop 0x4EAA60 (0x4EAAA0, slot in bl): a slot excluded by the filter jumps
// to the next round (0x4EB25A); otherwise the two original instructions and on to 0x4EAAAC
static NAKED void MenuSlotStub()
{
    ASM_BEGIN
    ASM(mov  al, byte ptr [g_menuSlot])
    ASM(cmp  al, 0xFF)
    ASM(je   MenuSlotStub_orig)
    ASM(test al, 0x80)
    ASM(jz   MenuSlotStub_except)
    ASM(and  al, 0x0F)
    ASM(cmp  bl, al)
    ASM(je   MenuSlotStub_orig)
    ASM(jmp  MenuSlotStub_skip)
ASM(MenuSlotStub_except:)
    ASM(cmp  bl, 2)                  // kExcept2: skip slots 2 and 3
    ASM(jb   MenuSlotStub_orig)
ASM(MenuSlotStub_skip:)
    ASM(push 0x4EB25A)
    ASM(ret)
ASM(MenuSlotStub_orig:)
    ASM(cmp  bl, byte ptr ds:[0x56DC75])
    ASM(mov  byte ptr ds:[0x8E2111], bl)
    ASM(push 0x4EAAAC)
    ASM(ret)
    ASM_END
}

// replaces 0x4EA200(slot): slot excluded from the menus (0x56DC75)
static void __cdecl LockPad(int slot)
{
    *(uint8_t*)0x56DC75 = (uint8_t)slot;
}

static void CharSelEndTurn(const char* why)
{
    if (!g_csTurn) return;
    g_csTurn = false;
    CharSelRestoreScreen();
    Log("selection: end of player %d's turn (%s)", g_csSlot + 1, why);
}

static void CharSelBeginTurn(int slot)
{
    RestoreNameLabels();   // player 1's label becomes the extra player's (CharSelMarkScreen)
    g_csTurn = true;
    g_csSlot = slot;
    g_csSaved1 = *(const int*)0x562A84;
    g_csSaved2 = *kChoice2;
    Log("selection: turn of player %d (players 1 and 2: heroes %d and %d, third player %d), pad %s", slot + 1, g_csSaved1, g_csSaved2,
        g_csChoice[2], PadConnected(slot) ? "its own" : "any player's (pad not connected)");
}

// from 0x47DF00 (player 1) and 0x47DF20 (player 2), replaced in Support.cpp: true = the choice belongs to an extra player
bool CharSelOnChoice(int player, int hero)
{
    if (!g_csTurn || *kMode != MODE_CHARSEL) return false;
    if (g_csLoadAt) return true;    // the extra player has already chosen: waiting for the next step
    if (player != 1) return true;   // in an extra player's turn player 2 does not choose (for safety)
    if (hero < 0 || hero > 8 || hero == g_csSaved1 || hero == g_csSaved2 || (g_csSlot == 3 && hero == g_csChoice[2])) {
        Log("selection: player %d chose hero %d, already taken: the turn is repeated", g_csSlot + 1, hero);
        RawSetMode(MODE_CHARSEL);
        return true;
    }
    g_csChoice[g_csSlot] = hero;
    if (g_csSlot >= CoopPlayers() - 1) g_csDone = true;
    // reopening the screen the game clears player 2's choice (0x562A88 = -1) without going through 0x47DF20: loading
    // would start without a hero for player 2 and the game crashed
    Log("selection: choices before restoring %d and %d, restored %d and %d", *(const int*)0x562A84, *kChoice2,
        g_csSaved1, g_csSaved2);
    *(int*)0x562A84 = g_csSaved1;
    *kChoice2 = g_csSaved2;
    // as for players 1 and 2 (the script requests loading about 3.1 seconds after player 2's choice), wait for the
    // chosen hero to play its animation: loading immediately skipped it
    g_csLoadAt = GetTickCount() + 3100;
    if (!g_csLoadAt) g_csLoadAt = 1;
    Log("selection: player %d plays %d, next step in 3.1 seconds", g_csSlot + 1, hero);
    return true;
}

// every frame (Pads.cpp) and after every script call (Support.cpp): deferred actions
static void MainMenuFrame();

// end of the turn of an extra player who has chosen: the next one's turn, or loading starts
static void CharSelAdvance()
{
    *(int*)0x562A84 = g_csSaved1;
    *kChoice2 = g_csSaved2;
    CharSelEndTurn("choice made");
    if (g_csSlot < CoopPlayers() - 1) {
        CharSelBeginTurn(g_csSlot + 1);
        RawSetMode(MODE_CHARSEL);
        return;
    }
    Log("selection: loading after the last extra player's choice");
    RawSetMode(MODE_LOADING);
}

static void NameLabelsFrame();

void AudioFrame();   // Audio.cpp: the streams in the log

// --- log of the mouse clicks (test of 30/10/2026: one click still did two things, and not always through the menu's
// action handler): every edge of the left button, every action of the menus (ActionFilter) and every SetMode, with the
// time in ms, the slot, the screen mode and the selected entry, at most ClickLogMax lines ([Diag] ClickLog=1)
static int g_clickLog;
static int ClickLogMax;   // 400 with [Diag] ClickLog=1, else 0 (nothing written)

static const char* ElemName(uint32_t e)
{
    if (!e) return "-";
    const char* n = nullptr;
    if (!Guard([&] { n = ((const char*(__cdecl*)(uint32_t))0x4ED810)(e); if (n && IsBadStringPtrA(n, 64)) n = nullptr; }))
        n = nullptr;
    return n ? n : "?";
}

static DWORD g_leftDownAt;   // the last press of the left button (ClickLogMouse, every frame)
static bool  g_leftDownWas;  // the button at the last frame
static DWORD g_screenAt;     // the last change of screen (SetMode, or the screen's root in ScreensFrame)

// the left button pressed a moment ago: down at this frame for the first time, or pressed less than 300 ms ago. A
// button that is only down does not count: after Alt+Tab Windows may keep it down for good (Players.cpp, FromLeftClick)
static bool LeftJustPressed()
{
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    return (down && !g_leftDownWas) || (g_leftDownAt && GetTickCount() - g_leftDownAt < 300);
}

static void ClickLogMouse()
{
    bool& was = g_leftDownWas;
    const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (down && !was) g_leftDownAt = GetTickCount() | 1;
    if (down == was || g_clickLog >= ClickLogMax) { was = down; return; }
    was = down;
    ++g_clickLog;
    Log("click log: mouse left %s (mode %d, selected %s, page %s)", down ? "down" : "up", *kMode,
        ElemName(*(const uint32_t*)0x8E2F70), PlayersPageOpen() ? "open" : "closed");
}

void ScreensFrame()
{
    ClickLogMouse();
    {
        static uint32_t lastRoot;
        const uint32_t root = *(const uint32_t*)0x8E2F68;
        if (root != lastRoot) g_screenAt = GetTickCount() | 1;
        if (root != lastRoot && g_clickLog < ClickLogMax) {
            ++g_clickLog;
            Log("click log: screen %s (mode %d)", ElemName(root), *kMode);
        }
        lastRoot = root;
    }
    UpdateMenuSlot();
    // After a versus game straight to the map ([PvP] SkipScoreScreens): the score screen does not come through
    // 0x47FA20 (SetMode below): the menus' own code writes the mode (0x4EA3A6, 0x4EA93B, 0x4EA9AA) after the end of the
    // level (999). Tests of 04 and 05/11/2026: the screens came all the same. So when mode 2 appears right after a level
    // the map is asked at once (the level is left as won first: the statistics and the Last game).
    {
        static int lastMode = -1;
        const int m = *kMode;
        if (m == MODE_SCORING && (lastMode == 0 || lastMode == 998 || lastMode == 999) && *kCoop == 1 && PvpActive() &&
            PvpSkipScoreScreens()) {
            PlayersLevelLeave(true);
            EndTurn();
            Log("pvp: versus game over, straight to the map (no score screens, the score screen came from mode %d)", lastMode);
            RawSetMode(MODE_SCENESEL);
            if (PvpLastGame()) PlayersOpenRecap();
        }
        lastMode = *kMode;
    }
    if (PlayersRecapOpen() && *kMode != MODE_SCENESEL) PlayersCloseRecap();   // (left the map some other way)
    PlayersFrame();
    AudioFrame();
    MainMenuFrame();
    NameLabelsFrame();
    if (!g_csLoadAt || (int)(GetTickCount() - g_csLoadAt) < 0) return;
    g_csLoadAt = 0;
    if (!g_csTurn || *kMode != MODE_CHARSEL) return;
    CharSelAdvance();
}

// replaces 0x47FA20
// Versus mode: the level is chosen on the map as usual (the versus mode is meant to work on any level, enemies
// included, "everybody against everybody"). Skipping the map left the hero selection without its 3D scene and the game
// crashed, so the level is not forced.
static void RestoreNameLabels();
static void MenuDiagLeave(int m);

static void __cdecl SetMode(int m)
{
    if (m != *kMode) g_screenAt = GetTickCount() | 1;
    if (g_clickLog < ClickLogMax && m != *kMode) {
        ++g_clickLog;
        Log("click log: SetMode %d -> %d (selected %s)", *kMode, m, ElemName(*(const uint32_t*)0x8E2F70));
    }
    MenuDiagLeave(m);
    const int cur = *kMode, prev = *kPrevMode;
    // After a versus game (04/11/2026): the game's screens after a victory (the score of each player, the co-op
    // statistics, the shop of each player, the saving) have nothing to say about it (no rating; only the level's
    // experience, 1000 for every hero, which a versus game should not give), and with four players they were ten. The
    // level is left as towards the score screen (the statistics, the Last game) and the game goes straight to the map,
    // as a level left from inside does ([PvP] SkipScoreScreens=0: the game's own screens).
    // (the game itself writes the mode 999 at the end of a level and only then asks for the score screen: test of
    // 04/11/2026, the screens came all the same)
    if ((cur == 0 || cur == 998 || cur == 999) && m == MODE_SCORING && *kCoop == 1 && PvpActive() && PvpSkipScoreScreens()) {
        PlayersLevelLeave(true);
        EndTurn();
        Log("pvp: versus game over, straight to the map (no score screens)");
        RawSetMode(MODE_SCENESEL);
        if (PvpLastGame()) PlayersOpenRecap();
        return;
    }
    // statistics of the players (Players.cpp): leaving a level towards the score screen (victory), the map, the hero
    // selection or the main menu. Other modes (e.g. a movie) do not end the level, nor does a loading: a level loaded
    // again directly is "Retry", still the same level (PlayersLevelStart).
    if (cur == 0 && (m == MODE_SCORING || m == MODE_CHARSEL || m == MODE_SCENESEL || m == MODE_MAINMENU))
        PlayersLevelLeave(m == MODE_SCORING);
    if (cur == MODE_CHARSEL && m != MODE_CHARSEL) RestoreNameLabels();   // while the selection's elements still exist
    // hero selection
    if (m == MODE_CHARSEL && cur != MODE_CHARSEL) { g_csDone = false; g_csChoice[2] = g_csChoice[3] = -1; g_csScene = nullptr; }
    if (cur == MODE_CHARSEL && m == MODE_LOADING && Coop3Wanted() && *kCoop == 1 && !g_csDone && !g_csTurn) {
        CharSelBeginTurn(2);
        RawSetMode(MODE_CHARSEL);
        return;
    }
    // The script's loading request, about 3.1 s after an extra player's choice, arrives together with our timer: if more
    // extra players still have to choose it counts as the end of the turn (otherwise the fourth player would be skipped)
    if (g_csTurn && cur == MODE_CHARSEL && m == MODE_LOADING && g_csChoice[g_csSlot] >= 0 && g_csSlot < CoopPlayers() - 1) {
        g_csLoadAt = 0;
        CharSelAdvance();
        return;
    }
    if (g_csTurn && m != MODE_CHARSEL) {
        g_csLoadAt = 0;
        // leaving without the extra player's choice (e.g. circle, back to the map): players 1 and 2's choices as they were
        // before the turn
        *(int*)0x562A84 = g_csSaved1;
        *kChoice2 = g_csSaved2;
        CharSelEndTurn("left the selection");
    }
    if (g_lastHero[2] >= 0 && *kCoop == 1 && cur != m) {
        // leaving a screen: if it was an extra player's round it ends (or passes to the fourth player)
        if (g_turn == cur && !(cur == MODE_COOPSTATS && m != MODE_UPGRADE)) {
            const int next = g_turnSlot + 1;
            // the shop passes to the fourth player from "continue" (UpgradeContinue)
            if (cur != MODE_UPGRADE && next <= 3 && g_lastHero[next] >= 0 && !g_done[next][cur]) {
                EndTurn();
                BeginTurn(cur, next);
                RawSetMode(cur);
                return;
            }
            EndTurn();
        }
        // score and shop: player 2's turn is being left (previous == current) -> the third player's round
        else if ((cur == MODE_SCORING || cur == MODE_UPGRADE) && prev == cur && !g_done[2][cur]) {
            BeginTurn(cur, 2);
            RawSetMode(cur);
            return;
        }
        // co-op statistics: once in two columns -> a second time with the third player in the second column (only
        // towards the shop: the screen that was just opened changes mode by itself, and starting the third player's turn
        // at once hid player 2's page)
        else if (cur == MODE_COOPSTATS && m == MODE_UPGRADE && !g_done[2][cur]) {
            BeginTurn(cur, 2);
            RawSetMode(cur);
            return;
        }
    }
    if (m == MODE_SAVE || m == 4) EndTurn();   // safety: never leave the screens with the choice changed
    RawSetMode(m);
}

// --- shop: "continue" (0x4F4110) ----------------------------------------------------------------
// With the shop state 0x8E57A0 == 4 ("continue"): if it is player 2's turn (previous == 3) the game writes player 2's
// record back to its profile (0x47F9D0) and asks "save?" at once; otherwise, in co-op, it passes to player 2 with
// 0x47FA20(3). Here, at the end of player 2's turn, the game's part that concerns player 2 is done (closing the window
// 0x4F7920 and 0x47F9D0) and the third player's turn starts; the question comes at the end of the last extra player's
// turn. Third -> fourth: only the window is closed (the extra players do not own a profile).
static NAKED void ContinueOrig()   // mov eax,[8E57A0h]; add eax,-2
{
    ASM_BEGIN
    ASM(mov  eax, dword ptr ds:[0x8E57A0])
    ASM(add  eax, -2)
    ASM(push 0x4F4118)
    ASM(ret)
    ASM_END
}

static void __cdecl UpgradeContinue()
{
    if (*(const int*)0x8E57A0 == 4 && *kPrevMode == MODE_UPGRADE && *kMode == MODE_UPGRADE &&
        g_lastHero[2] >= 0 && *kCoop == 1) {
        int next = -1;
        if (g_turn != MODE_UPGRADE) { if (!g_done[2][MODE_UPGRADE]) next = 2; }                      // end of player 2 -> third
        else if (g_turnSlot == 2 && g_lastHero[3] >= 0 && !g_done[3][MODE_UPGRADE]) next = 3;      // end of the third -> fourth
        if (next >= 0) {
            ((void(__cdecl*)())0x4F7920)();
            if (next == 2) ((void(__cdecl*)())0x47F9D0)();   // player 2's record goes back to its profile
            else EndTurn();
            BeginTurn(MODE_UPGRADE, next);
            RawSetMode(MODE_UPGRADE);
            return;
        }
    }
    GameFn((void(__cdecl*)())&ContinueOrig)();
}

// --- main menu: "Co-op" and "Versus" with 2, 3 or 4 players --------------------------------------------
// The main menu (MainMenuScreen, mode 17) has the entries Single, Coop, Online, PlayTrailer, Exit; 0x4EEF60 enables Coop
// (and colours its child label CoopLoadText1) only with at least two controllers. The selected element is at 0x8E2F70.
// Buttons become actions in the loop 0x4EAA60 and go to the handler [0x56DC70] (default 0x4EA0F0, table at 0x4EA198):
// 0 and 2 move the cursor (0x4E9B60, 0x4E9BF0), 4 and 5 (right and left) go only to the selected element and do
// nothing on Coop. Here, with Coop selected, right and left cycle through 2, 3 and 4 players; the label of Coop shows
// the choice, which stays in ROTKPlus.ini ([Coop] Players). With 2 players no extra player is loaded or has turns.
// Coop has two child labels with the same text "mainMenuCoop", one for the normal entry and one for the highlighted
// one (changing only the first showed the original text when the cursor was on it): all child labels are changed.
//
// Adding a real entry would mean changing the screens' data; but the menu already has the entry Online, hidden by
// 0x4EEF10 (visibility from 0x4E4750) in the empty space between Credits and Exit. The two entries become
// "Co-op: N players" (Coop) and "Versus: N players" (Online, made visible and enabled with 0x4FC3A0), each with its
// own value in the ini ([Coop] Players, [PvP] Players). Actions go to the selected element's scripts: 6 the one at +0x78
// (0x4E9F70), 7 the one at +0x8C (0x4E9F00, the confirmation: Coop has its "rpn!" there), 8 the one at +0x90 (0x4E9E90).
// Confirming Versus moves the selection to Coop and lets the co-op script run, with the versus mode turned on;
// confirming Coop turns it off.
struct MenuItem {
    const char* name;      // element of the screen
    uint8_t*    elem;
    uint8_t*    texts[4];  // child labels that were changed (normal and highlighted)
    uint32_t    old[4];    // their original texts
    int         n;
    int         players;   // 2, 3 or 4 players
    char        shown[96];
};
static MenuItem g_coopItem = { "MainMenuScreen.Coop", nullptr, {}, {}, 0, 3 };
static MenuItem g_pvpItem  = { "MainMenuScreen.Online", nullptr, {}, {}, 0, 2 };
// "Players" (names and statistics, Players.cpp): a hidden entry made visible, Online with the Multiplayer page (Versus is
// not shown then), otherwise PlayTrailer (MainMenuFrame chooses)
static MenuItem g_playersItem = { "MainMenuScreen.PlayTrailer", nullptr, {}, {}, 0, 0 };
static int  g_wantN = 3;        // players of the game in progress (from the confirmed entry)

// a label of the main menu (type 0x82, the menu's font) for the texts of the players' page: the Co-op entry has two,
// one for the normal entry and one for the highlighted one
uint8_t* MenuTextTemplate(bool highlighted)
{
    if (*kMode != MODE_MAINMENU || !g_coopItem.elem || g_coopItem.n == 0) return nullptr;
    return g_coopItem.texts[highlighted && g_coopItem.n > 1 ? 1 : 0];
}

int  CoopPlayers() { return g_cfg.coop ? g_wantN : 2; }
bool Coop3Wanted() { return CoopPlayers() >= 3; }
bool Coop4Wanted() { return CoopPlayers() >= 4; }
static void ShowChoice(MenuItem& m)
{
    if (&m == &g_playersItem) {
        strncpy_s(m.shown, ("@" + PlayersMenuLabel()).c_str(), _TRUNCATE);
        return;
    }
    if (&m == &g_coopItem && PlayersMultiEnabled()) {   // "Multiplayer": its page chooses the rest (Players.cpp)
        strncpy_s(m.shown, ("@" + PlayersMultiLabel()).c_str(), _TRUNCATE);
        return;
    }
    const std::string label = LangText(&m == &g_coopItem ? 0 : 1);
    // Versus: also 2 vs 2 and the heroes only (Pvp.cpp)
    const std::string text = "@" + (&m == &g_pvpItem ? PvpChoiceLabel(label) : LangText(2, m.players, "", label.c_str()));
    strncpy_s(m.shown, text.c_str(), _TRUNCATE);
}

static void LogTree(uint8_t* e, int depth)
{
    if (!e || depth > 3) return;
    const char* t = *(const char**)(e + 0x8C);
    Log("main menu: %*s element %08X type %u text \"%s\"", depth * 2, "", (unsigned)(uintptr_t)e,
        *(const uint16_t*)(e + 0x76), t && !IsBadStringPtrA(t, 64) ? t : "");
    const uint32_t* kids = *(const uint32_t**)(e + 4);
    if (kids) for (uint32_t i = 1; i <= kids[0] && i < 16; ++i) LogTree((uint8_t*)(uintptr_t)kids[i], depth + 1);
}

static void RestoreItem(MenuItem& m)
{
    for (int i = 0; i < m.n; ++i) *(uint32_t*)(m.texts[i] + 0x8C) = m.old[i];
    m.n = 0;
    m.elem = nullptr;
}

// finds the entry and its child labels (text elements, type 0x82); true if the entry exists
static bool HookItem(MenuItem& m)
{
    uint8_t* e = FindElement(m.name);
    if (!e) { RestoreItem(m); return false; }
    if (e == m.elem) return true;
    RestoreItem(m);
    m.elem = e;
    LogTree(e, 0);
    const uint32_t* kids = *(const uint32_t**)(e + 4);
    if (kids)
        for (uint32_t i = 1; i <= kids[0] && i < 16 && m.n < 4; ++i) {
            uint8_t* k = (uint8_t*)(uintptr_t)kids[i];
            if (!k || *(const uint16_t*)(k + 0x76) != 0x82) continue;
            const char* kt = *(const char**)(k + 0x8C);
            if (!kt || IsBadStringPtrA(kt, 64)) continue;
            m.old[m.n] = (uint32_t)(uintptr_t)kt;
            m.texts[m.n++] = k;
        }
    ShowChoice(m);
    Log("main menu: entry %s, %d labels -> \"%s\"", m.name, m.n, m.shown);
    return true;
}

// Versus right below Co-op. The cursor (0x4E9B60/0x4E9BF0) runs over the screen's children (0x8E2F74, otherwise
// 0x8E2F68; +4 -> {count, elements...}) and goes to the next visible one of type 10: the order in the list counts, not the
// position. So the Online entry is moved in the list right after Coop and the visible entries in between move down one
// place: each takes the vertical position (+0x44) of the next, and Online the first's. Changing only +0x44 of the entries
// (type 10 groups) moved the cursor correctly but nothing on the screen: the child labels have positions of their own, so
// they are moved too, by the same amount. Once per menu opening (if it is already after Coop nothing is done).
static void MoveVersusUnderCoop()
{
    uint8_t* root = *(uint8_t**)0x8E2F74;
    if (!root) root = *(uint8_t**)0x8E2F68;
    if (!root || !g_coopItem.elem || !g_pvpItem.elem) return;
    uint32_t* kids = *(uint32_t**)(root + 4);
    if (!kids) return;
    const int n = (int)kids[0];
    int ic = -1, io = -1;
    for (int i = 0; i < n && i < 64; ++i) {
        if (kids[1 + i] == (uint32_t)(uintptr_t)g_coopItem.elem) ic = i;
        if (kids[1 + i] == (uint32_t)(uintptr_t)g_pvpItem.elem) io = i;
    }
    if (ic < 0 || io < 0 || io == ic + 1 || io < ic) return;
    // visible entries between Coop and Online, then Online: the positions slide one place down
    uint8_t* row[16]; int r = 0;
    for (int i = ic + 1; i <= io && r < 16; ++i) {
        uint8_t* e = (uint8_t*)(uintptr_t)kids[1 + i];
        if (e && *(const uint16_t*)(e + 0x76) == 10 && (*(const uint16_t*)(e + 0x74) != 0 || e == g_pvpItem.elem)) row[r++] = e;
    }
    if (r >= 2) {
        // position of an entry: that of its first label (the groups may all have the same one)
        auto rowY = [](uint8_t* e) {
            const uint32_t* sub = *(const uint32_t**)(e + 4);
            return sub && sub[0] && sub[1] ? *(float*)(uintptr_t)(sub[1] + 0x44) : *(float*)(e + 0x44);
        };
        float y[16];
        for (int k = 0; k < r; ++k) y[k] = rowY(row[k]);
        for (int k = 0; k < r; ++k) {
            const float target = k + 1 < r ? y[k + 1] : y[0];   // the last (Online) takes the first's place
            const float dy = target - y[k];
            *(float*)(row[k] + 0x44) += dy;
            const uint32_t* sub = *(const uint32_t**)(row[k] + 4);
            if (sub)
                for (uint32_t i = 1; i <= sub[0] && i < 16; ++i)
                    if (uint8_t* c = (uint8_t*)(uintptr_t)sub[i]) *(float*)(c + 0x44) += dy;
        }
    }
    // in the list: Online right after Coop
    const uint32_t online = kids[1 + io];
    for (int i = io; i > ic + 1; --i) kids[1 + i] = kids[i];
    kids[1 + ic + 1] = online;
    Log("main menu: Versus moved under Co-op (%d entries moved down one place; positions %g %g %g)", r - 1,
        r > 0 ? *(float*)(row[0] + 0x44) : 0.0f, r > 1 ? *(float*)(row[1] + 0x44) : 0.0f, r > 2 ? *(float*)(row[2] + 0x44) : 0.0f);
}

// "Players" right below Versus (or Co-op). Unlike MoveVersusUnderCoop, where the hidden entry already sat in the free
// place of the list, the place of PlayTrailer is not known: every visible entry BELOW the anchor on the screen moves
// down by one row (the smallest distance between the anchor and an entry below it) and the item takes the place right
// below the anchor. The position on the screen counts, not the order in the list: Exit is not after Versus in the list
// (the first version moved only Options and Credits, and Exit ended up on top of Credits). Group and labels move
// together, as above. Once per menu opening; in the list the item goes right after the anchor (cursor order).
static float RowY(uint8_t* e)
{
    const uint32_t* sub = *(const uint32_t**)(e + 4);
    return sub && sub[0] && sub[1] ? *(float*)(uintptr_t)(sub[1] + 0x44) : *(float*)(e + 0x44);
}

static void ShiftRow(uint8_t* e, float dy)
{
    *(float*)(e + 0x44) += dy;
    const uint32_t* sub = *(const uint32_t**)(e + 4);
    if (sub)
        for (uint32_t i = 1; i <= sub[0] && i < 16; ++i)
            if (uint8_t* c = (uint8_t*)(uintptr_t)sub[i]) *(float*)(c + 0x44) += dy;
}

static void MoveUnder(uint8_t* anchor, uint8_t* item)
{
    uint8_t* root = *(uint8_t**)0x8E2F74;
    if (!root) root = *(uint8_t**)0x8E2F68;
    if (!root || !anchor || !item) return;
    uint32_t* kids = *(uint32_t**)(root + 4);
    if (!kids) return;
    const int n = (int)kids[0];
    int ia = -1, ii = -1;
    for (int i = 0; i < n && i < 64; ++i) {
        if (kids[1 + i] == (uint32_t)(uintptr_t)anchor) ia = i;
        if (kids[1 + i] == (uint32_t)(uintptr_t)item) ii = i;
    }
    if (ia < 0 || ii < 0 || ii == ia + 1) return;
    auto visibleEntry = [](uint8_t* e) { return e && *(const uint16_t*)(e + 0x76) == 10 && *(const uint16_t*)(e + 0x74) != 0; };
    const float ya = RowY(anchor);
    uint8_t* rows[16]; int r = 0;
    float d = 0.0f, above = 0.0f;   // smallest distance below the anchor; above it, as a fallback
    for (int i = 0; i < n && i < 64; ++i) {
        uint8_t* e = (uint8_t*)(uintptr_t)kids[1 + i];
        if (e == item || e == anchor || !visibleEntry(e)) continue;
        const float y = RowY(e);
        if (y > ya + 0.001f) {
            if (r < 16) rows[r++] = e;
            if (d == 0.0f || y - ya < d) d = y - ya;
        } else if (y < ya - 0.001f && (above == 0.0f || ya - y < above)) above = ya - y;
    }
    if (d == 0.0f) d = above;
    if (!(d > 0.0f) || d > 200.0f) { Log("main menu: Players entry not moved (row distance %g)", d); return; }
    // what the menu looks like (to understand a wrong layout from the log)
    for (int i = 0; i < n && i < 64; ++i)
        if (uint8_t* e = (uint8_t*)(uintptr_t)kids[1 + i])
            Log("main menu: child %d type %u visible %u y %g%s", i, *(const uint16_t*)(e + 0x76), *(const uint16_t*)(e + 0x74),
                RowY(e), e == anchor ? " (anchor)" : e == item ? " (Players)" : "");
    for (int k = 0; k < r; ++k) ShiftRow(rows[k], d);
    ShiftRow(item, RowY(anchor) + d - RowY(item));
    // Exit, the last entry on the screen, was not among the entries found below (second test: it stayed on top of
    // Credits): looked up by name and moved too, if it was not moved already
    if (uint8_t* ex = FindElement("MainMenuScreen.Exit")) {
        bool moved = false;
        for (int k = 0; k < r; ++k) moved |= rows[k] == ex;
        if (!moved && ex != anchor && ex != item) {
            Log("main menu: Exit at y %g (type %u) moved down by %g too", RowY(ex), *(const uint16_t*)(ex + 0x76), d);
            ShiftRow(ex, d);
        }
    }
    // in the list: right after the anchor
    const uint32_t moved = kids[1 + ii];
    if (ii > ia) { for (int i = ii; i > ia + 1; --i) kids[1 + i] = kids[i]; kids[1 + ia + 1] = moved; }
    else { for (int i = ii; i < ia; ++i) kids[1 + i] = kids[2 + i]; kids[1 + ia] = moved; }
    Log("main menu: Players entry placed under %s (%d entries moved down by %g)",
        anchor == g_pvpItem.elem ? "Versus" : "Co-op", r, d);
}

// while the players' page is open the menu's entries are hidden (and shown again when it closes); also the type 20
// group (0x4FA040 draws its children), the glowing star that marks the selected entry: it stayed where Fellowship was,
// over the pages' lists, and could not be moved (test of 03/11/2026)
static uint8_t* g_hidden[32];
static uint16_t g_hiddenVis[32];
static int      g_nHidden;

static void HideMenuEntries()
{
    uint8_t* root = *(uint8_t**)0x8E2F74;
    if (!root) root = *(uint8_t**)0x8E2F68;
    if (!root) return;
    const uint32_t* kids = *(const uint32_t**)(root + 4);
    if (!kids) return;
    for (uint32_t i = 1; i <= kids[0] && i < 64; ++i) {
        uint8_t* e = (uint8_t*)(uintptr_t)kids[i];
        const uint16_t type = e ? *(const uint16_t*)(e + 0x76) : 0;
        if (type != 10 && type != 20) continue;
        if (type == 20) {
            static bool logged;
            if (!logged) { logged = true; Log("main menu: group %s (type 20) hidden while a page is open", ElemName((uint32_t)(uintptr_t)e)); }
        }
        uint16_t* vis = (uint16_t*)(e + 0x74);
        if (!*vis) continue;
        int k = 0;
        while (k < g_nHidden && g_hidden[k] != e) ++k;
        if (k == g_nHidden) {
            if (g_nHidden >= 32) continue;
            g_hidden[g_nHidden] = e; g_hiddenVis[g_nHidden] = *vis; ++g_nHidden;
        }
        *vis = 0;
    }
}

static void ShowMenuEntries()
{
    for (int k = 0; k < g_nHidden; ++k) *(uint16_t*)(g_hidden[k] + 0x74) = g_hiddenVis[k];
    g_nHidden = 0;
}

// --- the main menu's demo (diagnostics, [Diag] MenuTimerLog=1) ----------------------------------------------------
// The main menu starts its demo after about 60 s without activity. Our pages take the menu's actions, so the demo also
// starts while a page is being used (test of 30/10/2026). What counts the 60 s is not in the C code: moving in the
// menu runs the entries' scripts (0x4E9890, "rpn!" scripts), and the front end's data has an element called
// "AttractTim...". Written here: the elements whose name has "attract" in it, with their first 0xC0 bytes and the text
// at +0x8C (the script, if it is one), the bytes that change every second while the main menu is up, and who leaves
// the main menu (SetMode) with the callers on the stack. Element names: table at [0x8DCF1C], +4 its size in bytes,
// entries of 3 dwords from +8 (name, ?, element), "XXXX" ends it (0x4ED6D0, 0x4ED810).
static bool g_menuDiag;
static int  g_menuDiagLines;
struct WatchedElem { uint32_t e; const char* name; uint32_t last[48]; };
static WatchedElem g_watch[8];
static int         g_nWatch;
static uint32_t    g_watchTable;

static bool NameHas(const char* s, const char* low)   // case-insensitive, at most 80 characters of s
{
    for (int i = 0; i < 80 && s[i]; ++i) {
        int k = 0;
        while (low[k] && s[i + k] && (s[i + k] | 0x20) == low[k]) ++k;
        if (!low[k]) return true;
    }
    return false;
}

static void Append(char* buf, size_t size, size_t& n, const char* fmt, ...)
{
    if (n + 1 >= size) return;
    va_list ap;
    va_start(ap, fmt);
    const int r = _vsnprintf_s(buf + n, size - n, _TRUNCATE, fmt, ap);
    va_end(ap);
    n = r < 0 ? size - 1 : n + r;
}

static void MenuDiagLine(const char* s)
{
    if (g_menuDiagLines >= 600) return;
    if (++g_menuDiagLines == 600) { Log("main menu demo: log limit reached"); return; }
    Log("main menu demo: %s", s);
}

// who leaves the main menu (from SetMode): the new mode and the game's return addresses on the stack
static void MenuDiagLeave(int m)
{
    if (!g_menuDiag || *kMode != MODE_MAINMENU || m == MODE_MAINMENU) return;
    char line[300];
    size_t n = 0;
    Append(line, sizeof line, n, "the main menu is left for mode %d (page %s); callers:", m, PlayersPageOpen() ? "open" : "closed");
    const uint32_t* sp = (const uint32_t*)_AddressOfReturnAddress();
    for (int i = 0, found = 0; i < 256 && found < 12; ++i) {
        uint32_t v;
        if (!Guard([&] { v = sp[i]; })) break;
        if (v < 0x401005 || v >= 0x535000) continue;
        const uint8_t* r = (const uint8_t*)(uintptr_t)v;
        if (r[-5] == 0xE8 || (r[-6] == 0xFF && (r[-5] & 0x38) == 0x10) || (r[-2] == 0xFF && (r[-1] & 0x38) == 0x10)) {
            Append(line, sizeof line, n, " %08X", v);
            ++found;
        }
    }
    MenuDiagLine(line);
}

static void MenuTimerFrame()
{
    static DWORD next;
    const DWORD now = GetTickCount();
    if ((int)(now - next) < 0) return;
    next = now + 1000;
    const uint32_t table = *(const uint32_t*)0x8DCF1C;
    if (!table) return;
    char line[900];
    if (table != g_watchTable) {   // a new front end: look for the elements again
        g_watchTable = table;
        g_nWatch = 0;
        Guard([&] {
            const uint32_t size = *(const uint32_t*)(table + 4);
            for (uint32_t p = table + 8; p + 12 <= table + size && g_nWatch < 8; p += 12) {
                const uint32_t name = *(const uint32_t*)p, e = *(const uint32_t*)(p + 8);
                if (name == 0x58585858 || *(const uint32_t*)(p + 4) == 0x58585858) break;
                if (!name || !e || !NameHas((const char*)(uintptr_t)name, "attract")) continue;
                WatchedElem& w = g_watch[g_nWatch++];
                w.e = e;
                w.name = (const char*)(uintptr_t)name;
                memcpy(w.last, (const void*)(uintptr_t)e, sizeof w.last);
                size_t n = 0;
                Append(line, sizeof line, n, "element \"%.40s\" at %08X, type %d:", w.name, e,
                       *(const int16_t*)(uintptr_t)(e + 0x76));
                for (int i = 0; i < 48; ++i) Append(line, sizeof line, n, " %08X", w.last[i]);
                MenuDiagLine(line);
                const char* text = *(const char* const*)(uintptr_t)(e + 0x8C);
                if (text) {
                    n = 0;
                    Append(line, sizeof line, n, "  text at +0x8C: ");
                    for (int k = 0; k < 400 && text[k] && n + 1 < sizeof line; ++k)
                        line[n++] = (text[k] >= 32 && text[k] < 127) ? text[k] : '.';
                    line[n] = 0;
                    MenuDiagLine(line);
                }
            }
        });
        if (!g_nWatch) MenuDiagLine("no element with \"attract\" in its name");
    }
    for (int w = 0; w < g_nWatch; ++w) {
        uint32_t cur[48];
        if (!Guard([&] { memcpy(cur, (const void*)(uintptr_t)g_watch[w].e, sizeof cur); })) continue;
        size_t n = 0;
        Append(line, sizeof line, n, "\"%.40s\" (page %s):", g_watch[w].name, PlayersPageOpen() ? "open" : "closed");
        const size_t head = n;
        for (int i = 0; i < 48; ++i)
            if (cur[i] != g_watch[w].last[i])
                Append(line, sizeof line, n, " +%X %08X->%08X (%.2f)", i * 4, g_watch[w].last[i], cur[i],
                       *(const float*)&cur[i]);
        memcpy(g_watch[w].last, cur, sizeof cur);
        if (n > head) MenuDiagLine(line);
    }
}

// The main menu's demo. The front end's element "AttractTimer" counts at +0x80, about 1200 a second (20 a frame at 60
// fps), up to +0x84 (72000: its 60.0 seconds at +0x78); then its script leaves the main menu for the demo (SetMode(14)
// from 0x480D69). Moving in the menu puts the counter back to 0 (test of 30/10/2026, [Diag] MenuTimerLog=1). Our pages
// take the menu's actions, so every action or key they use does the same.
void MenuIdleReset()
{
    Guard([&] {
        uint8_t* e = FindElement("AttractTimer");
        if (!e) return;
        const float secs = *(const float*)(e + 0x78);
        if (secs > 1.0f && secs < 1000.0f && *(const uint32_t*)(e + 0x84) != 0) *(uint32_t*)(e + 0x80) = 0;
    });
}

// The pointer over an entry of the main menu selects it. Test of 30/10/2026: with the Fellowship entry the pointer no
// longer moved the selection (the keys did). The game's own way is a command of the menu's scripts (0x4FF040, called
// through the table at 0x570100): the first child of the screen (*0x8E2F68) under the pointer, by 0x4FE9F0(element, x,
// y, 0) for each, becomes the selection (0x8E2F70) if it is an entry (type 10). Doing the same test here found the
// entry two places below the pointer (the pointer on Single lit Options), with the screen at 0, 0: the test's positions
// are not where the entries are drawn. So the entries' places are taken where they are really drawn: every label
// (type 0x82) of an entry, when the dispatcher draws it (ScreensSawElement, from Hud.cpp), is at the drawing origin
// (0x8E2F40/44) plus its own x / y, as 0x4FB800 computes them; the pointer, in the same absolute fractions as our
// pages' texts, is compared with those rows.
struct EntryPos { uint32_t entry; float x, y; };
static EntryPos g_entryPos[24];
static int      g_nEntryPos;
static uint32_t g_entryRoot;   // the screen the entries belong to: a new front end has new elements (old pointers die)

static float ElemX(uint32_t e) { return ((float(__cdecl*)(uint32_t))0x4F9FE0)(e); }   // x / y as fractions
static float ElemY(uint32_t e) { return ((float(__cdecl*)(uint32_t))0x4FA010)(e); }

void ScreensSawElement(uint32_t e)
{
    if (!e || *kMode != MODE_MAINMENU || PlayersPageOpen()) return;
    const uint32_t root = *(const uint32_t*)0x8E2F68;
    if (root != g_entryRoot) { g_entryRoot = root; g_nEntryPos = 0; }   // noted again at the next frame
    Guard([&] {
        if (*(const uint16_t*)(uintptr_t)(e + 0x76) != 0x82 || !*(const uint16_t*)(uintptr_t)(e + 0x74)) return;
        const uint32_t parent = *(const uint32_t*)(uintptr_t)e;   // +0: the parent (as 0x4FDEE0 reads it)
        if (!parent || *(const uint16_t*)(uintptr_t)(parent + 0x76) != 10) return;
        const float x = *kOriginX + ElemX(e), y = *kOriginY + ElemY(e);
        int i = 0;
        while (i < g_nEntryPos && g_entryPos[i].entry != parent) ++i;
        if (i == g_nEntryPos) {
            if (g_nEntryPos >= (int)(sizeof g_entryPos / sizeof g_entryPos[0])) return;
            ++g_nEntryPos;
        }
        g_entryPos[i] = EntryPos{ parent, x, y };
    });
}

static void MenuHover()
{
    static float lastX = -1.0f, lastY = -1.0f;
    static uint32_t lastOver = 1;
    static int logged;
    if (!*(const uint8_t*)0x8E38BC || !*(const uint32_t*)0x8E38C4) return;
    if (*(const uint32_t*)0x8E2F68 != g_entryRoot) return;   // the entries noted belong to another front end
    float x = 0.0f, y = 0.0f;
    if (!Guard([&] { ((void(__cdecl*)(float*, float*))0x501800)(&x, &y); })) return;
    if (fabsf(x - lastX) < 0.002f && fabsf(y - lastY) < 0.002f) return;
    lastX = x; lastY = y;
    uint32_t over = 0;
    float overY = 0.0f;
    if (!Guard([&] {
        // the visible entry whose row (from its label's top to a little below it) holds the pointer
        for (int i = 0; i < g_nEntryPos; ++i) {
            const EntryPos& p = g_entryPos[i];
            if (!*(const uint16_t*)(uintptr_t)(p.entry + 0x74)) continue;
            if (y >= p.y - 0.01f && y < p.y + 0.06f && x >= p.x - 0.30f && x < p.x + 0.50f) { over = p.entry; overY = p.y; break; }
        }
        if (over && *(const uint32_t*)0x8E2F70 != over) *(uint32_t*)0x8E2F70 = over;
    }))
        return;
    if (over != lastOver && logged < 12) {
        ++logged;
        const char* name = over ? ((const char*(__cdecl*)(uint32_t))0x4ED810)(over) : nullptr;
        Log("main menu: pointer at %.3f %.3f over entry %s (row at %.3f, %d entries known)", x, y,
            name && !IsBadStringPtrA(name, 64) ? name : over ? "?" : "none", overY, g_nEntryPos);
    }
    lastOver = over;
}

// every frame in the main menu: labels of the entries, Versus and Players entries visible and enabled
static void MainMenuFrame()
{
    if (g_menuDiag && *kMode == MODE_MAINMENU) MenuTimerFrame();
    if (!g_cfg.coop || *kMode != MODE_MAINMENU) {
        ShowMenuEntries();
        RestoreItem(g_coopItem); RestoreItem(g_pvpItem); RestoreItem(g_playersItem);
        return;
    }
    if (HookItem(g_coopItem))
        for (int i = 0; i < g_coopItem.n; ++i) *(const char**)(g_coopItem.texts[i] + 0x8C) = g_coopItem.shown;
    if (PlayersPageOpen()) { HideMenuEntries(); return; }
    ShowMenuEntries();
    if (PvpAllowed() && !PlayersMultiEnabled()) {   // (with the Multiplayer page the versus games start from there)
        uint8_t* before = g_pvpItem.elem;
        if (HookItem(g_pvpItem)) {
            *(uint16_t*)(g_pvpItem.elem + 0x74) = 1;
            for (int i = 0; i < g_pvpItem.n; ++i) {
                *(uint16_t*)(g_pvpItem.texts[i] + 0x74) = 1;
                *(const char**)(g_pvpItem.texts[i] + 0x8C) = g_pvpItem.shown;
            }
            if (g_pvpItem.elem != before) ((void(__cdecl*)(uint8_t*))0x4FC3A0)(g_pvpItem.elem);   // enabled
            MoveVersusUnderCoop();
        }
    }
    if (PlayersEnabled()) {
        // Fellowship takes the entry Versus leaves free with the Multiplayer page (Online, hidden by the game in every
        // edition), so PlayTrailer, which the English edition shows, keeps its own trailer; otherwise PlayTrailer
        g_playersItem.name = PlayersMultiEnabled() ? "MainMenuScreen.Online" : "MainMenuScreen.PlayTrailer";
        uint8_t* before = g_playersItem.elem;
        if (HookItem(g_playersItem)) {
            *(uint16_t*)(g_playersItem.elem + 0x74) = 1;
            for (int i = 0; i < g_playersItem.n; ++i) {
                *(uint16_t*)(g_playersItem.texts[i] + 0x74) = 1;
                *(const char**)(g_playersItem.texts[i] + 0x8C) = g_playersItem.shown;
            }
            if (g_playersItem.elem != before) ((void(__cdecl*)(uint8_t*))0x4FC3A0)(g_playersItem.elem);   // enabled
            MoveUnder(PvpAllowed() && !PlayersMultiEnabled() && g_pvpItem.elem ? g_pvpItem.elem : g_coopItem.elem,
                      g_playersItem.elem);
        }
    }
    MenuHover();
}

static NAKED void MenuActionOrig()   // mov al,[8E2111h]
{
    ASM_BEGIN
    ASM(mov  al, byte ptr ds:[0x8E2111])
    ASM(push 0x4EA0F5)
    ASM(ret)
    ASM_END
}

// "Who is playing?" (Players.cpp): the confirmation of Single, Co-op or Versus is held back while the page is open and
// run afterwards as the game would have: the same selected element (0x8E2F70) and the same controller's slot
// ([0x8E2111], which 0x4EA0F0 copies to 0x56DC74 for the entry's script)
static uint32_t g_pendSel;
static uint8_t  g_pendSlot;
static int      g_pendKind;     // 0 none, 1 Single, 2 Co-op, 3 Versus, 4 Multiplayer (its page says which)

static void ConfirmPending()
{
    const int kind = g_pendKind;
    g_pendKind = 0;
    if (!kind || *kMode != MODE_MAINMENU) return;
    ShowMenuEntries();   // as they were before the page
    uint32_t sel = g_pendSel;
    MultiChoice mc;
    if (kind == 4 && PlayersMultiChoice(mc)) {
        // the Co-op entry's own script in both cases; a versus game with the page's rules
        g_wantN = mc.players;
        if (!mc.versus) g_coopItem.players = mc.players;
        if (mc.versus) PvpConfigure(mc);
        PvpSetActive(mc.versus);
        Log("main menu: confirmed Multiplayer, %s with %d players", mc.versus ? "a versus game" : "the campaign", mc.players);
        if (g_coopItem.elem) sel = (uint32_t)(uintptr_t)g_coopItem.elem;
    } else if (kind >= 2) {
        MenuItem& m = kind == 3 ? g_pvpItem : g_coopItem;
        g_wantN = m.players;
        PvpSetActive(kind == 3);
        Log("main menu: confirmed %s with %d players", kind == 3 ? "Versus" : "Co-op", g_wantN);
        if (kind == 3 && g_coopItem.elem) sel = (uint32_t)(uintptr_t)g_coopItem.elem;   // the co-op script
    } else Log("main menu: confirmed Single");
    *(uint32_t*)0x8E2F70 = sel;
    uint8_t* const actSlot = (uint8_t*)0x8E2111;
    const uint8_t was = *actSlot;
    *actSlot = g_pendSlot;
    GameFn((void(__cdecl*)(int))&MenuActionOrig)(7);
    *actSlot = was;
}

// replaces 0x4EA0F0(action): the menus' default action handler
static void __cdecl MenuAction(int action)
{
    const uint32_t sel = *(const uint32_t*)0x8E2F70;
    const uint8_t slot = *(const uint8_t*)0x8E2111;   // the controller's slot of this action
    // the players' page takes the actions while it is open; confirming the Players entry opens it
    if (*kMode == MODE_MAINMENU &&
        PlayersMenuAction(action, sel && g_playersItem.elem && sel == (uint32_t)(uintptr_t)g_playersItem.elem, slot)) {
        // a key or button of the page (up, down, left, right, confirm, back): the menu is in use, no demo
        if (action == 0 || action == 2 || action == 4 || action == 5 || action == 7 || action == 8) MenuIdleReset();
        if (PlayersLineupDone()) ConfirmPending();
        return;
    }
    if (*kMode == MODE_MAINMENU && sel && action == 7 && PlayersMultiEnabled() && g_coopItem.elem &&
        sel == (uint32_t)(uintptr_t)g_coopItem.elem && *(const uint32_t*)(uintptr_t)(sel + 0x20) == 0) {
        // Multiplayer: its page first; the extra players' controllers are looked for as with 4 players (their own
        // places on the page), the real number comes with Start
        g_wantN = 4;
        if (PlayersOpenMulti()) {
            g_pendSel = sel;
            g_pendSlot = slot;
            g_pendKind = 4;
            Log("main menu: Multiplayer chosen (slot %u), its page first", slot);
            return;
        }
    }
    if (*kMode == MODE_MAINMENU && sel && action == 7 && PlayersAskWhoPlays()) {
        uint8_t* single = FindElement("MainMenuScreen.Single");
        const bool isSingle = single && sel == (uint32_t)(uintptr_t)single;
        MenuItem* m = sel == (uint32_t)(uintptr_t)g_coopItem.elem ? &g_coopItem
                    : sel == (uint32_t)(uintptr_t)g_pvpItem.elem ? &g_pvpItem : nullptr;
        // only an enabled entry (+0x20 == 0, see 0x4FC3A0): a disabled one goes to the game as usual
        if ((isSingle || m) && *(const uint32_t*)(uintptr_t)(sel + 0x20) == 0) {
            const int n = isSingle ? 1 : m->players;
            if (m) g_wantN = n;   // early: the fourth player's controller is looked for only with 4 players
            if (PlayersOpenLineup(n)) {
                g_pendSel = sel;
                g_pendSlot = slot;
                g_pendKind = isSingle ? 1 : m == &g_coopItem ? 2 : 3;
                Log("main menu: %s chosen (slot %u), who is playing? first", isSingle ? "Single" : m == &g_coopItem ?
                    "Co-op" : "Versus", slot);
                return;
            }
        }
    }
    if (*kMode == MODE_MAINMENU && sel) {
        MenuItem* m = sel == (uint32_t)(uintptr_t)g_coopItem.elem ? &g_coopItem
                    : sel == (uint32_t)(uintptr_t)g_pvpItem.elem ? &g_pvpItem : nullptr;
        if (m == &g_coopItem && PlayersMultiEnabled() && (action == 4 || action == 5)) m = nullptr;   // Multiplayer: nothing to change on it
        if (m && (action == 4 || action == 5)) {   // right (4) adds a player, left (5) removes one: 2 -> 3 -> 4 and round again
            if (m == &g_pvpItem) {   // Versus: 2, 3, 4, 2 vs 2, then the same with the heroes only (Pvp.cpp keeps it)
                PvpChoiceStep(action == 4 ? 1 : -1);
                m->players = PvpChoicePlayers();
                ShowChoice(*m);
                Log("main menu: Versus, %s", m->shown + 1);
                return;
            }
            m->players += action == 4 ? 1 : -1;
            if (m->players > 4) m->players = 2;
            if (m->players < 2) m->players = 4;
            ShowChoice(*m);
            IniWriteInt("Coop", "Players", m->players);
            Log("main menu: Co-op with %d players", m->players);
            return;
        }
        if (m && action == 7) {   // confirm
            const bool pvp = m == &g_pvpItem;
            g_wantN = m->players;
            PvpSetActive(pvp);
            Log("main menu: confirmed %s with %d players", m == &g_coopItem ? "Co-op" : "Versus", g_wantN);
            if (pvp && g_coopItem.elem) *(uint32_t*)0x8E2F70 = (uint32_t)(uintptr_t)g_coopItem.elem;   // the co-op script
        }
    }
    GameFn((void(__cdecl*)(int))&MenuActionOrig)(action);
}

// One mouse click brings two confirms: the pointer's (slot 3) and, about 15 ms later, one from slot 0 (the mouse button
// bound as a key). Where the first changes screen, the second lands on the new one: Back in Options or Credits returned
// to the main menu and the second confirm chose Single, the entry selected there (test of 30/10/2026; ROTK Fix removes
// the game's 50 ms pause on screen changes, which may have hidden it). Filtering it in our handler of the main menu
// (MenuAction) was not enough: Options and Credits have handlers of their own, and the first confirm went there. The
// menu loop (0x4EAA60) calls whatever handler [0x56DC70] holds, and only 0x4EA1D0 changes it (returning the previous
// one): that pointer now always holds ActionFilter, and 0x4EA1D0 (hooked) changes the handler ActionFilter calls. A
// confirm within 250 ms of one from another slot, either of them from slot 3, is dropped, in every screen.
static uintptr_t g_handler = 0x4EA0F0;   // the screen's own handler

// The Last game over the map after a versus game: the map's keys do not come through the menus' actions (the menu
// script passes the buttons as bits to 0x4F16A0, at 0x4F8391, Pvp.cpp's MapMove), so while it is open they are
// swallowed there too and the map's cursor stays on the arena just played.
static DWORD g_recapClosedAt;   // the key that closed it must not reach the map too (it would choose the arena)

bool ScreensMapFilter(int& input)
{
    (void)input;
    if (PlayersRecapOpen()) return false;
    return !(g_recapClosedAt && (int)(GetTickCount() - g_recapClosedAt) < 500);
}

static void __cdecl ActionFilter(int action)
{
    // the Last game over the map after a versus game takes every action while it is open (confirm or back close it)
    if (PlayersRecapOpen() && *kMode == MODE_SCENESEL) {
        const uint8_t slot = *(const uint8_t*)0x8E2111;
        // a mouse click is also a confirm of slot 0 a moment later (the button bound as a key): only the pointer's counts
        if (action == 7 && slot == 0 && LeftJustPressed() && !(GetAsyncKeyState(VK_RETURN) & 0x8000) &&
            !(GetAsyncKeyState(VK_SPACE) & 0x8000)) return;
        if (PlayersRecapAction(action, slot) == 2) g_recapClosedAt = GetTickCount() | 1;
        return;
    }
    if (g_clickLog < ClickLogMax && (action == 7 || action == 8)) {
        ++g_clickLog;
        Log("click log: action %d slot %u (mode %d, selected %s, handler %08X, page %s)", action,
            *(const uint8_t*)0x8E2111, *kMode, ElemName(*(const uint32_t*)0x8E2F70), (unsigned)g_handler,
            PlayersPageOpen() ? "open" : "closed");
    }
    // The left mouse button is also a key of player 1 (quick attack, "PSM" in Options), and in the menus it confirms
    // the entry selected by the keys: a click on Options opened Options through the pointer and, at the same time,
    // confirmed Single, the entry the keys had selected (a tester's reading of the test of 30/10/2026, and the log). Out of
    // our pages the pointer does the click by itself, so a confirm from slot 0 when the left button was just pressed,
    // with neither Enter nor Space held, is dropped. (On our pages the click is taken from it.)
    if (action == 7 && *(const uint8_t*)0x8E2111 == 0 && !PlayersPageOpen() && LeftJustPressed() &&
        !(GetAsyncKeyState(VK_RETURN) & 0x8000) && !(GetAsyncKeyState(VK_SPACE) & 0x8000)) {
        static int logged;
        if (logged++ < 8) Log("menu: confirm of the left mouse button as a key dropped (mode %d)", *kMode);
        return;
    }
    // A confirm right after a change of screen belongs to the screen that was left: the game waited 50 ms after every
    // change, ROTK Fix removes that pause, and the key or button that closed a screen confirmed the entry selected in
    // the next one (log of 30/10/2026: back from Credits, a confirm from slot 0 2 ms later chose Single). Only the
    // confirms, for 150 ms.
    if (action == 7 && g_screenAt && GetTickCount() - g_screenAt < 150) {
        static int logged;
        if (logged++ < 8) Log("menu: confirm right after a change of screen dropped (mode %d)", *kMode);
        return;
    }
    if (action == 7) {
        static DWORD lastAt;
        static uint8_t lastSlot;
        const uint8_t slot = *(const uint8_t*)0x8E2111;
        const DWORD now = GetTickCount();
        if (lastAt && now - lastAt < 250 && slot != lastSlot && (slot == 3 || lastSlot == 3)) {
            static int logged;
            if (logged++ < 8) Log("menu: second confirm of one click dropped (slots %u and %u, mode %d)", lastSlot, slot, *kMode);
            lastAt = 0;
            return;
        }
        lastAt = now | 1;
        lastSlot = slot;
    }
    ((void(__cdecl*)(int))g_handler)(action);
}

// replaces 0x4EA1D0(handler): 0 = the default one (0x4EA0F0); returns the previous one
static uintptr_t __cdecl SetActionHandler(uintptr_t h)
{
    const uintptr_t prev = g_handler;
    g_handler = h ? h : 0x4EA0F0;
    return prev;
}

static bool InstallActionFilter()
{
    static const uint8_t setOld[] = { 0x8B, 0x4C, 0x24, 0x04, 0x85, 0xC9, 0xA1, 0x70, 0xDC, 0x56, 0x00 };
    uintptr_t& cur = *(uintptr_t*)0x56DC70;
    const uintptr_t was = cur;
    if (!PatchJmp(0x4EA1D0, setOld, sizeof setOld, (const void*)&SetActionHandler)) return false;
    g_handler = was ? was : 0x4EA0F0;
    cur = (uintptr_t)&ActionFilter;
    return true;
}

bool ScreensInstall()
{
    {
        auto clamp = [](int v) { return v < 2 ? 2 : v > 4 ? 4 : v; };
        g_coopItem.players = clamp(IniInt("Coop", "Players", 3));
        g_pvpItem.players = PvpChoicePlayers();
        g_wantN = g_coopItem.players;
        Log("main menu: Co-op with %d players, Versus with %d (left/right on the entry; teams and enemies in [PvP])",
            g_coopItem.players, g_pvpItem.players);
        g_menuDiag = IniInt("Diag", "MenuTimerLog", 0) != 0;
        ClickLogMax = IniInt("Diag", "ClickLog", 0) != 0 ? 400 : 0;
        if (g_menuDiag) Log("main menu demo diagnostics: active ([Diag] MenuTimerLog=1)");
    }
    static const uint8_t actionOld[] = { 0xA0, 0x11, 0x21, 0x8E, 0x00 };
    bool okMenu = PatchJmp(0x4EA0F0, actionOld, sizeof actionOld, (const void*)&MenuAction);
    if (!okMenu) Log("main menu: choice of the number of players NOT installed");
    static const uint8_t old[] = { 0xA1, 0xC0, 0x2A, 0x56, 0x00 };
    static const uint8_t contOld[] = { 0xA1, 0xA0, 0x57, 0x8E, 0x00, 0x83, 0xC0, 0xFE };
    static const uint8_t stateOld[] = { 0x56, 0x8B, 0x74, 0x24, 0x0C, 0x57 };
    static const uint8_t lockOld[] = { 0x8A, 0x44, 0x24, 0x04, 0xA2, 0x75, 0xDC, 0x56, 0x00 };
    bool ok = PatchJmp(0x47FA20, old, sizeof old, (const void*)&SetMode);
    ok &= PatchJmp(0x456210, stateOld, sizeof stateOld, (const void*)&HeroState);
    ok &= PatchJmp(0x4EA200, lockOld, sizeof lockOld, (const void*)&LockPad);
    ok &= PatchJmp(0x4F4110, contOld, sizeof contOld, (const void*)&UpgradeContinue);
    static const uint8_t menuOld[] = { 0x3A, 0x1D, 0x75, 0xDC, 0x56, 0x00, 0x88, 0x1D, 0x11, 0x21, 0x8E, 0x00 };
    ok &= PatchJmp(0x4EAAA0, menuOld, sizeof menuOld, (const void*)&MenuSlotStub);
    if (!InstallActionFilter()) Log("menu: filter of the second confirm of a click NOT installed");
    Log("extra players' turns in the end-of-level screens: %s", ok ? "active" : "NOT installed");
    return ok;
}
