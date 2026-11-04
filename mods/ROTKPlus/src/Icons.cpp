// Icons.cpp - the portraits of the statistics pages, taken from the player's own game while it is played
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// The statistics pages draw an icon in front of the heroes and the kinds of enemy (Players.cpp, IconTexture: the file
// ROTKPlus_icons\<key>.raw in the game folder). ROTKPlus does not ship pictures of the game: the icons are made here,
// on the player's computer, from the game's own portraits (the 64x64 textures of the HUD: the heroes' faces, and the
// enemies' that come with their life bar), the first time the game uses each of them. A portrait is known by the
// fingerprint of its pixels, the same one ROTK Fix gives the textures it saves ("Save game textures": the file names
// <width>x<height>_<fingerprint>.png): size, format and the pixels of the top level, FNV-1a with an extra mix every 8
// bytes (ROTK Fix, textures.c, examine). The table below holds only those numbers and the icon each one becomes.
//
// How: the game binds every texture through the device's SetTexture (vtable slot 61); ROTKPlus puts itself in front
// of that slot (ROTK Fix's own hook stays behind it and still gets every call). A texture is looked at once: 64x64, not
// in the default pool, then its top level is read through its surface (LockRect of the surface: the texture's own
// UnlockRect is ROTK Fix's, which would take the read for a change of the picture), its fingerprint computed and, if
// it is in the table and the icon file does not exist yet, the drawn part of the portrait is cut out, put in the middle
// of a square, scaled to 128x128 and written. An icon already there (for example a better one made by hand) is never
// replaced. [Players] CaptureIcons=0 turns it off.
#include "ROTKPlus.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <unordered_set>

void PlayersIconReady(const char* key);   // Players.cpp: load the icon again at the next drawing

namespace {

struct Known { unsigned long long hash; const char* key; };
// the fingerprints of the portraits (64x64, from a dump on SteamOS of 01/11/2026), and their icons
const Known kKnown[] = {
    { 0xfb6a42cd90280d33ull, "Hero0" },   // Aragorn
    { 0x0a545eed222f4bd4ull, "Hero1" },   // Legolas
    { 0xe17361e3b8eb9771ull, "Hero2" },   // Gimli
    { 0xffb1b47145c1b30aull, "Hero3" },   // Gandalf
    { 0xf1cdc4c0001a67a7ull, "Hero4" },   // Sam
    { 0x9e47c2a7226b7fbcull, "Hero5" },   // Frodo
    { 0x874ed86c5488792cull, "Hero6" },   // Pippin
    { 0x943fc595eadaec4eull, "Hero7" },   // Merry
    { 0x7fc49b94eacb1af6ull, "Hero8" },   // Faramir
    { 0x923bce4680ad83eeull, "FoeOrcs" },         // the green orcs (02/11/2026: no longer the Uruks' portrait)
    { 0x10d59a06a8754637ull, "FoeUruks" },
    { 0xfaffa2bc35291bcfull, "FoeUrukHai" },
    { 0xaf58f15d340f2e5dull, "FoeBerserkers" },
    { 0x2c8ef389616b33ecull, "FoeDead" },
    { 0x28a65f506405e1fbull, "FoeEasterlings" },
    { 0x4213bdd9e3210a3full, "FoeSpiders" },
    { 0xeff4cbbe8cba23f4ull, "FoeTrolls" },
    { 0xed502ad599764b1bull, "FoeGollum" },
    { 0xd43b60bee3d8cec6ull, "FoeNazgul" },
    { 0x9f6c13290b1856b2ull, "FoeKingDead" },
    { 0x25c0a087dd768e84ull, "FoeMouth" },        // the Mouth of Sauron, with his life bar (Black Gate, 02/11/2026)
};

typedef long(__stdcall* SetTexFn)(void*, unsigned long, void*);
SetTexFn g_realSetTex;
bool g_enabled = true, g_allDone;
std::unordered_set<void*> g_seen;
int g_made;

bool IconExists(const char* key)
{
    char p[MAX_PATH];
    sprintf_s(p, "%s\\ROTKPlus_icons\\%s.raw", GameDir(), key);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

bool AllDone()
{
    for (const Known& k : kKnown) if (!IconExists(k.key)) return false;
    return true;
}

struct Desc { unsigned long format, type, usage, pool, size, msType, width, height; };   // D3DSURFACE_DESC (D3D8)

const unsigned long FMT_A8R8G8B8 = 21, FMT_X8R8G8B8 = 22, FMT_R5G6B5 = 23, FMT_X1R5G5B5 = 24, FMT_A1R5G5B5 = 25,
                    FMT_A4R4G4B4 = 26, FMT_DXT1 = '1TXD', FMT_DXT3 = '3TXD', FMT_DXT5 = '5TXD';

int BlockBytes(unsigned long f) { return f == FMT_DXT1 ? 8 : (f == FMT_DXT3 || f == FMT_DXT5) ? 16 : 0; }
int PixelBytes(unsigned long f)
{
    switch (f) {
    case FMT_A8R8G8B8: case FMT_X8R8G8B8: return 4;
    case FMT_R5G6B5: case FMT_X1R5G5B5: case FMT_A1R5G5B5: case FMT_A4R4G4B4: return 2;
    }
    return 0;
}

unsigned long long Fingerprint(const uint8_t* bits, int pitch, const Desc& d)
{
    const int blk = BlockBytes(d.format), bpp = PixelBytes(d.format);
    const unsigned rows = blk ? (d.height + 3) / 4 : d.height;
    const unsigned rowBytes = blk ? (d.width + 3) / 4 * blk : d.width * bpp;
    unsigned long long h = 1469598103934665603ull;
    h = (h ^ d.width) * 1099511628211ull; h = (h ^ d.height) * 1099511628211ull; h = (h ^ d.format) * 1099511628211ull;
    for (unsigned y = 0; y < rows; y++) {
        const uint8_t* p = bits + (size_t)y * pitch;
        unsigned i = 0;
        for (; i + 8 <= rowBytes; i += 8) { unsigned long long v; memcpy(&v, p + i, 8); h = (h ^ v) * 1099511628211ull; h ^= h >> 29; }
        for (; i < rowBytes; i++) h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

void C565(uint16_t c, uint8_t* o) { o[0] = (uint8_t)((c >> 11 & 31) * 255 / 31); o[1] = (uint8_t)((c >> 5 & 63) * 255 / 63); o[2] = (uint8_t)((c & 31) * 255 / 31); }

// the top level as R, G, B, A (the formats the game's textures use)
bool ToRgba(const uint8_t* src, int pitch, const Desc& d, std::vector<uint8_t>& o)
{
    const unsigned w = d.width, h = d.height;
    o.assign((size_t)w * h * 4, 0);
    const int blk = BlockBytes(d.format);
    if (blk) {
        for (unsigned by = 0; by < (h + 3) / 4; by++)
            for (unsigned bx = 0; bx < (w + 3) / 4; bx++) {
                const uint8_t* b = src + by * pitch + bx * blk;
                const uint8_t* cb = blk == 8 ? b : b + 8;
                const uint16_t c0 = (uint16_t)(cb[0] | cb[1] << 8), c1 = (uint16_t)(cb[2] | cb[3] << 8);
                uint8_t p[4][4] = {};
                C565(c0, p[0]); C565(c1, p[1]);
                p[0][3] = p[1][3] = 255;
                if (blk == 16 || c0 > c1) {
                    for (int k = 0; k < 3; k++) { p[2][k] = (uint8_t)((2 * p[0][k] + p[1][k]) / 3); p[3][k] = (uint8_t)((p[0][k] + 2 * p[1][k]) / 3); }
                    p[2][3] = p[3][3] = 255;
                } else {
                    for (int k = 0; k < 3; k++) p[2][k] = (uint8_t)((p[0][k] + p[1][k]) / 2);
                    p[2][3] = 255; p[3][3] = 0;
                }
                const uint32_t idx = (uint32_t)(cb[4] | cb[5] << 8 | cb[6] << 16 | (uint32_t)cb[7] << 24);
                uint8_t a[16];
                if (blk == 16 && d.format == FMT_DXT3) {
                    for (int i = 0; i < 16; i++) a[i] = (uint8_t)((b[i / 2] >> (4 * (i & 1)) & 15) * 17);
                } else if (blk == 16) {
                    uint8_t t[8] = { b[0], b[1] };
                    if (t[0] > t[1]) for (int i = 1; i < 7; i++) t[i + 1] = (uint8_t)(((7 - i) * t[0] + i * t[1]) / 7);
                    else { for (int i = 1; i < 5; i++) t[i + 1] = (uint8_t)(((5 - i) * t[0] + i * t[1]) / 5); t[6] = 0; t[7] = 255; }
                    unsigned long long bits = 0;
                    for (int i = 0; i < 6; i++) bits |= (unsigned long long)b[2 + i] << (8 * i);
                    for (int i = 0; i < 16; i++) a[i] = t[bits >> (3 * i) & 7];
                }
                for (int i = 0; i < 16; i++) {
                    const unsigned x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                    if (x >= w || y >= h) continue;
                    uint8_t* q = &o[((size_t)y * w + x) * 4];
                    memcpy(q, p[idx >> (2 * i) & 3], 4);
                    if (blk == 16) q[3] = a[i];
                }
            }
        return true;
    }
    for (unsigned y = 0; y < h; y++) {
        const uint8_t* s = src + (size_t)y * pitch;
        uint8_t* q = &o[(size_t)y * w * 4];
        for (unsigned x = 0; x < w; x++, q += 4) {
            const uint16_t v = (uint16_t)(s[x * 2] | s[x * 2 + 1] << 8);
            switch (d.format) {
            case FMT_A8R8G8B8: q[0] = s[x * 4 + 2]; q[1] = s[x * 4 + 1]; q[2] = s[x * 4]; q[3] = s[x * 4 + 3]; break;
            case FMT_X8R8G8B8: q[0] = s[x * 4 + 2]; q[1] = s[x * 4 + 1]; q[2] = s[x * 4]; q[3] = 255; break;
            case FMT_R5G6B5: C565(v, q); q[3] = 255; break;
            case FMT_X1R5G5B5: case FMT_A1R5G5B5:
                q[0] = (uint8_t)((v >> 10 & 31) * 255 / 31); q[1] = (uint8_t)((v >> 5 & 31) * 255 / 31); q[2] = (uint8_t)((v & 31) * 255 / 31);
                q[3] = d.format == FMT_A1R5G5B5 ? (v & 0x8000 ? 255 : 0) : 255; break;
            case FMT_A4R4G4B4:
                q[0] = (uint8_t)((v >> 8 & 15) * 17); q[1] = (uint8_t)((v >> 4 & 15) * 17); q[2] = (uint8_t)((v & 15) * 17);
                q[3] = (uint8_t)((v >> 12 & 15) * 17); break;
            default: return false;
            }
        }
    }
    return true;
}

// the drawn part (alpha above 8), in the middle of a square, scaled to 128x128 (bilinear), written as the icon
void WriteIcon(const char* key, const std::vector<uint8_t>& px, int w, int h)
{
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (px[((size_t)y * w + x) * 4 + 3] > 8) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    if (x1 < x0) return;
    const int cw = x1 - x0 + 1, ch = y1 - y0 + 1, side = cw > ch ? cw : ch;
    const float ox = x0 - (side - cw) * 0.5f, oy = y0 - (side - ch) * 0.5f;
    const int S = 128;
    std::vector<uint8_t> out((size_t)S * S * 4, 0);
    auto at = [&](int x, int y, int k) -> float {
        if (x < x0 || x > x1 || y < y0 || y > y1) return 0.0f;
        return px[((size_t)y * w + x) * 4 + k];
    };
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            const float sx = ox + (x + 0.5f) * side / S - 0.5f, sy = oy + (y + 0.5f) * side / S - 0.5f;
            const int ix = (int)floorf(sx), iy = (int)floorf(sy);
            const float fx = sx - ix, fy = sy - iy;
            float c[4], aSum = 0.0f;
            for (int k = 0; k < 4; k++) c[k] = 0.0f;
            // premultiplied by alpha, so the transparent border does not darken the edge
            const int dx[4] = { 0, 1, 0, 1 }, dyy[4] = { 0, 0, 1, 1 };
            const float wt[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
            for (int n = 0; n < 4; n++) {
                const float a = at(ix + dx[n], iy + dyy[n], 3);
                aSum += a * wt[n];
                for (int k = 0; k < 3; k++) c[k] += at(ix + dx[n], iy + dyy[n], k) * a * wt[n];
            }
            uint8_t* q = &out[((size_t)y * S + x) * 4];
            if (aSum > 0.5f) { q[0] = (uint8_t)(c[2] / aSum); q[1] = (uint8_t)(c[1] / aSum); q[2] = (uint8_t)(c[0] / aSum); }
            q[3] = (uint8_t)(aSum + 0.5f);   // stored B, G, R, A
        }
    char dir[MAX_PATH], p[MAX_PATH];
    sprintf_s(dir, "%s\\ROTKPlus_icons", GameDir());
    CreateDirectoryA(dir, nullptr);
    sprintf_s(p, "%s\\%s.raw", dir, key);
    FILE* f = nullptr;
    if (fopen_s(&f, p, "wb") || !f) return;
    const uint32_t hdr[2] = { (uint32_t)S, (uint32_t)S };
    fwrite("RPI1", 1, 4, f);
    fwrite(hdr, 4, 2, f);
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    ++g_made;
    Log("icons: %s made from the game's portrait", key);
    PlayersIconReady(key);
}

void Look(void* tex)
{
    void** tv = *(void***)tex;
    typedef unsigned long(__stdcall* GetTypeFn)(void*);
    typedef long(__stdcall* LevelDescFn)(void*, unsigned, Desc*);
    if (((GetTypeFn)tv[10])(tex) != 3 /* D3DRTYPE_TEXTURE */) return;
    Desc d = {};
    if (((LevelDescFn)tv[14])(tex, 0, &d) < 0 || d.width != 64 || d.height != 64 || d.pool == 0 /* default */) return;
    if (!BlockBytes(d.format) && !PixelBytes(d.format)) return;
    typedef long(__stdcall* SurfLevelFn)(void*, unsigned, void**);
    void* surf = nullptr;
    if (((SurfLevelFn)tv[15])(tex, 0, &surf) < 0 || !surf) return;
    void** sv = *(void***)surf;
    struct { int pitch; void* bits; } lr = {};
    typedef long(__stdcall* LockFn)(void*, void*, const RECT*, unsigned long);
    typedef long(__stdcall* UnlockFn)(void*);
    typedef unsigned long(__stdcall* ReleaseFn)(void*);
    if (((LockFn)sv[9])(surf, &lr, nullptr, 0x10 /* D3DLOCK_READONLY */) >= 0 && lr.bits) {
        const unsigned long long h = Fingerprint((const uint8_t*)lr.bits, lr.pitch, d);
        std::vector<uint8_t> px;
        bool converted = false;
        for (const Known& k : kKnown) {
            if (k.hash != h || IconExists(k.key)) continue;
            if (!converted) converted = ToRgba((const uint8_t*)lr.bits, lr.pitch, d, px);
            if (converted) WriteIcon(k.key, px, (int)d.width, (int)d.height);
        }
        ((UnlockFn)sv[10])(surf);
    }
    ((ReleaseFn)sv[2])(surf);
}

long __stdcall HookSetTexture(void* dev, unsigned long stage, void* tex)
{
    if (tex && g_enabled && !g_allDone && g_seen.size() < 20000 && g_seen.insert(tex).second) {
        Guard([&] { Look(tex); });
        if (g_made) { g_made = 0; g_allDone = AllDone(); }
    }
    return g_realSetTex(dev, stage, tex);
}

}   // namespace

// every frame (Players.cpp): put the hook in front of the device's SetTexture once the device exists; forget the
// textures seen at each new level (their addresses are reused)
void IconsFrame(bool newLevel)
{
    static bool installed, checked;
    if (!checked) {
        checked = true;
        g_enabled = IniInt("Players", "CaptureIcons", 1) != 0;
        g_allDone = AllDone();
        Log("icons: taken from the game's portraits while playing: %s", !g_enabled ? "off" : g_allDone ? "all there already" : "on");
    }
    if (newLevel) g_seen.clear();
    if (installed || !g_enabled || g_allDone) return;
    void* dev = *(void* const*)0x5C2364;
    if (!dev) return;
    void** vt = *(void***)dev;
    DWORD old;
    if (!VirtualProtect(&vt[61], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
    g_realSetTex = (SetTexFn)vt[61];
    vt[61] = (void*)&HookSetTexture;
    VirtualProtect(&vt[61], sizeof(void*), old, &old);
    installed = true;
}
