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
// of the game). The other controllers' pictures are Kenney's Input Prompts (CC0; the ports' shared
// pictures, sdk/tools/mkprompts.py), made smaller; the keyboard's are key caps with the key's name.
#include <chrono>
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
#include "app/prompt_style.h"
#include "core/input.h"
#include "core/keys.h"
#include "core/log.h"
#include "gpu/texture_pack.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

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

// the key a button's job is on, as a key cap (app/prompt_style.cpp, shared by the ports)
bool KeyCap(const char* button, gpu::pack::Image& out) { return app::KeyCapPicture(button, 128, 128, 0.05f, out); }

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

// the eight pictures in the chosen style (have: each one made; one not made stays the game's: in the Xbox style all but
// White and Black)
bool Pictures(uint8_t (*out)[kBytes], bool* have) {
    uint32_t style = app::PromptStyle(g_button_pictures);  // auto resolved (app/prompt_style.cpp)
    // xbox: only White and Black are drawn (LB and RB, where today's controllers have them, #82); the rest stay the
    // game's own Xbox pictures
    const char* shared = app::PromptStyleName(style);  // playstation, nintendo, steamdeck, steamcontroller
    const char* name = style == 1 ? "xbox" : style == 4 ? "keyboard" : shared ? shared : "xbox";
    for (uint32_t i = 0; i < kCount; i++) {
        gpu::pack::Image img;
        bool got = false;
        if (style == 4) {
            got = KeyCap(kButtons[i], img);
        } else {
            // xbox: only the bumpers (the game's White and Black), the rest the game's own
            bool wanted = style != 1 || !std::strcmp(kButtons[i], "WHITE") || !std::strcmp(kButtons[i], "BLACK");
            gpu::pack::Image pic;
            if (const app::PromptPicture* p = wanted ? app::FindPromptPicture(name, kButtons[i]) : nullptr)
                got = gpu::pack::DecodePng(p->png, p->size, pic) && !pic.levels.empty();
            // the shared pictures are about 96 across: fitted to four times the game's, which Shrink then reduces
            if (got) app::PlacePicture(pic, 0, 0, 1, kSide * 4, img);
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
    // a miss is tried again at the next buttons (the file can be opened before its pictures are read in); only after
    // 10 seconds of misses do they stay the game's (#102: one early miss used to keep the Xbox pictures for the whole
    // session). Time, not a count: a menu draws many buttons a frame, which would use up a count at once (review)
    static auto first_miss = std::chrono::steady_clock::time_point{};
    uint32_t va = Find();
    if (!va) {
        auto now = std::chrono::steady_clock::now();
        if (first_miss == std::chrono::steady_clock::time_point{}) first_miss = now;
        if (now - first_miss >= std::chrono::seconds(10)) {
            g_done = true;
            LOGW("fotr: the game's button pictures weren't found in memory; they stay the Xbox ones");
        }
        return;
    }
    g_done = true;
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
