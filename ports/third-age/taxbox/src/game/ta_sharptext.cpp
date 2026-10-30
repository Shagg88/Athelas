// The Third Age: [graphics] sharp_text, the game's text drawn sharper at high resolutions, as the other three ports
// have it (same name).
//
// The game's fonts are "FNTS" chunks (the same layout as Return of the King's: +0x1C the offset of the atlas header,
// width u16 at +4, height u16 at +6, then 4-bit coverage pixels, low nibble first). 0x1C1860 makes each one into a P8
// texture of the atlas's power-of-two size, one level, the coverage levels as its indices, coloured by a 16-entry
// palette. Made for a 640x480 TV, it goes soft on a large screen.
//
// There is no PC release to borrow larger fonts from (ROTK's way), so this is The Two Towers' way: the atlas is
// redrawn at four times its size, its coverage enlarged smoothly (cubic, then a little blur that rounds off the
// original pixel grid) and cut along its 50% outline with an edge one pixel wide. The letters keep their exact shapes
// and the game's own glyph coordinates; their edges are crisp at any size. The texture's own colours are kept.
//
// The atlas is read where the game makes the font ([[hook]] at 0x1C1860, eax: the chunk, already unpacked in memory),
// so it works whatever the scene file's packing; the texture is then known by its contents (P8, one level, the
// atlas's size, the sum of its indices equal to the atlas's coverage sum) and replaced through the SDK's texture hook.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "gpu/texture_pack.h"
#include "kernel/kernel.h"

namespace ta {
bool g_sharp_text = false;  // [graphics] sharp_text
}

namespace {
constexpr int kScale = 4;

struct Atlas {
    uint32_t w, h, tex_w, tex_h;
    uint64_t sum;
    std::vector<float> cov;  // 0..1, tex_w * kScale x tex_h * kScale, ready
};
std::mutex g_lock;
std::vector<Atlas> g_atlases;

uint32_t Pow2(uint32_t v) {
    uint32_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

uint32_t Log2(uint32_t v) {
    uint32_t r = 0;
    while ((1u << r) < v) r++;
    return r;
}
// Morton order, as the renderer reads swizzled Xbox textures (The Two Towers' port's tt_sharptext.cpp)
void Masks(uint32_t w, uint32_t h, uint32_t& mx, uint32_t& my) {
    mx = my = 0;
    uint32_t bit = 1, lx = Log2(w), ly = Log2(h);
    for (uint32_t i = 0; i < lx || i < ly; i++) {
        if (i < lx) mx |= bit, bit <<= 1;
        if (i < ly) my |= bit, bit <<= 1;
    }
}
uint32_t Deposit(uint32_t v, uint32_t mask) {
    uint32_t r = 0;
    for (uint32_t bit = 1; mask; bit <<= 1) {
        uint32_t low = mask & (~mask + 1);
        if (v & bit) r |= low;
        mask &= mask - 1;
    }
    return r;
}

// cubic weights (a = -0.75, as The Two Towers' port)
float Cubic(float x) {
    const float a = -0.75f;
    x = std::fabs(x);
    if (x <= 1) return ((a + 2) * x - (a + 3)) * x * x + 1;
    if (x < 2) return ((a * x - 5 * a) * x + 8 * a) * x - 4 * a;
    return 0;
}
struct Taps {
    std::vector<int> idx;
    std::vector<float> w;
    int n = 0;
};
Taps CubicTaps(int src, int dst) {
    Taps t;
    t.n = 4;
    float s = float(src) / dst;
    for (int o = 0; o < dst; o++) {
        float f = (o + 0.5f) * s - 0.5f;
        int i0 = int(std::floor(f));
        for (int k = -1; k <= 2; k++) {
            t.idx.push_back(std::clamp(i0 + k, 0, src - 1));
            t.w.push_back(Cubic(f - float(i0 + k)));
        }
    }
    return t;
}
Taps GaussTaps(int size, float sigma) {
    Taps t;
    int r = int(std::ceil(sigma * 3));
    t.n = 2 * r + 1;
    std::vector<float> k(t.n);
    float sum = 0;
    for (int i = -r; i <= r; i++) sum += k[i + r] = std::exp(-0.5f * i * i / (sigma * sigma));
    for (float& v : k) v /= sum;
    for (int o = 0; o < size; o++)
        for (int i = -r; i <= r; i++) {
            int j = o + i;
            if (j < 0) j = -j - 1;
            if (j >= size) j = 2 * size - j - 1;
            t.idx.push_back(std::clamp(j, 0, size - 1));
            t.w.push_back(k[i + r]);
        }
    return t;
}
// rows [0, n) split over the processor's cores (job 110: a 4x text page took 120 ms on one, a stutter each time the
// game wrote new words)
template <class F>
void Rows(int n, F f) {
    int t = std::clamp(int(std::thread::hardware_concurrency()), 1, 16);
    std::vector<std::thread> pool;
    for (int i = 1; i < t; i++) pool.emplace_back([&, i] { for (int y = n * i / t; y < n * (i + 1) / t; y++) f(y); });
    for (int y = 0; y < n / t; y++) f(y);
    for (auto& th : pool) th.join();
}
std::vector<float> Separable(const std::vector<float>& in, int w, int h, const Taps& tx, int ow, const Taps& ty, int oh) {
    std::vector<float> mid(size_t(ow) * h), out(size_t(ow) * oh);
    Rows(h, [&](int y) {
        for (int x = 0; x < ow; x++) {
            float s = 0;
            for (int k = 0; k < tx.n; k++) s += tx.w[size_t(x) * tx.n + k] * in[size_t(y) * w + tx.idx[size_t(x) * tx.n + k]];
            mid[size_t(y) * ow + x] = s;
        }
    });
    Rows(oh, [&](int y) {
        for (int x = 0; x < ow; x++) {
            float s = 0;
            for (int k = 0; k < ty.n; k++) s += ty.w[size_t(y) * ty.n + k] * mid[size_t(ty.idx[size_t(y) * ty.n + k]) * ow + x];
            out[size_t(y) * ow + x] = s;
        }
    });
    return out;
}

// the atlas (coverage 0..15, tw x th, placed at the texture's top left) redrawn at kScale, cut along its 50% outline
std::vector<float> Sharpen(const std::vector<float>& a, int tw, int th) {
    int W = tw * kScale, H = th * kScale;
    std::vector<float> u = Separable(a, tw, th, CubicTaps(tw, W), W, CubicTaps(th, H), H);
    u = Separable(u, W, H, GaussTaps(W, kScale * 0.35f), W, GaussTaps(H, kScale * 0.35f), H);
    std::vector<float> out(size_t(W) * H);
    Rows(H, [&](int y) {
        for (int x = 0; x < W; x++) {
            size_t i = size_t(y) * W + x;
            float gx = (u[size_t(y) * W + std::min(x + 1, W - 1)] - u[size_t(y) * W + std::max(x - 1, 0)]) * 0.5f;
            float gy = (u[size_t(std::min(y + 1, H - 1)) * W + x] - u[size_t(std::max(y - 1, 0)) * W + x]) * 0.5f;
            float g = std::sqrt(gx * gx + gy * gy) + 1e-6f;
            out[i] = std::clamp((u[i] - 0.5f) / g + 0.5f, 0.0f, 1.0f);
        }
    });
    return out;
}

// a font texture known by its look alone (test 101: the font hook saw no fonts made): P8, one level, power-of-two
// sides, every index a coverage level (under 16), its palette clear at 0 and opaque at 15; the coverage read from
// the texture itself, unswizzled
bool FontLike(const gpu::pack::Contents& t, Atlas& a) {
    if (!t.palette || t.width < 64 || t.height < 32 || t.width > 1024 || t.height > 1024) return false;
    if ((t.width & (t.width - 1)) || (t.height & (t.height - 1))) return false;
    if ((t.palette[0] >> 24) > 0x20 || (t.palette[15] >> 24) < 0xE0) return false;
    size_t n = size_t(t.width) * t.height, lit = 0;
    for (size_t i = 0; i < n; i++) {
        if (t.data[i] >= 16) return false;
        lit += t.data[i] != 0;
    }
    if (lit < n / 50) return false;
    uint32_t mx, my;
    Masks(t.width, t.height, mx, my);
    std::vector<float> c(n);
    for (uint32_t y = 0; y < t.height; y++)
        for (uint32_t x = 0; x < t.width; x++) c[size_t(y) * t.width + x] = t.data[Deposit(x, mx) | Deposit(y, my)] / 15.0f;
    a = Atlas{t.width, t.height, t.width, t.height, 0, Sharpen(c, int(t.width), int(t.height))};
    return true;
}

// The menus' and HUD's words: the game writes them with the CPU into a linear alpha-only page (LIN_A8, 512 x 512 in
// the menus: the renderer hashes it whole, d3d12.cpp's font cache) and draws them from it. That page is redrawn at
// kScale the same way as a font atlas, white with the new coverage as its alpha (as the renderer reads A8). Made
// again whenever the page's words change; the time it takes is logged for the first few.
bool TextPage(const gpu::pack::Contents& t, gpu::pack::Image& out) {
    if (t.format != 0x1F || t.levels != 1 || t.width < 128 || t.height < 64 || t.width > 1024 || t.height > 1024)
        return false;
    uint32_t pitch = uint32_t(t.size / t.height);
    if (pitch < t.width) return false;
    auto t0 = std::chrono::steady_clock::now();
    std::vector<float> c(size_t(t.width) * t.height);
    size_t lit = 0;
    for (uint32_t y = 0; y < t.height; y++)
        for (uint32_t x = 0; x < t.width; x++) {
            uint8_t v = t.data[size_t(y) * pitch + x];
            c[size_t(y) * t.width + x] = v / 255.0f;
            lit += v != 0;
        }
    if (!lit) return false;  // an empty page: as it is
    std::vector<float> cov = Sharpen(c, int(t.width), int(t.height));
    uint32_t W = t.width * kScale, H = t.height * kScale;
    out = {};
    out.width = W;
    out.height = H;
    out.format = gpu::pack::Format::BGRA8;
    std::vector<uint8_t>& px = out.levels.emplace_back(size_t(W) * H * 4);
    for (size_t i = 0; i < size_t(W) * H; i++) {
        px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 255;
        px[i * 4 + 3] = uint8_t(std::clamp(cov[i] * 255.0f + 0.5f, 0.0f, 255.0f));
    }
    gpu::pack::MakeLevels(out);
    static std::atomic<int> logged{0};
    if (logged++ < 8)
        LOGR("ta: sharp text: a %ux%u text page redrawn at %ux%u in %.1f ms", t.width, t.height, W, H,
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    return true;
}

bool Replace(const gpu::pack::Contents& t, gpu::pack::Image& out) {
    if (t.data && TextPage(t, out)) return true;
    if (!t.data || t.format != 0x0B || t.levels != 1 || t.size < size_t(t.width) * t.height) return false;
    std::lock_guard lock(g_lock);
    Atlas found;
    if (FontLike(t, found)) {
        g_atlases.push_back(std::move(found));
        g_atlases.back().sum = 0;
        for (size_t i = 0; i < size_t(t.width) * t.height; i++) g_atlases.back().sum += t.data[i];
    }
    for (const Atlas& s : g_atlases) {
        if (s.tex_w != t.width || s.tex_h != t.height) continue;
        uint64_t sum = 0;
        for (size_t i = 0; i < size_t(t.width) * t.height; i++) sum += t.data[i];
        if (sum != s.sum) continue;
        uint32_t pal[16];
        for (uint32_t i = 0; i < 16; i++) pal[i] = t.palette ? t.palette[i] : (i * 17u) << 24 | 0xFFFFFFu;
        uint32_t W = t.width * kScale, H = t.height * kScale;
        out = {};
        out.width = W;
        out.height = H;
        out.format = gpu::pack::Format::BGRA8;
        std::vector<uint8_t>& px = out.levels.emplace_back(size_t(W) * H * 4);
        for (size_t i = 0; i < size_t(W) * H; i++) {
            float c = s.cov[i] * 15.0f;
            uint32_t lo = std::min(uint32_t(c), 15u), hi = std::min(lo + 1, 15u);
            float a = c - float(lo);
            for (int k = 0; k < 4; k++) {
                float v = float((pal[lo] >> (8 * k)) & 255) * (1 - a) + float((pal[hi] >> (8 * k)) & 255) * a;
                px[i * 4 + k] = uint8_t(std::clamp(v + 0.5f, 0.0f, 255.0f));
            }
        }
        gpu::pack::MakeLevels(out);
        LOGR("ta: sharp text: a %ux%u font redrawn at %ux%u", s.w, s.h, W, H);
        return true;
    }
    return false;
}
}  // namespace

// [[hook]] 0x1C1860: the game is about to make a font texture from the FNTS chunk at eax
void TA_FontMade(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (!ta::g_sharp_text) return;
    uint32_t c = ctx.eax;
    if (c < 0x10000) return;
    uint32_t off = ::kernel::R32(c + 0x1C);
    static std::atomic<int> seen{0};
    if (seen++ < 8)  // the chunk as the game hands it over (test 100 matched none with an "FNTS" check)
        LOGR("ta: sharp text: font chunk at %08X, tag %08X, %u glyphs, atlas at +%X", c, ::kernel::R32(c),
             ::kernel::R16(c + 0xA), off);
    if (off < 0x14 || off > 0x100000) return;
    uint32_t tex = c + off;
    uint32_t w = ::kernel::R16(tex + 4), h = ::kernel::R16(tex + 6);
    if (!w || !h || w > 2048 || h > 2048) return;
    Atlas s{w, h, Pow2(w), Pow2(h), 0, {}};
    {
        std::lock_guard lock(g_lock);
        for (const Atlas& a : g_atlases)
            if (a.w == w && a.h == h) {
                // the same font made again (another scene): kept if its coverage is the same
                uint64_t sum = 0;
                const uint8_t* p = static_cast<const uint8_t*>(::kernel::HostPtr(tex + 16));
                for (size_t k = 0; k < size_t(w) * h / 2; k++) sum += (p[k] & 15) + (p[k] >> 4);
                if (sum == a.sum) return;
            }
    }
    std::vector<float> a(size_t(s.tex_w) * s.tex_h, 0.0f);
    const uint8_t* p = static_cast<const uint8_t*>(::kernel::HostPtr(tex + 16));
    for (size_t k = 0; k < size_t(w) * h; k++) {
        uint8_t v = (k & 1) ? p[k / 2] >> 4 : p[k / 2] & 15;
        s.sum += v;
        a[(k / w) * s.tex_w + k % w] = v / 15.0f;
    }
    s.cov = Sharpen(a, int(s.tex_w), int(s.tex_h));
    LOGR("ta: sharp text: font atlas %ux%u read (texture %ux%u)", w, h, s.tex_w, s.tex_h);
    std::lock_guard lock(g_lock);
    g_atlases.push_back(std::move(s));
}

namespace ta {
// after the settings load (ta::Init)
void InitSharpText() {
    if (!g_sharp_text) return;
    gpu::pack::g_builtin_contents = Replace;
    // the title screen's "The Lord of the Rings: The Third Age" logo, a 256x256 picture drawn far larger (Shagg: the
    // title blurry with the text sharp; found by the texture dump of job 111). Redrawn at 4x from the player's own.
    gpu::pack::g_upscale = [](uint64_t h) { return h == 0x4b5f42409d384d12ull; };
    LOGR("ta: sharper text on (the game's fonts redrawn at %dx)", kScale);
}
}  // namespace ta
