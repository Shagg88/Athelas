// The Two Towers: [graphics] sharp_text, the game's text drawn sharper at high resolutions, as the other two ports
// have it (same name). There is no PC version to borrow fonts from, as The Return of the King's port does, and the
// game doesn't draw its text from TrueType fonts, as Fellowship does: its letters are pictures, three fonts on four
// A8 textures (swizzled, 256x256 and 256x512: the small serif, the medium serif and two uncial ones), drawn as quads
// with the game's own texel coordinates. On a large screen they are enlarged 3x or more, so they blur.
//
// With sharp_text on, each of those four textures (found by a hash of its contents, so nothing else is touched) is
// given to the renderer redrawn at four times its size: the letters' coverage enlarged smoothly (cubic, then a
// little blur that rounds off the original pixel grid), then cut along its 50% outline with an edge one pixel wide
// (coverage minus one half, over its slope). The letters keep their exact shapes and spacing; their edges are crisp
// at any size. Made from the player's own game data as it loads, through the SDK's built-in texture hook.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/log.h"
#include "gpu/texture_pack.h"

namespace tt {
bool g_sharp_text = false;  // [graphics] sharp_text

namespace {
constexpr uint32_t kA8 = 0x19;  // Xbox D3DFMT_A8, swizzled
constexpr int kScale = 4;

// the four font textures: a hash of level 0 as the game stores it (FNV-1a 64), and their size
struct Font {
    uint64_t hash;
    uint32_t w, h;
    const char* what;
};
const Font kFonts[] = {
    {0xadf6ea7f941cdc9aull, 256, 256, "small serif"},
    {0x81a298fede2559bcull, 256, 512, "large uncial"},
    {0x3c80132170ffded3ull, 256, 512, "medium serif"},
    {0x9d30fcd549643144ull, 256, 512, "uncial (Orthanc)"},
};

uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

uint32_t Log2(uint32_t v) {
    uint32_t r = 0;
    while ((1u << r) < v) r++;
    return r;
}

// Morton order, as the renderer reads swizzled textures: bits of x and y interleave from the lowest, an axis drops
// out once its size is used up
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

// cubic weights (a = -0.75, as OpenCV's INTER_CUBIC, which the look was tuned with)
float Cubic(float x) {
    const float a = -0.75f;
    x = std::fabs(x);
    if (x <= 1) return ((a + 2) * x - (a + 3)) * x * x + 1;
    if (x < 2) return ((a * x - 5 * a) * x + 8 * a) * x - 4 * a;
    return 0;
}

// one axis of a separable pass: out[o] = sum w[o][k] * in[i[o][k]]
struct Taps {
    std::vector<int> idx;
    std::vector<float> w;
    int n = 0;
};
Taps CubicTaps(int src, int dst) {
    Taps t; t.n = 4;
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
    Taps t; int r = int(std::ceil(sigma * 3)); t.n = 2 * r + 1;
    std::vector<float> k(t.n);
    float sum = 0;
    for (int i = -r; i <= r; i++) sum += k[i + r] = std::exp(-0.5f * i * i / (sigma * sigma));
    for (float& v : k) v /= sum;
    for (int o = 0; o < size; o++)
        for (int i = -r; i <= r; i++) {
            // reflected at the borders (OpenCV's default)
            int j = o + i;
            if (j < 0) j = -j - 1;
            if (j >= size) j = 2 * size - j - 1;
            t.idx.push_back(std::clamp(j, 0, size - 1));
            t.w.push_back(k[i + r]);
        }
    return t;
}
// rows (x) then columns (y)
std::vector<float> Separable(const std::vector<float>& in, int w, int h, const Taps& tx, int ow, const Taps& ty, int oh) {
    std::vector<float> mid(size_t(ow) * h), out(size_t(ow) * oh);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < ow; x++) {
            float s = 0;
            for (int k = 0; k < tx.n; k++) s += tx.w[size_t(x) * tx.n + k] * in[size_t(y) * w + tx.idx[size_t(x) * tx.n + k]];
            mid[size_t(y) * ow + x] = s;
        }
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            float s = 0;
            for (int k = 0; k < ty.n; k++) s += ty.w[size_t(y) * ty.n + k] * mid[size_t(ty.idx[size_t(y) * ty.n + k]) * ow + x];
            out[size_t(y) * ow + x] = s;
        }
    return out;
}

bool Builtin(const gpu::pack::Contents& c, gpu::pack::Image& out) {
    if (c.format != kA8 || !c.data) return false;
    const Font* f = nullptr;
    for (const Font& k : kFonts)
        if (k.w == c.width && k.h == c.height && c.size >= size_t(k.w) * k.h && Fnv(c.data, size_t(k.w) * k.h) == k.hash) f = &k;
    if (!f) return false;
    int w = int(f->w), h = int(f->h), W = w * kScale, H = h * kScale;
    // the letters' coverage, unswizzled
    uint32_t mx, my;
    Masks(f->w, f->h, mx, my);
    std::vector<float> a(size_t(w) * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) a[size_t(y) * w + x] = c.data[Deposit(uint32_t(x), mx) | Deposit(uint32_t(y), my)] / 255.0f;
    // enlarged smoothly, the original pixel grid rounded off
    std::vector<float> u = Separable(a, w, h, CubicTaps(w, W), W, CubicTaps(h, H), H);
    u = Separable(u, W, H, GaussTaps(W, kScale * 0.35f), W, GaussTaps(H, kScale * 0.35f), H);
    // cut along the 50% outline, one pixel wide
    out = gpu::pack::Image{};
    out.width = uint32_t(W);
    out.height = uint32_t(H);
    out.format = gpu::pack::Format::BGRA8;
    std::vector<uint8_t> px(size_t(W) * H * 4, 255);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            size_t i = size_t(y) * W + x;
            float gx = (u[size_t(y) * W + std::min(x + 1, W - 1)] - u[size_t(y) * W + std::max(x - 1, 0)]) * 0.5f;
            float gy = (u[size_t(std::min(y + 1, H - 1)) * W + x] - u[size_t(std::max(y - 1, 0)) * W + x]) * 0.5f;
            float g = std::sqrt(gx * gx + gy * gy) + 1e-6f;
            float v = std::clamp((u[i] - 0.5f) / g + 0.5f, 0.0f, 1.0f);
            px[i * 4 + 3] = uint8_t(v * 255.0f + 0.5f);
        }
    out.levels.push_back(std::move(px));
    gpu::pack::MakeLevels(out);
    LOGR("tt: sharper text: the %s font (%ux%u) redrawn at %dx%d", f->what, f->w, f->h, W, H);
    return true;
}
}  // namespace

// after the settings load (tt::Init); the font textures are asked for as they load
void InitSharpText() {
    if (!g_sharp_text) return;
    gpu::pack::g_builtin_contents = Builtin;
    LOGR("tt: sharper text on (the game's fonts redrawn at %dx)", kScale);
}
}  // namespace tt
