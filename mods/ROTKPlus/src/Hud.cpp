// Hud.cpp - HUD of the third and fourth player
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// The game's HUD is made of screen elements (0x4ED940 finds them by name, 0x4F6A90 sets their visibility at +0x74,
// +0x76 is the type, +0x40/+0x44 the relative position, +0x88 the value of bars, +0x8C the text). There are two
// blocks: `playerhuda` (player 1, element pointers at 0x8E2984) and `playerhudb` (player 2, at 0x8E2A14), 0x24
// pointers each: [0] the block, [2] experience, [3] life, [6] ring, [0x11..0x22] portraits and weapons of the heroes.
// 0x4ED290 updates the HUD of a character: whoever is not player 2 ends up in player 1's block, so the third player
// used to take over the first player's HUD (and its "g1" mark).
//
// Here: 0x4ED290 skips the extra players; when the game draws player 2's block (0x4FD590, the dispatcher that draws by
// element type), it is drawn again right afterwards with the third player's data, shifted by the drawing origin
// (0x8E2F40/0x8E2F44, fractions of the screen), and then everything is put back as it was.
// Fourth player: same method but in player 1's block (playerhuda): 0x4ECD00 picks player 2's block only for
// g_players[1], everybody else ends up in block 1, so for the fourth player nothing needs to be swapped (the third,
// instead, is dressed as player 2). Its box sits at the top left, above player 1's (same HudX/HudY offset as the
// third player's). Extra slots: 2 = third, 3 = fourth.
#include "ROTKPlus.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static const uintptr_t kHudA        = 0x8E2984;
static const uintptr_t kHudB        = 0x8E2A14;
static const uintptr_t kHudUpdate   = 0x4ED290;   // void __cdecl(character**)
static const uintptr_t kHudValues   = 0x4ECD00;   // void __cdecl(character): bars and texts of its block
static const uintptr_t kDrawElement = 0x4FD590;   // void __cdecl(element)
static float* const    kOriginX     = (float*)0x8E2F40;
static float* const    kOriginY     = (float*)0x8E2F44;

extern uint32_t g_players[8];            // Coop.cpp
extern volatile int g_spawningP3;        // Coop.cpp: 0x47EF80 answers the extra player's hero (1 third, 2 fourth)
bool PvpIsOut(uint32_t c);               // Pvp.cpp

static int ActivePlayers()
{
    int n = 0;
    for (int i = 0; i < 4; ++i) n += g_players[i] != 0;
    return n < 2 ? 2 : n;
}
static int ExtraSlotOf(uint32_t c)   // 2 or 3 if c is the third or fourth player, otherwise 0
{
    for (int s = 2; s < 4; ++s) if (c && c == g_players[s]) return s;
    return 0;
}

static float g_hudDx = 0.0f, g_hudDy = -0.72f;   // offset of the extra players' boxes from player 2's
static bool  g_drawingExtra;

// portraits and weapons of the block: index in the block -> hero type (+0x78 of the character), as in 0x4EC610
static const struct { int slot; short type; } kHeroParts[] = {
    { 0x11, 0x1E }, { 0x12, 0x1E }, { 0x13, 8 }, { 0x14, 8 }, { 0x15, 0x0D }, { 0x16, 0x0D }, { 0x17, 0x0C },
    { 0x18, 0x59 }, { 0x19, 0x43 }, { 0x1A, 0x44 }, { 0x1B, 0x0C }, { 0x1C, 0x22 }, { 0x1D, 0x22 },
    { 0x1E, 4 }, { 0x1F, 0x43 }, { 0x20, 0x44 }, { 0x21, 0x59 }, { 0x22, 4 },
};

static void UpdateMarkPos(int slot);

// 0x4ED290 for everybody except the extra players (for them only the position of the "g3"/"g4" mark)
static void __cdecl HudUpdate(uint32_t* who)
{
    if (who && *who)
        if (const int slot = ExtraSlotOf(*who)) { UpdateMarkPos(slot); return; }
    // versus mode: an eliminated player stays down without the respawn countdown (0x4ECD00 for a character that is down
    // only hides the respawn text and marks the box's state)
    if (who && *who && PvpIsOut(*who)) { ((void(__cdecl*)(uint32_t))kHudValues)(*who); return; }
    ((void(__cdecl*)(uint32_t*))kHudUpdate)(who);
}

// trampoline for 0x4FD590: the first instructions (push esi; mov esi,[esp+8]) and then the rest
static NAKED void DrawElementOrig(uint32_t)
{
    ASM_BEGIN
    ASM(push esi)
    ASM(mov  esi, dword ptr [esp + 8])
    ASM(push 0x4FD595)
    ASM(ret)
    ASM_END
}

// --- per-player state of the block's elements ---------------------------------------------------------------
// Elements hold more than their value (+0x88): the bar drawing (0x4FA720) uses and evolves other state fields every
// frame (+0x84..+0xAC: displayed value, blinking, times). With one set of elements for two players the third
// player's bar "bounced" between its state and player 2's (wrong blinking and colours). Here the block has one state
// per extra player (g_sx), swapped around each use. Only the state fields of the types that use them are saved
// (measured from the drawing code of 0x4FD590), so no byte outside the element is touched.
struct ElemState { uint8_t* e; uint8_t lo, hi; uint8_t bytes[0x40]; };
static const int kMaxElems = 160;
static ElemState g_sx[2][kMaxElems];   // state of the third (0) and fourth (1) player's box
static int g_sxN[2] = { -1, -1 };      // -1: state not created yet

static uint8_t StateEnd(uint16_t type)
{
    switch (type) {
    case 0x46: case 0x50: case 0x5A: return 0xB0;               // bars (0x4FA720)
    case 0x64: return 0x98;                                       // 0x4FA360
    case 0x78: case 0x8C: return 0xA0;                            // 0x4FB410
    case 0x82: case 0x96: case 0xA0: case 0xAA: case 0xAB: case 0xAC: return 0xB0;   // texts (0x4FB800, 0x4FC900)
    case 0xB4: return 0xAC;                                       // 0x4FCF70
    default: return 0x78;                                         // groups and others: visibility only
    }
}

static int Collect(uint8_t* e, ElemState* out, int n)
{
    if (!e || n >= kMaxElems) return n;
    out[n].e = e; out[n].lo = 0x74; out[n].hi = StateEnd(*(const uint16_t*)(e + 0x76));
    memcpy(out[n].bytes, e + 0x74, out[n].hi - 0x74);
    ++n;
    const int* kids = *(const int* const*)(e + 4);
    if (kids)
        for (int i = 1; i <= kids[0] && n < kMaxElems; ++i) n = Collect((uint8_t*)kids[i], out, n);
    return n;
}

static void Apply(const ElemState* s, int n)
{
    for (int i = 0; i < n; ++i) memcpy(s[i].e + s[i].lo, s[i].bytes, s[i].hi - s[i].lo);
}

// Runs fn(arg) with the extra player's block in its own state. Third player: player 2's block, with the third player
// dressed as player 2 (0x4101A0(1), 0x47EF80, 0x562A88). Fourth player: player 1's block, where 0x4ECD00 already puts
// whoever is not player 2; 0x47EF80 answers its hero (g_spawningP3 = 2). Then the extra player's state is saved and
// everything is put back.
static int g_extraHudDepth;
static int g_hudSlot;   // extra player whose box is in use (2 or 3), 0 none

static void WithExtraHud(int slot, void (*fn)(void*), void* arg)
{
    const int si = slot - 2;
    uint8_t* root = (uint8_t*)*(const uint32_t*)(slot == 2 ? kHudB : kHudA);
    if (!root || !g_players[slot]) { fn(arg); return; }
    static ElemState s2[kMaxElems];
    const int n = Collect(root, s2, 0);
    if (g_sxN[si] == n) {
        bool same = true;
        for (int i = 0; i < n && same; ++i) same = g_sx[si][i].e == s2[i].e;
        if (same) Apply(g_sx[si], n);
    }
    uint32_t p2 = 0;
    int* choice2 = (int*)0x562A88;
    const int savedChoice = *choice2;
    if (slot == 2) {
        p2 = g_players[1];
        g_players[1] = g_players[2];
        if (HeroOfSlot(2) >= 0) *choice2 = HeroOfSlot(2);
    }
    const int savedFlag = g_spawningP3, savedSlot = g_hudSlot;
    g_spawningP3 = slot - 1;
    g_hudSlot = slot;
    ++g_extraHudDepth;
    fn(arg);
    --g_extraHudDepth;
    g_hudSlot = savedSlot;
    g_spawningP3 = savedFlag;
    if (slot == 2) { *choice2 = savedChoice; g_players[1] = p2; }
    g_sxN[si] = Collect(root, g_sx[si], 0);
    Apply(s2, n);
}

// --- row of reduced boxes at the bottom (HudRow=1) -------------------------------------------------------------
// The bar drawing (0x4FA720) colours "like life" (green/red) any bar in the upper half of the screen: the third
// player's box at the top had a green experience bar. In row mode all the boxes go at the bottom, reduced: sizes use
// the drawing's global scales (0x8E2F7C/0x8E2F80, which the game already uses that way in 0x4FC573), the positions
// of the block's parts are multiplied by hand, the bottom edge stays fixed (the vertical origin drops by 1 - scale).
// Positions >= 2 are in points on 512x448, otherwise in fractions of the screen.
static float g_rowScale = 0.66f;
// "upper half" threshold of the bar drawing (0x4FA886 and 0x4FA8AC compare y with the constant 0.5 at 0x535FB4): they
// now point to this variable, 0.5 for the game and -10 while an extra player's box is drawn at the top, so its bars
// (blue experience) get the right colours in the upper half of the screen too
static float g_halfLimit = 0.5f;
static bool  g_row = true;
static float* const kScaleX = (float*)0x8E2F7C;
static float* const kScaleY = (float*)0x8E2F80;

struct SavedPos { uint8_t* e; float x, y; };

static void ScalePos(float* v, float s, float unit)
{
    const float o = *v;
    if (o >= 2.0f) { const float n = o * s; *v = n >= 2.0f ? n : n / unit; }
    else *v = o * s;
}

static void DrawBlockScaled(uint8_t* root, float x0, void (*draw)(uint8_t*))
{
    static ElemState list[kMaxElems];
    static SavedPos pos[kMaxElems];
    const int n = Collect(root, list, 0);
    for (int i = 0; i < n; ++i) {
        uint8_t* e = list[i].e;
        pos[i] = { e, *(float*)(e + 0x40), *(float*)(e + 0x44) };
        if (i == 0) { *(float*)(e + 0x40) = x0; *(float*)(e + 0x44) = 0.0f; continue; }
        ScalePos((float*)(e + 0x40), g_rowScale, 512.0f);
        ScalePos((float*)(e + 0x44), g_rowScale, 448.0f);
    }
    const float sx = *kScaleX, sy = *kScaleY, oy = *kOriginY;
    *kScaleX = sx * g_rowScale; *kScaleY = sy * g_rowScale;
    *kOriginY = oy + (1.0f - g_rowScale);
    draw(root);
    *kScaleX = sx; *kScaleY = sy; *kOriginY = oy;
    for (int i = 0; i < n; ++i) { *(float*)(pos[i].e + 0x40) = pos[i].x; *(float*)(pos[i].e + 0x44) = pos[i].y; }
}

static void DrawPlain(uint8_t* root) { GameFn(&DrawElementOrig)((uint32_t)(uintptr_t)root); }

struct DrawArgs { uint8_t* p; uint32_t root; int slot; };

static void DrawExtraBoxInner(void* a)
{
    DrawArgs* d = (DrawArgs*)a;
    uint32_t* hud = (uint32_t*)(d->slot == 2 ? kHudB : kHudA);
    // portrait and weapon of the extra player
    const short type = *(const short*)(d->p + 0x78);
    for (const auto& h : kHeroParts)
        if (uint8_t* e = (uint8_t*)hud[h.slot]) *(uint16_t*)(e + 0x74) = h.type == type;
    // bars and texts (0x4ECD00 fills the block)
    ((void(__cdecl*)(uint8_t*))kHudValues)(d->p);
    if (g_row) { DrawBlockScaled((uint8_t*)d->root, (float)d->slot / (float)ActivePlayers(), &DrawPlain); return; }
    *kOriginX += g_hudDx; *kOriginY += g_hudDy;
    g_halfLimit = -10.0f;
    GameFn(&DrawElementOrig)(d->root);
    g_halfLimit = 0.5f;
    *kOriginX -= g_hudDx; *kOriginY -= g_hudDy;
}

static void DrawExtraHud(uint32_t root, int slot)
{
    DrawArgs d = { (uint8_t*)g_players[slot], root, slot };
    WithExtraHud(slot, &DrawExtraBoxInner, &d);
}

// A text element to copy for our own captions: the "kills" text of player 2's block (type 0x82). Looked up again for
// every level, because every level rebuilds the HUD elements (a pointer kept from the previous level pointed to freed
// memory and the captions disappeared).
static uint8_t* FindText(uint8_t* e, int depth);
static uint8_t* g_textSrc;
static uint8_t* g_textRoot;
static uint8_t* TextSource()
{
    if (uint8_t* root = *(uint8_t**)kHudB; root != g_textRoot) { g_textRoot = root; g_textSrc = FindText(root, 0); }
    return g_textSrc;
}
void HudNewLevel() { g_textRoot = nullptr; g_textSrc = nullptr; }

// a copy of that text element (0xB0 bytes) for Players.cpp's level summary on the score screen, where the HUD is gone:
// it holds no pointers the drawing follows (font index, colours, scale, box), so the copy stays good after the level
bool HudTextTemplate(uint8_t* out)
{
    uint8_t* src = TextSource();
    if (!src || IsBadReadPtr(src, 0xB0)) return false;
    memcpy(out, src, 0xB0);
    return true;
}

// Respawn countdown "Respawn in N" of the extra players. For players 1 and 2 it is written by 0x4ED110 (character in
// ESI) into the screen elements "respawna" and "respawnb", which are not part of the players' blocks: calling it for
// the extra players showed the text in player 2's place and made their boxes disappear. Here it is drawn by hand,
// like the winner banner (a copy of the "kills" element), next to the extra player's box. The text is the game's own:
// 0x4E3F00("respawnInLabel", language) with the format "@%s %d" and the number 10 - (+0xD0 -> +0x104), as in 0x4ED110.
static uint8_t g_respawn[0x200];
static char    g_respawnText[96];

static void DrawExtraCountdown(int slot)
{
    uint8_t* p = (uint8_t*)g_players[slot];
    if (!p || g_row || PvpIsOut((uint32_t)(uintptr_t)p)) return;
    if (((int(__cdecl*)(const void*))0x42F570)(p) != 0x28) return;   // only in the "die" state
    uint8_t* src = TextSource();
    if (!src) return;
    float left = *(const float*)0x53618C - *(const float*)(*(const uint32_t*)(p + 0xD0) + 0x104);
    int n = (int)(left + 0.5f);
    if (n < 0) n = 0;
    if (n > 10) n = 10;
    const int lang = ((int(__cdecl*)(int))0x47EF50)(0);
    const char* label = ((const char*(__cdecl*)(const char*, int))0x4E3F00)("respawnInLabel", lang);
    sprintf_s(g_respawnText, "@%s %d", label ? label : "", n);
    memcpy(g_respawn, src, 0xB0);
    *(uint32_t*)(g_respawn + 4) = 0;
    *(uint16_t*)(g_respawn + 0x74) = 1;
    *(float*)(g_respawn + 0x88) = 0.0f;
    *(const char**)(g_respawn + 0x8C) = g_respawnText;
    *(float*)(g_respawn + 0x40) = (slot == 2 ? 0.675f : 0.165f) + g_hudDx;   // like player 2's (0.675) and player 1's text
    *(float*)(g_respawn + 0x44) = 0.835f + g_hudDy;
    const float ox = *kOriginX, oy = *kOriginY;
    *kOriginX = 0.0f; *kOriginY = 0.0f;
    GameFn(&DrawElementOrig)((uint32_t)(uintptr_t)g_respawn);
    *kOriginX = ox; *kOriginY = oy;
}

// --- level-up (0x4ED4D0) and flash after a hit (0x4ED240) for the extra players --------------------------------
// They choose the block with "is it player 2? otherwise player 1's block": for the extra players they ended up in
// Aragorn's box. For them they are run in the state of their own box.
static NAKED void LevelUpOrig()   // push ebp; mov ebp,esp; and esp,-16; sub esp,18h
{
    ASM_BEGIN
    ASM(push ebp)
    ASM(mov  ebp, esp)
    ASM(and  esp, 0xFFFFFFF0)
    ASM(sub  esp, 0x18)
    ASM(push 0x4ED4D9)
    ASM(ret)
    ASM_END
}

static NAKED void FlashOrig()   // push 1; call 0x4101A0
{
    ASM_BEGIN
    ASM(push 1)
    ASM(mov  eax, 0x4101A0)
    ASM(call eax)
    ASM(push 0x4ED247)
    ASM(ret)
    ASM_END
}

// The "FE_LevelUpHUD" effect (0x4F9D70 called from 0x4ED5BA) appears at a fixed point: x 0.2 for player 1 and 0.7 for
// player 2, y 0.82. For the extra players y must move like their box.
static uint32_t __cdecl LevelUpEffect(const char* name, float* pos)
{
    if (g_extraHudDepth && pos && !g_row) { pos[0] += g_hudDx; pos[1] += g_hudDy; }
    return ((uint32_t(__cdecl*)(const char*, float*))0x4F9D70)(name, pos);
}

struct LevelUpArgs { uint32_t who; uint32_t level; };
static void LevelUpInner(void* a) { LevelUpArgs* l = (LevelUpArgs*)a; GameFn((void(__cdecl*)(uint32_t, uint32_t))&LevelUpOrig)(l->who, l->level); }
static void FlashInner(void* a) { GameFn((void(__cdecl*)(uint32_t))&FlashOrig)(*(uint32_t*)a); }

static void __cdecl LevelUp(uint32_t who, uint32_t level)
{
    if (const int slot = g_extraHudDepth ? 0 : ExtraSlotOf(who)) { LevelUpArgs l = { who, level }; WithExtraHud(slot, &LevelUpInner, &l); return; }
    GameFn((void(__cdecl*)(uint32_t, uint32_t))&LevelUpOrig)(who, level);
}

static void __cdecl Flash(uint32_t who)
{
    if (const int slot = g_extraHudDepth ? 0 : ExtraSlotOf(who)) { WithExtraHud(slot, &FlashInner, &who); return; }
    GameFn((void(__cdecl*)(uint32_t))&FlashOrig)(who);
}

// --- "g3" / "g4" marks above the extra players -------------------------------------------------------------
// The "g1"/"g2" marks are the elements coopmark1p/coopmark2p (0x8E2904/0x8E2908): images (type 100, 0x4FA360) 32x32
// whose +0x8C is the texture name ("1p", "2p") in the HUD set 0x8E2F94, which has no "3p". 0x4ED290 puts them above the
// character: point +0x30.. with the per-hero height 0x56DF08 added to y, 0x49A200 (camera matrix **0x565FA0 +4 +0x40)
// and 0x49D850 (2 = out of view), 0x43C950 position on the screen, minus half the width and height (0x4F9F80/0x4F9FB0).
// For each extra player, after coopmark2p is drawn, two copies are drawn: the arrow (the lower part of the "2p" image,
// cut by changing for a moment the initial v of the common texture coordinates 0x56DC10, and coloured with
// +0x78..+0x84, 0.5 = original colour) and the caption "g3"/"g4" (a copy of a text element of player 2's block, type
// 0x82, text "@g3"). The arrow of "2p" is blue and the colour multiplies the texture: from blue only green comes out,
// so the fourth player uses the orange arrow of "1p", tinted yellow. 0xA0 bytes of the image and 0xB0 of the text are
// copied (0x4FB800 also reads +0xA4 and +0xAC); the rest is margin.
static uint8_t g_mark[0x200], g_caption[0x200];
static const float kMarkCut = 0.62f;   // v at which the arrow starts in the image (estimate)

static uint8_t* FindText(uint8_t* e, int depth)
{
    if (!e || depth > 4 || IsBadReadPtr(e, 0xA0)) return nullptr;
    if (*(uint16_t*)(e + 0x76) == 0x82) {
        const char* t = *(const char**)(e + 0x8C);
        if (t && !IsBadStringPtrA(t, 32) && (strstr(t, "ccision") || strstr(t, "kill") || strstr(t, "Kill"))) return e;
    }
    const uint32_t* kids = *(const uint32_t**)(e + 4);
    if (kids && !IsBadReadPtr(kids, 4))
        for (uint32_t i = 1; i <= kids[0] && i < 64 && !IsBadReadPtr(kids + i, 4); ++i)
            if (uint8_t* f = FindText((uint8_t*)(uintptr_t)kids[i], depth + 1)) return f;
    return nullptr;
}

// Position of the mark on the screen: computed when the game updates the HUD (0x4ED290, which is skipped for the extra
// players), as the game does. Computing it while drawing left the camera transform set by 0x43C950 and every HUD
// image drawn afterwards disappeared (texts and bars did not).
static bool  g_markVis[4];
static float g_markX[4], g_markY[4];
static int   g_markCalc[4];

static void UpdateMarkPos(int slot)
{
    ++g_markCalc[slot];
    g_markVis[slot] = false;
    uint8_t* p = (uint8_t*)g_players[slot];
    const int h = HeroOfSlot(slot);
    if (!p || h < 0 || h > 8 || !*(const char*)0x8E2FC8 || *(const int*)0x562A98 != 1) return;
    float pos[4] = { *(float*)(p + 0x30), *(float*)(p + 0x34) + ((const float*)0x56DF08)[h], *(float*)(p + 0x38),
                     *(float*)(p + 0x3C) };
    const uint32_t cam = **(const uint32_t**)0x565FA0;
    float view[4], test[4], scr[4] = {};
    ((void(__cdecl*)(uint32_t, float*, float*))0x49A200)(*(const uint32_t*)(cam + 4) + 0x40, pos, view);
    test[0] = view[0]; test[1] = view[1]; test[2] = view[2]; test[3] = 0.5f;
    if (((char(__cdecl*)(float*, uint32_t, int))0x49D850)(test, cam, 0) == 2) return;
    ((void(__cdecl*)(float*, float*))0x43C950)(pos, scr);
    g_markX[slot] = scr[0]; g_markY[slot] = scr[1];
    g_markVis[slot] = true;
}

static void DrawExtraMark(uint8_t* src, int slot)
{
    // if the game does not call 0x4ED290 for the extra player, compute the position here
    static int lastCalc[4] = { -1, -1, -1, -1 };
    if (g_markCalc[slot] == lastCalc[slot]) UpdateMarkPos(slot);
    lastCalc[slot] = g_markCalc[slot];
    if (!g_markVis[slot] || !g_players[slot] || *(const int*)0x562A98 != 1) return;
    const float w = ((float(__cdecl*)(uint8_t*))0x4F9F80)(src);
    const float hgt = ((float(__cdecl*)(uint8_t*))0x4F9FB0)(src);
    const float left = g_markX[slot] - w * 0.5f, top = g_markY[slot] - hgt * 0.5f;
    // arrow: green for the third player, yellow for the fourth, only the lower part of the image
    memcpy(g_mark, src, 0xA0);
    *(uint32_t*)(g_mark + 4) = 0;
    *(uint16_t*)(g_mark + 0x74) = 1;
    *(float*)(g_mark + 0x40) = left; *(float*)(g_mark + 0x44) = top;
    const bool four = slot == 3;
    *(float*)(g_mark + 0x78) = four ? 0.50f : 0.12f;
    *(float*)(g_mark + 0x7C) = four ? 0.75f : 0.65f;
    *(float*)(g_mark + 0x80) = four ? 0.50f : 0.12f;
    *(float*)(g_mark + 0x44) = top + hgt * kMarkCut;
    *(float*)(g_mark + 0x24) *= 1.0f - kMarkCut;
    float* uv = (float*)0x56DC10;
    const float v0 = uv[1];
    uv[1] = kMarkCut;
    GameFn(&DrawElementOrig)((uint32_t)(uintptr_t)g_mark);
    uv[1] = v0;
    // caption: the player's name (Players.cpp), or "g3"/"g4"
    uint8_t* text = TextSource();
    if (!text) return;
    memcpy(g_caption, text, 0xB0);
    *(uint32_t*)(g_caption + 4) = 0;
    *(uint16_t*)(g_caption + 0x74) = 1;
    *(float*)(g_caption + 0x88) = 0.0f;
    static char named[2][48];
    const bool hasName = PlayersHeadNames() && PlayerHasName(slot);
    if (hasName) {
        _snprintf_s(named[slot - 2], sizeof named[0], _TRUNCATE, "@%s", PlayerName(slot).c_str());
        *(float*)(g_caption + 0x68) = (*(float*)(text + 0x68) ? *(float*)(text + 0x68) : 1.0f) * PlayersNameScale();
        *(float*)(g_caption + 0x6C) = (*(float*)(text + 0x6C) ? *(float*)(text + 0x6C) : 1.0f) * PlayersNameScale();
    }
    *(const char**)(g_caption + 0x8C) = hasName ? named[slot - 2] : four ? "@g4" : "@g3";
    *(float*)(g_caption + 0x40) = left;
    *(float*)(g_caption + 0x44) = top;
    GameFn(&DrawElementOrig)((uint32_t)(uintptr_t)g_caption);
}

// --- names of players 1 and 2 above their heads, and of everybody next to the HUD boxes (Players.cpp) -------------
// Players 1 and 2: the game draws their marks "1p"/"2p" (coopmark1p/coopmark2p, 0x8E2904/0x8E2908) at a position it
// computes every frame (+0x40/+0x44 of the element, left/top): the name takes the place of the letters (DrawMark).
// The HUD boxes: the name just above each box, at the height of the boxes' texts (the respawn countdown is at 0.835,
// x 0.165 for player 1's box and 0.675 for player 2's; the extra players' boxes are moved by HudX/HudY). Not in the
// reduced row (HudRow=1), where the boxes are small and close together.
static uint8_t g_nameText[0x200];

static void DrawNameAt(const std::string& name, float x, float y, bool originZero)
{
    uint8_t* src = TextSource();
    if (!src) return;
    static char buf[48];
    _snprintf_s(buf, sizeof buf, _TRUNCATE, "@%s", name.c_str());
    memcpy(g_nameText, src, 0xB0);
    *(uint32_t*)(g_nameText + 4) = 0;
    *(uint16_t*)(g_nameText + 0x74) = 1;
    *(float*)(g_nameText + 0x88) = 0.0f;
    *(const char**)(g_nameText + 0x8C) = buf;
    *(float*)(g_nameText + 0x40) = x;
    *(float*)(g_nameText + 0x44) = y;
    *(float*)(g_nameText + 0x68) = (*(float*)(src + 0x68) ? *(float*)(src + 0x68) : 1.0f) * PlayersNameScale();
    *(float*)(g_nameText + 0x6C) = (*(float*)(src + 0x6C) ? *(float*)(src + 0x6C) : 1.0f) * PlayersNameScale();
    if (!originZero) { GameFn(&DrawElementOrig)((uint32_t)(uintptr_t)g_nameText); return; }
    const float ox = *kOriginX, oy = *kOriginY;
    *kOriginX = 0.0f; *kOriginY = 0.0f;
    GameFn(&DrawElementOrig)((uint32_t)(uintptr_t)g_nameText);
    *kOriginX = ox; *kOriginY = oy;
}

// The marks of players 1 and 2 (0x8E2904 / 0x8E2908): the "g1"/"g2" letters are the upper part of the mark's own image.
// With a name, the mark is drawn like the extra players' ones: only the arrow (lower part of the image), and the name
// above it in place of the letters (test of 30/10/2026: "g1 Alex" side by side was too much; the name alone is better).
static uint8_t g_markP[0x200];

static void DrawMark(uint32_t e)
{
    if (!e) { GameFn(&DrawElementOrig)(e); return; }
    const int slot = e == *(const uint32_t*)0x8E2904 ? 0 : e == *(const uint32_t*)0x8E2908 ? 1 : -1;
    uint8_t* src = (uint8_t*)(uintptr_t)e;
    uint8_t* text = slot >= 0 ? TextSource() : nullptr;
    if (!text || !PlayersHeadNames() || !PlayerHasName(slot) || *(const int*)0x562A98 != 1 || !*(const uint16_t*)(src + 0x74)) {
        GameFn(&DrawElementOrig)(e);
        return;
    }
    const float left = *(const float*)(src + 0x40), top = *(const float*)(src + 0x44);
    const float hgt = ((float(__cdecl*)(uint8_t*))0x4F9FB0)(src);
    memcpy(g_markP, src, 0xA0);
    *(uint32_t*)(g_markP + 4) = 0;
    *(float*)(g_markP + 0x44) = top + hgt * kMarkCut;
    *(float*)(g_markP + 0x24) *= 1.0f - kMarkCut;
    float* uv = (float*)0x56DC10;
    const float v0 = uv[1];
    uv[1] = kMarkCut;
    GameFn(&DrawElementOrig)((uint32_t)(uintptr_t)g_markP);
    uv[1] = v0;
    DrawNameAt(PlayerName(slot), left, top, false);
}

static void DrawHudNames()
{
    if (!PlayersHudNames() || g_row || *(const int*)0x562A98 != 1) return;
    const float y = 0.79f;
    if (g_players[0] && PlayerHasName(0)) DrawNameAt(PlayerName(0), 0.165f, y, true);
    if (g_players[1] && PlayerHasName(1)) DrawNameAt(PlayerName(1), 0.675f, y, true);
    if (g_players[2] && PlayerHasName(2)) DrawNameAt(PlayerName(2), 0.675f + g_hudDx, y + g_hudDy, true);
    if (g_players[3] && PlayerHasName(3)) DrawNameAt(PlayerName(3), 0.165f + g_hudDx, y + g_hudDy, true);
}

// --- big text in the middle of the screen (winner of the versus mode, Pvp.cpp) --------------------------------------
// Like the "g3" caption: a copy of the "kills" text element of player 2's block (type 0x82, 0xB0 bytes), literal text,
// position in fractions of the screen and a larger scale (+0x68/+0x6C). Drawn right after player 2's block (it always
// exists in co-op), until it expires.
static char    g_bannerText[96];
static DWORD   g_bannerUntil;
static uint8_t g_banner[0x200];

void HudBanner(const char* text, DWORD ms)
{
    strncpy_s(g_bannerText, text, _TRUNCATE);
    g_bannerUntil = GetTickCount() + ms;
    if (!g_bannerUntil) g_bannerUntil = 1;
}

static void DrawBanner()
{
    if (!g_bannerUntil || (int)(GetTickCount() - g_bannerUntil) >= 0) { g_bannerUntil = 0; return; }
    uint8_t* text = TextSource();
    if (!text) return;
    memcpy(g_banner, text, 0xB0);
    *(uint32_t*)(g_banner + 4) = 0;
    *(uint16_t*)(g_banner + 0x74) = 1;
    *(float*)(g_banner + 0x88) = 0.0f;
    *(const char**)(g_banner + 0x8C) = g_bannerText;
    *(float*)(g_banner + 0x40) = 0.18f;
    *(float*)(g_banner + 0x44) = 0.42f;
    *(float*)(g_banner + 0x68) = 1.8f;
    *(float*)(g_banner + 0x6C) = 1.8f;
    const float ox = *kOriginX, oy = *kOriginY;
    *kOriginX = 0.0f; *kOriginY = 0.0f;
    GameFn(&DrawElementOrig)((uint32_t)(uintptr_t)g_banner);
    *kOriginX = ox; *kOriginY = oy;
}

static void DrawElementHud(uint32_t e)
{
    if (e && e == *(const uint32_t*)0x8E2908 && g_players[2] && !g_extraHudDepth) {
        DrawMark(e);   // draws nothing if player 2 is out of view (+0x74 = 0); the extra players do, if in view
        DrawExtraMark((uint8_t*)(uintptr_t)e, 2);
        if (g_players[3] && *(const uint32_t*)0x8E2904) DrawExtraMark((uint8_t*)(uintptr_t)*(const uint32_t*)0x8E2904, 3);   // arrow of "1p" (orange)
        return;
    }
    const bool row = g_row && g_players[2] && !g_drawingExtra && !g_extraHudDepth && e &&
                     (e == *(const uint32_t*)kHudA || e == *(const uint32_t*)kHudB);
    if (row) DrawBlockScaled((uint8_t*)(uintptr_t)e, e == *(const uint32_t*)kHudA ? 0.0f : 1.0f / (float)ActivePlayers(), &DrawPlain);
    else DrawMark(e);   // any element; the marks of players 1 and 2 with their names
    if (!e || g_drawingExtra || e != *(const uint32_t*)kHudB) return;
    if (!g_extraHudDepth) { DrawBanner(); DrawHudNames(); }
    if (!g_players[2] || !g_players[1] || g_extraHudDepth) return;   // also with the extra players dead or waiting
    g_drawingExtra = true;
    DrawExtraHud(e, 2);
    DrawExtraCountdown(2);
    if (g_players[3] && *(const uint32_t*)kHudA) { DrawExtraHud(*(const uint32_t*)kHudA, 3); DrawExtraCountdown(3); }
    g_drawingExtra = false;
}

// replaces 0x4FD590: the HUD of the extra players (if installed), then the players' page (Players.cpp), which goes on
// top of the main menu's last root (0x4F6690 draws *0x8E2F68 and then *0x8E2F74)
static bool g_hudOn;
void ScreensSawElement(uint32_t e);   // Screens.cpp: where the main menu's entries are drawn

static void __cdecl DrawElement(uint32_t e)
{
    ScreensSawElement(e);
    if (g_hudOn) DrawElementHud(e);
    else if (e) DrawMark(e);
    else GameFn(&DrawElementOrig)(e);
    PlayersAfterDraw(e);
}

bool DrawHookInstall()
{
    static int state;   // 0 not yet, 1 installed, -1 failed
    if (!state) {
        static const uint8_t drawOld[] = { 0x56, 0x8B, 0x74, 0x24, 0x08 };
        state = PatchJmp(kDrawElement, drawOld, sizeof drawOld, (const void*)&DrawElement) ? 1 : -1;
    }
    return state == 1;
}

static int g_ownText;   // DrawGameElement is drawing: the texts are the mod's (FastText 1)

// one element drawn on its own (texts of the players' page), with the drawing origin at the top left of the screen
void DrawGameElement(uint8_t* e)
{
    const float ox = *kOriginX, oy = *kOriginY;
    *kOriginX = 0.0f; *kOriginY = 0.0f;
    ++g_ownText;
    GameFn(&DrawElementOrig)((uint32_t)(uintptr_t)e);
    --g_ownText;
    *kOriginX = ox; *kOriginY = oy;
}

// --- texts drawn in one go ---------------------------------------------------------------------------------------
// The game draws a text one letter at a time: 0x49C4D0 calls 0x49CC50 for each letter (and for each letter of the
// shadow, if the text has one), which locks the 2D vertex buffer for four vertices (0x4A2BA0, format 3: x, y, z, colour,
// u, v, 24 bytes), writes them, unlocks it (0x4A2CF0) and draws a fan of two triangles. Under DXVK, with ROTK Fix keeping
// a copy of every lock of the 2D buffers, the lock is the slow part: a page of ours (some 600 letters) took the menu
// from 60 to 40-55 fps, with ROTK Fix 1.7.8 as with 1.8.0 (logs of 02-03/11/2026, the stalls in the lock at 0x4A2C7F).
// Here the letters of a text are written into a list, as 0x49CC50 computes them, and drawn with one lock and one list
// of triangles when the text is done: still inside 0x4DF5E0, after the font's set-up (0x49BF50) and before its
// clean-up (0x49C040), so with the same texture and states, and through the same device calls ROTK Fix watches.
// [General] FastText: 1 = the mod's own texts (the default), 2 = every text the game draws this way, 0 = off.
static int  g_fastText;
static bool g_batching;
struct GlyphVtx { float x, y, z; uint32_t c; float u, v; };
static const int kMaxGlyphs = 512;   // a text has at most 191 letters, twice with its shadow
static GlyphVtx g_glyphVtx[kMaxGlyphs * 6];
static int g_glyphs;
static DWORD g_fastT0;
static unsigned g_fastTexts, g_fastLetters;

static void FlushGlyphs()
{
    if (!g_glyphs) return;
    const int n = g_glyphs * 6;
    g_fastLetters += g_glyphs;
    ++g_fastTexts;
    g_glyphs = 0;
    Guard([&] {
        // the game's own lock and unlock: they pick the buffer, its offset and the stream source
        void* p = ((void*(__cdecl*)(int, int))0x4A2BA0)(3, n);
        if (p) memcpy(p, g_glyphVtx, n * sizeof(GlyphVtx));
        const unsigned start = ((unsigned(__cdecl*)())0x4A2CF0)() & 0xFFFF;   // (0x49CDC3: movzx eax, ax)
        void* dev = *(void* const*)0x5C2364;
        typedef long(__stdcall* DrawPrim)(void*, int, unsigned, unsigned);
        if (p && dev) ((DrawPrim)(*(void***)dev)[70])(dev, 4 /* triangle list */, start, n / 3);
    });
    if (LogDetail()) {
        const DWORD now = GetTickCount();
        if (!g_fastT0) g_fastT0 = now;
        else if (now - g_fastT0 >= 60000) {
            Log("texts: %u drawn in one go in %u s (%u letters)", g_fastTexts, (now - g_fastT0) / 1000, g_fastLetters);
            g_fastT0 = now; g_fastTexts = g_fastLetters = 0;
        }
    }
}

// in place of 0x49CC50(font, glyph record, x, y, x offset, y offset, x scale, y scale, colour), called at 0x49C853
static void __cdecl Glyph(uint8_t* font, const float* g, float x, float y, float dx, float dy, float sx, float sy, uint32_t colour)
{
    if (!g_batching) {
        ((void(__cdecl*)(uint8_t*, const float*, float, float, float, float, float, float, uint32_t))0x49CC50)(font, g, x, y, dx, dy, sx, sy, colour);
        return;
    }
    if (g_glyphs == kMaxGlyphs) FlushGlyphs();
    const uint32_t c = ((colour >> 16) & 0xFF) | ((colour & 0xFF) << 16) | (colour & 0xFF00FF00);   // red and blue swapped
    x += dx;
    y += dy;
    if (*(const int*)0x562AC0 != 8) {   // on the screen's pixels
        const double w = *(const uint16_t*)0x569C24, h = *(const uint16_t*)0x569C28;
        x = (float)(nearbyint(w * x) / w);
        y = (float)(nearbyint(y * h) / h);
    }
    const float x1 = x + sx * g[0], y1 = y + sy * g[1];
    const float fu = *(const float*)(font + 0x444), fv = *(const float*)(font + 0x448);
    const float u0 = g[2] * fu, u1 = g[3] * fu, v0 = g[4] * fv, v1 = g[5] * fv;
    // the fan's corners 0 1 2 3 (top left, top right, bottom right, bottom left) as the triangles 0 1 2 and 0 2 3
    const GlyphVtx a = { x, y, 0.0f, c, u0, v0 }, b = { x1, y, 0.0f, c, u1, v0 };
    const GlyphVtx d = { x1, y1, 0.0f, c, u1, v1 }, e = { x, y1, 0.0f, c, u0, v1 };
    GlyphVtx* v = g_glyphVtx + g_glyphs++ * 6;
    v[0] = a; v[1] = b; v[2] = d; v[3] = a; v[4] = d; v[5] = e;
}

// in place of 0x49C4D0(font, text state, text), called at 0x4DF65A (the text manager's immediate draw)
static void __cdecl TextRun(uint8_t* font, uint8_t* state, const char* text)
{
    typedef void(__cdecl* Run)(uint8_t*, uint8_t*, const char*);
    if (g_batching || !(g_fastText == 2 || (g_fastText == 1 && g_ownText))) { ((Run)0x49C4D0)(font, state, text); return; }
    g_batching = true;
    ((Run)0x49C4D0)(font, state, text);
    g_batching = false;
    FlushGlyphs();
}

void FastTextInstall(int mode)
{
    if (mode <= 0) { Log("texts: drawn a letter at a time, as the game does ([General] FastText=0)"); return; }
    g_fastText = mode >= 2 ? 2 : 1;
    const bool ok = PatchCall(0x49C853, 0x49CC50, (const void*)&Glyph) && PatchCall(0x4DF65A, 0x49C4D0, (const void*)&TextRun);
    if (!ok) g_fastText = 0;
    Log("texts: %s", !ok ? "NOT drawn in one go (the game's code is not the expected one)" :
        g_fastText == 2 ? "every text drawn in one go ([General] FastText=2)" : "the mod's texts drawn in one go");
}

bool HudInstall(float dx, float dy, bool row, float rowScale)
{
    g_hudDx = dx; g_hudDy = dy; g_row = row; g_rowScale = rowScale;
    bool ok = true;
    ok &= PatchCall(0x40CF50, kHudUpdate, (const void*)&HudUpdate);   // jmp of the thunk
    ok &= PatchCall(0x40D091, kHudUpdate, (const void*)&HudUpdate);
    ok &= PatchCall(0x40D0A0, kHudUpdate, (const void*)&HudUpdate);
    const bool draw = DrawHookInstall();
    ok &= draw;
    g_hudOn = draw;   // as before: the extra players' boxes are drawn whenever the drawing hook is in place
    static const uint8_t levelOld[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83, 0xEC, 0x18 };
    static const uint8_t flashOld[] = { 0x6A, 0x01, 0xE8 };
    ok &= PatchJmp(0x4ED4D0, levelOld, sizeof levelOld, (const void*)&LevelUp);
    ok &= PatchCall(0x4ED5BA, 0x4F9D70, (const void*)&LevelUpEffect);
    ok &= PatchJmp(0x4ED240, flashOld, sizeof flashOld, (const void*)&Flash);
    ok &= PatchDword(0x4FA888, 0x535FB4, (uint32_t)(uintptr_t)&g_halfLimit);
    ok &= PatchDword(0x4FA8AE, 0x535FB4, (uint32_t)(uintptr_t)&g_halfLimit);
    Log("HUD of the extra players: %s (%s, scale %.2f)", ok ? "active" : "NOT installed", row ? "row of reduced boxes at the bottom" : "boxes shifted", rowScale);
    return ok;
}
