// ROTK Xbox: sharper text from ROTK PC's own fonts ([graphics] sharp_text, ROTKFix.ini [Graphics] SharperText; off by
// default).
//
// The Xbox game draws its text from three fonts in Data/Game/GlobScen.scx, made for a 640x480 TV: a body face (31
// pixel lines) and a title face (38 and 19). Each is a 4-bit coverage atlas (256x232, 512x312, 256x184) put in a P8
// texture, with one colour per coverage level; scaled up to a 1440 line screen it goes soft. ROTK PC has the same two
// faces in its GlobScen.scw at sizes up to 50 and 61 pixel lines, made for higher resolutions, and picks a size by
// resolution, so its text stays sharp.
//
// With sharp_text on and ROTK PC's Data folder beside the ROTKFix.ini the port is given (the launcher's game folder),
// each Xbox font texture is drawn from an atlas four times its size, laid out as the Xbox one, so the game's own glyph
// coordinates still apply. Every glyph there is the PC's largest glyph of the same face and code, scaled from its ink
// box onto the Xbox glyph's ink box (so spacing and placement stay the Xbox game's). A glyph the PC font lacks is the
// Xbox one scaled up. The texture's own colours are kept, blended between levels. Nothing of ROTK PC is copied into
// the port: the PC files are read from the player's own install at start, and without them nothing changes.
//
// File layout (both releases): the .scx/.scw file is a run of chunks (tag, size with header, 0x40 byte header); the
// SDAT chunks' payloads, joined, hold "FNTS" chunks: +0xA glyph count (u16), +0x13 line height, +0x14 offset of the
// 12-byte glyph records (code u16, w, h, x u16, y u16, advance, x offset, y offset, pad), +0x1C offset of the atlas
// header (width u16 at +4, height u16 at +6), whose 4-bit pixels (low nibble first) follow its 16 bytes.
// The Xbox texture is found by its contents: P8, the atlas's power-of-two size, one level, and the sum of its indices
// equal to the atlas's sum of coverage (the indices are the coverage levels, whatever the swizzle).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "gpu/texture_pack.h"
#include "kernel/kernel.h"

namespace rotk {
bool g_sharp_text = false;                 // [graphics] sharp_text, ROTKFix.ini [Graphics] SharperText
extern std::filesystem::path g_ini_file;  // rotk_settings.cpp: the ROTKFix.ini given, empty without one
void PrepareMenuArt();                                                       // rotk_menu_art.cpp
bool ReplaceMenuArt(const gpu::pack::Contents& t, gpu::pack::Image& out);  // rotk_menu_art.cpp

namespace {
constexpr uint32_t kScale = 4;  // the replacement atlas's size over the Xbox one

struct Glyph {
    int w, h, x, y, adv, xo, yo;
};
struct Font {
    int line = 0;
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;  // coverage 0..15, w x h
    std::map<uint16_t, Glyph> glyphs;
    uint8_t At(int x, int y) const { return px[size_t(y) * w + x]; }
};

uint32_t U16(const std::vector<uint8_t>& d, size_t o) { return uint32_t(d[o] | d[o + 1] << 8); }
uint32_t U32(const std::vector<uint8_t>& d, size_t o) { return U16(d, o) | U16(d, o + 2) << 16; }

// the SDAT payloads of a SHOC file, joined
std::vector<uint8_t> Stream(const std::vector<uint8_t>& d) {
    std::vector<uint8_t> out;
    for (size_t o = 0x18; o + 0x40 <= d.size();) {
        uint32_t size = U32(d, o + 4);
        if (size < 8 || o + size > d.size()) break;
        if (size >= 0x40 && std::memcmp(&d[o + 0x10], "TADS", 4) == 0) out.insert(out.end(), d.begin() + o + 0x40, d.begin() + o + size);
        o += size;
    }
    return out;
}

std::vector<Font> Fonts(const std::vector<uint8_t>& s) {
    std::vector<Font> out;
    for (size_t i = 0; i + 0x20 <= s.size(); i++) {
        if (std::memcmp(&s[i], "FNTS", 4) != 0) continue;
        Font f;
        uint32_t count = U16(s, i + 0xA), rec = U32(s, i + 0x14), tex = U32(s, i + 0x1C);
        f.line = s[i + 0x13];
        if (i + tex + 16 > s.size() || i + rec + count * 12 > s.size()) continue;
        f.w = U16(s, i + tex + 4);
        f.h = U16(s, i + tex + 6);
        if (!f.w || !f.h || f.w > 4096 || f.h > 4096) continue;
        f.px.assign(size_t(f.w) * f.h, 0);
        size_t at = i + tex + 16, n = std::min(f.px.size() / 2, s.size() - at);
        for (size_t k = 0; k < n; k++) {
            f.px[2 * k] = s[at + k] & 15;
            f.px[2 * k + 1] = s[at + k] >> 4;
        }
        for (uint32_t k = 0; k < count; k++) {
            size_t r = i + rec + k * 12;
            Glyph g{s[r + 2], s[r + 3], int(U16(s, r + 4)), int(U16(s, r + 6)), s[r + 8], int8_t(s[r + 9]), int8_t(s[r + 10])};
            if (g.x + g.w > int(f.w) || g.y + g.h > int(f.h)) continue;
            f.glyphs[uint16_t(U16(s, r))] = g;
        }
        out.push_back(std::move(f));
    }
    return out;
}

struct Box {
    int x0, y0, x1, y1;  // ink, within the glyph; x1, y1 past the end
    bool ok;
};
Box Ink(const Font& f, const Glyph& g) {
    Box b{g.w, g.h, 0, 0, false};
    for (int y = 0; y < g.h; y++)
        for (int x = 0; x < g.w; x++)
            if (f.At(g.x + x, g.y + y) > 3) {
                b.x0 = std::min(b.x0, x), b.y0 = std::min(b.y0, y), b.x1 = std::max(b.x1, x + 1), b.y1 = std::max(b.y1, y + 1);
                b.ok = true;
            }
    return b;
}

// coverage of f at (x, y) in atlas pixels, bilinear, 0 outside the glyph's own rectangle
float Sample(const Font& f, const Glyph& g, float x, float y) {
    x -= 0.5f;
    y -= 0.5f;
    if (x < -0.5f || y < -0.5f || x > g.w - 0.5f || y > g.h - 0.5f) return 0;
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    float fx = x - x0, fy = y - y0;
    auto at = [&](int xx, int yy) {
        xx = std::clamp(xx, 0, g.w - 1);
        yy = std::clamp(yy, 0, g.h - 1);
        return float(f.At(g.x + xx, g.y + yy));
    };
    return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

// how unlike two fonts' letters are, 0 the same shapes: each letter's ink box resampled onto the other's
float Unlike(const Font& xb, const Font& pc) {
    float sum = 0;
    int n = 0;
    for (char c : std::string("aegnoRSt")) {
        auto a = xb.glyphs.find(uint8_t(c)), b = pc.glyphs.find(uint8_t(c));
        if (a == xb.glyphs.end() || b == pc.glyphs.end()) continue;
        Box ba = Ink(xb, a->second), bb = Ink(pc, b->second);
        if (!ba.ok || !bb.ok) continue;
        float sx = float(bb.x1 - bb.x0) / (ba.x1 - ba.x0), sy = float(bb.y1 - bb.y0) / (ba.y1 - ba.y0);
        float e = 0;
        for (int y = ba.y0; y < ba.y1; y++)
            for (int x = ba.x0; x < ba.x1; x++)
                e += std::fabs(xb.At(a->second.x + x, a->second.y + y) -
                               Sample(pc, b->second, bb.x0 + (x + 0.5f - ba.x0) * sx, bb.y0 + (y + 0.5f - ba.y0) * sy));
        sum += e / (15.0f * (ba.x1 - ba.x0) * (ba.y1 - ba.y0));
        n++;
    }
    return n ? sum / n : 1e9f;
}

// one Xbox font, ready to replace its texture
struct Sharp {
    uint32_t tex_w, tex_h;  // the texture: the atlas's power-of-two size
    uint64_t sum;           // its coverage summed
    std::vector<float> cov; // tex_w * kScale x tex_h * kScale, 0..15
    int line, pc_line;
};

uint32_t Pow2(uint32_t v) {
    uint32_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

Sharp Build(const Font& xb, const std::vector<Font>& pc) {
    Sharp s{Pow2(xb.w), Pow2(xb.h), 0, {}, xb.line, 0};
    for (uint8_t v : xb.px) s.sum += v;
    // the PC face: the fonts whose letters match, the largest of them
    float best = 1e9f;
    std::vector<float> unlike(pc.size());
    for (size_t i = 0; i < pc.size(); i++) best = std::min(best, unlike[i] = Unlike(xb, pc[i]));
    const Font* src = nullptr;
    for (size_t i = 0; i < pc.size(); i++)
        if (unlike[i] <= std::max(best * 2.0f, best + 0.04f) && (!src || pc[i].line > src->line)) src = &pc[i];
    uint32_t W = s.tex_w * kScale, H = s.tex_h * kScale;
    s.cov.assign(size_t(W) * H, 0.0f);
    // the Xbox atlas, scaled up: what stays for glyphs the PC font lacks
    for (uint32_t y = 0; y < xb.h * kScale; y++)
        for (uint32_t x = 0; x < xb.w * kScale; x++) {
            float fx = (x + 0.5f) / kScale - 0.5f, fy = (y + 0.5f) / kScale - 0.5f;
            int x0 = std::clamp(int(std::floor(fx)), 0, int(xb.w) - 1), y0 = std::clamp(int(std::floor(fy)), 0, int(xb.h) - 1);
            int x1 = std::min(x0 + 1, int(xb.w) - 1), y1 = std::min(y0 + 1, int(xb.h) - 1);
            float ax = std::clamp(fx - x0, 0.0f, 1.0f), ay = std::clamp(fy - y0, 0.0f, 1.0f);
            s.cov[size_t(y) * W + x] = (xb.At(x0, y0) * (1 - ax) + xb.At(x1, y0) * ax) * (1 - ay) + (xb.At(x0, y1) * (1 - ax) + xb.At(x1, y1) * ax) * ay;
        }
    if (!src) return s;
    s.pc_line = src->line;
    for (const auto& [code, g] : xb.glyphs) {
        auto it = src->glyphs.find(code);
        if (it == src->glyphs.end() || g.w < 2 || g.h < 2) continue;
        const Glyph& pg = it->second;
        Box bx = Ink(xb, g), bp = Ink(*src, pg);
        if (!bx.ok || !bp.ok) continue;
        float sx = float(bp.x1 - bp.x0) / (bx.x1 - bx.x0), sy = float(bp.y1 - bp.y0) / (bx.y1 - bx.y0);
        for (int v = 0; v < g.h * int(kScale); v++)
            for (int u = 0; u < g.w * int(kScale); u++) {
                float cx = (u + 0.5f) / kScale, cy = (v + 0.5f) / kScale;  // in the Xbox glyph
                s.cov[size_t(g.y * kScale + v) * W + g.x * kScale + u] =
                    Sample(*src, pg, bp.x0 + (cx - bx.x0) * sx, bp.y0 + (cy - bx.y0) * sy);
            }
    }
    return s;
}

std::vector<Sharp> Prepare() {
    PrepareMenuArt();
    std::vector<uint8_t> xb_file, pc_file;
    // D: may be mounted a moment after the image loads
    bool read = false;
    for (int i = 0; i < 100 && !(read = kernel::ReadGameFile("D:\\Data\\Game\\GlobScen.scx", xb_file)); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!read) {
        LOGR("rotk: sharp text: Data\\Game\\GlobScen.scx not readable, text left as it is");
        return {};
    }
    std::filesystem::path pc = g_ini_file.empty() ? std::filesystem::path() : g_ini_file.parent_path() / "Data" / "Game" / "GlobScen.scw";
    std::ifstream f(pc, std::ios::binary);
    if (pc.empty() || !f) {
        LOGR("rotk: sharp text needs ROTK PC's Data folder beside ROTKFix.ini (%s); text left as it is", pc.string().c_str());
        return {};
    }
    pc_file.assign(std::istreambuf_iterator<char>(f), {});
    std::vector<Font> xb = Fonts(Stream(xb_file)), pcf = Fonts(Stream(pc_file));
    if (xb.empty() || pcf.empty()) {
        LOGR("rotk: sharp text: fonts not found (Xbox %zu, PC %zu); text left as it is", xb.size(), pcf.size());
        return {};
    }
    std::vector<Sharp> out;
    for (const Font& x : xb) {
        out.push_back(Build(x, pcf));
        const Sharp& s = out.back();
        LOGR("rotk: sharp text: the %d pixel font (%ux%u) from ROTK PC's %d pixel one, at %ux%u", s.line, x.w, x.h,
             s.pc_line, s.tex_w * kScale, s.tex_h * kScale);
    }
    return out;
}

std::shared_future<std::vector<Sharp>> g_prepared;

bool Replace(const gpu::pack::Contents& t, gpu::pack::Image& out) {
    if (!t.data || !g_prepared.valid() || (t.format != 0x0B && t.format != 0x24)) return false;
    const std::vector<Sharp>& fonts = g_prepared.get();
    if (t.format == 0x24) return ReplaceMenuArt(t, out);  // YUY2: the menu pictures
    if (t.levels != 1) return false;                       // P8 fonts, one level
    for (const Sharp& s : fonts) {
        if (s.tex_w != t.width || s.tex_h != t.height || t.size < size_t(t.width) * t.height) continue;
        uint64_t sum = 0;
        for (size_t i = 0; i < size_t(t.width) * t.height; i++) sum += t.data[i];
        if (sum != s.sum) continue;
        // the texture's colours by coverage level (a white ramp without them), blended between levels
        uint32_t pal[16];
        for (uint32_t i = 0; i < 16; i++) pal[i] = t.palette ? t.palette[i] : (i * 17u) << 24 | 0xFFFFFFu;
        static std::atomic<int> logged{0};
        if (t.palette && !logged++) {
            LOGR("rotk: sharp text: font colours %08X %08X %08X %08X ... %08X", pal[0], pal[1], pal[2], pal[8], pal[15]);
        }
        uint32_t W = t.width * kScale, H = t.height * kScale;
        out = {};
        out.width = W;
        out.height = H;
        out.format = gpu::pack::Format::BGRA8;  // D3DCOLOR as stored
        std::vector<uint8_t>& px = out.levels.emplace_back(size_t(W) * H * 4);
        for (size_t i = 0; i < size_t(W) * H; i++) {
            float c = std::clamp(s.cov[i], 0.0f, 15.0f);
            uint32_t lo = uint32_t(c), hi = std::min(lo + 1, 15u);
            float a = c - lo;
            for (int k = 0; k < 4; k++) {
                float v = float((pal[lo] >> (8 * k)) & 255) * (1 - a) + float((pal[hi] >> (8 * k)) & 255) * a;
                px[i * 4 + k] = uint8_t(v + 0.5f);
            }
        }
#ifndef _WIN32
        if (std::getenv("ROTK_FONT_DUMP")) {  // test build: the replacement as a PNG
            std::vector<uint8_t> rgba(px);
            for (size_t i = 0; i < rgba.size(); i += 4) std::swap(rgba[i], rgba[i + 2]);
            gpu::pack::WritePng(std::string(std::getenv("ROTK_FONT_DUMP")) + "/font" + std::to_string(s.line) + ".png", W, H, rgba);
        }
#endif
        gpu::pack::MakeLevels(out);
        LOGR("rotk: sharp text: the %d pixel font drawn from ROTK PC's", s.line);
        return true;
    }
    return false;
}
}  // namespace

// GameInfo::loaded (rotk_movies.cpp Loaded): the fonts are prepared on a thread of their own
void InitFonts() {
    if (!g_sharp_text) return;
    g_prepared = std::async(std::launch::async, Prepare).share();
    gpu::pack::g_builtin_contents = Replace;
}
}  // namespace rotk
