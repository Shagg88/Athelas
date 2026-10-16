// ROTK Xbox: button pictures in the game's on-screen prompts ("A Speed", "Y Fierce").
//
// The Xbox game shows its controller's buttons as small textures (a coloured disc with the letter, top left of a
// 64x64 texture). [graphics] button_pictures (ROTKFix.ini [Gamepad] ButtonNames): auto follows the controller on
// port 1, or always xbox, playstation or nintendo. For PlayStation and Nintendo the port draws its own pictures in
// their place (tools/mkprompts.py, original artwork) through the SDK's built-in texture hook, found by the textures'
// pack hashes. A texture pack file of the same name still wins. "original" is the game's own, as xbox.
// The style is fixed the first time each picture loads.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>
#include <cstring>

#include "app/app.h"
#include "core/input.h"
#include "core/keys.h"
#include "core/log.h"
#include "gpu/texture_pack.h"
#include "rotk_prompts_data.h"

namespace rotk {
uint32_t g_button_pictures = 0;  // 0 auto, 1 xbox, 2 playstation, 3 nintendo, 4 keyboard

namespace {
// the game's prompt textures, by pack hash (64x64)
struct Known {
    uint64_t hash;
    const char* button;
};
const Known kKnown[] = {
    {0x0d3c98d63170de0dull, "A"},      // green disc
    {0x1893c32b5190d3fbull, "B"},      // red disc
    {0x2accbbd50631e250ull, "X"},      // blue disc
    {0x8fc49efe4a49539aull, "Y"},      // yellow disc
    {0x3ada26e2ce4fdef3ull, "WHITE"},  // white disc
    {0x5f36f0391f521310ull, "BLACK"},  // black disc
    {0x495a8fcbed4702f9ull, "LT"},     // grey disc with L
};

#ifdef _WIN32
// the key a button's job is on, as a key cap: a dark rounded square with a light edge and the key's short name
// ("Q", "Ctrl", "LMB"), drawn here (Windows' Arial Bold through GDI) at the size of the port's other pictures
// (128x128, the cap in the top left 43/64 where the Xbox disc is)
bool KeyCap(const char* button, gpu::pack::Image& out) {
    keys::Action a = keys::kCount;
    static const struct { const char* b; keys::Action a; } kMap[] = {
        {"A", keys::kA}, {"B", keys::kB}, {"X", keys::kX}, {"Y", keys::kY},
        {"WHITE", keys::kWhite}, {"BLACK", keys::kBlack}, {"LT", keys::kLeftTrigger}};
    for (auto& m : kMap)
        if (!std::strcmp(m.b, button)) a = m.a;
    if (a == keys::kCount) return false;
    std::string key = keys::FirstKey(a);
    if (key.empty()) return false;
    const char* text = keys::ShortName(key.c_str());
    constexpr int N = 128, D = N * 43 / 64;
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
    TextOutA(dc, (D - ext.cx) / 2, (D - ext.cy) / 2 - 1, text, int(std::strlen(text)));
    GdiFlush();
    const uint8_t* g = static_cast<const uint8_t*>(bits);  // the text's coverage, white on black
    out = gpu::pack::Image{};
    out.width = out.height = N;
    out.format = gpu::pack::Format::RGBA8;
    std::vector<uint8_t> px(N * N * 4, 0);
    // the cap: rounded square, anti-aliased from its edge distance; a shadow to the bottom right as the discs have
    auto inside = [&](float x, float y, float x0, float y0, float x1, float y1, float r) {
        float dx = std::max({x0 + r - x, 0.0f, x - (x1 - r)}), dy = std::max({y0 + r - y, 0.0f, y - (y1 - r)});
        float d = std::sqrt(dx * dx + dy * dy) - r;
        return std::clamp(0.5f - d, 0.0f, 1.0f);
    };
    const float e = D * 0.94f, r = D * 0.2f;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float fx = x + 0.5f, fy = y + 0.5f;
            float shadow = inside(fx, fy, D * 0.05f, D * 0.07f, e + D * 0.05f, e + D * 0.07f, r);
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
    if (width != 64 || height != 64) return false;
    const char* button = nullptr;
    for (const Known& k : kKnown)
        if (k.hash == hash) button = k.button;
    if (!button) return false;
    uint32_t style = g_button_pictures;
    // auto: the controller on port 1; the keyboard's keys when it plays there and no controller is plugged in
    if (style == 0) {
        const app::Settings& st = app::Get();
        if (st.keyboard && st.keyboard_player == 1 && !input::Raw(0).connected) style = 4;
        else style = uint32_t(input::Style(0)) + 1;  // 1 xbox, 2 playstation, 3 nintendo
    }
    if (style == 4) {
        if (!KeyCap(button, out)) return false;
        static std::atomic<uint32_t> logged{0};
        if (!logged++) LOGR("rotk: keyboard key pictures in the hints");
        return true;
    }
    const char* name = style == 2 ? "playstation" : style == 3 ? "nintendo" : nullptr;
    if (!name) return false;
    for (const PromptPicture& p : kPromptPictures) {
        if (std::strcmp(p.style, name) || std::strcmp(p.button, button)) continue;
        if (!gpu::pack::DecodePng(p.png, p.size, out)) return false;
        static std::atomic<uint32_t> logged{0};
        uint32_t bit = 1u << (&p - kPromptPictures);
        if (!(logged.fetch_or(bit) & bit)) LOGR("rotk: %s button picture for %s", name, button);
        return true;
    }
    return false;
}
}  // namespace

// after the settings load
void InitPrompts() {
    if (g_button_pictures != 1) gpu::pack::g_builtin = Builtin;
}
}  // namespace rotk
