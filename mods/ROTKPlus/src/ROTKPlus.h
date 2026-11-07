// ROTKPlus.h - declarations shared by all modules
// Language: C++17. Compilers: MSVC (Visual Studio 2022 or later) and mingw-w64 g++ (i686), Win32 (x86).
//
// ROTKPlus is a mod for The Lord of the Rings: The Return of the King (PC, 2003): rotk.exe 1.0,
// 1,703,936 bytes. It coexists with ROTK Fix / Athelas. One source, two builds:
// - MSVC (src/ROTKPlus.vcxproj): DivxMediaLib.dll, loaded by the game in place of its movie library (renamed
//   DivxMediaLib_orig.dll, to which every export is forwarded, Forward.cpp);
// - g++ (build.sh, ROTKPLUS_MOD defined): ROTKPlus.dll, a mod of Athelas in ROTKFix\mods, started by ROTK Fix with
//   ModInit (Main.cpp).
#pragma once

#if !defined(_M_IX86) && !defined(__i386__)
#error "ROTKPlus must be built for Win32 (x86): rotk.exe is a 32-bit program."
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string>

#define ROTKPLUS_VERSION "1.4.1"

// --- one source for MSVC and g++ ----------------------------------------------------------------------------------
// Inline assembly, Intel syntax, one instruction per ASM(...) line between ASM_BEGIN and ASM_END: MSVC reads every line
// as an __asm statement, g++ the whole as one asm block. A C++ name the assembly uses (a variable, a function, another
// naked function) is declared with ASM_NAME(name), which gives it that plain name for g++ and keeps it even if only the
// assembly uses it (MSVC sees the C++ names directly); ASM_EXTERN(name) does the same for a declaration of something
// defined in another file. Memory operands always say their size (dword ptr [name]), and labels start with the
// function's name: for g++ they are visible to the whole file.
#if defined(_MSC_VER)
#define NAKED            __declspec(naked)
#define ASM_BEGIN
#define ASM(...)         __asm __VA_ARGS__
#define ASM_END
#define ASM_NAME(name)
#define ASM_EXTERN(name)
#else
#include <intrin.h>
#define NAKED            __attribute__((naked))
#define ASM_TEXT(...)    #__VA_ARGS__
#define ASM_BEGIN        __asm__(".intel_syntax noprefix\n"
#define ASM(...)         ASM_TEXT(__VA_ARGS__) "\n"
#define ASM_END          ".att_syntax prefix\n");
#define ASM_NAME(name)   __asm__(#name) __attribute__((used))
#define ASM_EXTERN(name) __asm__(#name)
// MSVC's intrinsics, which mingw-w64's intrin.h only declares
#define _ReturnAddress() __builtin_return_address(0)
#define _AddressOfReturnAddress() ((void*)((char*)__builtin_frame_address(0) + sizeof(void*)))
#endif

// Guard(f) runs f() and returns false if it raised an exception instead (a character, a stream or a texture that is
// gone): what __try/__except (EXCEPTION_EXECUTE_HANDLER) did, for both compilers (g++ has no __try). Guard.cpp.
typedef void(__cdecl* GuardFn)(void*);
extern "C" int __cdecl GuardedRun(GuardFn fn, void* ctx) ASM_EXTERN(GuardedRun);
template <class F> inline bool Guard(const F& f)
{
    return GuardedRun([](void* p) { (*(const F*)p)(); }, (void*)&f) != 0;
}

// Call a naked trampoline (one that continues into the game's code) from C++: GameFn((type)&Trampoline)(...).
// When it sees a static naked function in the same translation unit, MSVC may assume that it does not touch the
// volatile registers and keep values in them across the call. This once broke the whole HUD: a value kept in xmm0
// across a call into the game was overwritten by the game (and by DXVK) and the "restore" corrupted the texture
// coordinates of every HUD image for good. Reading the pointer through a volatile variable hides the callee and
// forces the standard convention (eax, ecx, edx and xmm0-7 are assumed lost).
template <class F> inline F GameFn(F f) { F volatile v = f; return v; }

// --- Main.cpp: log, game folder, writing into the game's code ------------------------------------------------
void        Log(const char* fmt, ...);
bool        LogDetail();                       // [Log] Detail: 1 = periodic snapshots and statistics dumps (default)
void        LogStats(const char* when);        // Support.cpp: the game's per-hero statistics records
const char* GameDir();
// Writes n bytes at addr if the current bytes are the expected ones (expect, n bytes). Returns false otherwise.
bool        Patch(uintptr_t addr, const void* expect, const void* data, size_t n);
// Replaces the 32-bit value at addr (inside an instruction) if it equals oldValue.
bool        PatchDword(uintptr_t addr, uint32_t oldValue, uint32_t newValue);
// Replaces the target of the rel32 call/jmp at addr (opcode E8/E9) if it points to oldTarget.
bool        PatchCall(uintptr_t addr, uintptr_t oldTarget, const void* newTarget);
// Writes a rel32 jmp at addr to target, if the first n bytes are the expected ones.
bool        PatchJmp(uintptr_t addr, const void* expect, size_t n, const void* target);

// --- Config.cpp: ROTKPlus.ini in the game folder -------------------------------------------------------------
struct Config {
    bool coop       = false;  // [Coop] Enabled: third and fourth player in co-op
    int  hero3      = -1;     // [Coop] Hero: 0 Aragorn ... 8 Faramir, -1 = automatic choice (third player only)
    int  input3     = 2;      // [Coop] Input: input slot of the third player (2 = its own; 0 = copies player 1)
    bool pads       = true;   // [Coop] Pad: controllers for the third and fourth player; 0 = keyboard only
    bool kbFallback = false;  // [Coop] KeyboardFallback: an extra player without a controller moves with player 1's keys
    char pad3Name[64] = "";   // [Coop] PadName: for testing, pick the third player's controller by part of its name
    bool  hud       = true;   // [Coop] Hud: HUD boxes of the extra players
    float hudDx     = 0.0f;   // [Coop] HudX/HudY: offset of the extra players' boxes from player 2's box, as
    float hudDy     = -0.72f; //   fractions of the screen (negative = up)
    bool  hudRow    = false;  // [Coop] HudRow: 1 = all boxes in a reduced row at the bottom, 0 = extra boxes at the top
    float hudScale  = 0.66f;  // [Coop] HudScale: size of the boxes in the row
};
extern Config g_cfg;
void LoadConfig();
// ROTKPlus.ini reads: the current section is [Coop] / [PvP]; the old [Coop3] section is still accepted.
int  IniInt(const char* section, const char* key, int def);
void IniWriteInt(const char* section, const char* key, int value);

// --- Coop.cpp: third and fourth player ------------------------------------------------------------------------
bool CoopInstall();
extern volatile int g_spawningP3 ASM_EXTERN(g_spawningP3);   // 1 or 2 while the third or fourth player is handled
int  HeroOfSlot(int slot);        // hero of an extra player (slot 2 = third, 3 = fourth) in the current level, -1 none

// --- Pads.cpp: controllers of the third and fourth player (SDL3) -----------------------------------------------
bool PadsInstall(const char* padName);
bool PadConnected(int slot);      // controller opened for slot 2 (third) or 3 (fourth)
std::string PadKeyOfSlot(int slot);   // a stable key of that controller (serial or path, hashed), "" if none

// --- Sim.cpp: keyboard-simulated controllers for testing ([Debug] SimPads=1) ------------------------------
void SimInstall(bool on);
bool SimActive();
void SimFrame(bool (*hasPad)(int slot));

// --- HeroIndex.cpp: hero selection in functions that pick by player index --------------------------------------
bool HeroInstall();

// --- Screens.cpp: turns in the end-of-level screens, hero selection, main menu ----------------------------------
bool ScreensInstall();
int  CharSelChoice(int slot);                 // hero chosen by an extra player in the selection screen, -1 none
bool CharSelOnChoice(int player, int hero);   // from 0x47DF00/0x47DF20: true = the choice belongs to an extra player
int  CoopPlayers();                           // players chosen in the main menu: 2, 3 or 4 (2 if the mod is off)
bool Coop3Wanted();                           // at least 3 players
bool Coop4Wanted();                           // 4 players
void MenuIdleReset();                         // the main menu is in use: its demo timer starts again

// --- the Multiplayer page's choice (Players.cpp hands it to Screens.cpp, which gives a versus game's to Pvp.cpp) ---
struct MultiChoice {
    int  players = 2;          // 2, 3 or 4 (4 in a 2 vs 2)
    bool versus = false;       // a versus game (otherwise the campaign)
    bool teams = false, enemies = true, timed = false;
    int  lives = 2, minutes = 3, rounds = 1, team1 = 12;   // rounds 1, 3 or 5; team1 the two players of team 1 (12, 13, 14)
};

// --- Pvp.cpp: versus mode ------------------------------------------------------------------------------------------
bool        PvpAllowed();                     // [PvP] Enabled
bool        PvpActive();                      // a versus game was chosen (Co-op turns it off)
void        PvpSetActive(bool on);            // confirmed Versus (on) or Co-op (off) in the main menu
int         PvpChoicePlayers();               // players of the Versus entry's choice: 2, 3 or 4 (4 for 2 vs 2)
void        PvpChoiceStep(int dir);           // left/right on the Versus entry: the next choice (+1) or the previous one
std::string PvpChoiceLabel(const std::string& label);   // "Versus: 2 vs 2, heroes only" and the like
void        PvpConfigure(const MultiChoice& m);   // the Multiplayer page's choice
void        PvpOnPlayerHit(int attacker, int victim, bool down);   // Players.cpp: a player hit another one
bool        PvpInGame();                      // a versus game is being played (Hud.cpp: the shared lives are hidden)
bool        PvpHeroesOnlyGame();              // ... with the heroes only (Hud.cpp: the level's enemy bar is hidden too)
int         PvpTeamOf(int slot);              // the team of a player in a 2 vs 2 (0 the Sun, 1 the Moon), -1 otherwise
bool        PvpSkipScoreScreens();            // [PvP] SkipScoreScreens: after a versus game straight to the map
bool        PvpLastGame();                    // [PvP] LastGame: after a versus game the Last game over the map
bool        ScreensMapFilter(int& input);     // the map's input (Pvp.cpp, MapMove): false = swallowed (the recap is open)

// --- Lang.cpp: texts in the game's language ---------------------------------------------------------------------
// id: 0 Co-op menu entry, 1 Versus menu entry, 2 "{label}: {n} players", 3 "Player {n}", 4 winner, 5 draw,
// 6 "{label}: 2 vs 2", 7 "{label}, heroes only", 8 "{label} wins" (a team's name), 9 "{label}: {hero}" (a team's name and
// its players), 10 "{label} and {hero}", 11 and 12 the two teams' names (Sun, Moon), 13 "{label} downs {hero}",
// 14 "{label} falls", 15 "{label} is out", 16 "Round {n}: {label} wins", 17 "Round {n}: draw", 18 "Round {n}",
// 19 "{label} wins the Cup!", 20 "out", 21 "Time is up", 22 the same as 19 for a team ("The {label}..."), 23 "{label}
// knocks out {hero}", 24 "1 life", 25 "{n} lives", 26 "1 point", 27 "{n} points"
std::string LangText(int id, int n = 0, const char* hero = "", const char* label = "");

std::string LangCode();                       // language of the mod's texts: "en", "it", ...
std::string LangGameText(const char* key);    // the game's own text for a key, in its language ("" if none)

// --- Hud.cpp: HUD of the third and fourth player -----------------------------------------------------------------
bool HudInstall(float dx, float dy, bool row, float rowScale);
// Pads.cpp: a shoulder button (LB/LT -1, RB/RT +1) pressed on any open controller since the last call, 0 none; sdl = SDL
// could be read (otherwise the game's input records are used)
int  PadsShoulder(bool& sdl);
void AudioNewLevel();                         // Audio.cpp: the audio log's lines count again from a level's start
bool DrawHookInstall();                       // hook of the screen elements' drawing (0x4FD590), installed once
void DrawGameElement(uint8_t* e);             // draws one screen element with the drawing origin at the top left
bool HudTextTemplate(uint8_t* out);           // copies the HUD's text element (0xB0 bytes), false if not there
void FastTextInstall(int mode);               // [General] FastText: texts drawn in one go (1 the mod's, 2 all, 0 off)
// the versus game's texts (Pvp.cpp): the banner in the middle (its time starts when it is first drawn; with a cup), the
// feed of knock-downs (at the bottom, above the boxes), in the gap between the top boxes the clock of a timed game, the
// teams' totals (parts with a tone: 0 as it is, 1 gold, 2 pale blue, 3 dim) and the round of a tournament; in each
// player's box its lives or points (in place of the game's kills, or after them: append); when the HUD was last drawn
void  HudBanner(const char* text, DWORD ms);
void  HudBannerCup(const char* text, DWORD ms);
bool  HudBannerBusy();
void  HudFeed(const char* text, int tone);
void  HudFeedClear();
void  HudStatus(int n, const char* const* texts, const int* tones);
void  HudClock(const char* text);
void  HudRound(const char* text);
void  HudBoxText(int slot, const char* text, bool append, int tone = 0);
DWORD HudDrawnAt();

// --- Players.cpp: names of the players and their statistics ------------------------------------------------------
void        PlayersInstall();
bool        PlayersEnabled();
bool        PlayerHasName(int slot);          // slot 0..3 has a name of its own
std::string PlayerName(int slot);             // the name, or "Player N" in the game's language
std::string PlayersMenuLabel();               // text of the "Players" entry of the main menu
std::string PlayersChooseText();              // "choose your hero", the end of the named label in the hero selection
bool        PlayersPageOpen();                // the players' page is open in the main menu
bool        PlayersMenuAction(int action, bool onEntry, int slot);   // menu action taken by the page (true = used)
bool        PlayersAskWhoPlays();             // "Who is playing?" before every game ([Players] AskWhoPlays)
bool        PlayersOpenLineup(int n);         // opens "Who is playing?" for n players (false = not shown)
bool        PlayersLineupDone();              // Start was just confirmed (read once): run the entry's confirmation
bool        PlayersMultiEnabled();            // the Co-op entry is "Multiplayer" with its page ([Players] MultiplayerPage)
std::string PlayersMultiLabel();              // "Multiplayer" in the language of the pages
bool        PlayersOpenMulti();               // opens the Multiplayer page (false = not shown)
// after Start on the Multiplayer page: what to start (false = the page was not the Multiplayer one)
bool        PlayersMultiChoice(MultiChoice& m);
void        PlayersScriptCall(int id, uint32_t code, const float* args);   // Players.cpp: the trace of what the level does
void        PlayersVictoryKey();                 // Players.cpp: F9 ended the level (not counted as completed)
void        IconsFrame(bool newLevel);        // Icons.cpp: the portraits taken while playing
void        PlayersFrame();                   // keyboard of the name editor
void        PlayersAfterDraw(uint32_t e);     // draws the page after the main menu's root
void        PlayersLevelStart(const char* level);   // a game level is being loaded ("min02")
void        PlayersLevelLeave(bool won);      // the level is left (won = towards the score screen)
void        PlayersOnFall(uint32_t c);        // a player enters the "die" state
// a versus game is over: the winners' slots as bits (0 a draw), what kind of game it was and how it ended (the banner)
struct PvpResult {
    unsigned winners = 0;
    bool teams = false, heroesOnly = false, timed = false;
    int  lives = 0, minutes = 0, rounds = 1;
    std::string text;            // "The Team of the Sun wins the Cup! (2 - 0)" and the like
};
void        PlayersPvpOver(const PvpResult& r);
void        PlayersPvpRound(unsigned winners);   // a round of a tournament is over: its winners, as bits
void        PlayersPvpElim(int attacker);        // a player eliminated an opponent (knocked down on the last life)
void        PlayersOpenRecap();                  // after a versus game, on the map: the Last game with two buttons
bool        PlayersRecapOpen();
void        PlayersCloseRecap();
int         PlayersRecapAction(int action, int slot);   // a menu action on it: 2 = it closed, the map
void        PlayersDrawCup(float cx, float top, float h);   // a gold cup (the tournament's banner, Hud.cpp)
void __cdecl PlayersGameKill(int index, uint32_t victim) ASM_EXTERN(PlayersGameKill);   // HeroIndex.cpp: the game credits a kill (0x40D110)
std::string PlayersWinnerText(int slot, const char* hero);   // "Alex wins: Gandalf", "" if the player has no name
void        PlayersPadOpened(int slot, const std::string& key);   // Pads.cpp: a controller opened for slot 2 or 3
bool        PlayersHeadNames();               // names above the players' heads ([Players] HeadNames)
bool        PlayersHudNames();                // names next to the HUD boxes ([Players] HudNames)
float       PlayersNameScale();               // size of those names ([Players] NameScale)
