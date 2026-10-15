// ROTK Xbox: menu pictures from ROTK PC's larger copies (part of [graphics] sharp_text, ROTKFix.ini SharperText).
//
// The Xbox game shows its title screen and its main menu over full-screen pictures it decodes at run time into a
// 512x448 YUY2 texture (linear, format 0x24). ROTK PC has the same pictures as 1024x1024 DXT1 textures named "layer1"
// (title) and "mainback" (main menu) in Data/Game/fro/fro03.scw, framed a little differently. With sharp_text on and
// ROTK PC's Data folder beside ROTKFix.ini, such a texture is drawn from the PC picture instead, at twice its size,
// resampled into the Xbox picture's own framing (measured once from both pictures, below), so nothing on the menus
// moves. Rows the PC picture does not reach keep the Xbox picture. A texture is matched to a PC picture by comparing
// small copies of both.
//
// One that matches neither is scaled up four times instead (Upscale, below), as it is drawn about that large at 1440p
// and above: the save and options screens' art, which the PC version does not have, and the title and main menu when
// the PC files are missing. Luma gets a light sharpen and Lanczos-3, the YUY2 texture's half-width colour stays
// smooth, so it is as crisp as the art allows without inventing detail. Never while a movie plays (movie frames are
// linear textures too, each one new), and at most 8 different pictures.
//
// fro03.scw is a SHOC file: chunks of tag, size (with header) and a 0x40-byte header; "FILL" pads to the next 64 KB.
// SHOC chunks carry SDAT (stored) or Rdat (compressed: the game's decompressor, Xbox 0xF2CE0, big-endian 16-bit
// tokens) payloads; joined, they hold "TXHE" texture tables (tag, size, then 0x50-byte records: name[8], data offset
// u32 at +8, width u16 at +0x38, height u16 at +0x3A, format at +0x3C) whose pixels start 0x18 bytes after the table.
#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/texture_pack.h"

namespace rotk {
extern std::filesystem::path g_ini_file;  // rotk_settings.cpp

namespace {
// the PC pictures: name, and where an Xbox texel (x, y) of the 512x448 picture falls in the 1024x1024 one
struct Known {
    const char* name;
    float ax, bx, ay, by;  // pc = a * xbox + b
};
constexpr Known kKnown[] = {
    {"layer1", 1.8660f, 68.17f, 2.4779f, -31.81f},    // title
    {"mainback", 1.6762f, 43.97f, 2.3042f, -5.81f},  // main menu
};
constexpr uint32_t kW = 512, kH = 448, kScale = 2, kThumbW = 64, kThumbH = 56;

struct Picture {
    const Known* known = nullptr;
    std::vector<uint8_t> rgb;            // 1024 x 1024 x 3
    std::array<float, kThumbW * kThumbH * 3> thumb{};  // the PC picture as the Xbox one frames it, small
};
std::vector<Picture> g_pictures;

uint32_t U32(const std::vector<uint8_t>& d, size_t o) { return uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24); }

// the game's Rdat decompressor (Xbox 0xF2CE0)
bool Unpack(const uint8_t* src, size_t src_size, size_t size, std::vector<uint8_t>& out) {
    size_t base = out.size(), end = base + size, pos = 0;
    out.reserve(end);
    while (out.size() < end) {
        if (pos + 2 > src_size) return false;
        uint32_t t = uint32_t(src[pos] << 8 | src[pos + 1]);
        pos += 2;
        if ((t & 0x8800) == 0x8800) {
            uint32_t n = (t >> 12) & 7;
            if (n == 0) {
                uint32_t k = t & 0x7FF;
                if (pos + k > src_size) return false;
                out.insert(out.end(), src + pos, src + pos + k);
                pos += k;
            } else {
                uint32_t dist = ((t >> 5) & 0x38) | n, len = (t & 0xFF) + 3;
                if (dist > out.size() - base) return false;
                uint8_t b = out[out.size() - dist];
                out.insert(out.end(), len, b);
            }
        } else {
            uint32_t len = (t >> 12) & 7;
            if (len == 7) {
                if (pos >= src_size) return false;
                len = src[pos++] + 7u;
            }
            len += 3;
            uint32_t dist = t & 0xFFF;
            if (dist > out.size() - base) return false;
            if (t & 0x8000) {  // backwards from two past the distance
                size_t s = out.size() - dist + 2;
                for (uint32_t i = 0; i < len; i++) {
                    if (s < i + base || s - i >= out.size()) return false;
                    out.push_back(out[s - i]);
                }
            } else {
                size_t s = out.size() - dist;
                for (uint32_t i = 0; i < len; i++) out.push_back(out[s + i]);
            }
        }
    }
    out.resize(end);
    return true;
}

// the payloads joined, until want(stream) says enough
template <class Want>
std::vector<uint8_t> Stream(const std::vector<uint8_t>& d, Want want) {
    std::vector<uint8_t> out;
    auto find = [&](size_t from) {
        for (size_t i = from; i + 4 <= d.size(); i += 4)
            if (std::memcmp(&d[i], "COHS", 4) == 0) return i;
        return d.size();
    };
    for (size_t o = find(0); o + 0x44 <= d.size();) {
        uint32_t size = U32(d, o + 4);
        if (std::memcmp(&d[o], "LLIF", 4) == 0) {  // FILL: to the next 64 KB block
            o = (o / 0x10000 + 1) * 0x10000;
            continue;
        }
        if (size < 8 || o + size > d.size()) {  // padding
            o = find(o + 4);
            continue;
        }
        bool chunk = !std::memcmp(&d[o], "COHS", 4) || !std::memcmp(&d[o], "ONOS", 4);
        if (chunk && size >= 0x40 && !std::memcmp(&d[o + 0x10], "TADS", 4)) {
            out.insert(out.end(), d.begin() + o + 0x40, d.begin() + o + size);
        } else if (chunk && size >= 0x44 && !std::memcmp(&d[o + 0x10], "tadR", 4)) {
            if (!Unpack(&d[o + 0x44], size - 0x44, U32(d, o + 0x40), out)) break;
        }
        if (want(out)) break;
        o += size;
    }
    return out;
}

void Dxt1(const uint8_t* b, uint32_t w, uint32_t h, std::vector<uint8_t>& rgb) {
    rgb.assign(size_t(w) * h * 3, 0);
    auto c565 = [](uint32_t c, int* o) { o[0] = int((c >> 11) & 31) * 255 / 31, o[1] = int((c >> 5) & 63) * 255 / 63, o[2] = int(c & 31) * 255 / 31; };
    for (uint32_t by = 0; by < h / 4; by++)
        for (uint32_t bx = 0; bx < w / 4; bx++, b += 8) {
            uint32_t c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
            int p[4][3];
            c565(c0, p[0]);
            c565(c1, p[1]);
            for (int k = 0; k < 3; k++) {
                p[2][k] = c0 > c1 ? (2 * p[0][k] + p[1][k]) / 3 : (p[0][k] + p[1][k]) / 2;
                p[3][k] = c0 > c1 ? (p[0][k] + 2 * p[1][k]) / 3 : 0;
            }
            uint32_t bits = b[4] | b[5] << 8 | b[6] << 16 | uint32_t(b[7]) << 24;
            for (int i = 0; i < 16; i++) {
                const int* c = p[(bits >> (2 * i)) & 3];
                uint8_t* o = &rgb[(size_t(by * 4 + i / 4) * w + bx * 4 + i % 4) * 3];
                o[0] = uint8_t(c[0]), o[1] = uint8_t(c[1]), o[2] = uint8_t(c[2]);
            }
        }
}

// bilinear RGB at (x, y) in a w x h picture; false outside it
bool At(const std::vector<uint8_t>& rgb, uint32_t w, uint32_t h, float x, float y, float* out) {
    if (x < 0 || y < 0 || x > w - 1.0f || y > h - 1.0f) return false;
    uint32_t x0 = std::min(uint32_t(x), w - 2), y0 = std::min(uint32_t(y), h - 2);
    float fx = x - x0, fy = y - y0;
    for (int k = 0; k < 3; k++) {
        auto v = [&](uint32_t xx, uint32_t yy) { return float(rgb[(size_t(yy) * w + xx) * 3 + k]); };
        out[k] = (v(x0, y0) * (1 - fx) + v(x0 + 1, y0) * fx) * (1 - fy) + (v(x0, y0 + 1) * (1 - fx) + v(x0 + 1, y0 + 1) * fx) * fy;
    }
    return true;
}

// the Xbox YUY2 picture as RGB (BT.601), w x h x 3
std::vector<uint8_t> FromYuy2(const uint8_t* p, uint32_t w, uint32_t h, uint32_t pitch) {
    std::vector<uint8_t> rgb(size_t(w) * h * 3);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t* q = p + size_t(y) * pitch + (x & ~1u) * 2;
            float Y = q[(x & 1) * 2], U = q[1] - 128.0f, V = q[3] - 128.0f;
            float c[3] = {Y + 1.402f * V, Y - 0.344f * U - 0.714f * V, Y + 1.772f * U};
            for (int k = 0; k < 3; k++) rgb[(size_t(y) * w + x) * 3 + k] = uint8_t(std::clamp(c[k], 0.0f, 255.0f) + 0.5f);
        }
    return rgb;
}

template <class Get>
void Thumb(Get get, float* out) {  // kThumbW x kThumbH means of 8x8 Xbox texels
    for (uint32_t ty = 0; ty < kThumbH; ty++)
        for (uint32_t tx = 0; tx < kThumbW; tx++) {
            float sum[3] = {0, 0, 0};
            int n = 0;
            for (uint32_t y = ty * 8 + 1; y < ty * 8 + 8; y += 2)
                for (uint32_t x = tx * 8 + 1; x < tx * 8 + 8; x += 2) {
                    float c[3];
                    if (!get(float(x), float(y), c)) continue;
                    for (int k = 0; k < 3; k++) sum[k] += c[k];
                    n++;
                }
            for (int k = 0; k < 3; k++) out[(ty * kThumbW + tx) * 3 + k] = n ? sum[k] / n : -1.0f;
        }
}

// Lanczos-3 weights for an enlargement by scale: output texel u reads source texels around (u + 0.5) / scale - 0.5
struct Taps {
    int first;
    float w[6];
};
float Lanczos3(float x) {
    x = std::fabs(x);
    if (x < 1e-5f) return 1.0f;
    if (x >= 3.0f) return 0.0f;
    constexpr float kPi = 3.14159265f;
    return 3.0f * std::sin(kPi * x) * std::sin(kPi * x / 3.0f) / (kPi * kPi * x * x);
}
std::vector<Taps> Weights(uint32_t out_n, uint32_t scale) {
    std::vector<Taps> t(out_n);
    for (uint32_t u = 0; u < out_n; u++) {
        float x = (u + 0.5f) / float(scale) - 0.5f;
        int f = int(std::floor(x)) - 2;
        float sum = 0;
        for (int i = 0; i < 6; i++) sum += t[u].w[i] = Lanczos3(x - float(f + i));
        for (float& w : t[u].w) w /= sum;
        t[u].first = f;
    }
    return t;
}

// fn(first, end) over rows [0, n), split between a few threads
template <class Fn>
void Rows(uint32_t n, Fn fn) {
    uint32_t k = std::clamp(std::thread::hardware_concurrency(), 1u, 8u), step = (n + k - 1) / k;
    std::vector<std::thread> threads;
    for (uint32_t a = step; a < n; a += step) threads.emplace_back(fn, a, std::min(a + step, n));
    fn(0u, std::min(step, n));
    for (std::thread& t : threads) t.join();
}

// a 512x448 YUY2 picture made kUpscale times larger, RGBA8: luma given a light unsharp mask (3x3, limited so hard
// edges do not ring) and enlarged by Lanczos-3, the half-width colour (co-sited with the even texels) bilinear
constexpr uint32_t kUpscale = 4;
std::vector<uint8_t> Upscale(const uint8_t* p, uint32_t pitch) {
    constexpr uint32_t W = kW * kUpscale, H = kH * kUpscale;
    std::vector<float> Y(size_t(kW) * kH), U(size_t(kW / 2) * kH), V(U.size());
    for (uint32_t y = 0; y < kH; y++)
        for (uint32_t x = 0; x < kW; x += 2) {
            const uint8_t* q = p + size_t(y) * pitch + x * 2;
            Y[size_t(y) * kW + x] = q[0], Y[size_t(y) * kW + x + 1] = q[2];
            U[size_t(y) * (kW / 2) + x / 2] = q[1] - 128.0f, V[size_t(y) * (kW / 2) + x / 2] = q[3] - 128.0f;
        }
    constexpr float kAmount = 0.5f, kLimit = 16.0f;
    std::vector<float> sharp(Y.size());
    auto at = [&](int x, int y) { return Y[size_t(std::clamp(y, 0, int(kH) - 1)) * kW + std::clamp(x, 0, int(kW) - 1)]; };
    for (int y = 0; y < int(kH); y++)
        for (int x = 0; x < int(kW); x++) {
            float blur = (4 * at(x, y) + 2 * (at(x - 1, y) + at(x + 1, y) + at(x, y - 1) + at(x, y + 1)) +
                          at(x - 1, y - 1) + at(x + 1, y - 1) + at(x - 1, y + 1) + at(x + 1, y + 1)) / 16.0f;
            sharp[size_t(y) * kW + x] = at(x, y) + std::clamp((at(x, y) - blur) * kAmount, -kLimit, kLimit);
        }
    // luma: across, then down (edges clamped)
    std::vector<Taps> tx = Weights(W, kUpscale), ty = Weights(H, kUpscale);
    std::vector<float> across(size_t(W) * kH);
    Rows(kH, [&](uint32_t first, uint32_t end) {
        for (uint32_t y = first; y < end; y++)
            for (uint32_t u = 0; u < W; u++) {
                float v = 0;
                for (int i = 0; i < 6; i++) v += tx[u].w[i] * sharp[size_t(y) * kW + std::clamp(tx[u].first + i, 0, int(kW) - 1)];
                across[size_t(y) * W + u] = v;
            }
    });
    std::vector<uint8_t> px(size_t(W) * H * 4);
    Rows(H, [&](uint32_t first, uint32_t end) {
        for (uint32_t v = first; v < end; v++) {
            float sy = std::clamp((v + 0.5f) / kUpscale - 0.5f, 0.0f, kH - 1.0f);
            uint32_t y0 = std::min(uint32_t(sy), kH - 2);
            float fy = sy - y0;
            const float* rows[6];
            for (int i = 0; i < 6; i++) rows[i] = &across[size_t(std::clamp(ty[v].first + i, 0, int(kH) - 1)) * W];
            for (uint32_t u = 0; u < W; u++) {
                float l = 0;
                for (int i = 0; i < 6; i++) l += ty[v].w[i] * rows[i][u];
                float cx = std::clamp(((u + 0.5f) / kUpscale - 0.5f) / 2.0f, 0.0f, kW / 2 - 1.0f);
                uint32_t x0 = std::min(uint32_t(cx), kW / 2 - 2);
                float fx = cx - x0;
                auto bl = [&](const std::vector<float>& c) {
                    const float* r0 = &c[size_t(y0) * (kW / 2)];
                    const float* r1 = r0 + kW / 2;
                    return (r0[x0] * (1 - fx) + r0[x0 + 1] * fx) * (1 - fy) + (r1[x0] * (1 - fx) + r1[x0 + 1] * fx) * fy;
                };
                float cu = bl(U), cv = bl(V);
                float c[3] = {l + 1.402f * cv, l - 0.344f * cu - 0.714f * cv, l + 1.772f * cu};
                uint8_t* to = &px[(size_t(v) * W + u) * 4];
                for (int i = 0; i < 3; i++) to[i] = uint8_t(std::clamp(c[i], 0.0f, 255.0f) + 0.5f);
                to[3] = 255;
            }
        }
    });
    return px;
}
}  // namespace

// after the image loads, on the fonts' thread (rotk_fonts.cpp Prepare)
void PrepareMenuArt() {
    if (g_ini_file.empty()) return;
    std::filesystem::path file = g_ini_file.parent_path() / "Data" / "Game" / "fro" / "fro03.scw";
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        LOGR("rotk: sharp menus: %s not found, menu pictures left as they are", file.string().c_str());
        return;
    }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    // the table holding the menu pictures, and its pixels (10 MB or so into the stream)
    size_t table = 0, table_size = 0, need = 0, scanned = 0;
    auto want = [&](const std::vector<uint8_t>& s) {
        for (; !table && scanned + 0x58 <= s.size(); scanned++) {
            if (std::memcmp(&s[scanned], "TXHE", 4) != 0) continue;
            uint32_t size = U32(s, scanned + 4);
            if (size < 0x58 || size > 0x100000) continue;
            if (scanned + size > s.size()) return false;  // the rest of the table is still to come
            for (size_t r = scanned + 8; r + 0x50 <= scanned + size; r += 0x50)
                if (!std::memcmp(&s[r], "mainback", 8)) table = scanned, table_size = size;
        }
        if (!table) return false;
        if (!need)
            for (size_t r = table + 8; r + 0x50 <= table + table_size; r += 0x50)
                need = std::max<size_t>(need, table + table_size + 0x18 + U32(s, r + 8) + size_t(1024) * 1024 / 2);
        return s.size() >= need;
    };
    std::vector<uint8_t> s = Stream(d, want);
    if (!table || s.size() < need) {
        LOGR("rotk: sharp menus: the menu pictures were not found in %s", file.string().c_str());
        return;
    }
    for (const Known& k : kKnown) {
        for (size_t r = table + 8; r + 0x50 <= table + table_size; r += 0x50) {
            if (std::strncmp(reinterpret_cast<const char*>(&s[r]), k.name, 8) != 0) continue;
            uint32_t w = s[r + 0x38] | s[r + 0x39] << 8, h = s[r + 0x3A] | s[r + 0x3B] << 8, fmt = s[r + 0x3C];
            size_t at = table + table_size + 0x18 + U32(s, r + 8);
            if (w != 1024 || h != 1024 || fmt != 1 || at + size_t(w) * h / 2 > s.size()) continue;
            Picture& p = g_pictures.emplace_back();
            p.known = &k;
            Dxt1(&s[at], w, h, p.rgb);
            Thumb([&](float x, float y, float* c) { return At(p.rgb, 1024, 1024, k.ax * x + k.bx, k.ay * y + k.by, c); }, p.thumb.data());
            LOGR("rotk: sharp menus: ROTK PC's %s ready", k.name);
        }
    }
}

// a texture the game made: the Xbox menu picture drawn from ROTK PC's, true with it in out
bool ReplaceMenuArt(const gpu::pack::Contents& t, gpu::pack::Image& out) {
    if (t.format != 0x24 || t.width != kW || t.height != kH || t.size < size_t(kW) * 2 * kH || gpu::g_movie) return false;
    uint32_t pitch = uint32_t(t.size / kH);  // linear: its rows
    std::vector<uint8_t> xb = FromYuy2(t.data, kW, kH, pitch);
    std::array<float, kThumbW * kThumbH * 3> thumb;
    Thumb([&](float x, float y, float* c) { return At(xb, kW, kH, x, y, c); }, thumb.data());
    const Picture* best = nullptr;
    float best_err = 1e9f;
    for (const Picture& p : g_pictures) {
        float e = 0;
        int n = 0;
        for (size_t i = 0; i < thumb.size(); i++)
            if (p.thumb[i] >= 0) e += std::fabs(p.thumb[i] - thumb[i]), n++;
        e = n ? e / n : 1e9f;
        if (e < best_err) best_err = e, best = &p;
    }
    if (!best || best_err > 30.0f) {  // a picture the PC version does not have: the Xbox one scaled up
        // the renderer keeps what it is given by contents: a handful of menu pictures, never a stream of them
        static std::mutex lock;
        static std::vector<uint64_t> seen;
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < size_t(pitch) * kH; i++) h = (h ^ t.data[i]) * 1099511628211ull;
        [[maybe_unused]] size_t n;
        {
            std::lock_guard<std::mutex> g(lock);
            bool again = std::find(seen.begin(), seen.end(), h) != seen.end();
            if (!again && seen.size() >= 8) return false;
            if (!again) seen.push_back(h);
            n = seen.size();
            if (again) LOG_FIRST(4, "rotk: sharp menus: a scaled-up menu picture asked for again");
        }
        out = {};
        out.width = kW * kUpscale;
        out.height = kH * kUpscale;
        out.format = gpu::pack::Format::RGBA8;
        auto t0 = std::chrono::steady_clock::now();
        out.levels.push_back(Upscale(t.data, pitch));
#ifndef _WIN32
        if (const char* dir = std::getenv("ROTK_FONT_DUMP"))  // test build: the replacement as a PNG
            gpu::pack::WritePng(std::string(dir) + "/upscaled" + std::to_string(n) + ".png", out.width, out.height, out.levels[0]);
#endif
        gpu::pack::MakeLevels(out);
        LOGR("rotk: sharp menus: a menu picture ROTK PC does not have (closest %s, difference %.1f), scaled up in %d ms",
             best ? best->known->name : "-", best_err,
             int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()));
        return true;
    }
    const Known& k = *best->known;
    uint32_t W = kW * kScale, H = kH * kScale;
    out = {};
    out.width = W;
    out.height = H;
    out.format = gpu::pack::Format::RGBA8;
    std::vector<uint8_t>& px = out.levels.emplace_back(size_t(W) * H * 4);
    for (uint32_t v = 0; v < H; v++)
        for (uint32_t u = 0; u < W; u++) {
            float x = (u + 0.5f) / kScale - 0.5f, y = (v + 0.5f) / kScale - 0.5f, c[3], o[3];
            float pxx = k.ax * x + k.bx, pyy = k.ay * y + k.by;
            // the PC picture, blended into the Xbox one over its last 48 texels where the Xbox picture reaches further
            float edge = std::min({pxx, 1023.0f - pxx, pyy, 1023.0f - pyy}), w = std::clamp(edge / 48.0f, 0.0f, 1.0f);
            At(xb, kW, kH, std::clamp(x, 0.0f, kW - 1.0f), std::clamp(y, 0.0f, kH - 1.0f), o);
            if (w > 0 && At(best->rgb, 1024, 1024, pxx, pyy, c)) {
                for (int i = 0; i < 3; i++) c[i] = c[i] * w + o[i] * (1 - w);
            } else {
                std::memcpy(c, o, sizeof c);
            }
            uint8_t* to = &px[(size_t(v) * W + u) * 4];
            for (int i = 0; i < 3; i++) to[i] = uint8_t(std::clamp(c[i], 0.0f, 255.0f) + 0.5f);
            to[3] = 255;
        }
#ifndef _WIN32
    if (const char* dir = std::getenv("ROTK_FONT_DUMP"))  // test build: the replacement as a PNG
        gpu::pack::WritePng(std::string(dir) + "/" + k.name + ".png", W, H, px);
#endif
    gpu::pack::MakeLevels(out);
    LOGR("rotk: sharp menus: the %s picture drawn from ROTK PC's (difference %.1f)", k.name, best_err);
    return true;
}
}  // namespace rotk
