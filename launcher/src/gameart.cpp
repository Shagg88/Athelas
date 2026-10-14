// ROTK Fix Launcher - background art from the player's own game files (see gameart.h)
//
// Level files (.scw) are SHOC chunks (see docs/ROTK-ENGINE-NOTES.md, section 1). A
// texture resource ("txfg", "txfe") holds a TXW container: HEAD, TXHE (80-byte
// entries: name[8], data offset u32 at +8, size/16 u16 at +12, width and height
// u16 at +0x38, format at +0x3c: 1 = DXT1), CLHE, TXDA (the pixels), CLDA.
#include "gameart.h"
#include <windows.h>
#include <string.h>
#include <stdlib.h>

// Pictures without text or logos ("layer1", the title screen, has the game's logo).
static const char *k_wanted[] = {
    "mainback", "fellback", "aragback", "gandback", "frodback", "samback", "legoback", "gimlback",
    "pippback", "merrback", "faraback", "codebkgd", "frttt1", "frttt2", "frfotk1", "frfotk2",
};   // (frttt0 and frfotk0, misty and almost white, leave the title unreadable)

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// The game's own decompressor (rotk.exe 1.0 at 0x4dfe10), with bounds checks.
static bool unpack(const uint8_t *src, size_t srcLen, size_t size, std::vector<uint8_t> &out)
{
    size_t base = out.size(), i = 0;
    out.reserve(base + size);
    while (out.size() - base < size) {
        if (i + 2 > srcLen) return false;
        unsigned w = (src[i] << 8) | src[i + 1]; i += 2;
        if ((w & 0x8800) == 0x8800) {
            unsigned n = (w >> 12) & 7;
            if (n == 0) {
                size_t c = w & 0x7ff;
                if (i + c > srcLen) return false;
                out.insert(out.end(), src + i, src + i + c); i += c;
            } else {
                size_t dist = ((w >> 5) & 0x38) | n, ln = (w & 0xff) + 3;
                if (dist > out.size() - base) return false;
                uint8_t b = out[out.size() - dist];
                out.insert(out.end(), ln, b);
            }
        } else {
            size_t ln = (w >> 12) & 7, off = w & 0xfff;
            if (ln == 7) { if (i >= srcLen) return false; ln = src[i++] + 7; }
            ln += 3;
            if (off > out.size() - base) return false;
            size_t p = out.size() - off;
            if (w & 0x8000) {
                p += 2;
                for (size_t k = 0; k < ln; k++) {
                    if (p < k || p - k >= out.size() || p - k < base) return false;
                    out.push_back(out[p - k]);
                }
            } else {
                for (size_t k = 0; k < ln; k++) {
                    if (p + k >= out.size()) return false;
                    out.push_back(out[p + k]);
                }
            }
        }
    }
    out.resize(base + size);
    return true;
}

static void take_textures(const std::vector<uint8_t> &d, std::vector<ArtPic> &out)
{
    size_t txhe = 0, txheLen = 0, txda = 0, txdaLen = 0, o = 8;
    while (o + 8 <= d.size()) {
        uint32_t len = rd32(&d[o + 4]);
        if (len > d.size() - o - 8) break;
        if (!memcmp(&d[o], "TXHE", 4)) { txhe = o + 8; txheLen = len; }
        if (!memcmp(&d[o], "TXDA", 4)) { txda = o + 8; txdaLen = len; }
        o += 8 + len;
    }
    if (!txhe || !txda) return;
    for (size_t e = txhe; e + 80 <= txhe + txheLen; e += 80) {
        char name[9] = {};
        memcpy(name, &d[e], 8);
        bool want = false;
        for (const char *n : k_wanted) if (!strcmp(n, name)) want = true;
        for (auto &p : out) if (p.name == name) want = false;      // already have it
        if (!want || (d[e + 0x3c] != 1 && d[e + 0x3c] != 0x0c)) continue;   // DXT1 only (1 on PC, 0x0c = Xbox D3DFMT_DXT1)
        int w = rd16(&d[e + 0x38]), h = rd16(&d[e + 0x3a]);
        size_t off = rd32(&d[e + 8]), need = (size_t)w * h / 2;
        if (w < 256 || h < 256 || w > 2048 || h > 2048 || (w | h) & 3) continue;
        if (off > txdaLen || need > txdaLen - off) continue;
        ArtPic p;
        p.name = name; p.w = w; p.h = h;
        p.dxt.assign(d.begin() + txda + off, d.begin() + txda + off + need);
        out.push_back(std::move(p));
    }
}

bool rotk_menu_art_load(const std::string &gameDir, std::vector<ArtPic> &out)
{
    std::string path = gameDir + "\\Data\\Game\\fro\\fro03.scw";
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) path.back() = 'x';   // the Xbox copy: fro03.scx
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER fsz;
    if (!GetFileSizeEx(f, &fsz) || fsz.QuadPart < 64 || fsz.QuadPart > (512ll << 20)) { CloseHandle(f); return false; }
    HANDLE m = CreateFileMappingA(f, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(f);
    if (!m) return false;
    const uint8_t *d = (const uint8_t *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(m);
    if (!d) return false;
    size_t n = (size_t)fsz.QuadPart, o = 0;
    bool cur = false;                  // inside a texture resource we want
    std::vector<uint8_t> res;
    while (o + 16 <= n) {
        if (memcmp(d + o, "COHS", 4)) { o += 4; continue; }
        size_t sz = rd32(d + o + 4);
        if (sz > n - o) break;
        const uint8_t *sub = d + o + 16;
        if (!memcmp(sub, "RDHS", 4) && o + 28 <= n) {
            if (cur) take_textures(res, out);
            res.clear();
            cur = !memcmp(d + o + 24, "gfxt", 4) || !memcmp(d + o + 24, "efxt", 4);   // "txfg", "txfe" stored reversed
        } else if (cur && !memcmp(sub, "TADS", 4) && sz > 0x40) {
            res.insert(res.end(), d + o + 0x40, d + o + sz);
        } else if (cur && !memcmp(sub, "tadR", 4) && sz > 0x44) {
            size_t len = rd32(d + o + 0x40);
            if (len > (64u << 20) || !unpack(d + o + 0x44, sz - 0x44, len, res)) { cur = false; res.clear(); }
        }
        o += sz >= 16 ? sz : 16;
    }
    if (cur) take_textures(res, out);
    UnmapViewOfFile(d);
    return !out.empty();
}

unsigned char *dxt1_to_rgba(const uint8_t *src, size_t srcLen, int w, int h)
{
    if ((size_t)w * h / 2 > srcLen) return nullptr;
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
    if (!px) return nullptr;
    for (int by = 0; by < h / 4; by++)
        for (int bx = 0; bx < w / 4; bx++, src += 8) {
            unsigned c0 = rd16(src), c1 = rd16(src + 2);
            uint32_t idx = rd32(src + 4);
            int pal[4][3];
            for (int k = 0; k < 2; k++) {
                unsigned c = k ? c1 : c0;
                pal[k][0] = ((c >> 11) & 31) * 255 / 31;
                pal[k][1] = ((c >> 5) & 63) * 255 / 63;
                pal[k][2] = (c & 31) * 255 / 31;
            }
            for (int ch = 0; ch < 3; ch++) {
                if (c0 > c1) {
                    pal[2][ch] = (2 * pal[0][ch] + pal[1][ch]) / 3;
                    pal[3][ch] = (pal[0][ch] + 2 * pal[1][ch]) / 3;
                } else {
                    pal[2][ch] = (pal[0][ch] + pal[1][ch]) / 2;
                    pal[3][ch] = 0;
                }
            }
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++) {
                    const int *c = pal[(idx >> (2 * (y * 4 + x))) & 3];
                    unsigned char *d = px + ((size_t)(by * 4 + y) * w + bx * 4 + x) * 4;
                    d[0] = (unsigned char)c[0]; d[1] = (unsigned char)c[1]; d[2] = (unsigned char)c[2]; d[3] = 255;
                }
        }
    return px;
}

// ---------------------------------------------------------------------------
// The Two Towers: UI\*.BFF. Each picture has a 0x80-byte header (magic 0x00056278,
// width, height, type: 10 = 32-bit BGRA, 17 = DXT1) followed by its pixels.
void tt_art_list(const std::string &gameDir, std::vector<std::string> &out)
{
    struct { const char *file; uint32_t hdr; } pics[48];
    int n = 0;
    static const char *stills[] = { "UI\\stills1.BFF", "UI\\stills2.BFF", "UI\\stills3.BFF", "UI\\stills4.BFF" };
    for (int f = 0; f < 4; f++)
        for (int k = 0; k < 10; k++)
            if (!(f == 1 && k == 3)) pics[n++] = { stills[f], 0xF80u + (uint32_t)k * 0x119000u };   // stills2 #3 has black side bars
    pics[n++] = { "UI\\mainmenu.BFF", 0xF80 };       // the parchment level-select map (no lettering)
    pics[n++] = { "UI\\mainmenu.BFF", 0x170F80 };    // main menu art (not the title screens after it: logo)
    pics[n++] = { "UI\\options.BFF", 0xF80 };        // the cave troll
    for (int i = 0; i < n; i++) {
        std::string p = gameDir + "\\" + pics[i].file;
        if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        out.push_back("\002tt:" + std::to_string(pics[i].hdr) + ":" + p);
    }
}

unsigned char *tt_picture_load(const std::string &entry, int &w, int &h)
{
    if (entry.compare(0, 4, "\002tt:")) return nullptr;
    size_t colon = entry.find(':', 4);
    if (colon == std::string::npos) return nullptr;
    uint32_t hdrOff = (uint32_t)strtoul(entry.c_str() + 4, nullptr, 10);
    HANDLE f = CreateFileA(entry.c_str() + colon + 1, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    uint8_t hd[0x80];
    DWORD got = 0;
    unsigned char *px = nullptr;
    if (SetFilePointer(f, hdrOff, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER && ReadFile(f, hd, sizeof hd, &got, NULL) && got == sizeof hd
        && rd32(hd) == 0x00056278) {
        uint32_t pw = rd32(hd + 4), ph = rd32(hd + 8), type = rd32(hd + 12);
        if (pw >= 64 && ph >= 64 && pw <= 2048 && ph <= 2048 && !(pw & 3) && !(ph & 3) && (type == 10 || type == 17)) {
            size_t len = type == 10 ? (size_t)pw * ph * 4 : (size_t)pw * ph / 2;
            std::vector<uint8_t> raw(len);
            if (ReadFile(f, raw.data(), (DWORD)len, &got, NULL) && got == len) {
                if (type == 17) px = dxt1_to_rgba(raw.data(), len, (int)pw, (int)ph);
                else if ((px = (unsigned char *)malloc(len)) != nullptr)
                    for (size_t i = 0; i < len; i += 4) {        // BGRA -> RGBA
                        px[i] = raw[i + 2]; px[i + 1] = raw[i + 1]; px[i + 2] = raw[i]; px[i + 3] = 255;
                    }
                w = (int)pw; h = (int)ph;
            }
        }
    }
    CloseHandle(f);
    return px;
}
