// The Two Towers: play as Gandalf or Boromir in the Gates of Moria and Balin's Tomb, chosen on the hero screen
// (after Aragorn: the end of this file).
//
// In those two levels (02GATE2, 04BALIN1) they are companions the game controls. Every character is one class
// (vtable 0x1741C0, in the contiguous memory 0x8...), with its name at +0x18 (a pointer to the text) and its render
// instance at +0x3D0. The player is [0x43B15C]. The part of a render instance that animates and draws (its +0x1B0,
// "the part" below) has the clip bank (+0x58: the skeleton, then every clip), its posed bones (+0x5E0: one skinning
// matrix per bone, from the skeleton's rest pose to the pose, after a first slot), its per-bone pose functions
// (+0x5E4), its model (+0x5EC) and its textures (+0x5F4).
//
// Chosen, two seconds into the level (never while a movie plays: a level that opens with one is still being set up
// behind it) the player's part is given the companion's model, textures, room for his bones and what he holds, and
// the companion is hidden with the game's own @SetInvisible flag (bit 1 of +0x110). Everything else stays the
// player's: the hero's controls, clips, timings and moves; and the companion keeps his own (Balin's Tomb's scripts
// wait on Gandalf's clips). The model is the companion's, so it is posed on his skeleton, worked out from the hero's
// (Retarget): the pose 0x2CA60 works out the hero's 51 bones, and each of his then turns as the hero's bone in the
// same place does (Who::map), the bones the hero has no match for (a robe, a hat, a beard) with the bone they hang
// from, each as far from that one as on him, so he keeps his own build. Where the game draws the part (Drawing) it
// takes the bones' count from the bank: the hero's skeleton says his count there, and 51 everywhere else (no one else
// has the hero's bank). Running, walking and standing are his own clips (OwnClips). Nothing of ours is left in the
// game's memory: the two parts only trade what they had, and trade it back.
//
// Gandalf's ranged attack is a blast of light (sub_000D4180). 16ROHAN2 has Gandalf too, but in a different part (the
// camera follows a Gandalf the White who fights there): left out. In any other level nothing changes. The skeletons
// are mapped for Aragorn and Isildur (alike), so the hero screen offers them in Aragorn's place.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace tt {
bool InMenus();                           // tt_extras.cpp
extern std::atomic<int> g_movie_playing;  // tt_framerate.cpp
}  // namespace tt

namespace {
constexpr uint32_t kPlayer = 0x43B15C, kCharVtable = 0x1741C0;
constexpr uint32_t kInstance = 0x3D0, kFlags = 0x110, kInvisible = 2;
constexpr uint32_t kPart = 0x1B0, kBank = 0x58, kPosed = 0x5E0, kModel = 0x5EC, kSkin = 0x5F4;
constexpr uint32_t kCharsFrom = 0x80010000, kCharsTo = 0x84000000;
constexpr int kHeroBones = 51, kMostBones = 96;

// Who can be played: the name his companion has (its start), the name the hero screen shows, his skeleton's bones,
// each bone -> the hero's (Aragorn and Isildur) in the same place or -1 (found from the two skeletons' rest poses:
// hips, legs, spine, neck, collar bones, arms, hands and fingers, head and jaw), his hands (put where the hero's are:
// what they hold is worked out on the hero's bones), his neck and how he carries his head, and his ranged attack a
// blast
constexpr int kGandalfMap[66] = {
    0,  1,  -1, -1, -1, 2,  3,  6,  4,  5,  -1, -1, -1, -1, 9,  10, -1, -1, -1, -1, 11, 12,
    13, -1, -1, -1, -1, 14, 8,  15, 7,  -1, 16, 17, 18, 21, 22, -1, 24, 23, -1, 25, 26, 27,
    28, 29, 30, 31, -1, 33, 32, -1, 34, 35, 36, 37, 38, 39, -1, 40, -1, -1, -1, -1, -1, -1,
};
// Boromir: his skirt's four panels (8 to 11) swing with the thigh on their side; his scabbard (14) as the hero's (8)
constexpr int kBoromirMap[50] = {
    0,  1,  2,  3,  5,  4,  -1, -1, 4,  5,  4,  5,  -1, -1, 8,  -1, -1, 12, 13, 14, 9,  10, 11, 15, 16,
    17, 18, 21, 22, 24, 25, 26, 27, 28, 29, 30, 31, 33, 34, 35, 36, 37, 38, 39, -1, -1, 40, -1, -1, -1,
};
struct Who {
    const char* name;   // his companion's name starts so (lower case)
    const char* shown;  // on the hero screen
    int bones;
    const int* map;
    int right_hand, left_hand, neck;
    float head;  // radians the head is carried forward (down) about the neck, back (up) if less than 0
    bool blast;
};
constexpr Who kWho[] = {
    {"gandalf", "Gandalf", 66, kGandalfMap, 38, 49, 32, 0.17f, true},   // an old man's stoop
    {"boromir", "Boromir", 50, kBoromirMap, 29, 37, 24, -0.10f, false}, // a captain of Gondor's chin
};
constexpr int kWhoCount = int(sizeof(kWho) / sizeof(*kWho));

std::atomic<bool> g_level_has_them{false};  // the world opened is one of the two
std::atomic<int> g_worlds{0};               // worlds opened: a new one, the old one's parts are gone
std::atomic<bool> g_stale{false};           // a world opened or the front end shown: the parts kept are not to be
                                            // touched (another level's may be where they were) until Reset
int Chosen();                               // the hero screen's choice (below), taken: -1 none
std::atomic<int> g_who{-1};                 // this level is played as kWho[g_who], -1 as the hero
uint32_t g_player = 0, g_him = 0;           // worn: the player, and the companion whose model he has
bool g_worn = false, g_given_up = false;
int g_frames = 0;

bool Char(uint32_t a) { return a >= kCharsFrom && a < kCharsTo && kernel::R32(a) == kCharVtable; }

// its name: the text +0x18 points to (or, failing that, text at +0x18)
bool NameIs(uint32_t at, const char* want) {
    for (size_t i = 0; want[i]; i++) {
        uint8_t c = kernel::G<uint8_t>(at + uint32_t(i));
        if (char(c | 0x20) != want[i]) return false;
    }
    return true;
}
bool Named(uint32_t obj, const char* want) {
    uint32_t p = kernel::R32(obj + 0x18);
    if (p >= 0x10000 && p < kCharsTo) return NameIs(p, want);
    return NameIs(obj + 0x18, want);
}
std::string Name(uint32_t obj) {
    uint32_t at = kernel::R32(obj + 0x18);
    if (!(at >= 0x10000 && at < kCharsTo)) at = obj + 0x18;
    std::string n;
    for (int i = 0; i < 31; i++) {
        char c = char(kernel::G<uint8_t>(at + uint32_t(i)));
        if (c < 32 || c > 126) break;
        n += c;
    }
    return n;
}

// the companion on screen: a level may have another, kept hidden for a cut-scene. Characters are 16-byte aligned (the
// game's allocator), so a search a second costs a quarter of a word-by-word one
uint32_t FindCompanion(uint32_t player, const char* name) {
    for (uint32_t a = kCharsFrom; a < kCharsTo; a += 16)
        if (a != player && kernel::R32(a) == kCharVtable && Named(a, name) && !(kernel::R32(a + kFlags) & kInvisible) &&
            kernel::R32(a + kInstance))
            return a;
    return 0;
}

float F(uint32_t a) {
    uint32_t v = kernel::R32(a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
void WF(uint32_t a, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    kernel::W32(a, v);
}
uint32_t Hier(uint32_t bank) { return bank + kernel::R32(bank + 8); }  // the skeleton: +8 its bones' count
uint32_t Bones(uint32_t bank) { return kernel::R32(Hier(bank) + 8); }
// each bone's rest position: the second matrix of its pair (+0x40 of 0x80, the bone in the skeleton's space), last row
std::vector<float> Rest(uint32_t bank) {
    uint32_t h = Hier(bank), n = Bones(bank), m = h + kernel::R32(h + 0xC);
    std::vector<float> r(n * 3);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t k = 0; k < 3; k++) r[i * 3 + k] = F(m + i * 0x80 + 0x40 + 0x30 + k * 4);
    return r;
}

// what the two parts swap: the model, the textures, the posed bones (the companion's room for his; the hidden
// companion is posed into the hero's, for 51 of his bones), and what hangs on them (the instance's attachments, from
// +0x74 of the part's +0x550, their count at +0x174: 8 bytes each, the thing, its bone and flags), the player holding
// the companion's (Gandalf's staff and sword, Boromir's sword and shield) on the hero's bones in the same places, the
// hidden companion nothing
constexpr uint32_t kSwapped[] = {kModel, kSkin, kPosed};
constexpr int kMostHeld = 8;
struct Side {
    uint32_t part = 0, bank = 0;
    uint32_t was[3] = {};  // its kSwapped as they were
    uint32_t held = 0, held_was[kMostHeld][2] = {};
    uint32_t held_count = 0;  // the count as it was
};
struct Worn {
    const Who* who = nullptr;
    Side hero, his;
    std::vector<float> hero_rest, his_rest;
    int his_parent[kMostBones] = {};
} g_w;
// the hero screen's figure dressed as one of them (the end of this file): its part while drawn so, and his skeleton
Worn g_m;
uint32_t g_m_part = 0;

// the bone of the hero's in the same place as his bone g (or the nearest one it hangs from), -1 none
int HeroBoneFor(int g) {
    const Who& who = *g_w.who;
    while (g >= 0 && g < who.bones && who.map[g] < 0) g = g_w.his_parent[g] < g ? g_w.his_parent[g] : -1;
    return g >= 0 && g < who.bones ? who.map[g] : -1;
}

Side Keep(uint32_t part) {
    Side s;
    s.part = part;
    s.bank = kernel::R32(part + kBank);
    for (int i = 0; i < 3; i++) s.was[i] = kernel::R32(part + kSwapped[i]);
    uint32_t m = kernel::R32(part + 0x550);
    s.held_count = m ? kernel::R32(m + 0x174) : 0;
    s.held = std::min(s.held_count, uint32_t(kMostHeld));
    for (uint32_t k = 0; k < s.held; k++)
        for (uint32_t w = 0; w < 2; w++) s.held_was[k][w] = kernel::R32(m + 0x74 + k * 8 + w * 4);
    return s;
}
// a side back as it was, if it is still there
void Restore(const Side& s, uint32_t owner) {
    if (!s.part || !Char(owner) || kernel::R32(owner + kInstance) + kPart != s.part) return;
    for (int i = 0; i < 3; i++) kernel::W32(s.part + kSwapped[i], s.was[i]);
    uint32_t m = kernel::R32(s.part + 0x550);
    for (uint32_t k = 0; m && k < s.held; k++)
        for (uint32_t w = 0; w < 2; w++) kernel::W32(m + 0x74 + k * 8 + w * 4, s.held_was[k][w]);
    if (m) kernel::W32(m + 0x174, s.held_count);
}

bool Wear(const Who& who, uint32_t player, uint32_t him) {
    uint32_t part = kernel::R32(player + kInstance) + kPart, his = kernel::R32(him + kInstance) + kPart;
    uint32_t bank = kernel::R32(part + kBank), his_bank = kernel::R32(his + kBank);
    if (Bones(bank) != uint32_t(kHeroBones) || Bones(his_bank) != uint32_t(who.bones) || who.bones > kMostBones) {
        LOGI("tt: %s: %u and %u bones, can't be worn", who.shown, Bones(bank), Bones(his_bank));
        return false;
    }
    Worn w;
    w.who = &who;
    w.hero = Keep(part);
    w.his = Keep(his);
    w.hero_rest = Rest(bank);
    w.his_rest = Rest(his_bank);
    uint32_t h = Hier(his_bank), par = h + kernel::R32(h + 0x10);  // one byte a bone: its parent + 1, 0 none
    for (int i = 0; i < who.bones; i++) w.his_parent[i] = int(kernel::G<uint8_t>(par + uint32_t(i))) - 1;
    for (int i = 0; i < 3; i++) {
        kernel::W32(part + kSwapped[i], w.his.was[i]);
        kernel::W32(his + kSwapped[i], w.hero.was[i]);
    }
    g_w = w;  // the parts are recognised from here on (one thread: the game's)
    uint32_t ma = kernel::R32(part + 0x550), mb = kernel::R32(his + 0x550);
    uint32_t held = std::min(g_w.hero.held, g_w.his.held);
    for (uint32_t k = 0; ma && mb && k < held; k++) {
        uint32_t ea = ma + 0x74 + k * 8, eb = mb + 0x74 + k * 8;
        uint32_t bone_flags = g_w.his.held_was[k][1];
        int hero = HeroBoneFor(int(int16_t(bone_flags & 0xFFFF)));
        kernel::W32(ea, g_w.his.held_was[k][0]);
        kernel::W32(ea + 4, (bone_flags & 0xFFFF0000u) | uint16_t(hero >= 0 ? hero : 0));
        kernel::W32(eb, g_w.hero.held_was[k][0]);
        kernel::W32(eb + 4, g_w.hero.held_was[k][1]);
    }
    for (uint32_t k = 0; k < std::max(g_w.hero.held, g_w.his.held); k++)
        LOGI("tt: %s: held %u: the hero's %08X on %d, his %08X on %d", who.shown, k,
             k < g_w.hero.held ? g_w.hero.held_was[k][0] : 0, k < g_w.hero.held ? int(int16_t(g_w.hero.held_was[k][1])) : -1,
             k < g_w.his.held ? g_w.his.held_was[k][0] : 0, k < g_w.his.held ? int(int16_t(g_w.his.held_was[k][1])) : -1);
    // and the hidden companion holds nothing: what hangs on a hidden character is still drawn (the hero's bow was,
    // beside the player, where the companion walks)
    if (mb) kernel::W32(mb + 0x174, 0);
    return true;
}

// both sides back (each one still there)
void TakeOff(uint32_t player, uint32_t him) {
    Restore(g_w.hero, player);
    Restore(g_w.his, him);
    g_w = Worn();
}

// the hero's 51 skinning matrices in the table (from slot 1) -> his. A skinning matrix: rows x y z (the turn), then
// the translation; a point at rest p goes to p R + t, so a bone's joint is at rest R + t
void Retarget(const Worn& w, uint32_t table) {
    const Who& who = *w.who;
    const int n = who.bones;
    float hr[kHeroBones][9], ht[kHeroBones][3];
    for (int b = 0; b < kHeroBones; b++) {
        uint32_t m = table + uint32_t(b + 1) * 64;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) hr[b][r * 3 + c] = F(m + uint32_t(r * 16 + c * 4));
        for (int c = 0; c < 3; c++) ht[b][c] = F(m + 48 + uint32_t(c * 4));
    }
    auto mul = [](const float* v, const float* R, float* o) {
        for (int c = 0; c < 3; c++) o[c] = v[0] * R[c] + v[1] * R[3 + c] + v[2] * R[6 + c];
    };
    float gr[kMostBones][9], gj[kMostBones][3];
    for (int g = 0; g < n; g++) {
        int h = who.map[g], p = w.his_parent[g];
        if (p >= g) p = -1;  // a parent comes first
        if (h >= 0) std::memcpy(gr[g], hr[h], sizeof gr[g]);
        else if (p >= 0) std::memcpy(gr[g], gr[p], sizeof gr[g]);
        else for (int k = 0; k < 9; k++) gr[g][k] = k % 4 == 0 ? 1.0f : 0.0f;
        // the root, and the hands: where the hero's are (what they hold is worked out on the hero's bones; arms a
        // little shorter reach)
        if (p < 0 || g <= 1 || g == who.right_hand || g == who.left_hand) {
            int hh = h >= 0 ? h : 0;
            mul(&w.hero_rest[size_t(hh) * 3], hr[hh], gj[g]);
            for (int c = 0; c < 3; c++) gj[g][c] += ht[hh][c];
        } else {  // as far from its parent as on him, turned as its parent is
            float d[3], o[3];
            for (int c = 0; c < 3; c++) d[c] = w.his_rest[size_t(g) * 3 + c] - w.his_rest[size_t(p) * 3 + c];
            mul(d, gr[p], o);
            for (int c = 0; c < 3; c++) gj[g][c] = gj[p][c] + o[c];
        }
    }
    // his posture over the hero's: the head carried his way (the neck and all that hangs from it: the head, a hat,
    // a beard, turned about the neck's joint): Gandalf's a little forward and down, an old man's stoop; Boromir's up
    if (who.neck >= 0 && who.head != 0) {
        const float c = std::cos(who.head), sn = std::sin(who.head);  // forward: -y, the way he faces, about x
        const float S[9] = {1, 0, 0, 0, c, -sn, 0, sn, c};  // v S: z (up) toward -y (forward)
        const int neck = who.neck;
        float px = gj[neck][0], py = gj[neck][1], pz = gj[neck][2];
        for (int g = neck; g < n; g++) {
            int a = g;
            while (a > neck && a >= 0) a = w.his_parent[a] < a ? w.his_parent[a] : -1;
            if (a != neck) continue;
            float r[9];
            for (int i = 0; i < 3; i++)
                for (int k = 0; k < 3; k++)
                    r[i * 3 + k] = gr[g][i * 3] * S[k] + gr[g][i * 3 + 1] * S[3 + k] + gr[g][i * 3 + 2] * S[6 + k];
            std::memcpy(gr[g], r, sizeof r);
            float d[3] = {gj[g][0] - px, gj[g][1] - py, gj[g][2] - pz}, o[3];
            mul(d, S, o);
            gj[g][0] = px + o[0];
            gj[g][1] = py + o[1];
            gj[g][2] = pz + o[2];
        }
    }
    for (int g = 0; g < n; g++) {
        uint32_t m = table + uint32_t(g + 1) * 64;
        float o[3];
        mul(&w.his_rest[size_t(g) * 3], gr[g], o);
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) WF(m + uint32_t(r * 16 + c * 4), gr[g][r * 3 + c]);
            WF(m + uint32_t(r * 16 + 12), 0.0f);
        }
        for (int c = 0; c < 3; c++) WF(m + 48 + uint32_t(c * 4), gj[g][c] - o[c]);
        WF(m + 60, 1.0f);
    }
}

void UndressAll();  // the end of this file
void ForgetBlasts();  // below

void Reset() {
    g_w = Worn();  // a level left: its parts are gone with it
    g_player = g_him = 0;
    g_worn = g_given_up = false;
    g_frames = 0;
    ForgetBlasts();
    g_stale = false;
}

// the part is the player's wearing him (or the hidden companion's) as worn: the same part, with what it was given
bool HeroPart(uint32_t part) {
    return part && !g_stale && part == g_w.hero.part && kernel::R32(part + kModel) == g_w.his.was[0] &&
           kernel::R32(part + kPosed) == g_w.his.was[2];
}
bool HisPart(uint32_t part) {
    return part && !g_stale && part == g_w.his.part && kernel::R32(part + kModel) == g_w.hero.was[0] &&
           kernel::R32(part + kPosed) == g_w.hero.was[2];
}
}  // namespace

namespace tt {
// each file the game opens (tt_extras.cpp, lower case): a world tells which level
void GandalfWorld(const std::string& name) {
    size_t w = name.find("\\worlds\\");
    if (w == std::string::npos) return;
    size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot < w) return;
    std::string ext = name.substr(dot);  // the world itself: one file a hero (the level's sounds are there too)
    if (ext != ".bfa" && ext != ".bfg" && ext != ".bfi" && ext != ".bfl") return;
    std::string level = name.substr(w + 8, dot - w - 8);
    g_level_has_them = level == "02gate2" || level == "04balin1";
    g_stale = true;  // the parts worn are the last level's: let alone until the next frame lets them go
    ++g_worlds;
    // the hero screen's choice (or TT_PLAY_AS_COMPANION=gandalf|boromir, testing) for the level now opening; the
    // same level opened again from within it (a restart, a checkpoint: not from the front end) stays as it was
    static const int test = [] {
        const char* e = std::getenv("TT_PLAY_AS_COMPANION");
        for (int i = 0; e && i < kWhoCount; i++)
            if (std::strcmp(e, kWho[i].name) == 0) return i;
        return -1;
    }();
    static std::string last;
    bool again = level == last && !InMenus();
    last = level;
    int chosen = Chosen();
    int who = chosen >= 0 ? chosen : test >= 0 ? test : again ? g_who.load() : -1;
    g_who = g_level_has_them ? who : -1;
}

// once a frame, from the frame wait (tt_cheats.cpp), on the game's thread
void GandalfFrame() {
    static int world = 0;
    if (world != g_worlds || (InMenus() && (g_w.who || g_worn))) {  // a new level (or the same again), or back to the
        world = g_worlds;                                              // front end: nothing of the last one is there
        Reset();
    }
    if (!InMenus()) UndressAll();  // the hero screen's dress: not kept in a level
    // in the menus (the pause menu too) as it is; the worn companion stays worn
    if (InMenus() || !g_level_has_them) return;
    uint32_t player = kernel::R32(kPlayer);
    static const bool probe = std::getenv("TT_GANDALF_PROBE") != nullptr;  // testing: the level's characters
    static int probed = 0;
    if (probe && ++probed == 600)
        for (uint32_t a = kCharsFrom; a < kCharsTo; a += 4)
            if (kernel::R32(a) == kCharVtable && kernel::R32(a + kInstance)) {
                uint32_t part = kernel::R32(a + kInstance) + kPart, bank = kernel::R32(part + kBank);
                LOGI("tt: probe: %08X '%s' bank %08X bones %u model %08X skin %08X (%08X)", a, Name(a).c_str(), bank,
                     bank ? Bones(bank) : 0, kernel::R32(part + kModel), kernel::R32(part + kSkin),
                     kernel::R32(part + kSkin) ? kernel::R32(kernel::R32(part + kSkin) + 8) : 0);
            }
    int who = g_who;
    if (g_worn) {
        // the same characters with the same parts, still wearing what they were given (a checkpoint may make them
        // again where they were)
        bool still = player == g_player && Char(g_player) && Char(g_him) &&
                     kernel::R32(g_player + kInstance) + kPart == g_w.hero.part &&
                     kernel::R32(g_him + kInstance) + kPart == g_w.his.part && HeroPart(g_w.hero.part) &&
                     HisPart(g_w.his.part);
        if (still && who >= 0 && g_w.who == &kWho[who]) {
            kernel::W32(g_him + kFlags, kernel::R32(g_him + kFlags) | kInvisible);
            static const bool ammo = std::getenv("TT_GANDALF_AMMO") != nullptr;  // testing: missiles to fire
            if (ammo && kernel::R32(g_player + 0x304) < 5) kernel::W32(g_player + 0x304, 20);
            return;
        }
        // a new player (a checkpoint): what is still there back as it was, the companion shown; worn again when he
        // is found
        const char* shown = g_w.who ? g_w.who->shown : "?";
        bool his_there = Char(g_him) && kernel::R32(g_him + kInstance) + kPart == g_w.his.part;
        // each side back only if it still has what it was given: a part made again has its own
        if (!HeroPart(g_w.hero.part)) g_w.hero = Side();
        if (!HisPart(g_w.his.part)) g_w.his = Side();
        TakeOff(g_player, g_him);
        if (his_there) kernel::W32(g_him + kFlags, kernel::R32(g_him + kFlags) & ~kInvisible);
        LOGI("tt: %s back to the companion", shown);
        Reset();
        return;
    }
    if (who < 0 || g_given_up || !Char(player)) return;
    const Who& w = kWho[who];
    // not while a movie plays: a level opening with one (Balin's Tomb) is still being set up behind it. Two seconds
    // after, then every second for five minutes
    if (tt::g_movie_playing > 0) {
        g_frames = 0;
        return;
    }
    ++g_frames;
    if (g_frames < 120 || g_frames % 60) return;
    if (g_frames > 120 + 300 * 60) {
        g_given_up = true;
        LOGI("tt: %s not found in this level", w.shown);
        return;
    }
    uint32_t him = FindCompanion(player, w.name);
    if (!him || !kernel::R32(player + kInstance) || !kernel::R32(him + kInstance)) return;
    if (!Wear(w, player, him)) {
        g_given_up = true;
        return;
    }
    kernel::W32(him + kFlags, kernel::R32(him + kFlags) | kInvisible);
    g_player = player;
    g_him = him;
    g_worn = true;
    LOGI("tt: playing as %s (player %08X '%s', %08X '%s')", w.shown, player, Name(player).c_str(), him,
         Name(him).c_str());
}
}  // namespace tt

// His own walk, run and standing: while the hero's clips playing are only those (the animation's two layers, from
// +0x94, 0x44 bytes each: the clip at +4, the time it was and is at +8 and +0xC in ms, the clip's length at +0x10,
// the clip's tracks for each bone at +0x40; their count at +0x528), the player's part is posed once with the
// companion's own skeleton and clips of the same numbers (0 and 1 running, 2 walking, 33 standing, where his bank has
// them), each at the same point of its cycle; what he holds on his own bones for it. Everything else (fighting,
// jumping, being hit) is the hero's moves on him (Retarget).
constexpr int kOwnClips[] = {0, 1, 2, 33};
X86_EXTERN_FUNC(__imp__sub_0002CA60);
namespace {
bool OwnClip(uint32_t clip) {
    for (int c : kOwnClips)
        if (clip == uint32_t(c)) return true;
    return false;
}
uint32_t ClipLength(uint32_t bank, uint32_t clip) {  // ms, from the clip's header (+8), 0 none
    uint32_t off = kernel::R32(bank + kernel::R32(bank + 0x10) + clip * 4);
    return off ? kernel::R32(bank + off + 8) : 0;
}
bool OwnClips(X86Context& ctx, uint8_t* base, uint32_t part) {
    const int n = g_w.who->bones;
    uint32_t layers = kernel::R32(part + 0x528);
    if (layers < 1 || layers > 2) return false;
    uint32_t his_bank = g_w.his.bank, his_funcs = kernel::R32(g_w.his.part + 0x5E4);
    uint32_t was[2][4] = {}, caches_was[2] = {};
    static uint32_t caches = 0;  // the layers' per-bone tracks for his skeleton (8 bytes a bone, 0x29FD2's form)
    if (!caches && !(caches = mem::AllocSystem(2 * kMostBones * 8, 16))) return false;
    static const Who* told = nullptr;
    if (told != g_w.who) {
        told = g_w.who;
        LOGI("tt: %s's own clips: run %u %u, walk %u, standing %u ms", g_w.who->shown, ClipLength(his_bank, 0),
             ClipLength(his_bank, 1), ClipLength(his_bank, 2), ClipLength(his_bank, 33));
    }
    for (uint32_t i = 0; i < layers; i++) {
        uint32_t l = part + 0x94 + i * 0x44, clip = kernel::R32(l + 4);
        if (!OwnClip(clip) || !ClipLength(his_bank, clip) || !kernel::R32(l + 0x10)) return false;
    }
    // both layers are read for every bone, the second too while only the first plays: it is given the first's clip
    // for this pose (the tracks it has from the hero's clips are for his 51 bones)
    for (uint32_t i = 0; i < 2; i++)
        for (uint32_t w = 0; w < 4; w++) was[i][w] = kernel::R32(part + 0x94 + i * 0x44 + 4 + w * 4);
    if (layers == 1)
        for (uint32_t w = 0; w < 4; w++) kernel::W32(part + 0xD8 + 4 + w * 4, was[0][w]);
    for (uint32_t i = 0; i < 2; i++) {  // the same point of his clip's cycle
        uint32_t l = part + 0x94 + i * 0x44, clip = kernel::R32(l + 4);
        uint32_t src = layers == 1 ? 0 : i;
        uint32_t t_was = was[src][1], t_now = was[src][2];
        uint32_t len = was[src][3], his_len = ClipLength(his_bank, clip);
        auto scale = [&](uint32_t t) { return uint32_t((uint64_t(t % (len + 1)) * his_len) / len); };
        kernel::W32(l + 8, scale(t_was));
        kernel::W32(l + 0xC, scale(t_now));
        kernel::W32(l + 0x10, his_len);
        // the clip's track for each of his bones (0x25700) and its two kind bytes, as a clip's start (0x29FA4)
        // lays them out for the pose; the first two from the track or 1, 1, by the part's +0x534 (0x29FA9)
        uint32_t cache = caches + i * kMostBones * 8;
        caches_was[i] = kernel::R32(l + 0x40);
        uint32_t c = his_bank + kernel::R32(his_bank + kernel::R32(his_bank + 0x10) + clip * 4);
        bool kinds = F(part + 0x534) < F(0x18AC58);
        for (uint32_t b = 0; b < uint32_t(n); b++) {
            uint32_t at = kernel::R32(c + kernel::R32(c + 0x20) + b * 4), t = at ? c + at : 0, e = cache + b * 8;
            kernel::W32(e, t ? t + 4 : 0);
            uint8_t k0 = t ? kernel::G<uint8_t>(t) : 0, k1 = t ? kernel::G<uint8_t>(t + 1) : 0;
            kernel::G<uint8_t>(e + 4) = t ? (kinds ? k0 : 1) : 0;
            kernel::G<uint8_t>(e + 5) = t ? (kinds ? k1 : 1) : 0;
            kernel::G<uint8_t>(e + 6) = k0;
            kernel::G<uint8_t>(e + 7) = k1;
        }
        kernel::W32(l + 0x40, cache);
    }
    // what he holds, on his own bones for this pose
    uint32_t m = kernel::R32(part + 0x550);
    uint32_t held = m ? std::min(g_w.hero.held, g_w.his.held) : 0;
    uint32_t bones[kMostHeld] = {};
    for (uint32_t k = 0; k < held; k++) {
        bones[k] = kernel::R32(m + 0x74 + k * 8 + 4);
        kernel::W32(m + 0x74 + k * 8 + 4, (bones[k] & 0xFFFF0000u) | (g_w.his.held_was[k][1] & 0xFFFF));
    }
    // and his per-bone arrays (+0x5DC, the bones' own working state, and +0x5E4, the pose functions)
    uint32_t own_bank = kernel::R32(part + kBank), own_funcs = kernel::R32(part + 0x5E4);
    uint32_t own_work = kernel::R32(part + 0x5DC), his_work = kernel::R32(g_w.his.part + 0x5DC);
    kernel::W32(part + kBank, his_bank);
    kernel::W32(part + 0x5E4, his_funcs);
    kernel::W32(part + 0x5DC, his_work);
    __imp__sub_0002CA60(ctx, base);
    kernel::W32(part + kBank, own_bank);
    kernel::W32(part + 0x5E4, own_funcs);
    kernel::W32(part + 0x5DC, own_work);
    for (uint32_t k = 0; k < held; k++) kernel::W32(m + 0x74 + k * 8 + 4, bones[k]);
    for (uint32_t i = 0; i < 2; i++) {
        for (uint32_t w = 0; w < 4; w++) kernel::W32(part + 0x94 + i * 0x44 + 4 + w * 4, was[i][w]);
        kernel::W32(part + 0x94 + i * 0x44 + 0x40, caches_was[i]);
    }
    return true;
}
}  // namespace

// The pose (ecx the part; returns its posed bones; +0x90 set: already posed this frame, the bones as they are). The
// player's is worked out for the hero's 51 bones, then made his; the hidden companion's for at most 51 of his, all
// the hero's room holds.
X86_FUNC(sub_0002CA60) {
    uint32_t part = ctx.ecx;
    if (part && part == g_m_part && g_m.who) {  // the hero screen's figure, dressed: the hero's 51, then his
        uint32_t count_at = Hier(kernel::R32(part + kBank)) + 8, count = kernel::R32(count_at);
        kernel::W32(count_at, kHeroBones);
        __imp__sub_0002CA60(ctx, base);
        kernel::W32(count_at, count);
        Retarget(g_m, ctx.eax);
        return;
    }
    bool hero = HeroPart(part), his = !hero && HisPart(part);
    if (!hero && !his) {
        __imp__sub_0002CA60(ctx, base);
        return;
    }
    bool fresh = kernel::G<uint8_t>(part + 0x90) == 0;
    if (hero && fresh && OwnClips(ctx, base, part)) return;
    const Side& s = hero ? g_w.hero : g_w.his;
    uint32_t count_at = Hier(s.bank) + 8, count = kernel::R32(count_at);
    // the hero's 51 (whatever the drawing has it say just now: 0x2D5C0 poses inside); the hidden companion's, at most
    kernel::W32(count_at, hero ? uint32_t(kHeroBones) : std::min(count, uint32_t(kHeroBones)));
    __imp__sub_0002CA60(ctx, base);
    kernel::W32(count_at, count);
    if (hero && fresh && HeroPart(part)) Retarget(g_w, ctx.eax);
}

// Drawing: the two functions that pose a part to draw it (0x2D5C0, 0x2F5A0; ecx the part) take its bones' count from
// its bank; for the player wearing a companion, his
X86_EXTERN_FUNC(__imp__sub_0002D5C0);
X86_EXTERN_FUNC(__imp__sub_0002F5A0);
namespace {
bool DressedDraw(X86Context& ctx, uint8_t* base, uint32_t part, void (*draw)(X86Context&, uint8_t*));  // below
template <void (*Draw)(X86Context&, uint8_t*)>
void Drawing(X86Context& ctx, uint8_t* base) {
    uint32_t part = ctx.ecx;
    if (part && DressedDraw(ctx, base, part, Draw)) return;
    // the player: his count; the hidden companion (should he be drawn): at most the 51 the hero's room holds
    bool ours = HeroPart(part), his = !ours && HisPart(part);
    uint32_t count_at = ours ? Hier(g_w.hero.bank) + 8 : his ? Hier(g_w.his.bank) + 8 : 0;
    uint32_t count = count_at ? kernel::R32(count_at) : 0;
    if (ours) kernel::W32(count_at, uint32_t(g_w.who->bones));
    if (his) kernel::W32(count_at, std::min(count, uint32_t(kHeroBones)));
    Draw(ctx, base);
    if (count_at) kernel::W32(count_at, count);
}
}  // namespace
X86_FUNC(sub_0002D5C0) { Drawing<__imp__sub_0002D5C0>(ctx, base); }
X86_FUNC(sub_0002F5A0) { Drawing<__imp__sub_0002F5A0>(ctx, base); }

// Gandalf's ranged attack: the hero's arrow or axe as a blast of light. A projectile is started by 0xD4180(projectile)
// (from the spawn 0xD9460, which has set its type +0x3D0, its record in the level's projectile table +0x3DC and its
// models +0x554..+0x560, and its owner +0x11C); the start spawns the record's flight effect (+0x2C) to follow it, and
// its impact effect (+0x34) plays where it hits. The player's shot as Gandalf gets type 24's (a straight bolt): flight
// effect 229, impact 281, and no model drawn (the render 0xD4D60 skips a projectile whose +0x554 is 0). The record is
// shared by every projectile of that type, so it is put back as soon as this one has started. Two more of its light
// at the staff as he casts, and two more of its burst where it ends (0xD4260), the game's own effects added up.
X86_EXTERN_FUNC(__imp__sub_000D4180);
namespace {
constexpr int kBlastFlight = 229, kBlastImpact = 281;
constexpr uint32_t kSpawnEffect = 0x8E470;  // cdecl (effect, &position, &direction, 5.0): a handle
uint32_t g_blasts[16] = {};                  // the player's blasts in flight (forgotten with the level)
// one more of an effect where the projectile is. The direction is (0, 0, 0, 1), as the projectile's own start gives
// it, in a few bytes of ours the game only reads
void MoreLight(X86Context& ctx, int fx, uint32_t proj, int times) {
    static uint32_t dir = 0;
    if (!dir && (dir = mem::AllocSystem(16, 16)) != 0) {
        for (uint32_t i = 0; i < 3; i++) ::kernel::W32(dir + i * 4, 0);
        ::kernel::W32(dir + 12, 0x3F800000);
    }
    for (int i = 0; dir && i < times; i++)
        ::kernel::CallGuest(ctx, kSpawnEffect, {uint32_t(fx), proj + 0xF0, dir, 0x40A00000});
}
}  // namespace
namespace {
void ForgetBlasts() {
    for (uint32_t& b : g_blasts) b = 0;
}
}  // namespace
X86_FUNC(sub_000D4180) {
    uint32_t proj = ::kernel::R32(ctx.esp + 4);
    uint32_t record = proj ? ::kernel::R32(proj + 0x3DC) : 0;
    static const bool any = std::getenv("TT_BLAST_ANY") != nullptr;  // testing: every projectile a blast
    bool ours = proj && record &&
                (any || (g_worn && !g_stale && g_w.who && g_w.who->blast && ::kernel::R32(proj + 0x11C) == g_player));
    uint32_t flight = 0, impact = 0;
    if (ours) {
        flight = ::kernel::R32(record + 0x2C);
        impact = ::kernel::R32(record + 0x34);
        ::kernel::W32(record + 0x2C, uint32_t(kBlastFlight));
        ::kernel::W32(record + 0x34, uint32_t(kBlastImpact));
    }
    __imp__sub_000D4180(ctx, base);
    if (ours) {
        ::kernel::W32(record + 0x2C, flight);
        ::kernel::W32(record + 0x34, impact);
        ::kernel::W32(proj + 0x554, 0);
        MoreLight(ctx, kBlastFlight, proj, 2);  // a flash at the staff as he casts
        for (uint32_t& b : g_blasts)
            if (!b) {
                b = proj;
                break;
            }
        static int logged = 0;
        if (logged++ < 5)
            LOGI("tt: Gandalf's blast (projectile %08X, type %u, owner %08X, player %08X)", proj, ::kernel::R32(proj + 0x3D0),
                 ::kernel::R32(proj + 0x11C), g_player);
    }
}

// a projectile's end (cdecl, an entry whose +0xC is the projectile): the blast's burst where it ends, brighter
X86_EXTERN_FUNC(__imp__sub_000D4260);
X86_FUNC(sub_000D4260) {
    uint32_t entry = ::kernel::R32(ctx.esp + 4), proj = entry ? ::kernel::R32(entry + 0xC) : 0;
    bool ours = false;
    for (uint32_t& b : g_blasts)
        if (proj && b == proj) {
            b = 0;
            ours = true;
        }
    if (ours) MoreLight(ctx, kBlastImpact, proj, 2);
    __imp__sub_000D4260(ctx, base);
}

// The hero screen (the screen object: its level at +0x14, its slot at +0x18; the slots' heroes in the table
// 0x1A40F4, 8 bytes a slot: slot 0 Aragorn, 1 Legolas, 2 Gimli, 3 Isildur; +0x210 the way the last move went, 1 to
// the left). On the Gates of Moria and Balin's Tomb Gandalf and Boromir are more choices there, after Aragorn: on
// Aragorn's slot, the name at the top shown as theirs (GandalfText), and the level, when it starts, played as the
// one chosen (Wear) as Aragorn. The moves (0x76740 right, 0x76800 left, thiscall) go through them: right from Aragorn
// to Gandalf, Boromir, then on to the next hero; left back the same way. Aragorn's figure is shown dressed as the one
// chosen (DressedDraw, the end of this file).
X86_EXTERN_FUNC(__imp__sub_00076740);
X86_EXTERN_FUNC(__imp__sub_00076800);
namespace {
constexpr uint32_t kLevelAt = 0x14, kSlotAt = 0x18, kWentLeft = 0x210;
std::atomic<uint32_t> g_screen{0};  // the hero screen one of them is chosen on, 0 none
std::atomic<int> g_pick{-1};        // which of kWho, on it
// the levels they are in, by the screen's numbers (0 the Prologue, 1 Weathertop, 2 the Gates of Moria, 3 Balin's Tomb)
void ReadTheirs();  // the end of this file
bool TheirLevel(uint32_t screen) {
    uint32_t level = kernel::R32(screen + kLevelAt);
    return level == 2 || level == 3;
}
int Picked(uint32_t screen) { return g_screen == screen ? g_pick.load() : -1; }
void Pick(uint32_t screen, int pick) {
    if (pick >= 0) ReadTheirs();  // his figure for the screen (from either way)
    g_pick = pick;
    g_screen = pick >= 0 ? screen : 0;
}
void Trace(const char* what, uint32_t screen) {
    static const bool on = std::getenv("TT_GANDALF_TRACE") != nullptr;
    if (on)
        LOGI("tt: hero screen %s: screen %08X level %u slot %u pick %d", what, screen, kernel::R32(screen + kLevelAt),
             kernel::R32(screen + kSlotAt), Picked(screen));
}
}  // namespace
namespace {
int Chosen() {
    uint32_t screen = g_screen.exchange(0);
    int pick = g_pick.exchange(-1);
    return screen && kernel::R32(screen + kSlotAt) == 0 && TheirLevel(screen) ? pick : -1;
}
}  // namespace
X86_FUNC(sub_00076740) {  // right
    uint32_t screen = ctx.ecx;
    int pick = Picked(screen);
    bool on_aragorn = TheirLevel(screen) && kernel::R32(screen + kSlotAt) == 0;
    if (on_aragorn && pick + 1 < kWhoCount) {  // Aragorn, or one of them: the next of them
        Pick(screen, pick + 1);
        kernel::G<uint8_t>(screen + kWentLeft) = 1;
        ctx.esp += 4;  // its ret
    } else {  // the last of them, as from Aragorn: on to the next hero
        Pick(screen, -1);
        __imp__sub_00076740(ctx, base);
    }
    Trace("right", screen);
}
X86_FUNC(sub_00076800) {  // left
    uint32_t screen = ctx.ecx;
    int pick = Picked(screen);
    uint32_t was = kernel::R32(screen + kSlotAt);
    if (pick >= 0 && TheirLevel(screen) && was == 0) {  // one of them: the one before, or Aragorn
        Pick(screen, pick - 1);
        kernel::G<uint8_t>(screen + kWentLeft) = 0;
        ctx.esp += 4;
    } else {
        if (pick >= 0) Pick(screen, -1);  // left over from another visit to the screen
        __imp__sub_00076800(ctx, base);
        // from the next hero back to Aragorn: the last of them first
        if (TheirLevel(screen) && was != 0 && kernel::R32(screen + kSlotAt) == 0) Pick(screen, kWhoCount - 1);
    }
    Trace("left", screen);
}

namespace tt {
// the text lookups (tt_text.cpp): Aragorn's name (116) on the hero screen while one of them is chosen: his, in a few
// bytes of ours (UTF-16, as the game's text is). 0: the game's own
uint32_t GandalfText(uint32_t id, uint32_t ret) {
    if (id != 116 || ret < 0x763B0 || ret >= 0x76440) return 0;  // the hero screen's name at the top (0x763B0)
    uint32_t screen = g_screen;
    int pick = g_pick;
    if (!screen || pick < 0 || pick >= kWhoCount || !InMenus() || kernel::R32(screen + kSlotAt) != 0 ||
        !TheirLevel(screen))
        return 0;
    static uint32_t text[kWhoCount] = {};
    if (!text[pick] && (text[pick] = mem::AllocSystem(64, 16)) != 0) {
        const char* n = kWho[pick].shown;
        for (uint32_t i = 0; i < 31; i++) {
            kernel::G<uint16_t>(text[pick] + i * 2) = uint16_t(n[i]);
            if (!n[i]) break;
        }
    }
    return text[pick];
}
}  // namespace tt

// The hero screen's figure dressed as the one chosen. Their models, skeletons and textures are only in their levels'
// files; the Gates of Moria's (02GATE2.bfa) has both. A package file (the levels' and the menus' alike: a version, then
// where its table is; the table: a count, then the type, place and key of each piece, 12 bytes, a piece running to the
// next one's place) is read on a thread of ours when one of them is first chosen, his three pieces kept (model, type
// 100, skeleton and clips 150, textures 300), and on the game's thread put in memory of ours as the game's loader has
// them (each at its place's offset in a page, as in the file): the model made ready by 0x25740, the textures by the
// maker of the loader's (0xE2E40: the textures object a part takes). The menu's packages have the same pieces as types
// 1050, 1100 and 1150; Aragorn's figure is the part whose clips are the menu package's type 1100 key 1. While it is
// drawn, it has his model, his textures and room for his bones; the pose is the figure's own (its stance on the
// menu), the hero's 51 bones made his (Retarget); the hero's sword in his hand (Gandalf's Glamdring, Boromir's own).
// All back as it was after each drawing; the memory given back once a level is played.
namespace {
struct Theirs {
    uint32_t model, bank, skin;  // the keys of his pieces in 02GATE2.bfa
};
constexpr Theirs kTheirs[kWhoCount] = {
    {0x7139CB4A, 0xA9D6B5BC, 0x0D02E40C},  // Gandalf
    {0x7D506646, 0x3BF89E36, 0xA9443216},  // Boromir
};
constexpr uint32_t kTypeModel = 100, kTypeBank = 150, kTypeSkin = 300;
constexpr uint32_t kMenuPackage = 0x4ACC20;  // the package loaded: its count, where it is +8, its table +0xC (type, place, key)
constexpr uint32_t kMenuBank = 1100, kAragornKey = 1;
constexpr uint32_t kPrepareModel = 0x25740, kPrepareTextures = 0xE2140, kTexturesVtable = 0x1709C4;
struct Piece {
    uint32_t at = 0;  // its place in the file
    std::vector<uint8_t> bytes;
};
struct Dress {
    Piece piece[3];                     // model, bank, textures as read
    uint32_t model = 0, bank = 0, skin = 0;  // in memory
};
Dress g_dress[kWhoCount];
std::vector<uint32_t> g_dress_blocks;
std::vector<uint32_t> g_dress_objects;  // the textures objects, of ours
uint32_t g_dress_posed = 0;           // room for his posed bones
std::atomic<int> g_dress_state{0};    // 0 not read, 1 reading, 2 read, 3 in memory, -1 not there

bool ReadPieces(const std::vector<uint8_t>& f) {
    auto u32 = [&](size_t at) {
        uint32_t v = 0;
        if (at + 4 <= f.size()) std::memcpy(&v, &f[at], 4);
        return v;
    };
    uint32_t table = u32(4), count = u32(table);
    if (!table || table + 4 + uint64_t(count) * 12 > f.size()) return false;
    std::vector<uint32_t> places;
    for (uint32_t i = 0; i < count; i++) places.push_back(u32(table + 4 + i * 12 + 4));
    places.push_back(table);
    std::sort(places.begin(), places.end());
    for (int w = 0; w < kWhoCount; w++) {
        const uint32_t want[3][2] = {{kTypeModel, kTheirs[w].model}, {kTypeBank, kTheirs[w].bank}, {kTypeSkin, kTheirs[w].skin}};
        for (int k = 0; k < 3; k++) {
            Piece& p = g_dress[w].piece[k];
            p = Piece();
            for (uint32_t i = 0; i < count && p.bytes.empty(); i++) {
                uint32_t e = table + 4 + i * 12;
                if (u32(e) != want[k][0] || u32(e + 8) != want[k][1]) continue;
                uint32_t at = u32(e + 4);
                auto next = std::upper_bound(places.begin(), places.end(), at);
                if (next == places.end() || *next > f.size()) return false;
                p.at = at;
                p.bytes.assign(f.begin() + at, f.begin() + *next);
            }
            if (p.bytes.empty()) return false;
        }
    }
    return true;
}
}  // namespace

namespace {
void ReadTheirs() {
    int expected = 0;
    if (!g_dress_state.compare_exchange_strong(expected, 1)) return;
    std::thread([] {
        std::vector<uint8_t> f;
        bool ok = kernel::ReadGameFile("d:\\worlds\\02GATE2.bfa", f) && ReadPieces(f);
        LOGI("tt: hero screen: Gandalf's and Boromir's figures %s", ok ? "read" : "not found in 02GATE2.bfa");
        g_dress_state = ok ? 2 : -1;
    }).detach();
}

// on the game's thread: the pieces read into memory, made ready as the loader makes them
bool DressReady(X86Context& ctx) {
    if (g_dress_state == 3) return true;
    if (g_dress_state != 2) return false;
    bool ok = true;
    for (int w = 0; w < kWhoCount && ok; w++) {
        Dress& d = g_dress[w];
        uint32_t at[3] = {};
        for (int k = 0; k < 3 && ok; k++) {
            const Piece& p = d.piece[k];
            uint32_t in = p.at & 0xFFF, block = mem::AllocContiguous(uint32_t(p.bytes.size()) + in, 0, 0xFFFFFFFF, 0x1000);
            if (!block) {
                ok = false;
                break;
            }
            g_dress_blocks.push_back(block);
            at[k] = block + in;
            std::memcpy(kernel::HostPtr(at[k]), p.bytes.data(), p.bytes.size());
        }
        if (!ok) break;
        d.model = at[0];
        d.bank = at[1];
        kernel::CallGuest(ctx, kPrepareModel, {d.model});
        // the textures object as 0xE2E40 makes it (the data made ready by 0xE2140, then 0x14 bytes: its vtable, three
        // words 0, the key at +8, the data at +0x10), but in memory of ours: the game's own (0xE6B50) is taken back
        // each time a menu screen loads its package, the hero screen's too
        if (!(kernel::CallGuest(ctx, kPrepareTextures, {at[2], at[2]}) & 0xFF) || !(d.skin = mem::AllocSystem(0x14, 16))) {
            ok = false;
            break;
        }
        g_dress_objects.push_back(d.skin);
        kernel::W32(d.skin, kTexturesVtable);
        for (uint32_t o = 4; o < 0x10; o += 4) kernel::W32(d.skin + o, 0);
        kernel::W32(d.skin + 8, kTheirs[w].skin);
        kernel::W32(d.skin + 0x10, at[2]);
        ok = d.skin && kernel::R32(d.bank) == 0x7A696E61 && Bones(d.bank) == uint32_t(kWho[w].bones);  // "aniz"
    }
    if (ok && !(g_dress_posed = mem::AllocContiguous((kMostBones + 1) * 64, 0, 0xFFFFFFFF, 0x1000))) ok = false;
    if (ok) g_dress_blocks.push_back(g_dress_posed);
    if (!ok) {
        for (uint32_t b : g_dress_blocks) mem::FreeContiguous(b);
        for (uint32_t o : g_dress_objects) mem::FreeSystem(o);
        g_dress_blocks.clear();
        g_dress_objects.clear();
        g_dress_posed = 0;
        LOGI("tt: hero screen: their figures could not be made");
    }
    for (Dress& d : g_dress)
        for (Piece& p : d.piece) p = Piece();  // the copies read are not needed again
    g_dress_state = ok ? 3 : -1;
    return ok;
}

// the menu package's Aragorn's clips (type 1100, key 1): his figure's bank, 0 none
uint32_t AragornFigureBank() {
    uint32_t count = kernel::R32(kMenuPackage), table = kernel::R32(kMenuPackage + 0xC);
    if (!table || count > 4096) return 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t e = table + i * 12;
        if (kernel::R32(e) == kMenuBank && kernel::R32(e + 8) == kAragornKey)
            return kernel::R32(kMenuPackage + 8) + kernel::R32(e + 4);  // its place, from where the package is
    }
    return 0;
}

// the one chosen, while the hero screen shows him: -1 none
int Dressing() {
    uint32_t screen = g_screen;
    int pick = g_pick;
    if (!screen || pick < 0 || pick >= kWhoCount || kernel::R32(screen + kSlotAt) != 0 || !TheirLevel(screen)) return -1;
    return pick;
}

void UndressAll() {
    if (g_dress_state != 3 && g_dress_blocks.empty() && g_dress_objects.empty()) return;
    for (uint32_t b : g_dress_blocks) mem::FreeContiguous(b);
    for (uint32_t o : g_dress_objects) mem::FreeSystem(o);
    g_dress_blocks.clear();
    g_dress_objects.clear();
    g_dress_posed = 0;
    for (Dress& d : g_dress) d.model = d.bank = d.skin = 0;
    g_m = Worn();
    g_m_part = 0;
    if (g_dress_state == 3) g_dress_state = 0;
}

// the one Aragorn's figure is dressed as, -1 none
int DressedAs(X86Context& ctx, uint32_t part) {
    if (!tt::InMenus()) return -1;
    uint32_t bank = kernel::R32(part + kBank);
    if (!bank || bank != AragornFigureBank() || Bones(bank) != uint32_t(kHeroBones)) return -1;
    int pick = Dressing();
    return pick >= 0 && DressReady(ctx) ? pick : -1;
}

bool DressedDraw(X86Context& ctx, uint8_t* base, uint32_t part, void (*draw)(X86Context&, uint8_t*)) {
    int pick = DressedAs(ctx, part);
    if (pick < 0) return false;
    uint32_t bank = kernel::R32(part + kBank);
    const Who& who = kWho[pick];
    const Dress& d = g_dress[pick];
    if (g_m.who != &who || g_m.hero.bank != bank) {
        Worn w;
        w.who = &who;
        w.hero.part = part;
        w.hero.bank = bank;
        w.his.bank = d.bank;
        w.hero_rest = Rest(bank);
        w.his_rest = Rest(d.bank);
        uint32_t h = Hier(d.bank), par = h + kernel::R32(h + 0x10);
        for (int i = 0; i < who.bones; i++) w.his_parent[i] = int(kernel::G<uint8_t>(par + uint32_t(i))) - 1;
        g_m = w;
        LOGI("tt: hero screen: Aragorn's figure dressed as %s", who.shown);
    }
    uint32_t was[3], fresh = kernel::G<uint8_t>(part + 0x90);
    for (int i = 0; i < 3; i++) was[i] = kernel::R32(part + kSwapped[i]);
    kernel::W32(part + kModel, d.model);
    kernel::W32(part + kSkin, d.skin);
    kernel::W32(part + kPosed, g_dress_posed);
    kernel::G<uint8_t>(part + 0x90) = 0;  // posed again, into his room
    uint32_t count_at = Hier(bank) + 8;
    kernel::W32(count_at, uint32_t(who.bones));
    g_m_part = part;
    draw(ctx, base);
    g_m_part = 0;
    kernel::W32(count_at, kHeroBones);
    for (int i = 0; i < 3; i++) kernel::W32(part + kSwapped[i], was[i]);
    kernel::G<uint8_t>(part + 0x90) = uint8_t(fresh);
    return true;
}
}  // namespace

// The draw of a part (0x2BC40, thiscall: flags, a textures object to draw with instead of its own, and one more) is
// given one by the hero screen: the figure's own. Dressed, his instead, and none for the second set (+0x5F8, which the
// menu's figures have: Aragorn's again)
X86_EXTERN_FUNC(__imp__sub_0002BC40);
X86_FUNC(sub_0002BC40) {
    uint32_t part = ctx.ecx;
    int pick = part ? DressedAs(ctx, part) : -1;
    if (pick < 0) {
        __imp__sub_0002BC40(ctx, base);
        return;
    }
    uint32_t more = kernel::R32(part + 0x5F8);
    kernel::W32(ctx.esp + 0xC, g_dress[pick].skin);
    kernel::W32(part + 0x5F8, 0);
    __imp__sub_0002BC40(ctx, base);
    kernel::W32(part + 0x5F8, more);
}
