// The Two Towers: [graphics] button_pictures, the controller buttons the game's hints, menus and pause screens show,
// as The Return of the King's port has it (same name, same choices).
//
// The game shows the Xbox controller's buttons as coloured discs with the letter, in two sets of textures: 64x64 in
// the front end and the Prologue's lessons (the disc 51 pixels across, at 5, 7), and 32x32 on the pause screens and
// in levels (the disc filling the texture). auto follows the controller on port 1 (the keyboard's keys when the
// keyboard plays there and no controller is plugged in), or always xbox, playstation, nintendo or keyboard. For
// PlayStation and Nintendo the port draws its own discs in their place (tools/mkprompts.py, original artwork, the
// same as ROTK's), for the keyboard a key cap with the key's name; through the SDK's built-in texture hook, found by
// the textures' pack hashes. A texture pack file of the same name still wins. The style is fixed the first time each
// picture loads.
//
// Seen so far (texture dumps of every level, the front end and the pause screens): A, B, X and Y at 32x32, and A, X
// and Y at 64x64; a picture not in the list keeps the game's own.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // std::min and std::max, not Windows' macros
#endif
#include <windows.h>
#pragma comment(lib, "gdi32")  // the key caps' text
#endif

#include "app/app.h"
#include "core/input.h"
#include "core/keys.h"
#include "core/log.h"
#include "gpu/texture_pack.h"
#include "tt_prompts_data.h"

namespace tt {
uint32_t g_button_pictures = 0;  // [graphics] button_pictures: 0 auto, 1 xbox, 2 playstation, 3 nintendo, 4 keyboard

namespace {
// the game's button textures, by pack hash, and where their disc is (a share of the texture: left, top, across)
struct Known {
    uint64_t hash;
    uint32_t size;
    const char* button;
    float x, y, d;
};
const Known kKnown[] = {
    {0xde68b21786d495c1ull, 32, "A", 0, 0, 1},  // green disc
    {0xce41150702d5dd62ull, 32, "B", 0, 0, 1},  // red
    {0xa1bf0dc4c267bbe0ull, 32, "X", 0, 0, 1},  // blue
    {0x548332f9b3df50b1ull, 64, "A", 5 / 64.0f, 7 / 64.0f, 51 / 64.0f},
    {0x73a802aade5d5690ull, 64, "X", 5 / 64.0f, 7 / 64.0f, 51 / 64.0f},
    {0x1ff51bff42dd7ff7ull, 64, "Y", 5 / 64.0f, 7 / 64.0f, 51 / 64.0f},
};
constexpr uint32_t kOut = 256;  // the pictures given: 256x256 (4x or 8x the game's)

// a picture made for a full square (the disc filling it) put where the game's disc is in its texture: scaled into
// that box with a box filter, transparent around it
void Place(const gpu::pack::Image& src, const Known& k, gpu::pack::Image& out) {
    out = gpu::pack::Image{};
    out.width = out.height = kOut;
    out.format = gpu::pack::Format::RGBA8;
    std::vector<uint8_t> px(kOut * kOut * 4, 0);
    const std::vector<uint8_t>& s = src.levels[0];
    float bx = k.x * kOut, by = k.y * kOut, bd = k.d * kOut, step = float(src.width) / bd;
    for (uint32_t y = 0; y < kOut; y++)
        for (uint32_t x = 0; x < kOut; x++) {
            float u0 = (x - bx) * step, v0 = (y - by) * step;
            if (u0 + step <= 0 || v0 + step <= 0 || u0 >= float(src.width) || v0 >= float(src.height)) continue;
            float acc[4] = {}, n = 0;
            int n_side = std::max(1, int(std::ceil(step)));
            for (int j = 0; j < n_side; j++)
                for (int i = 0; i < n_side; i++) {
                    int u = int(u0 + (i + 0.5f) * step / n_side), v = int(v0 + (j + 0.5f) * step / n_side);
                    if (u < 0 || v < 0 || u >= int(src.width) || v >= int(src.height)) {
                        n++;
                        continue;
                    }
                    const uint8_t* p = &s[(size_t(v) * src.width + u) * 4];
                    float a = p[3] / 255.0f;
                    for (int c = 0; c < 3; c++) acc[c] += p[c] * a;  // colour weighted by coverage, as it blends
                    acc[3] += a;
                    n++;
                }
            uint8_t* o = &px[(size_t(y) * kOut + x) * 4];
            for (int c = 0; c < 3; c++) o[c] = acc[3] > 0 ? uint8_t(std::clamp(acc[c] / acc[3], 0.0f, 255.0f)) : 0;
            o[3] = uint8_t(std::clamp(acc[3] / n * 255.0f, 0.0f, 255.0f));
        }
    out.levels.push_back(std::move(px));
}

#ifdef _WIN32
// the key a button's job is on, as a key cap filling the picture: a dark rounded square with a light edge and the
// key's short name ("Q", "Ctrl", "LMB"), Windows' Arial Bold through GDI
bool KeyCap(const char* button, gpu::pack::Image& out) {
    keys::Action a = keys::kCount;
    static const struct {
        const char* b;
        keys::Action a;
    } kMap[] = {{"A", keys::kA}, {"B", keys::kB}, {"X", keys::kX}, {"Y", keys::kY}};
    for (auto& m : kMap)
        if (!std::strcmp(m.b, button)) a = m.a;
    if (a == keys::kCount) return false;
    std::string key = keys::FirstKey(a);
    if (key.empty()) return false;
    const char* text = keys::ShortName(key.c_str());
    constexpr int N = kOut, D = N;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = N;
    bi.bmiHeader.biHeight = -N;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return false;
    }
    HGDIOBJ old_bmp = SelectObject(dc, bmp);
    std::memset(bits, 0, N * N * 4);
    // the largest bold font the name fits in, inside the cap's face
    int size = int(text[1] ? (text[2] ? D * 0.36 : D * 0.5) : D * 0.62);
    HFONT font = nullptr;
    SIZE ext{};
    for (; size > 10; size -= 2) {
        if (font) DeleteObject(font);
        font = CreateFontA(-size, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, "Arial");
        SelectObject(dc, font);
        GetTextExtentPoint32A(dc, text, int(std::strlen(text)), &ext);
        if (ext.cx <= D * 0.78) break;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    const float e = D * 0.94f, r = D * 0.2f;
    TextOutA(dc, int(e - ext.cx) / 2, int(e - ext.cy) / 2 - 1, text, int(std::strlen(text)));
    GdiFlush();
    const uint8_t* g = static_cast<const uint8_t*>(bits);  // the text's coverage, white on black
    out = gpu::pack::Image{};
    out.width = out.height = N;
    out.format = gpu::pack::Format::RGBA8;
    std::vector<uint8_t> px(N * N * 4, 0);
    // the cap: rounded square, anti-aliased from its edge distance; a shadow to the bottom right as the discs have
    auto inside = [&](float x, float y, float x0, float y0, float x1, float y1, float rr) {
        float dx = std::max({x0 + rr - x, 0.0f, x - (x1 - rr)}), dy = std::max({y0 + rr - y, 0.0f, y - (y1 - rr)});
        float d = std::sqrt(dx * dx + dy * dy) - rr;
        return std::clamp(0.5f - d, 0.0f, 1.0f);
    };
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            float shadow = inside(fx, fy, D * 0.05f, D * 0.05f, e + D * 0.05f, e + D * 0.05f, r);
            float body = inside(fx, fy, 0, 0, e, e, r);
            float face = inside(fx, fy, D * 0.04f, D * 0.04f, e - D * 0.04f, e - D * 0.04f, r * 0.8f);
            float t = g[(y * N + x) * 4 + 1] / 255.0f;  // the text, on the face
            float shade = 1.0f - 0.35f * fy / e;        // lighter towards the top
            float c[3], al = std::max(body, shadow * 0.8f);
            for (int k = 0; k < 3; k++) {
                float edge = 150.0f, inner = (k == 2 ? 46.0f : 38.0f) * shade + 10;
                float v = body * (face * inner + (1 - face) * edge);
                v = v * (1 - t * face) + 255.0f * t * face;
                c[k] = al > 0 ? v / std::max(al, 1e-3f) * (body > 0 ? 1.0f : 0.0f) : 0;
            }
            uint8_t* o = &px[(y * N + x) * 4];
            for (int k = 0; k < 3; k++) o[k] = uint8_t(std::clamp(c[k], 0.0f, 255.0f));
            o[3] = uint8_t(std::clamp(al * 255.0f, 0.0f, 255.0f));
        }
    out.levels.push_back(std::move(px));
    SelectObject(dc, old_bmp);
    if (font) DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);
    return true;
}
#else
bool KeyCap(const char*, gpu::pack::Image&) { return false; }
#endif

bool Builtin(uint64_t hash, uint32_t width, uint32_t height, gpu::pack::Image& out) {
    const Known* k = nullptr;
    for (const Known& c : kKnown)
        if (c.hash == hash && c.size == width && c.size == height) k = &c;
    if (!k) return false;
    uint32_t style = g_button_pictures;
    // auto: the controller on port 1; the keyboard's keys when it plays there and no controller is plugged in
    if (style == 0) {
        const app::Settings& st = app::Get();
        if (st.keyboard && st.keyboard_player == 1 && !input::Raw(0).connected) style = 4;
        else style = uint32_t(input::Style(0)) + 1;  // 1 xbox, 2 playstation, 3 nintendo
    }
    gpu::pack::Image full;
    if (style == 4) {
        if (!KeyCap(k->button, full)) return false;
        static std::atomic<uint32_t> logged{0};
        if (!logged++) LOGR("tt: keyboard key pictures in the hints");
    } else {
        const char* name = style == 2 ? "playstation" : style == 3 ? "nintendo" : nullptr;
        if (!name) return false;
        const PromptPicture* pic = nullptr;
        for (const PromptPicture& p : kPromptPictures)
            if (!std::strcmp(p.style, name) && !std::strcmp(p.button, k->button)) pic = &p;
        if (!pic || !gpu::pack::DecodePng(pic->png, pic->size, full) || full.levels.empty()) return false;
        static std::atomic<uint32_t> logged{0};
        uint32_t bit = 1u << (k - kKnown);
        if (!(logged.fetch_or(bit) & bit)) LOGR("tt: %s button picture for %s (%ux%u)", name, k->button, width, height);
    }
    Place(full, *k, out);
    return true;
}
}  // namespace

// after the settings load (tt::Init)
void InitPrompts() {
    if (g_button_pictures != 1) gpu::pack::g_builtin = Builtin;
}
}  // namespace tt
