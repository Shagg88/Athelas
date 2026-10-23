// FOTR Xbox: [graphics] button_pictures, the controller buttons the game's hints and menus show ("Press <A> to
// break containers", "(A) Select"), as the other two ports have it (same name, same choices).
//
// The game draws its text on the processor into textures of its own, a line at a time, and puts a button's picture in
// where the text has <A>, <B>, <LT> ... from D:\xboxmedia\xboxdings\xboxdings.xpr: eight 32x32 A8R8G8B8 pictures
// (linear, 4 KB each, one after another: A, B, X, Y, White, Black, LT, RT). Its line textures are made afresh for
// each text, so no texture hook can find the buttons in them; instead the eight pictures are written over in the
// game's memory once it has loaded the file, just before the first button goes into a text (some texts, such as the
// cutscene hints, are drawn once at the start and kept): found by their pixels, a page at a time (the file's contents
// are loaded page aligned).
//
// auto follows the controller on port 1 (the keyboard's keys when the keyboard plays there and no controller is
// plugged in), or always xbox, playstation, nintendo or keyboard; read when the pictures are written over (the start
// of the game). The PlayStation and Nintendo discs are the port's own (tools/mkprompts.py, original artwork, the
// same as the other ports'), made smaller; the keyboard's are key caps with the key's name.
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
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "fotr_prompts_data.h"

namespace fotr {
uint32_t g_button_pictures = 0;  // [graphics] button_pictures: 0 auto, 1 xbox, 2 playstation, 3 nintendo, 4 keyboard

namespace {
constexpr uint32_t kSide = 32, kBytes = kSide * kSide * 4, kCount = 8;
const char* const kButtons[kCount] = {"A", "B", "X", "Y", "WHITE", "BLACK", "LT", "RT"};
// FNV-1a of the A picture's 4 KB, as in the file, and one of its pixels (row 8, column 12) for a quick first look
constexpr uint64_t kFirst = 0x7178a4c411f9481bull;
constexpr uint32_t kPixelAt = (8 * kSide + 12) * 4, kPixel = 0xFFA6B988u;
std::atomic<bool> g_loaded{false}, g_done{false};

uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

#ifdef _WIN32
// the key a button's job is on, as a key cap filling the picture: a dark rounded square with a light edge and the
// key's short name ("Q", "Ctrl", "LMB"), Windows' Arial Bold through GDI
bool KeyCap(const char* button, gpu::pack::Image& out) {
    keys::Action a = keys::kCount;
    static const struct {
        const char* b;
        keys::Action a;
    } kMap[] = {{"A", keys::kA},         {"B", keys::kB},         {"X", keys::kX},
                {"Y", keys::kY},         {"WHITE", keys::kWhite}, {"BLACK", keys::kBlack},
                {"LT", keys::kLeftTrigger}, {"RT", keys::kRightTrigger}};
    for (auto& m : kMap)
        if (!std::strcmp(m.b, button)) a = m.a;
    if (a == keys::kCount) return false;
    std::string key = keys::FirstKey(a);
    if (key.empty()) return false;
    const char* text = keys::ShortName(key.c_str());
    constexpr int N = 128, D = N;
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

// a 128x128 RGBA picture as the game's 32x32 one: a box filter (colour weighted by coverage), stored B G R A
void Shrink(const gpu::pack::Image& src, uint8_t* out) {
    const std::vector<uint8_t>& s = src.levels[0];
    uint32_t f = src.width / kSide;
    for (uint32_t y = 0; y < kSide; y++)
        for (uint32_t x = 0; x < kSide; x++) {
            float acc[4] = {};
            for (uint32_t j = 0; j < f; j++)
                for (uint32_t i = 0; i < f; i++) {
                    const uint8_t* p = &s[((size_t(y) * f + j) * src.width + x * f + i) * 4];
                    float a = p[3] / 255.0f;
                    for (int c = 0; c < 3; c++) acc[c] += p[c] * a;
                    acc[3] += a;
                }
            uint8_t* o = out + (size_t(y) * kSide + x) * 4;
            for (int c = 0; c < 3; c++) o[2 - c] = acc[3] > 0 ? uint8_t(std::clamp(acc[c] / acc[3], 0.0f, 255.0f)) : 0;
            o[3] = uint8_t(std::clamp(acc[3] / float(f * f) * 255.0f, 0.0f, 255.0f));
        }
}

// the eight pictures in the chosen style (have: each one made; one not made stays the game's); false for the Xbox's
bool Pictures(uint8_t (*out)[kBytes], bool* have) {
    uint32_t style = g_button_pictures;
    if (style == 0) {
        const app::Settings& st = app::Get();
        if (st.keyboard && st.keyboard_player == 1 && !input::Raw(0).connected) style = 4;
        else style = uint32_t(input::Style(0)) + 1;  // 1 xbox, 2 playstation, 3 nintendo
    }
    if (style == 1) return false;
    const char* name = style == 2 ? "playstation" : style == 3 ? "nintendo" : "keyboard";
    for (uint32_t i = 0; i < kCount; i++) {
        gpu::pack::Image img;
        bool got = false;
        if (style == 4) {
            got = KeyCap(kButtons[i], img);
        } else {
            for (const PromptPicture& p : kPromptPictures)
                if (!std::strcmp(p.style, name) && !std::strcmp(p.button, kButtons[i]))
                    got = gpu::pack::DecodePng(p.png, p.size, img);
        }
        have[i] = got && !img.levels.empty() && img.width == img.height && img.width % kSide == 0;
        if (have[i]) Shrink(img, out[i]);
    }
    LOGR("fotr: %s button pictures in the hints and menus", name);
    return true;
}

// the A picture's page, found by its pixels: the eight follow it
uint32_t Find() {
    for (uint32_t base : {0x80000000u, 0x00010000u})
        for (uint32_t va = base; va < base + 0x04000000u; va += 0x1000)
            if (mem::IsCommitted(va) && ::kernel::R32(va + kPixelAt) == kPixel &&
                mem::IsCommitted(va + kBytes * kCount - 1) &&
                Fnv(static_cast<const uint8_t*>(::kernel::HostPtr(va)), kBytes) == kFirst)
                return va;
    return 0;
}
}  // namespace

void PromptsFile(const char* name) {
    if (std::strstr(name, "xboxdings.xpr") || std::strstr(name, "XBOXDINGS.XPR")) g_loaded = true;
}

// once the file is in memory, its pictures written over: at the first button the game draws into a text (0xD0680;
// the cutscene hints "(A) Skip" and "(X) Continue" are drawn once, 10 ms after the file is read, and kept), else at
// the next frame
void PromptsFrame() {
    if (!g_loaded || g_done) return;
    g_done = true;
    if (g_button_pictures == 1) return;
    uint32_t va = Find();
    if (!va) {
        LOGW("fotr: the game's button pictures weren't found in memory; they stay the Xbox ones");
        return;
    }
    static uint8_t pics[kCount][kBytes];
    bool have[kCount] = {};
    if (!Pictures(pics, have)) return;
    for (uint32_t i = 0; i < kCount; i++)
        if (have[i]) std::memcpy(::kernel::HostPtr(va + i * kBytes), pics[i], kBytes);
    LOGR("fotr: button pictures written over at %08X", va);
}
}  // namespace fotr

// 0xD0680: a button picture put into a text being drawn (ecx..: bundle, button index, x, y, ...)
X86_EXTERN_FUNC(__imp__sub_000D0680);
X86_FUNC(sub_000D0680) {
    fotr::PromptsFrame();
    __imp__sub_000D0680(ctx, base);
}
