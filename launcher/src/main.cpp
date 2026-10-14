// ROTK Fix Launcher
//
// The one place to start the game from: shows whether the game and ROTK Fix
// are set up, lets the player change every setting, writes ROTKFix.ini and
// the game's own ROTK.ini, then starts rotk.exe.
//
// Dear ImGui (MIT) on SDL3 (zlib). Fonts: Cinzel and EB Garamond (SIL OFL).
// Pictures: stb_image (public domain / MIT).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "config.h"
#include "library.h"
#include "port.h"
#include "ornament.h"
#include <shlobj.h>
#include "font_cinzel.h"
#include "font_garamond.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "../third_party/stb_image.h"
#pragma GCC diagnostic pop

#define LAUNCHER_VERSION "0.8 (test)"

// ---------------------------------------------------------------------------
// Theme

static ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }
// Dark fantasy: deep brown-black, parchment text and old gold.
static const ImVec4 C_TEXT   = rgb(234, 223, 200);
static const ImVec4 C_DIM    = rgb(168, 152, 124);
static const ImVec4 C_GOLD   = rgb(206, 166, 78);
static const ImVec4 C_GOLD_H = rgb(232, 196, 112);
static const ImVec4 C_GOOD   = rgb(150, 190, 110);
static const ImVec4 C_WARN   = rgb(222, 150, 80);
static const ImVec4 C_BAD    = rgb(220, 95, 80);
static const ImVec4 C_PANEL  = rgb(16, 12, 9, 0.85f);
static const ImVec4 C_LINE   = rgb(150, 118, 60, 0.6f);

static ImFont *F_BODY, *F_SMALL, *F_HEAD, *F_TITLE, *F_PLAY;
static float g_scale = 1.0f;
static float S(float v) { return v * g_scale; }

static void setup_style()
{
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 0; st.ChildRounding = 0; st.FrameRounding = S(2); st.PopupRounding = S(2);
    st.GrabRounding = S(2); st.TabRounding = 0; st.ScrollbarRounding = S(2);
    st.WindowBorderSize = 0; st.ChildBorderSize = 1; st.FrameBorderSize = 1; st.PopupBorderSize = 1;
    st.FramePadding = ImVec2(S(10), S(5)); st.ItemSpacing = ImVec2(S(10), S(8)); st.ItemInnerSpacing = ImVec2(S(8), S(6));
    st.WindowPadding = ImVec2(S(18), S(14)); st.GrabMinSize = S(14); st.ScrollbarSize = S(12);
    st.DisabledAlpha = 0.5f;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_Text] = C_TEXT;                    c[ImGuiCol_TextDisabled] = C_DIM;
    c[ImGuiCol_WindowBg] = rgb(0, 0, 0, 0);       c[ImGuiCol_ChildBg] = rgb(16, 12, 9, 0.80f);
    c[ImGuiCol_PopupBg] = rgb(24, 19, 14, 0.98f); c[ImGuiCol_Border] = rgb(150, 118, 60, 0.55f);
    c[ImGuiCol_FrameBg] = rgb(34, 27, 20);        c[ImGuiCol_FrameBgHovered] = rgb(48, 38, 27);
    c[ImGuiCol_FrameBgActive] = rgb(58, 46, 32);
    c[ImGuiCol_Button] = rgb(44, 35, 25);         c[ImGuiCol_ButtonHovered] = rgb(70, 55, 34);
    c[ImGuiCol_ButtonActive] = rgb(92, 72, 40);
    c[ImGuiCol_Header] = rgb(70, 55, 34);         c[ImGuiCol_HeaderHovered] = rgb(92, 72, 40);
    c[ImGuiCol_HeaderActive] = rgb(110, 86, 46);
    c[ImGuiCol_CheckMark] = C_GOLD_H;             c[ImGuiCol_SliderGrab] = C_GOLD; c[ImGuiCol_SliderGrabActive] = C_GOLD_H;
    c[ImGuiCol_Tab] = rgb(30, 24, 18);            c[ImGuiCol_TabHovered] = rgb(80, 62, 36);
    c[ImGuiCol_TabSelected] = rgb(64, 49, 29);    c[ImGuiCol_TabSelectedOverline] = C_GOLD;
    c[ImGuiCol_TabDimmed] = rgb(30, 24, 18);      c[ImGuiCol_TabDimmedSelected] = rgb(64, 49, 29);
    c[ImGuiCol_Separator] = rgb(150, 118, 60, 0.45f);
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0.2f); c[ImGuiCol_ScrollbarGrab] = rgb(90, 72, 44);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(120, 96, 56); c[ImGuiCol_ScrollbarGrabActive] = C_GOLD;
    c[ImGuiCol_TableRowBg] = rgb(0, 0, 0, 0);     c[ImGuiCol_TableRowBgAlt] = rgb(255, 230, 180, 0.04f);
    c[ImGuiCol_NavCursor] = C_GOLD_H;             c[ImGuiCol_TextSelectedBg] = rgb(110, 86, 46);
    c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.55f);
    c[ImGuiCol_CheckboxSelectedBg] = rgb(34, 27, 20);   c[ImGuiCol_InputTextCursor] = C_TEXT;
    c[ImGuiCol_TextLink] = C_GOLD_H;                    c[ImGuiCol_TabDimmedSelectedOverline] = C_GOLD;
    c[ImGuiCol_SeparatorHovered] = C_GOLD;              c[ImGuiCol_SeparatorActive] = C_GOLD_H;
    c[ImGuiCol_ResizeGrip] = rgb(0, 0, 0, 0);           c[ImGuiCol_ResizeGripHovered] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripActive] = rgb(0, 0, 0, 0);     c[ImGuiCol_DragDropTarget] = C_GOLD_H;
}

// ---------------------------------------------------------------------------
// Backgrounds: pictures in ROTKFix\backgrounds (the player's own screenshots
// can be added back later), shown one at a time with a slow cross-fade. Without any
// pictures, an original drawn landscape of misty mountains is used.

struct Picture { SDL_Texture *tex = nullptr; int w = 0, h = 0; };

struct BgJob;
struct Backgrounds {
    std::vector<std::string> files;
    int index = -1;
    Picture cur, prev;
    double changedAt = 0;
    BgJob *job = nullptr;          // picture being read in the background
};

static void list_pictures(const std::string &dir, std::vector<std::string> &out, size_t maxCount)
{
    std::vector<std::pair<FILETIME, std::string>> found;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string n = fd.cFileName, ext = n.size() > 4 ? n.substr(n.size() - 4) : "";
        for (auto &ch : ext) ch = (char)tolower((unsigned char)ch);
        if (ext == ".jpg" || ext == "jpeg" || ext == ".png") found.push_back({ fd.ftLastWriteTime, dir + "\\" + n });
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return CompareFileTime(&a.first, &b.first) > 0; });
    for (size_t i = 0; i < found.size() && i < maxCount; i++) out.push_back(found[i].second);
}

// Title, legal and loading screens are grey text on black and make poor
// backgrounds; skip screenshots that have almost no colour or light.
static bool looks_like_text_screen(const unsigned char *px, int w, int h)
{
    double chroma = 0, light = 0; int n = 0;
    for (int y = 0; y < h; y += 8)
        for (int x = 0; x < w; x += 8) {
            const unsigned char *c = px + ((size_t)y * w + x) * 4;
            int mx = std::max(c[0], std::max(c[1], c[2])), mn = std::min(c[0], std::min(c[1], c[2]));
            chroma += mx - mn; light += mx; n++;
        }
    // (dim, muted scenes such as the misty forests still count as pictures)
    return n == 0 || chroma / n < 2.5 || light / n < 14.0;
}

// Pictures are read and shrunk on a separate thread, so the launcher opens
// at once and never pauses when the background changes (screenshots can be
// 4K PNGs of 10 MB or more).
struct BgJob {
    std::vector<std::string> files;
    int start = 0, found = -1;
    unsigned char *px = nullptr;
    int w = 0, h = 0;
    SDL_AtomicInt done;
};

// Halve (2x2 average) until no wider than the launcher could ever show.
static void shrink_picture(unsigned char *&px, int &w, int &h)
{
    while (w > 1600 && h >= 2) {
        int nw = w / 2, nh = h / 2;
        unsigned char *o = (unsigned char *)malloc((size_t)nw * nh * 4);
        if (!o) return;
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                const unsigned char *a = px + ((size_t)(2 * y) * w + 2 * x) * 4, *b = a + (size_t)w * 4;
                unsigned char *d = o + ((size_t)y * nw + x) * 4;
                for (int c = 0; c < 4; c++) d[c] = (unsigned char)((a[c] + a[c + 4] + b[c] + b[c + 4] + 2) / 4);
            }
        stbi_image_free(px);
        px = o; w = nw; h = nh;   // freed with free(), same as stbi_image_free
    }
}

static int SDLCALL bg_worker(void *p)
{
    BgJob *j = (BgJob *)p;
    size_t n = j->files.size();
    for (size_t tries = 0; tries < n && !j->px; tries++) {
        int i = (int)((j->start + tries) % n);
        int w, h, c;
        unsigned char *px = stbi_load(j->files[i].c_str(), &w, &h, &c, 4);
        if (!px) continue;
        if (j->files[i].find("\\Screenshots\\") != std::string::npos && looks_like_text_screen(px, w, h)) { stbi_image_free(px); continue; }
        shrink_picture(px, w, h);
        j->px = px; j->w = w; j->h = h; j->found = i;
    }
    SDL_SetAtomicInt(&j->done, 1);
    return 0;
}

// Ask for the next picture (does nothing while one is still being read).
static void backgrounds_next(Backgrounds &b)
{
    if (b.files.empty() || b.job) return;
    BgJob *j = new BgJob();
    j->files = b.files;
    j->start = (b.index + 1) % (int)b.files.size();
    SDL_SetAtomicInt(&j->done, 0);
    SDL_Thread *t = SDL_CreateThread(bg_worker, "bg", j);
    if (!t) { delete j; return; }
    SDL_DetachThread(t);
    b.job = j;
}

// Called every frame: show a picture once it has been read.
static void backgrounds_poll(Backgrounds &b, SDL_Renderer *ren)
{
    BgJob *j = b.job;
    if (!j || !SDL_GetAtomicInt(&j->done)) return;
    b.job = nullptr;
    if (j->px) {
        SDL_Surface *s = SDL_CreateSurfaceFrom(j->w, j->h, SDL_PIXELFORMAT_RGBA32, j->px, j->w * 4);
        SDL_Texture *t = s ? SDL_CreateTextureFromSurface(ren, s) : nullptr;
        if (s) SDL_DestroySurface(s);
        if (t) {
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
            if (b.prev.tex) SDL_DestroyTexture(b.prev.tex);
            b.prev = b.cur;
            b.cur.tex = t; b.cur.w = j->w; b.cur.h = j->h;
            b.changedAt = ImGui::GetTime();
        }
        b.index = j->found;
        stbi_image_free(j->px);
    } else {
        b.files.clear();          // nothing usable: keep the drawn landscape
    }
    delete j;
}

// Draw a picture to cover the rect (cropping, never stretching).
static void draw_cover(ImDrawList *dl, const Picture &p, ImVec2 size, float alpha)
{
    if (!p.tex || alpha <= 0.0f) return;
    float ia = (float)p.w / p.h, sa = size.x / size.y;
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (ia > sa) { float f = sa / ia; uv0.x = (1 - f) * 0.5f; uv1.x = 1 - uv0.x; }
    else         { float f = ia / sa; uv0.y = (1 - f) * 0.35f; uv1.y = uv0.y + f; }
    dl->AddImage((ImTextureID)(intptr_t)p.tex, ImVec2(0, 0), size, uv0, uv1, IM_COL32(255, 255, 255, (int)(alpha * 255)));
}

// Original drawn backdrop: layered mountain ridges in mist under a pale moon.
static float ridge(float x, float seed)
{
    return sinf(x * 0.0071f + seed) * 0.5f + sinf(x * 0.0173f + seed * 2.1f) * 0.28f + sinf(x * 0.041f + seed * 3.7f) * 0.12f
         + fabsf(sinf(x * 0.0023f + seed * 0.7f)) * 0.6f;
}
static void draw_landscape(ImDrawList *dl, ImVec2 size)
{
    ImU32 top = IM_COL32(12, 28, 19, 255), mid = IM_COL32(40, 72, 50, 255);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, size.y * 0.62f), top, top, mid, mid);
    dl->AddRectFilled(ImVec2(0, size.y * 0.62f), size, IM_COL32(6, 13, 9, 255));
    ImVec2 moon(size.x * 0.74f, size.y * 0.2f);
    for (int i = 10; i > 0; i--) dl->AddCircleFilled(moon, S(26) + i * S(14), IM_COL32(200, 220, 190, 5));
    dl->AddCircleFilled(moon, S(26), IM_COL32(214, 226, 204, 190));
    struct L { float base, amp, seed; ImU32 col; } layers[] = {
        { 0.40f, 0.16f, 1.3f, IM_COL32(30, 50, 37, 255) },
        { 0.50f, 0.13f, 4.1f, IM_COL32(21, 38, 27, 255) },
        { 0.62f, 0.10f, 7.7f, IM_COL32(13, 27, 18, 255) },
        { 0.74f, 0.07f, 2.9f, IM_COL32(6, 15, 10, 255) } };
    float step = S(6);
    for (auto &l : layers) {
        for (float x = 0; x < size.x; x += step) {
            float y0 = size.y * (l.base - l.amp * ridge(x / g_scale, l.seed));
            float y1 = size.y * (l.base - l.amp * ridge((x + step) / g_scale, l.seed));
            dl->AddQuadFilled(ImVec2(x, y0), ImVec2(x + step + 1, y1), ImVec2(x + step + 1, size.y), ImVec2(x, size.y), l.col);
        }
        // pines along the nearer ridges
        if (l.base > 0.55f) {
            for (float x = 0; x < size.x; x += S(7)) {
                float r = fabsf(sinf(x / g_scale * 12.9898f + l.seed * 78.233f));
                float y = size.y * (l.base - l.amp * ridge(x / g_scale, l.seed)) + S(2);
                float h = S(10) + S(18) * r, w = h * 0.32f;
                dl->AddTriangleFilled(ImVec2(x - w, y), ImVec2(x, y - h), ImVec2(x + w, y), l.col);
            }
        }
        // mist above each ridge line
        float my = size.y * l.base;
        dl->AddRectFilledMultiColor(ImVec2(0, my - size.y * 0.06f), ImVec2(size.x, my + size.y * 0.02f),
                                    IM_COL32(140, 170, 145, 0), IM_COL32(140, 170, 145, 0), IM_COL32(140, 170, 145, 20), IM_COL32(140, 170, 145, 20));
    }
}

static void draw_background(ImDrawList *dl, ImVec2 size, Backgrounds &b)
{
    if (b.cur.tex) {
        float t = (float)(ImGui::GetTime() - b.changedAt), fade = std::min(1.0f, t / 1.5f);
        dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(6, 13, 9, 255));
        if (fade < 1.0f) {
            if (b.prev.tex) draw_cover(dl, b.prev, size, 1.0f);
            else draw_landscape(dl, size);      // the first picture fades in over the drawn backdrop
        }
        draw_cover(dl, b.cur, size, fade);
    } else {
        draw_landscape(dl, size);
    }
    // darken for legibility: lighter at the top (banner), darker behind the settings
    ImU32 a0 = IM_COL32(3, 9, 5, 50), a1 = IM_COL32(3, 9, 5, 130);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), size, a0, a0, a1, a1);
    orn_window_frame(dl, size);   // carved stone border (ornament.cpp)
}

// ---------------------------------------------------------------------------
// Widgets

static float g_labelW;
static const char *g_rowDesc;

// One setting: label on the left, control on the right; the description shows
// as a tooltip when the mouse is over the row.
static void row_begin(const char *label, const char *desc)
{
    g_rowDesc = desc;
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (desc && *desc) {
        ImGui::SameLine(0, S(6));
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(rgb(150, 118, 60), "(?)");
        ImGui::PopFont();
    }
    ImGui::SameLine(g_labelW);
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
}
static void row_end()
{
    ImGui::EndGroup();
    ImGui::EndGroup();
    if (g_rowDesc && *g_rowDesc && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled |
                                                         ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        ImGui::BeginTooltip();
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::PushTextWrapPos(S(380));
        ImGui::TextUnformatted(g_rowDesc);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndTooltip();
    }
}

// A heading: the title, then a tapering rule ending in a leaf scroll (ornament.cpp)
static void section(const char *title)
{
    ImGui::Spacing();
    float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
    ImGui::TextColored(C_GOLD, "%s", title);
    ImGui::PopFont();
    ImVec2 t0 = ImGui::GetItemRectMin(), t1 = ImGui::GetItemRectMax();
    orn_elf_rule(ImGui::GetWindowDrawList(), t1.x + S(16), right - S(4), (t0.y + t1.y) * 0.5f + S(1));
    ImGui::Dummy(ImVec2(0, S(2)));
}

// A choice between a few named options, shown as a drop-down menu.
// Options marked disabled (like "Custom") are shown when current but can't be picked.
static bool segmented(const char *id, int *v, const char *const *names, int n, const bool *enabled = nullptr)
{
    bool changed = false;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
    if (ImGui::BeginCombo("##c", (*v >= 0 && *v < n) ? names[*v] : "")) {
        for (int i = 0; i < n; i++) {
            if (enabled && !enabled[i]) continue;
            if (ImGui::Selectable(names[i], *v == i) && *v != i) { *v = i; changed = true; }
            if (*v == i) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

static bool gold_button(const char *label, ImVec2 size, ImFont *font)
{
    return orn_arch_button(label, size, font, true);
}

// ---------------------------------------------------------------------------
// App state

struct App {
    std::string dir;
    // the player's games (this folder's PC game first); cur = the one shown, -1 = none yet
    std::vector<LibGame> lib;
    int cur = -1;
    GameInfo other;                    // inspect_game() of a PC game in another folder, when that one is shown
    std::string otherFor;
    GameInfo game;
    Settings s;
    std::vector<Monitor> monitors;
    std::string controller;
    int controllers = 0;
    SDL_GamepadType controllerType = SDL_GAMEPAD_TYPE_UNKNOWN;
    std::vector<SDL_Gamepad *> pads;   // opened so their button presses arrive as events
    int capture = -1;                  // move waiting for "press a button", or -1
    int padPlayer = 0;                 // whose buttons the Controls tab shows (0 = player 1)
    bool trigDown[2] = { false, false };   // trigger edge detection while mapping
    // first-run help when the launcher is not in the game folder
    bool searched = false;
    std::vector<FoundGame> found;
    std::string setupErr, deniedDir;
    double captureAt = 0;
    bool captureTT = false;            // the move waiting is on The Two Towers' Controls tab (its [controls] buttons)
    std::string message;
    bool messageBad = false;
    double messageAt = 0;
    bool quit = false;
    Backgrounds bg;
    SDL_Window *win = nullptr;
    // The Two Towers (Xbox) port
    Port tt;
    bool ttLooked = false;             // looked for the port program and asked it for its settings
    PortSettingsFile ttFile;
    bool ttLoaded = false;
    // The Return of the King (Xbox) port: same settings page as the PC version (ROTKFix.ini)
    Port rx;
    bool rxLooked = false;
    HANDLE running = NULL;             // a port that is playing; the launcher waits, hidden
    std::string runningLog, runningGame;
};

static const Monitor &mon(const App &a) { return a.monitors[std::min<size_t>(a.s.monitor, a.monitors.size() - 1)]; }

static void effective_res(const App &a, int &w, int &h)
{
    if (a.s.width > 0 && a.s.height > 0) { w = a.s.width; h = a.s.height; }
    else { w = mon(a).deskW; h = mon(a).deskH; }
}

static std::vector<int> refresh_list(const App &a)
{
    const Monitor &m = mon(a);
    if (a.s.displayMode == 1) {
        int w, h; effective_res(a, w, h);
        const Mode *md = m.find(w, h);
        if (md && !md->hz.empty()) return md->hz;
    }
    return { m.deskHz };
}
static int max_refresh(const App &a) { return refresh_list(a).back(); }
static int effective_refresh(const App &a)
{
    return a.s.displayMode == 1 ? pick_refresh(refresh_list(a), a.s.targetFps) : mon(a).deskHz;
}

static void refresh_controllers(App &a)
{
    for (SDL_Gamepad *g : a.pads) SDL_CloseGamepad(g);
    a.pads.clear();
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    a.controllers = n;
    a.controller = n > 0 && SDL_GetGamepadNameForID(ids[0]) ? SDL_GetGamepadNameForID(ids[0]) : "";
    a.controllerType = n > 0 ? SDL_GetGamepadTypeForID(ids[0]) : SDL_GAMEPAD_TYPE_UNKNOWN;
    for (int i = 0; i < n; i++) if (SDL_Gamepad *g = SDL_OpenGamepad(ids[i])) a.pads.push_back(g);
    SDL_free(ids);
}

static bool g_removeSelf;      // uninstalled: delete the launcher when it closes
static bool g_xboxPage;        // the Return of the King page is showing the Xbox version (PC-only options greyed out)

// A note under a section whose options are the PC game's own and don't apply on Xbox
static void pc_only_note()
{
    if (!g_xboxPage) return;
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::TextColored(C_DIM, "PC version only.");
    ImGui::PopFont();
}

static void set_message(App &a, const std::string &m, bool bad) { a.message = m; a.messageBad = bad; a.messageAt = ImGui::GetTime(); }

static bool save(App &a)
{
    std::string err;
    a.s.refresh = a.s.displayMode == 1 ? pick_refresh(refresh_list(a), a.s.targetFps) : 0;
    {
        const Monitor &m = mon(a);
        std::string d = m.device;
        if (d.rfind("\\\\.\\", 0) == 0) d = d.substr(4);   // \\.\DISPLAY2 -> DISPLAY2
        a.s.screen = m.primary ? "main" : d;
    }
    if (!(g_xboxPage ? save_rotkfix_ini(a.dir, a.s, err) : save_settings(a.dir, a.s, err))) { set_message(a, err, true); return false; }
    a.game = inspect_game(a.dir);
    set_message(a, "Settings saved", false);
    return true;
}

// ---------------------------------------------------------------------------
// Tabs

static void tab_display(App &a)
{
    Settings &s = a.s;
    section("Screen");
    if (a.monitors.size() > 1) {
        row_begin("Monitor", "Which screen the game runs on.");
        if (ImGui::BeginCombo("##mon", mon(a).name.c_str())) {
            for (int i = 0; i < (int)a.monitors.size(); i++) {
                char b[256];
                snprintf(b, sizeof b, "%d: %s%s  (%dx%d @ %d Hz)", i + 1, a.monitors[i].name.c_str(),
                         a.monitors[i].primary ? ", main" : "", a.monitors[i].deskW, a.monitors[i].deskH, a.monitors[i].deskHz);
                if (ImGui::Selectable(b, s.monitor == i)) { s.monitor = i; s.width = s.height = 0; }
            }
            ImGui::EndCombo();
        }
        row_end();
    }
    static const char *modes[] = { "Borderless", "Fullscreen", "Windowed" };
    row_begin("Display mode", "Borderless: fills the screen and switches instantly with Alt-Tab (recommended).\n"
                              "Fullscreen: the screen switches to the chosen resolution and refresh rate.\n"
                              "Windowed: a normal window you can move around.");
    if (segmented("mode", &s.displayMode, modes, 3)) {
        if (s.displayMode != 1 && (s.width > mon(a).deskW || s.height > mon(a).deskH))
            s.width = s.height = 0;     // supersampling resolutions are for fullscreen only
        if (s.displayMode != 2 && s.width > 0 && !mon(a).find(s.width, s.height))
            s.width = s.height = 0;     // free window sizes are for windowed only
    }
    row_end();

    const Monitor &m = mon(a);
    row_begin("Resolution", "Only resolutions your monitor supports are listed.\n"
                            "In fullscreen, sizes marked \"supersampling\" are larger than your screen: the game is drawn at that size "
                            "and your graphics driver shrinks it down for a cleaner, sharper picture. They appear when NVIDIA DSR/DLDSR "
                            "or AMD VSR is turned on in the driver's control panel.");
    char cur[64];
    if (s.width > 0) snprintf(cur, sizeof cur, "%d x %d", s.width, s.height);
    else snprintf(cur, sizeof cur, "%d x %d  (screen)", m.deskW, m.deskH);
    if (ImGui::BeginCombo("##res", cur, ImGuiComboFlags_HeightLarge)) {
        char b[64];
        snprintf(b, sizeof b, "%d x %d  (screen)", m.deskW, m.deskH);
        if (ImGui::Selectable(b, s.width == 0)) { s.width = s.height = 0; }
        for (int i = (int)m.modes.size() - 1; i >= 0; i--) {
            const Mode &md = m.modes[i];
            bool bigger = md.w > m.deskW || md.h > m.deskH;
            if ((bigger && s.displayMode != 1) || (md.w == m.deskW && md.h == m.deskH)) continue;
            const char *tag = md.h * 16 == md.w * 9 ? "  16:9" : md.h * 16 == md.w * 10 ? "  16:10" : md.h * 4 == md.w * 3 ? "  4:3" : "";
            snprintf(b, sizeof b, "%d x %d%s%s", md.w, md.h, tag, bigger ? "  (supersampling)" : "");
            if (ImGui::Selectable(b, s.width == md.w && s.height == md.h)) { s.width = md.w; s.height = md.h; }
        }
        if (s.displayMode == 2) {
            // a window can be any size that fits the screen, including shapes the monitor doesn't have
            static const struct { int w, h; const char *tag; } extra[] = {
                { 3440, 1440, "  21:9 ultrawide" }, { 2560, 1080, "  21:9 ultrawide" }, { 2560, 1440, "  16:9" },
                { 1920, 1080, "  16:9" }, { 1600, 900, "  16:9" }, { 1280, 720, "  16:9" }, { 1440, 1080, "  4:3" } };
            ImGui::SeparatorText("Window sizes");
            for (auto &e : extra) {
                if (e.w > m.deskW || e.h > m.deskH || (e.w == m.deskW && e.h == m.deskH) || m.find(e.w, e.h)) continue;
                snprintf(b, sizeof b, "%d x %d%s", e.w, e.h, e.tag);
                if (ImGui::Selectable(b, s.width == e.w && s.height == e.h)) { s.width = e.w; s.height = e.h; }
            }
        }
        ImGui::EndCombo();
    }
    row_end();

    section("Smoothness");
    row_begin("V-Sync", "Prevents tearing. The frame rate can't go above the screen's refresh rate.");
    ImGui::Checkbox("##vsync", &s.vsync);
    row_end();

    int maxHz = max_refresh(a);
    row_begin("Frame rate", g_xboxPage
        ? "The Xbox game was made for 30. Higher rates play at normal speed.\n"
          "In fullscreen the screen switches to the matching refresh rate. In borderless and windowed, "
          "rates that divide evenly into your screen's refresh rate are the smoothest."
        : "The game was made for 60. Higher rates were tested and play at normal speed.\n"
          "In fullscreen the screen switches to the matching refresh rate. In borderless and windowed, "
          "rates that divide evenly into your screen's refresh rate are the smoothest.");
    static const int ratesPc[] = { 60, 90, 120, 144, 165, 180, 240 };
    static const int ratesXbox[] = { 30, 60, 90, 120, 144, 165, 180, 240 };
    const int original = g_xboxPage ? 30 : 60;
    std::vector<int> rates = g_xboxPage ? std::vector<int>(std::begin(ratesXbox), std::end(ratesXbox))
                                        : std::vector<int>(std::begin(ratesPc), std::end(ratesPc));
    char c[64];
    snprintf(c, sizeof c, "%d fps%s", s.targetFps, s.targetFps == original ? "  (original)" : "");
    if (ImGui::BeginCombo("##fps", c)) {
        for (int r : rates) {
            if (s.vsync && r > maxHz) continue;
            snprintf(c, sizeof c, "%d fps%s", r, r == original ? "  (original)" : "");
            if (ImGui::Selectable(c, s.targetFps == r)) s.targetFps = r;
        }
        if (std::find(rates.begin(), rates.end(), maxHz) == rates.end() && maxHz > 60) {
            snprintf(c, sizeof c, "%d fps", maxHz);
            if (ImGui::Selectable(c, s.targetFps == maxHz)) s.targetFps = maxHz;
        }
        ImGui::EndCombo();
    }
    row_end();
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::SetCursorPosX(g_labelW);
    ImGui::PushTextWrapPos(0);
    if (s.vsync && s.targetFps > maxHz)
        ImGui::TextColored(C_WARN, "Above this screen's %d Hz - the game will run at %d fps.", maxHz, maxHz);
    else if (s.displayMode == 1)
        ImGui::TextColored(C_DIM, "Screen will run at %d Hz.", effective_refresh(a));
    else if (maxHz % s.targetFps)
        ImGui::TextColored(C_DIM, "Your screen runs at %d Hz; %d fps won't divide evenly into it, so motion may be slightly uneven.", maxHz, s.targetFps);
    else
        ImGui::TextColored(C_DIM, "Your screen runs at %d Hz - %d fps fits it evenly.", maxHz, s.targetFps);
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
}

static float hfov_deg(const App &a, int pct)
{
    int w, h; effective_res(a, w, h);
    float aspect = a.s.widescreenFix ? std::max((float)w / (float)h, 4.0f / 3.0f) : 4.0f / 3.0f;   // narrower screens keep the 4:3 width
    float v = 46.8f * pct / 100.0f * 3.14159265f / 180.0f;
    return 2.0f * atanf(tanf(v * 0.5f) * aspect) * 180.0f / 3.14159265f;
}

static void tab_graphics(App &a)
{
    Settings &s = a.s;
    bool vk = s.renderer == 0;
    section("Quality");
    static const char *pnames[] = { "Original", "Balanced", "High", "Ultra", "Custom" };
    int p = detect_preset(s);
    row_begin("Preset", "Original: as it looked in 2003, no anti-aliasing or filtering.\n"
                        "Balanced: light anti-aliasing and filtering, for older graphics cards.\n"
                        "High: 4x anti-aliasing, smooth leaf edges, sharp textures.\n"
                        "Ultra: 8x anti-aliasing and stronger sharpening. Heavy: at 1440p and above it can\n"
                        "stutter in foggy scenes above about 120 fps - use High for 144-240 fps.\n"
                        "Custom: shown when you change individual settings.");
    const bool en[5] = { true, true, true, true, false };
    int sel = p;
    if (segmented("preset", &sel, pnames, 5, en)) apply_preset(s, sel);
    row_end();

    section("Image");
    static const char *aa[] = { "Off", "2x", "4x", "8x" };
    int aai = s.msaa == 2 ? 1 : s.msaa == 4 ? 2 : s.msaa == 8 ? 3 : 0;
    row_begin("Anti-aliasing", "Smooths jagged edges (MSAA). 8x together with smooth leaf edges is heavy:\n"
                               "at high frame rates 4x looks almost the same and stays smooth.");
    if (segmented("aa", &aai, aa, 4)) s.msaa = aai == 0 ? 0 : 1 << aai;
    row_end();
    row_begin("Smooth leaf edges", "Also smooths cut-out textures like leaves, grass and fences. Needs anti-aliasing.");
    ImGui::BeginDisabled(s.msaa == 0 || !vk);
    ImGui::Checkbox("##edges", &s.alphaEdges);
    ImGui::EndDisabled();
    row_end();
    static const char *af[] = { "Off", "2x", "4x", "8x", "16x" };
    int afi = s.aniso >= 16 ? 4 : s.aniso >= 8 ? 3 : s.aniso >= 4 ? 2 : s.aniso >= 2 ? 1 : 0;
    row_begin("Texture filtering", "Anisotropic filtering: keeps ground and walls sharp at a distance.");
    ImGui::BeginDisabled(!vk);
    if (segmented("af", &afi, af, 5)) s.aniso = afi == 0 ? 0 : 1 << afi;
    ImGui::EndDisabled();
    row_end();
    int ts = (int)lroundf(-s.lodBias * 100.0f);
    row_begin("Texture sharpness", "Shows textures at higher detail sooner. Too high can shimmer.");
    ImGui::BeginDisabled(!vk);
    if (ImGui::SliderInt("##lod", &ts, 0, 100, ts == 0 ? "Game default" : "%d%%")) s.lodBias = -ts / 100.0f;
    ImGui::EndDisabled();
    row_end();
    row_begin("Sharpening", "Crisper picture. 0 turns it off. While playing, [ switches it on and off to compare.\n"
                            "The HUD and menu pictures are sharpened to match your resolution automatically.");
    ImGui::BeginDisabled(!vk);
    ImGui::SliderInt("##sharp", &s.sharpening, 0, 100, s.sharpening == 0 ? "Off" : "%d");
    ImGui::EndDisabled();
    row_end();
    static const char *looks[] = { "Original", "Film", "Vivid", "Dark fantasy" };
    row_begin("Colour look", "Original: the game's own colours.\n"
                             "Film: richer contrast, cool shadows and warm highlights.\n"
                             "Vivid: stronger colour.\n"
                             "Dark fantasy: cooler and moodier, with less colour.\n"
                             "While playing, ] switches between them.");
    ImGui::BeginDisabled(!vk);
    segmented("look", &s.colourLook, looks, 4);
    ImGui::EndDisabled();
    row_end();
    if (s.colourLook != 0) {
        row_begin("Look strength", "How strongly the colour look is applied.");
        ImGui::BeginDisabled(!vk);
        ImGui::SliderInt("##lookpct", &s.lookStrength, 10, 100, "%d%%");
        ImGui::EndDisabled();
        row_end();
    }
    row_begin("Brightness", "Added on top of the game's own Gamma option, which now also works in borderless.");
    ImGui::BeginDisabled(!vk);
    ImGui::SliderFloat("##bright", &s.brightness, 0.5f, 2.0f, s.brightness == 1.0f ? "Game's own" : "%.2f");
    ImGui::EndDisabled();
    row_end();

    section("View");
    row_begin("Widescreen correction", "Shows more at the sides instead of stretching the 4:3 picture.");
    ImGui::Checkbox("##ws", &s.widescreenFix);
    row_end();
    if (g_xboxPage && s.widescreenFix) {   // the Xbox port's own option; the PC version keeps the HUD in the middle 16:9
        row_begin("HUD at the screen edges", "With widescreen correction on: health bars and other HUD pieces sit at the screen's edges "
                                             "instead of in the middle 4:3 area.");
        ImGui::Checkbox("##hudedges", &s.hudEdges);
        row_end();
    }
    char fov[48];
    snprintf(fov, sizeof fov, s.fovPct == 100 ? "%.0f%s  (game's own)" : "%.0f%s", hfov_deg(a, s.fovPct), "\xC2\xB0");
    row_begin("Field of view", "How wide the camera sees, in horizontal degrees. Wider shows more of the battle but makes characters look further away.");
    ImGui::SliderInt("##fov", &s.fovPct, 70, 150, fov);
    row_end();

    section("Game detail");
    pc_only_note();
    ImGui::BeginDisabled(g_xboxPage);   // the PC game's own ROTK.ini options
    static const char *sh[] = { "Off", "Low", "High" };
    row_begin("Shadows", "The game's own shadow setting. Low uses simple round shadows; High uses full character shadows.");
    segmented("sh", &s.shadows, sh, 3);
    row_end();
    static const char *cd[] = { "Low", "High" };
    int cdi = s.charDetailHigh;
    row_begin("Texture detail", "The game's own texture setting. Low uses smaller textures, for very old graphics cards.");
    if (segmented("cd", &cdi, cd, 2)) s.charDetailHigh = cdi != 0;
    row_end();
    static const char *fx[] = { "Low", "Medium", "High" };
    row_begin("Effects", "The game's own special effects setting (fire, magic, sparks).");
    segmented("fx", &s.sfx, fx, 3);
    row_end();
    row_begin("Alternate cameras", "The game's own camera option: uses its alternative camera angles during play.");
    ImGui::Checkbox("##altcam", &s.altCameras);
    row_end();
    ImGui::EndDisabled();

    section("Texture packs");
    row_begin("Use texture packs", "Shows sharper or remade pictures from texture packs. A pack goes in its own folder inside "
                                   "ROTKFix\\textures\\replace; remove the folder to go back to the original pictures.");
    ImGui::Checkbox("##texrepl", &s.texReplace);
    row_end();
    row_begin("Save game textures", "For making texture packs: every picture the game uses is saved as a PNG in "
                                    "ROTKFix\\textures\\dump as you play. Levels load a little slower the first time. "
                                    "Turn it off when you have what you need.");
    ImGui::Checkbox("##texdump", &s.texDump);
    row_end();
    row_begin("Textures folder", "Opens ROTKFix\\textures, where the replace and dump folders are.");
    if (ImGui::Button("Open folder")) {
        std::string t = a.dir + "\\ROTKFix\\textures";
        CreateDirectoryA(t.c_str(), NULL);
        CreateDirectoryA((t + "\\replace").c_str(), NULL);
        ShellExecuteA(NULL, "open", t.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    row_end();

    section("Movies");
    static const char *md[] = { "Fit", "Stretch", "4:3" };
    row_begin("Movie display", "Fit: cutscenes and the opening logos fill the width with correct proportions; the trailer is shown 4:3.\n"
                               "Stretch: the whole movie frame is stretched to the screen (thick bars, wider picture).\n"
                               "4:3: every movie in its original frame with bars at the sides.");
    segmented("md", &s.movieDisplay, md, 3);
    row_end();
    row_begin("Sharper cutscenes", "The movies are only 512 x 480. This sharpens them at their own pixel size before they are stretched to your screen, "
                                   "so faces and edges look crisper. 0 shows them as the game always did.");
    ImGui::SliderInt("##mvsharp", &s.sharperMovies, 0, 100, s.sharperMovies == 0 ? "Off" : "%d");
    row_end();
    row_begin("Skip any video", "Every movie can be skipped with Start, including cutscenes you have not seen yet.");
    ImGui::Checkbox("##skipall", &s.skipAllVideos);
    row_end();
    ImGui::BeginDisabled(g_xboxPage);   // on Xbox the logos are the start of the first movie: Skip any video covers them
    row_begin("Skip opening logos", g_xboxPage ? "PC version only. On Xbox the logos are the start of the Helm's Deep intro movie: "
                                                 "turn on \"Skip any video\" and press Start to skip it."
                                               : "Go straight to the menus at startup.");
    ImGui::Checkbox("##logos", &s.skipLogos);
    row_end();
    ImGui::EndDisabled();
    row_begin("Cutscene sound delay", "Bluetooth speakers and headphones play sound late. This holds the picture back to match (150-300 ms is typical). 0 = off.");
    ImGui::SliderInt("##adelay", &s.audioDelayMs, 0, 500, s.audioDelayMs == 0 ? "Off" : "%d ms");
    row_end();
    ImGui::BeginDisabled(g_xboxPage);
    row_begin("Subtitles", g_xboxPage ? "PC version only: on Xbox, subtitles are in the game's own options." : nullptr);
    ImGui::Checkbox("##subs", &s.subtitles);
    row_end();
    ImGui::EndDisabled();
}

enum { NAMES_AUTO, NAMES_XBOX, NAMES_PS, NAMES_NINTENDO, NAMES_ORIGINAL };

// Which brand's names to show for buttons in the launcher
static int label_style(const App &a)
{
    int st = a.s.buttonNames;
    if (st == NAMES_AUTO) {
        switch (a.controllerType) {
        case SDL_GAMEPAD_TYPE_PS3: case SDL_GAMEPAD_TYPE_PS4: case SDL_GAMEPAD_TYPE_PS5: return NAMES_PS;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return NAMES_NINTENDO;
        default: return NAMES_XBOX;
        }
    }
    return st == NAMES_ORIGINAL ? NAMES_XBOX : st;
}

static const char *phys_label(int ph, int style)
{
    static const char *const xbox[PAD_PHYS_COUNT] = { "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "Left stick", "Right stick", "LT", "RT" };
    static const char *const ps[PAD_PHYS_COUNT] = { "Cross", "Circle", "Square", "Triangle", "L1", "R1", "Share", "Options", "L3", "R3", "L2", "R2" };
    static const char *const nin[PAD_PHYS_COUNT] = { "B", "A", "Y", "X", "L", "R", "Minus", "Plus", "Left stick", "Right stick", "ZL", "ZR" };
    return style == NAMES_PS ? ps[ph] : style == NAMES_NINTENDO ? nin[ph] : xbox[ph];
}

// Give a move a new button; the move that had that button gets this move's old one.
static void assign_button(Settings &s, int pl, int action, int ph)
{
    for (int b = 0; b < PAD_ACTION_COUNT; b++)
        if (b != action && s.padMap[pl][b] == ph) s.padMap[pl][b] = s.padMap[pl][action];
    s.padMap[pl][action] = ph;
}

static int  tt_button_count(App &a);
static void tt_assign_button(App &a, int idx, int ph);

// Called for every SDL event while a move waits for a button press. After a
// press the next move waits, like the mapping screens of Dolphin and RPCS3.
static void capture_event(App &a, const SDL_Event &e)
{
    if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.value < 8000) {
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) a.trigDown[0] = false;
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) a.trigDown[1] = false;
    }
    if (a.capture < 0) return;
    int ph = -1;
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: ph = PH_A; break;        case SDL_GAMEPAD_BUTTON_EAST: ph = PH_B; break;
        case SDL_GAMEPAD_BUTTON_WEST: ph = PH_X; break;         case SDL_GAMEPAD_BUTTON_NORTH: ph = PH_Y; break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: ph = PH_LB; break; case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: ph = PH_RB; break;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: ph = PH_LS; break;  case SDL_GAMEPAD_BUTTON_RIGHT_STICK: ph = PH_RS; break;
        case SDL_GAMEPAD_BUTTON_START: case SDL_GAMEPAD_BUTTON_BACK: a.capture = -1; return;   // stop
        default: return;
        }
    } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.value > 20000) {
        int k = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 0 : e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? 1 : -1;
        if (k < 0 || a.trigDown[k]) return;
        a.trigDown[k] = true;
        ph = k ? PH_RT : PH_LT;
    } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) { a.capture = -1; return; }
    else return;
    // ignore the press that started it (A on a controller clicking the slot)
    if (ImGui::GetTime() - a.captureAt < 0.25) return;
    int count = PAD_ACTION_COUNT;
    if (a.captureTT) { count = tt_button_count(a); tt_assign_button(a, a.capture, ph); }
    else assign_button(a.s, a.padPlayer, a.capture, ph);
    a.capture = a.capture + 1 < count ? a.capture + 1 : -1;
    a.captureAt = ImGui::GetTime();
}

static void tab_controls(App &a)
{
    Settings &s = a.s;
    section("Controller");
    // on Xbox the controller is the game's own input, so this switch is the PC version's only
    bool padOn = s.padEnabled || g_xboxPage;
    row_begin("Use a controller", g_xboxPage ? "Always on for the Xbox version: the controller is the game's own input."
                                             : "Xbox, PlayStation, Switch and most other controllers work, wired or wireless.");
    ImGui::BeginDisabled(g_xboxPage);
    if (g_xboxPage) { bool on = true; ImGui::Checkbox("##pad", &on); } else ImGui::Checkbox("##pad", &s.padEnabled);
    ImGui::EndDisabled();
    if (padOn) {
        ImGui::SameLine(0, S(16));
        ImGui::AlignTextToFramePadding();
        if (a.controllers > 1) ImGui::TextColored(C_GOOD, "%d controllers connected", a.controllers);
        else if (a.controllers) ImGui::TextColored(C_GOOD, "Connected");
        else ImGui::TextColored(C_WARN, "None connected");
    }
    row_end();
    ImGui::BeginDisabled(!padOn);
    section("Buttons");
    if (!s.player2) a.padPlayer = 0;
    // whose layout
    for (int pl = 0; pl < 2; pl++) {
        if (pl) ImGui::SameLine(0, S(6));
        bool on = a.padPlayer == pl;
        ImGui::BeginDisabled(pl == 1 && !s.player2);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? rgb(92, 72, 40) : rgb(44, 35, 25));
        if (ImGui::Button(pl ? "Player 2" : "Player 1", ImVec2(S(110), 0)) && !on) { a.padPlayer = pl; a.capture = -1; }
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
    }
    int pl = a.padPlayer;
    static const char *moves[PAD_ACTION_COUNT] = { "Speed attack", "Physical attack", "Parry", "Fierce attack", "Jump back",
                                                   "Action", "Special ability", "Ranged attack", "Killing move" };
    static const char *tips[PAD_ACTION_COUNT] = {
        "Also selects in menus and fires when aiming a ranged attack.", "Also goes back in menus.", nullptr,
        "Also shows level info in menus.", nullptr, "Opens doors, pulls levers and picks things up.",
        "Can also be done by pressing Ranged attack and Killing move together.", "Hold to aim; press Speed attack to fire.", nullptr };
    int style = label_style(a);
    if (a.captureTT) a.capture = -1;
    a.captureTT = false;
    if (a.capture >= 0 && (ImGui::GetTime() - a.captureAt > 8.0 || !a.controllers)) a.capture = -1;

    // left: the moves; right: the controller
    float slotW = S(170);
    ImGui::BeginGroup();
    for (int m = 0; m < PAD_ACTION_COUNT; m++) {
        ImGui::PushID(m);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(moves[m]);
        if (tips[m] && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tips[m]);
        ImGui::SameLine(g_labelW);
        bool waiting = a.capture == m;
        char lab[64];
        snprintf(lab, sizeof lab, "%s##slot", waiting ? "Press a button..." : phys_label(s.padMap[pl][m], style));
        if (waiting) {
            float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 6.0);
            ImGui::PushStyleColor(ImGuiCol_Button, rgb(110 + (int)(40 * pulse), 82 + (int)(30 * pulse), 36));
            ImGui::PushStyleColor(ImGuiCol_Border, C_GOLD_H);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, rgb(34, 27, 20));
            ImGui::PushStyleColor(ImGuiCol_Border, C_LINE);
        }
        if (ImGui::Button(lab, ImVec2(slotW, 0))) {
            if (waiting) a.capture = -1;
            else if (a.controllers) { a.capture = m; a.captureAt = ImGui::GetTime(); }
            else ImGui::OpenPopup("pick");
        }
        ImGui::PopStyleColor(2);
        if (ImGui::BeginPopup("pick")) {
            static const int choices[] = { PH_A, PH_B, PH_X, PH_Y, PH_LB, PH_RB, PH_LT, PH_RT, PH_LS, PH_RS };
            for (int ph : choices)
                if (ImGui::Selectable(phys_label(ph, style), s.padMap[pl][m] == ph)) assign_button(s, pl, m, ph);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::PushTextWrapPos(0);
    if (a.capture >= 0)
        ImGui::TextColored(C_GOLD_H, "Press the button for \"%s\". Start, Back or Esc stops.", moves[a.capture]);
    else if (a.controllers)
        ImGui::TextColored(C_DIM, "Click a move, then press the button you want on the controller. It moves on to the next move by itself.");
    else
        ImGui::TextColored(C_DIM, "Connect a controller to set buttons by pressing them, or click a move to pick from a list.");
    ImGui::PopTextWrapPos();
    ImGui::PopFont();

    ImGui::Spacing();
    ImGui::BeginDisabled(!a.controllers);
    if (ImGui::Button("Set all, one by one")) { a.capture = 0; a.captureAt = ImGui::GetTime(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool isDefault = true;
    for (int m = 0; m < PAD_ACTION_COUNT; m++) if (s.padMap[pl][m] != k_padActionDefault[m]) isDefault = false;
    ImGui::BeginDisabled(isDefault);
    if (ImGui::Button("Original layout")) { for (int m = 0; m < PAD_ACTION_COUNT; m++) s.padMap[pl][m] = k_padActionDefault[m]; a.capture = -1; }
    ImGui::EndDisabled();
    if (pl == 1) {
        ImGui::SameLine();
        bool same = !memcmp(s.padMap[0], s.padMap[1], sizeof s.padMap[0]);
        ImGui::BeginDisabled(same);
        if (ImGui::Button("Same as player 1")) { memcpy(s.padMap[1], s.padMap[0], sizeof s.padMap[0]); a.capture = -1; }
        ImGui::EndDisabled();
    }
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::TextColored(C_DIM, "%s pauses and skips movies, %s is Select. The D-pad and left stick move through menus.",
                       phys_label(PH_START, style), phys_label(PH_BACK, style));
    ImGui::PopFont();
    section("Controller options");
    static const char *p2[] = { "Keyboard and mouse", "Second controller" };
    row_begin("Player 2 (co-op)", "Who controls the second player in co-op.\n"
                                  "With two controllers, the first one connected is player 1 and the next is player 2.");
    segmented("p2", &s.player2, p2, 2);
    row_end();
    static const char *bn[] = { "Automatic", "Xbox", "PlayStation", "Nintendo", "Original (\"Button 7\")" };
    row_begin("Button names", g_xboxPage ? "Which controller's buttons the game's hints and tutorials show, as pictures.\n"
                                           "Automatic follows the controller you play with. \"Original\" keeps the Xbox pictures."
                                         : "Which controller's buttons the game's hints and tutorials show, as pictures.\n"
                                           "Automatic follows the controller you play with. \"Original\" keeps the game's own text.");
    segmented("names", &s.buttonNames, bn, 5);
    row_end();
    row_begin("Vibration", "Controller rumble from the original Xbox version, switched back on. 0 turns it off.");
    ImGui::SliderInt("##rumble", &s.vibration, 0, 100, s.vibration == 0 ? "Off" : "%d%%");
    row_end();
    row_begin("Stick deadzone", "How far a stick must move before it counts.");
    ImGui::SliderInt("##dz", &s.deadzone, 0, 40, "%d%%");
    row_end();
    row_begin("Trigger sensitivity", "How far the triggers must be pulled to count as pressed.");
    ImGui::SliderInt("##tt", &s.triggerThreshold, 5, 90, "%d%%");
    row_end();

    ImGui::EndDisabled();
}

static void tab_audio(App &a)
{
    Settings &s = a.s;
    section("Sound");
    static const char *s3[] = { "Off (game's original)", "Speakers", "Headphones (3D surround)" };
    pc_only_note();
    ImGui::BeginDisabled(g_xboxPage);   // DirectSound/EAX on the PC only
    row_begin("3D sound and reverb", "Brings back the game's 3D sound and EAX reverb (echoes in caves and halls), "
                                     "which Windows stopped supporting after XP. Uses DSOAL and OpenAL Soft.\n"
                                     "Headphones adds virtual surround (HRTF): you can hear sounds above, behind and around you.");
    segmented("s3d", &s.sound3d, s3, 3);
    row_end();
    ImGui::EndDisabled();
    static int vcrt = -1;
    if (vcrt < 0) {
        // the game is 32-bit: look in SysWOW64 on 64-bit Windows, System32 on 32-bit Windows
        char sys[MAX_PATH] = "";
        if (!GetSystemWow64DirectoryA(sys, MAX_PATH)) GetSystemDirectoryA(sys, MAX_PATH);
        vcrt = GetFileAttributesA((std::string(sys) + "\\msvcp140.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    if (s.sound3d && !vcrt && !g_xboxPage) {
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::SetCursorPosX(g_labelW);
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_WARN, "Needs the Microsoft Visual C++ Redistributable (x86) from Microsoft's website. Without it the game uses its original sound.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
    section("Volume");
    pc_only_note();
    ImGui::BeginDisabled(g_xboxPage);   // the PC game's ROTK.ini volumes
    row_begin("Music", nullptr);  ImGui::SliderInt("##vm", &s.volMusic, 0, 100, "%d%%"); row_end();
    row_begin("Voices", nullptr); ImGui::SliderInt("##vv", &s.volVoice, 0, 100, "%d%%"); row_end();
    row_begin("Effects", nullptr); ImGui::SliderInt("##vs", &s.volSfx, 0, 100, "%d%%"); row_end();
    ImGui::EndDisabled();
}

static void status_dot(bool ok, bool warn, const char *label, const char *tip)
{
    ImVec4 col = ok ? C_GOOD : warn ? C_WARN : C_BAD;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImGui::BeginGroup();
    dl->AddCircleFilled(ImVec2(p.x + S(5), p.y + h * 0.55f), S(4), ImGui::GetColorU32(col));
    ImGui::SetCursorScreenPos(ImVec2(p.x + S(15), p.y));
    ImGui::TextUnformatted(label);
    ImGui::EndGroup();
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(S(380));
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// ---------------------------------------------------------------------------
// The player's games

// Is the game shown the PC game in this launcher's own folder (the normal case)?
static bool own_game(const App &a)
{
    return a.cur < 0 || (a.lib[a.cur].kind == GK_ROTK_PC && !_stricmp(a.lib[a.cur].path.c_str(), a.dir.c_str()));
}

static void add_game_from(App &a, const std::string &path)
{
    LibGame g;
    if (detect_game_at(path, g) == GK_NONE) {
        set_message(a, g.iso ? "That disc image isn't The Two Towers or The Return of the King for Xbox."
                             : "No supported game there: choose the folder with rotk.exe, or an extracted Xbox game (with default.xbe).", true);
        return;
    }
    a.cur = library_add(a.lib, g);
    library_save(exe_dir(), a.lib);
    set_message(a, std::string("Added: ") + game_kind_name(g.kind), false);
}

// "Your games" (About tab): the list, adding and removing
static void games_section(App &a)
{
    section("Your games");
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "The launcher can keep track of every version of the games you have, wherever they are.");
    ImGui::PopTextWrapPos();
    int remove = -1;
    for (size_t i = 0; i < a.lib.size(); i++) {
        const LibGame &g = a.lib[i];
        ImGui::PushID((int)i);
        ImGui::Spacing();
        ImGui::TextColored((int)i == a.cur ? C_GOLD_H : C_TEXT, "%s", game_kind_name(g.kind));
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::TextColored(C_DIM, "%s%s", g.path.c_str(), g.iso ? "  (disc image)" : "");
        ImGui::PopFont();
        bool isOwn = g.kind == GK_ROTK_PC && !_stricmp(g.path.c_str(), a.dir.c_str());
        if ((int)i != a.cur && ImGui::SmallButton("Show")) a.cur = (int)i;
        if (!isOwn) {
            if ((int)i != a.cur) ImGui::SameLine();
            if (ImGui::SmallButton("Remove from list")) remove = (int)i;
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        a.lib.erase(a.lib.begin() + remove);
        if (a.cur == remove) a.cur = a.lib.empty() ? -1 : 0;
        else if (a.cur > remove) a.cur--;
        library_save(exe_dir(), a.lib);
    }
    ImGui::Spacing();
    if (ImGui::Button("Add a game folder...")) {
        std::string d;
        if (pick_folder(d, L"Choose a game folder (with rotk.exe, or an extracted Xbox game)")) add_game_from(a, d);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add an Xbox disc image...")) {
        std::string f;
        if (pick_disc_image(f)) add_game_from(a, f);
    }

    section("Background pictures");
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "Pictures you put in this folder (PNG or JPG) are shown behind the launcher, changing every few seconds. "
                              "They're your own: none come with the download.");
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Open backgrounds folder")) {
        std::string d = exe_dir() + "\\ROTKFix\\backgrounds";
        SHCreateDirectoryExA(NULL, d.c_str(), NULL);
        ShellExecuteA(NULL, "open", d.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("Show new pictures")) {
        a.bg.files.clear();
        list_pictures(exe_dir() + "\\ROTKFix\\backgrounds", a.bg.files, 100);
        backgrounds_next(a.bg);
    }
}

static void rx_about(App &a);
static void tab_about(App &a)
{
    if (g_xboxPage) { rx_about(a); return; }
    games_section(a);
    section("Setup");
    const GameInfo &g = a.game;
    status_dot(g.exeFound && g.versionOk, g.exeFound, g.exeFound ? g.versionText.c_str() : "rotk.exe not found - put the launcher in your game folder", nullptr);
    status_dot(g.hasDll && g.hasProxyD3D8 && g.hasProxyDinput, false, "ROTK Fix files", nullptr);
    status_dot(g.hasDxvk, false, "Vulkan renderer (DXVK)", nullptr);
    status_dot(g.hasSdl, false, "Controller support (SDL3)", nullptr);
    status_dot(g.rotkIniExists, true, g.rotkIniExists ? "Game settings file found" : "Game settings file will be created", g.rotkIniPath.c_str());
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::TextColored(C_DIM, "Game folder: %s", a.dir.c_str());
    ImGui::PopFont();
    if (ImGui::Button("Open game folder")) ShellExecuteA(NULL, "open", a.dir.c_str(), NULL, NULL, SW_SHOWNORMAL);

    if (g.discProtected) {
        section("Original disc version");
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("This is the game as installed from the disc. Its SafeDisc copy protection relies on an old Windows "
                               "driver that Windows 10 and 11 block, so on its own it will not start.");
        ImGui::TextUnformatted("1. Install SafeDiscShim, a free tool that stands in for that driver (the copy protection still "
                               "checks for your disc).");
        ImGui::TextUnformatted("2. Put the game disc in the drive.");
        ImGui::TextUnformatted("3. Press Play and allow the Windows prompt - the disc version asks to run as administrator.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Get SafeDiscShim (GitHub)"))
            ShellExecuteA(NULL, "open", "https://github.com/RibShark/SafeDiscShim/releases", NULL, NULL, SW_SHOWNORMAL);
    }

    section("ROTK Fix");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("An unofficial, free fan project that lets owners of The Lord of the Rings: The Return of the King (PC, 2003) "
                           "play their own copy on modern PCs with modern display, graphics and controller support.");
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::TextColored(C_DIM, "Not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate. "
                              "No game files are included; you need your own copy of the game. Background paintings are in the public domain.");
    ImGui::PopFont();
    section("Open-source components");
    static const char *lic[][2] = {
        { "DXVK", "Vulkan renderer - zlib licence" },
        { "SDL3", "Controllers and this window - zlib licence" },
        { "Dear ImGui", "Launcher interface - MIT licence" },
        { "stb_image, stb_image_write", "Pictures and screenshots - public domain / MIT" },
        { "DSOAL, OpenAL Soft", "3D sound and EAX reverb - LGPL" },
        { "Cinzel, EB Garamond", "Fonts - SIL Open Font License" },
        { "Liberation Sans", "Button pictures - SIL Open Font License" } };
    for (auto &l : lic) { ImGui::TextColored(C_GOLD, "%s", l[0]); ImGui::SameLine(S(230)); ImGui::TextUnformatted(l[1]); }
    ImGui::PopTextWrapPos();
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::TextColored(C_DIM, "Launcher version " LAUNCHER_VERSION);
    ImGui::PopFont();

    section("Remove ROTK Fix");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("Takes ROTK Fix out of the game folder and puts the game's own controller and settings files back. "
                           "Your saved games and screenshots are kept.");
    ImGui::PopTextWrapPos();
    ImGui::PushStyleColor(ImGuiCol_Button, rgb(90, 36, 28));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(120, 46, 34));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(230, 186, 170));
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(250, 228, 214));
    if (ImGui::Button("Remove ROTK Fix...")) ImGui::OpenPopup("Remove ROTK Fix?");
    ImGui::PopStyleColor(4);

    ImGui::SetNextWindowSize(ImVec2(S(520), 0));
    if (ImGui::BeginPopupModal("Remove ROTK Fix?", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        static std::vector<std::string> done;
        static std::string err;
        static bool finished;
        ImGui::PushTextWrapPos(0);
        if (!finished) {
            ImGui::TextUnformatted("This removes ROTK Fix, its settings and this launcher from the game folder, "
                                   "and restores the game's Input.ini and your ROTK.ini. "
                                   "The game will run as it did before ROTK Fix. Saved games and screenshots are kept.");
            if (!err.empty()) ImGui::TextColored(C_BAD, "%s", err.c_str());
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, rgb(90, 36, 28));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(120, 46, 34));
            ImGui::PushStyleColor(ImGuiCol_Text, rgb(250, 228, 214));
            if (ImGui::Button("Remove", ImVec2(S(150), 0))) {
                done.clear(); err.clear();
                if (uninstall(a.dir, done, err)) { finished = true; g_removeSelf = true; }
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(S(150), 0))) { err.clear(); ImGui::CloseCurrentPopup(); }
        } else {
            ImGui::TextColored(C_GOOD, "ROTK Fix has been removed.");
            ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
            for (auto &d : done) ImGui::BulletText("%s", d.c_str());
            ImGui::PopFont();
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(S(150), 0))) a.quit = true;
        }
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
}


// Shown instead of the settings when the launcher is not next to rotk.exe
static void setup_here(App &a, const std::string &dst)
{
    bool denied = false;
    a.setupErr.clear(); a.deniedDir.clear();
    if (install_to(a.dir, dst, a.setupErr, denied)) {
        if (start_launcher(dst, false, "")) a.quit = true;
        else a.setupErr = "ROTK Fix was copied to " + dst + ". Open ROTKFixLauncher.exe there.";
    } else if (denied) a.deniedDir = dst;
}


// ---------------------------------------------------------------------------
// The Two Towers (Xbox), played through its PC port, TwoTowers.exe.
// The port is where the player pointed to it (remembered in ROTKFix\ports.ini), or
// else next to the launcher (TwoTowers\TwoTowers.exe). Its settings, texture
// packs and log live in ROTKFix\TwoTowers next to the launcher.

static std::string tt_data_dir() { return exe_dir() + "\\ROTKFix\\TwoTowers"; }
static std::string tt_settings_path() { return tt_data_dir() + "\\settings.toml"; }
static std::string tt_log_path() { return tt_data_dir() + "\\tt.log"; }
static std::string ports_ini() { return exe_dir() + "\\ROTKFix\\ports.ini"; }

static bool file_there(const std::string &p) { DWORD a = GetFileAttributesA(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

static void tt_find(App &a)
{
    a.ttLooked = true;
    a.tt = Port();
    std::string cand[2];
    // ports.ini first (a path the player set, e.g. a development build), then the
    // TwoTowers folder next to the launcher (release copies)
    char b[MAX_PATH * 2] = "";
    GetPrivateProfileStringA("Ports", "TwoTowers", "", b, sizeof b, ports_ini().c_str());
    cand[0] = b;
    cand[1] = exe_dir() + "\\TwoTowers\\TwoTowers.exe";
    for (auto &c : cand) if (!c.empty() && file_there(c)) { a.tt.exe = c; break; }
    if (a.tt.exe.empty()) return;
    port_query(a.tt);
    // Frame rate is the player's own choice on the Display tab: only the Original preset
    // (the game as it was, 30 fps) sets it. Other presets are picture quality only, as in ROTK.
    for (auto &pr : a.tt.presets) {
        if (pr.name == "Original") continue;
        for (size_t i = 0; i < pr.values.size(); )
            if (pr.values[i].first == "graphics.fps") pr.values.erase(pr.values.begin() + i); else i++;
    }
    a.ttLoaded = false;
}

static bool tt_ready(const App &a) { return !a.tt.exe.empty() && a.tt.err.empty() && !a.tt.settings.empty(); }

// The value of a setting: from the file, else its default
static std::string tt_val(const App &a, const PortSetting &ps)
{
    std::string v = a.ttFile.get(ps.section, ps.key, ps.def);
    if (ps.type == "choice" && !ps.choices.empty()) {   // a choice the port no longer offers (an old "unlimited"): its default, as the port does
        std::string u = toml_unquote(v);
        for (auto &c : ps.choices) if (c == u) return v;
        return ps.def;
    }
    return v;
}
static const PortSetting *tt_setting(const App &a, const char *sec, const char *key)
{
    for (auto &ps : a.tt.settings) if (ps.section == sec && ps.key == key) return &ps;
    return nullptr;
}

static void tt_load_settings(App &a)
{
    a.ttLoaded = true;
    a.ttFile = PortSettingsFile();
    settings_file_load(tt_settings_path(), a.ttFile);   // missing: every setting at its default
}

static void tt_apply_refresh(App &a);
static bool tt_save(App &a)
{
    SHCreateDirectoryExA(NULL, tt_data_dir().c_str(), NULL);
    tt_apply_refresh(a);   // fullscreen: the refresh rate that suits the frame rate, as ROTK does
    // the old frame limit is gone from the port: take it out of the file
    if (!tt_setting(a, "display", "fps_cap")) a.ttFile.remove("display", "fps_cap");
    // every setting is written, so the file is complete even before the first run
    for (auto &ps : a.tt.settings) a.ttFile.set(ps.section, ps.key, tt_val(a, ps));
    if (!settings_file_save(tt_settings_path(), a.ttFile)) { set_message(a, "Could not save " + tt_settings_path(), true); return false; }
    set_message(a, "Settings saved", false);
    return true;
}

static void tt_play(App &a, const LibGame &g)
{
    if (!tt_save(a)) return;
    std::string tex = tt_data_dir() + "\\textures";
    SHCreateDirectoryExA(NULL, (tex + "\\replace").c_str(), NULL);
    // save games: kept with the launcher. The first time, bring over any the port
    // already has in its own folder (hdd\ beside TwoTowers.exe), so no progress is lost.
    std::string saves = tt_data_dir() + "\\saves";
    if (GetFileAttributesA(saves.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::string old = a.tt.exe.substr(0, a.tt.exe.find_last_of('\\')) + "\\hdd";
        if (GetFileAttributesA(old.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::string from = old + "\\*";
            from.push_back('\0');                 // SHFileOperation lists end with two zeros
            std::string to = saves; to.push_back('\0');
            SHCreateDirectoryExA(NULL, saves.c_str(), NULL);
            SHFILEOPSTRUCTA op; memset(&op, 0, sizeof op);
            op.wFunc = FO_COPY; op.pFrom = from.c_str(); op.pTo = to.c_str();
            op.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;
            SHFileOperationA(&op);
        }
    }
    std::string shots = exe_dir() + "\\Screenshots";   // with ROTK's; the names start TT_
    std::string args = "--game \"" + g.path + "\" --config \"" + tt_settings_path() + "\" --textures \"" + tex +
                       "\" --log \"" + tt_log_path() + "\" --saves \"" + saves + "\" --shader-cache \"" + tt_data_dir() +
                       "\\shadercache\" --screenshots \"" + shots + "\" --no-dialogs";
    std::string err;
    HANDLE h = port_start(a.tt.exe, args, err);
    if (!h) { set_message(a, err, true); return; }
    a.running = h;
    a.runningLog = tt_log_path();
    a.runningGame = "The Two Towers";
    SDL_HideWindow(a.win);
}

// The port has closed: back to the launcher, saying why if it wasn't a normal quit
static void port_finished(App &a, DWORD code)
{
    std::string why = port_log_reason(a.runningLog);
    std::string logDir = a.runningLog.substr(0, a.runningLog.find_last_of('\\'));
    std::string logName = a.runningLog.substr(a.runningLog.find_last_of('\\') + 1);
    std::string m;
    switch (code) {
    case 0: break;
    case 1: m = "The game couldn't start" + (why.empty() ? std::string(".") : ": " + why + ".") + " Details are in " + logName + " in " + logDir + "."; break;
    case 2: m = "The game files couldn't be found there, or they aren't " + a.runningGame + ". Check the game's location in the About tab."; break;
    case 3: m = "This copy isn't the US release. Only the US version is supported for now."; break;
    case 4: m = "Direct3D 12 couldn't start. Update your graphics driver; the port needs a DirectX 12 graphics card."; break;
    case 5: m = "The settings file is damaged" + (why.empty() ? std::string("") : " (" + why + ")") + ". Use \"Reset to defaults\" below, then Play again."; break;
    case 0xC0000135:   // a DLL is missing, before the port could write its log (older builds need the VC++ runtime)
        m = "The game couldn't start because a Windows component is missing. Installing the \"Microsoft Visual C++ "
            "Redistributable (x64)\" from Microsoft usually fixes this.";
        break;
    default: {
        char b[32]; snprintf(b, sizeof b, "0x%08lX", (unsigned long)code);
        m = std::string("The game closed unexpectedly (") + b + "). Details are in " + logName + " in " + logDir + ".";
    }
    }
    a.running = NULL;
    if (code == 0) { a.quit = true; return; }       // played and quit: close, as after ROTK
    a.ttLoaded = false;                              // the port may have rewritten its settings
    SDL_ShowWindow(a.win);
    SDL_RaiseWindow(a.win);
    set_message(a, m, true);
}

static std::string cap_first(std::string s) { if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] -= 32; return s; }

static const char *tt_section_title(const std::string &s)
{
    if (s == "display") return "Screen";
    if (s == "graphics") return "Graphics";
    if (s == "textures") return "Texture packs";
    if (s == "network") return "Network";
    return nullptr;
}

// How a choice is shown in its menu
static std::string tt_choice_name(const PortSetting &ps, const std::string &c)
{
    // the port's own display names, when it gives them
    for (size_t i = 0; i < ps.choiceLabels.size(); i++) if (ps.choices[i] == c) return ps.choiceLabels[i];
    // otherwise, for older ports:
    if (ps.key == "anisotropic_filtering") return c == "0" ? "Off" : c + "x";
    if (ps.key == "fps") return c + " fps";
    if (ps.key == "msaa" || ps.key == "anti_aliasing") return c == "0" ? "Off" : c + "x";
    return cap_first(c);
}

// Screen size: one menu with the main screen's sizes (width and height are two settings in the file)
// The screen The Two Towers is set to use ("main", or "DISPLAY2" and so on)
static const Monitor &tt_monitor(const App &a)
{
    std::string scr = "main";
    if (const PortSetting *ps = tt_setting(a, "display", "screen")) scr = toml_unquote(tt_val(a, *ps));
    if (_stricmp(scr.c_str(), "main"))
        for (const Monitor &m : a.monitors)
            if (m.device.size() > 4 && !_stricmp(m.device.c_str() + 4, scr.c_str())) return m;
    for (const Monitor &m : a.monitors) if (m.primary) return m;
    return a.monitors[0];
}

// How a screen choice is shown: the monitor's own name, as on ROTK's Display tab
static std::string tt_screen_name(const App &a, const std::string &c)
{
    for (const Monitor &m : a.monitors) {
        bool match = !_stricmp(c.c_str(), "main") ? m.primary : m.device.size() > 4 && !_stricmp(m.device.c_str() + 4, c.c_str());
        if (!match) continue;
        char b[160];
        snprintf(b, sizeof b, "%s%s (%d x %d)", m.name.c_str(), m.primary ? ", main screen" : "", m.deskW, m.deskH);
        return b;
    }
    return !_stricmp(c.c_str(), "main") ? "Main screen" : c;
}

static void tt_resolution_row(App &a, const PortSetting &pw, const PortSetting &ph)
{
    int w = atoi(tt_val(a, pw).c_str()), h = atoi(tt_val(a, ph).c_str());
    const Monitor &m = tt_monitor(a);
    char cur[64];
    if (w <= 0 || h <= 0) snprintf(cur, sizeof cur, "Desktop (%d x %d)", m.deskW, m.deskH);
    else snprintf(cur, sizeof cur, "%d x %d", w, h);
    row_begin("Resolution", "The size of the picture on screen. Desktop uses the screen's own size.");
    if (ImGui::BeginCombo("##ttres", cur)) {
        char d[64]; snprintf(d, sizeof d, "Desktop (%d x %d)", m.deskW, m.deskH);
        if (ImGui::Selectable(d, w <= 0)) { a.ttFile.set(pw.section, pw.key, "0"); a.ttFile.set(ph.section, ph.key, "0"); }
        for (auto it = m.modes.rbegin(); it != m.modes.rend(); ++it) {
            if (it->w < 640 || it->h < 480) continue;
            char l[64]; snprintf(l, sizeof l, "%d x %d", it->w, it->h);
            if (ImGui::Selectable(l, it->w == w && it->h == h)) {
                a.ttFile.set(pw.section, pw.key, std::to_string(it->w));
                a.ttFile.set(ph.section, ph.key, std::to_string(it->h));
            }
        }
        ImGui::EndCombo();
    }
    row_end();
}

// Refresh rate: the rates the screen offers at the chosen size; only used in fullscreen
static void tt_refresh_row(App &a, const PortSetting &pr)
{
    const Monitor &m = tt_monitor(a);
    int w = 0, h = 0;
    if (const PortSetting *ps = tt_setting(a, "display", "width")) w = atoi(tt_val(a, *ps).c_str());
    if (const PortSetting *ps = tt_setting(a, "display", "height")) h = atoi(tt_val(a, *ps).c_str());
    if (w <= 0 || h <= 0) { w = m.deskW; h = m.deskH; }
    std::vector<int> hz;
    if (const Mode *md = m.find(w, h)) hz = md->hz;
    if (hz.empty()) hz.push_back(m.deskHz);
    int r = atoi(tt_val(a, pr).c_str());
    bool full = true;
    if (const PortSetting *pm = tt_setting(a, "display", "mode")) full = toml_unquote(tt_val(a, *pm)) == "fullscreen";
    char cur[48];
    if (r <= 0) snprintf(cur, sizeof cur, "Highest (%d Hz)", hz.back());
    else snprintf(cur, sizeof cur, "%d Hz", r);
    ImGui::BeginDisabled(!full);
    row_begin(pr.label.c_str(), full ? pr.help.c_str() : "Only used in fullscreen. Borderless and windowed use the desktop's refresh rate.");
    if (ImGui::BeginCombo("##tthz", cur)) {
        char b[48]; snprintf(b, sizeof b, "Highest (%d Hz)", hz.back());
        if (ImGui::Selectable(b, r <= 0)) a.ttFile.set(pr.section, pr.key, "0");
        for (auto it = hz.rbegin(); it != hz.rend(); ++it) {
            snprintf(b, sizeof b, "%d Hz", *it);
            if (ImGui::Selectable(b, *it == r)) a.ttFile.set(pr.section, pr.key, std::to_string(*it));
        }
        ImGui::EndCombo();
    }
    row_end();
    ImGui::EndDisabled();
}

static void tt_setting_row(App &a, const PortSetting &ps)
{
    std::string v = tt_val(a, ps);
    ImGui::PushID((ps.section + "." + ps.key).c_str());
    row_begin(ps.label.c_str(), ps.help.c_str());
    if (ps.type == "bool") {
        bool b = v == "true";
        if (ImGui::Checkbox("##b", &b)) a.ttFile.set(ps.section, ps.key, b ? "true" : "false");
    } else if (ps.type == "choice") {
        std::string cur = toml_unquote(v);
        int idx = -1;
        std::vector<std::string> names;
        for (size_t i = 0; i < ps.choices.size(); i++) {
            names.push_back(ps.section == "display" && ps.key == "screen" ? tt_screen_name(a, ps.choices[i]) : tt_choice_name(ps, ps.choices[i]));
            if (ps.choices[i] == cur) idx = (int)i;
        }
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
        if (ImGui::BeginCombo("##c", idx >= 0 ? names[idx].c_str() : cur.c_str())) {
            bool screen = ps.section == "display" && ps.key == "screen";
            for (size_t i = 0; i < names.size(); i++) {
                // the main screen also listed by its DISPLAY number: shown once, as "main"
                if (screen && (int)i != idx && _stricmp(ps.choices[i].c_str(), "main")) {
                    bool isPrimary = false;
                    for (const Monitor &m : a.monitors)
                        if (m.primary && m.device.size() > 4 && !_stricmp(m.device.c_str() + 4, ps.choices[i].c_str())) isPrimary = true;
                    if (isPrimary) continue;
                }
                if (ImGui::Selectable(names[i].c_str(), (int)i == idx)) a.ttFile.set(ps.section, ps.key, toml_quote(ps.choices[i]));
            }
            ImGui::EndCombo();
        }
    } else if (ps.type == "int") {
        int n = atoi(v.c_str()), lo = (int)ps.min, hi = (int)ps.max;
        bool changed;
        if (hi > lo && hi - lo <= 400) changed = ImGui::SliderInt("##i", &n, lo, hi, ps.key == "fov" || ps.key == "render_percent" ? "%d%%" : "%d");
        else changed = ImGui::InputInt("##i", &n, 0, 0);
        if (changed) { if (hi > lo) n = std::max(lo, std::min(hi, n)); a.ttFile.set(ps.section, ps.key, std::to_string(n)); }
    } else if (ps.type == "float") {
        float f = (float)atof(v.c_str());
        bool changed = ps.max > ps.min ? ImGui::SliderFloat("##f", &f, (float)ps.min, (float)ps.max, "%.2f") : ImGui::InputFloat("##f", &f);
        if (changed) { char b[32]; snprintf(b, sizeof b, "%.2f", f); a.ttFile.set(ps.section, ps.key, b); }
    } else {
        char b[256]; snprintf(b, sizeof b, "%s", toml_unquote(v).c_str());
        if (ImGui::InputText("##s", b, sizeof b)) a.ttFile.set(ps.section, ps.key, toml_quote(b));
    }
    row_end();
    ImGui::PopID();
}

static void tt_tabs(App &a, const LibGame &g);
static void draw_tt(App &a, const LibGame &g)
{
    if (!a.ttLooked) tt_find(a);
    ImGui::PushTextWrapPos(0);
    if (a.tt.exe.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(C_WARN, "The Two Towers port (TwoTowers.exe) wasn't found.");
        ImGui::TextUnformatted("It is normally in a TwoTowers folder next to this launcher. If you have it somewhere else, show the launcher where.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Locate TwoTowers.exe...")) {
            std::string f;
            if (pick_file(f, L"Choose TwoTowers.exe", L"The Two Towers port (TwoTowers.exe)", L"TwoTowers.exe")) {
                SHCreateDirectoryExA(NULL, (exe_dir() + "\\ROTKFix").c_str(), NULL);
                WritePrivateProfileStringA("Ports", "TwoTowers", f.c_str(), ports_ini().c_str());
                tt_find(a);
            }
        }
        return;
    }
    if (!tt_ready(a)) {
        ImGui::Spacing();
        ImGui::TextColored(C_BAD, "%s", a.tt.err.c_str());
        ImGui::TextColored(C_DIM, "%s", a.tt.exe.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Try again")) tt_find(a);
        ImGui::SameLine();
        if (ImGui::Button("Locate TwoTowers.exe...")) {
            std::string f;
            if (pick_file(f, L"Choose TwoTowers.exe", L"The Two Towers port (TwoTowers.exe)", L"TwoTowers.exe")) {
                WritePrivateProfileStringA("Ports", "TwoTowers", f.c_str(), ports_ini().c_str());
                tt_find(a);
            }
        }
        return;
    }
    ImGui::PopTextWrapPos();
    if (!a.ttLoaded) tt_load_settings(a);

    tt_tabs(a, g);
}

// Which tab a section goes on (as ROTK's: Display, Graphics, Controls, Audio)
static int tt_tab_of(const std::string &sec)
{
    if (sec == "display") return 0;
    if (sec == "controls" || sec == "input") return 2;
    if (sec == "audio" || sec == "sound") return 3;
    if (sec == "network") return 4;
    return 1;   // graphics, textures and anything else
}

static bool tt_value_equal(const std::string &x, const std::string &y)
{
    if (x == y) return true;
    char *e1, *e2;
    double a = strtod(x.c_str(), &e1), b = strtod(y.c_str(), &e2);
    return *e1 == 0 && *e2 == 0 && e1 != x.c_str() && e2 != y.c_str() && fabs(a - b) < 1e-6;
}

// Preset menu: the port's presets, or "Custom" when the settings match none
static void tt_preset_row(App &a)
{
    if (a.tt.presets.empty()) return;
    int cur = -1;
    for (size_t i = 0; i < a.tt.presets.size() && cur < 0; i++) {
        bool all = true;
        for (auto &kv : a.tt.presets[i].values) {
            size_t dot = kv.first.find('.');
            const PortSetting *ps = tt_setting(a, kv.first.substr(0, dot).c_str(), kv.first.substr(dot + 1).c_str());
            if (!ps || !tt_value_equal(tt_val(a, *ps), kv.second)) { all = false; break; }
        }
        if (all) cur = (int)i;
    }
    row_begin("Preset", "A starting point for the graphics settings. Changing any of them afterwards makes it Custom.");
    if (ImGui::BeginCombo("##ttpreset", cur >= 0 ? a.tt.presets[cur].name.c_str() : "Custom")) {
        for (size_t i = 0; i < a.tt.presets.size(); i++)
            if (ImGui::Selectable(a.tt.presets[i].name.c_str(), (int)i == cur))
                for (auto &kv : a.tt.presets[i].values) {
                    size_t dot = kv.first.find('.');
                    a.ttFile.set(kv.first.substr(0, dot), kv.first.substr(dot + 1), kv.second);
                }
        ImGui::EndCombo();
    }
    row_end();
}

static void tt_open_textures()
{
    std::string t = tt_data_dir() + "\\textures";
    SHCreateDirectoryExA(NULL, (t + "\\replace").c_str(), NULL);
    ShellExecuteA(NULL, "open", t.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

// Settings the launcher sets by itself instead of showing: the frame limit follows the
// frame rate (one "Frame rate" choice, as for ROTK)
static bool tt_hidden(const PortSetting &ps) { return ps.section == "display" && ps.key == "fps_cap"; }
// The tab a setting is shown on: its section's, except the frame rate, which is on Display as in ROTK's
static int tt_tab_of_setting(const PortSetting &ps) { return ps.section == "graphics" && ps.key == "fps" ? 0 : tt_tab_of(ps.section); }


// ---- The Two Towers' Display tab, laid out exactly like ROTK's (tab_display):
// Screen (Monitor, Display mode, Resolution) and Smoothness (V-Sync, Frame rate,
// and the note under it). The refresh rate isn't shown: as in ROTK, fullscreen
// picks it from the frame rate (written to display.refresh on save).

static std::string tt_get(const App &a, const char *sec, const char *key, const char *def)
{
    const PortSetting *ps = tt_setting(a, sec, key);
    return ps ? toml_unquote(tt_val(a, *ps)) : def;
}
static int tt_geti(const App &a, const char *sec, const char *key, int def)
{
    const PortSetting *ps = tt_setting(a, sec, key);
    return ps ? atoi(toml_unquote(tt_val(a, *ps)).c_str()) : def;
}
static void tt_put(App &a, const char *sec, const char *key, const std::string &v)
{
    const PortSetting *ps = tt_setting(a, sec, key);
    if (!ps) return;
    a.ttFile.set(sec, key, ps->type == "choice" || ps->type == "string" ? toml_quote(v) : v);
}
static int tt_mode_index(const App &a)   // 0 borderless, 1 fullscreen, 2 windowed (ROTK's order)
{
    std::string m = tt_get(a, "display", "mode", "borderless");
    return m == "fullscreen" ? 1 : m == "windowed" ? 2 : 0;
}
static int tt_max_refresh(const App &a)
{
    const Monitor &m = tt_monitor(a);
    if (tt_mode_index(a) == 1) {
        int w = tt_geti(a, "display", "width", 0), h = tt_geti(a, "display", "height", 0);
        if (w <= 0 || h <= 0) { w = m.deskW; h = m.deskH; }
        if (const Mode *md = m.find(w, h)) if (!md->hz.empty()) return md->hz.back();
    }
    return m.deskHz;
}
static std::vector<int> tt_refresh_list(const App &a)
{
    const Monitor &m = tt_monitor(a);
    int w = tt_geti(a, "display", "width", 0), h = tt_geti(a, "display", "height", 0);
    if (w <= 0 || h <= 0) { w = m.deskW; h = m.deskH; }
    if (const Mode *md = m.find(w, h)) if (!md->hz.empty()) return md->hz;
    return { m.deskHz };
}
// The refresh rate the port should use: in fullscreen, the best match for the frame rate
static int tt_effective_refresh(const App &a)
{
    if (tt_mode_index(a) != 1) return tt_monitor(a).deskHz;
    return pick_refresh(tt_refresh_list(a), tt_geti(a, "graphics", "fps", 30));
}
static void tt_apply_refresh(App &a)
{
    if (tt_setting(a, "display", "refresh")) tt_put(a, "display", "refresh", std::to_string(tt_mode_index(a) == 1 ? tt_effective_refresh(a) : 0));
}
// "main" for the main screen, else its device name without \\.\ ("DISPLAY2")
static std::string tt_screen_value(const Monitor &m)
{
    if (m.primary) return "main";
    return m.device.rfind("\\\\.\\", 0) == 0 ? m.device.substr(4) : m.device;
}

static void tt_tab_display(App &a)
{
    const Monitor &m = tt_monitor(a);
    section("Screen");
    const PortSetting *pscr = tt_setting(a, "display", "screen");
    if (pscr && a.monitors.size() > 1) {
        row_begin("Monitor", "Which screen the game runs on.");
        if (ImGui::BeginCombo("##ttmon", m.name.c_str())) {
            for (int i = 0; i < (int)a.monitors.size(); i++) {
                const Monitor &mi = a.monitors[i];
                std::string v = tt_screen_value(mi);
                bool offered = v == "main";
                for (auto &c : pscr->choices) if (!_stricmp(c.c_str(), v.c_str())) offered = true;
                if (!offered) continue;
                char b[256];
                snprintf(b, sizeof b, "%d: %s%s  (%dx%d @ %d Hz)", i + 1, mi.name.c_str(), mi.primary ? ", main" : "", mi.deskW, mi.deskH, mi.deskHz);
                if (ImGui::Selectable(b, &mi == &m)) { tt_put(a, "display", "screen", v); tt_put(a, "display", "width", "0"); tt_put(a, "display", "height", "0"); }
            }
            ImGui::EndCombo();
        }
        row_end();
    }

    static const char *modes[] = { "Borderless", "Fullscreen", "Windowed" };
    static const char *modeVals[] = { "borderless", "fullscreen", "windowed" };
    int mode = tt_mode_index(a);
    int w = tt_geti(a, "display", "width", 0), h = tt_geti(a, "display", "height", 0);
    row_begin("Display mode", "Borderless: fills the screen and switches instantly with Alt-Tab (recommended).\n"
                              "Fullscreen: the screen switches to the chosen resolution and refresh rate.\n"
                              "Windowed: a normal window you can move around.");
    if (segmented("ttmode", &mode, modes, 3)) {
        tt_put(a, "display", "mode", modeVals[mode]);
        if (mode != 1 && (w > m.deskW || h > m.deskH)) { w = h = 0; }      // supersampling resolutions are for fullscreen only
        if (mode != 2 && w > 0 && !m.find(w, h)) { w = h = 0; }            // free window sizes are for windowed only
        tt_put(a, "display", "width", std::to_string(w)); tt_put(a, "display", "height", std::to_string(h));
    }
    row_end();

    row_begin("Resolution", "Only resolutions your monitor supports are listed.\n"
                            "In fullscreen, sizes marked \"supersampling\" are larger than your screen: the game is drawn at that size "
                            "and your graphics driver shrinks it down for a cleaner, sharper picture. They appear when NVIDIA DSR/DLDSR "
                            "or AMD VSR is turned on in the driver's control panel.");
    char cur[64];
    if (w > 0 && h > 0) snprintf(cur, sizeof cur, "%d x %d", w, h);
    else snprintf(cur, sizeof cur, "%d x %d  (screen)", m.deskW, m.deskH);
    auto setres = [&](int nw, int nh) { tt_put(a, "display", "width", std::to_string(nw)); tt_put(a, "display", "height", std::to_string(nh)); };
    if (ImGui::BeginCombo("##ttres", cur, ImGuiComboFlags_HeightLarge)) {
        char b[64];
        snprintf(b, sizeof b, "%d x %d  (screen)", m.deskW, m.deskH);
        if (ImGui::Selectable(b, w <= 0)) setres(0, 0);
        for (int i = (int)m.modes.size() - 1; i >= 0; i--) {
            const Mode &md = m.modes[i];
            bool bigger = md.w > m.deskW || md.h > m.deskH;
            if ((bigger && mode != 1) || (md.w == m.deskW && md.h == m.deskH)) continue;
            const char *tag = md.h * 16 == md.w * 9 ? "  16:9" : md.h * 16 == md.w * 10 ? "  16:10" : md.h * 4 == md.w * 3 ? "  4:3" : "";
            snprintf(b, sizeof b, "%d x %d%s%s", md.w, md.h, tag, bigger ? "  (supersampling)" : "");
            if (ImGui::Selectable(b, w == md.w && h == md.h)) setres(md.w, md.h);
        }
        if (mode == 2) {
            static const struct { int w, h; const char *tag; } extra[] = {
                { 3440, 1440, "  21:9 ultrawide" }, { 2560, 1080, "  21:9 ultrawide" }, { 2560, 1440, "  16:9" },
                { 1920, 1080, "  16:9" }, { 1600, 900, "  16:9" }, { 1280, 720, "  16:9" }, { 1440, 1080, "  4:3" } };
            ImGui::SeparatorText("Window sizes");
            for (auto &e : extra) {
                if (e.w > m.deskW || e.h > m.deskH || (e.w == m.deskW && e.h == m.deskH) || m.find(e.w, e.h)) continue;
                snprintf(b, sizeof b, "%d x %d%s", e.w, e.h, e.tag);
                if (ImGui::Selectable(b, w == e.w && h == e.h)) setres(e.w, e.h);
            }
        }
        ImGui::EndCombo();
    }
    row_end();

    // The Two Towers only: drawn at a share of the screen, then fitted (ROTK has no such setting).
    // Each choice shows the size it draws at, from the resolution above (the screen's own when 0),
    // as the port works it out: round(width * pct / 100) x round(height * pct / 100).
    if (const PortSetting *rp = tt_setting(a, "display", "render_percent")) {
        int rw = w > 0 && h > 0 ? w : m.deskW, rh = w > 0 && h > 0 ? h : m.deskH;
        auto sized = [&](const std::string &choice) {
            int pct = atoi(choice.c_str());
            char b[48];
            snprintf(b, sizeof b, "  (%d x %d)", (int)lround(rw * pct / 100.0), (int)lround(rh * pct / 100.0));
            return tt_choice_name(*rp, choice) + b;
        };
        if (rp->type == "choice") {
            std::string cur = toml_unquote(tt_val(a, *rp));
            row_begin(rp->label.c_str(), rp->help.c_str());
            ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
            if (ImGui::BeginCombo("##ttrender", sized(cur).c_str())) {
                for (auto &c : rp->choices)
                    if (ImGui::Selectable(sized(c).c_str(), c == cur)) a.ttFile.set(rp->section, rp->key, toml_quote(c));
                ImGui::EndCombo();
            }
            row_end();
        } else {   // older ports: a percentage slider
            int pct = atoi(tt_val(a, *rp).c_str());
            char fmt[64];
            snprintf(fmt, sizeof fmt, "%%d%%%%  (%d x %d)", (int)lround(rw * pct / 100.0), (int)lround(rh * pct / 100.0));
            row_begin(rp->label.c_str(), rp->help.c_str());
            if (ImGui::SliderInt("##ttrender", &pct, (int)rp->min, (int)rp->max, fmt)) a.ttFile.set(rp->section, rp->key, std::to_string(pct));
            row_end();
        }
    }

    section("Smoothness");
    bool vsync = tt_get(a, "display", "vsync", "true") == "true";
    if (tt_setting(a, "display", "vsync")) {
        row_begin("V-Sync", "Prevents tearing. The frame rate can't go above the screen's refresh rate.");
        if (ImGui::Checkbox("##ttvsync", &vsync)) tt_put(a, "display", "vsync", vsync ? "true" : "false");
        row_end();
    }

    const PortSetting *pf = tt_setting(a, "graphics", "fps");
    if (pf) {
        int maxHz = tt_max_refresh(a);
        int fps = tt_geti(a, "graphics", "fps", 30);
        row_begin("Frame rate", "The game was made for 30. Higher rates play at normal speed.\n"
                                "In fullscreen the screen switches to the matching refresh rate. In borderless and windowed, "
                                "rates that divide evenly into your screen's refresh rate are the smoothest.");
        char c[64];
        snprintf(c, sizeof c, "%d fps%s", fps, fps == 30 ? "  (original)" : "");
        if (ImGui::BeginCombo("##ttfps", c)) {
            for (auto &ch : pf->choices) {
                int r = atoi(ch.c_str());
                if (r <= 0 || (vsync && r > maxHz && r != fps)) continue;
                snprintf(c, sizeof c, "%d fps%s", r, r == 30 ? "  (original)" : "");
                if (ImGui::Selectable(c, fps == r)) tt_put(a, "graphics", "fps", ch);
            }
            ImGui::EndCombo();
        }
        row_end();
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::SetCursorPosX(g_labelW);
        ImGui::PushTextWrapPos(0);
        if (vsync && fps > maxHz)
            ImGui::TextColored(C_WARN, "Above this screen's %d Hz - the game will run at %d fps.", maxHz, maxHz);
        else if (mode == 1)
            ImGui::TextColored(C_DIM, "Screen will run at %d Hz.", tt_effective_refresh(a));
        else if (fps > 0 && maxHz % fps)
            ImGui::TextColored(C_DIM, "Your screen runs at %d Hz; %d fps won't divide evenly into it, so motion may be slightly uneven.", maxHz, fps);
        else
            ImGui::TextColored(C_DIM, "Your screen runs at %d Hz - %d fps fits it evenly.", maxHz, fps);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }

    // anything else the port adds to [display] later, with its own label
    bool more = false;
    for (auto &ps : a.tt.settings) {
        if (ps.section != "display") continue;
        if (ps.key == "screen" || ps.key == "mode" || ps.key == "width" || ps.key == "height" || ps.key == "refresh" ||
            ps.key == "vsync" || ps.key == "render_percent" || tt_hidden(ps)) continue;
        if (!more) { section("More"); more = true; }
        tt_setting_row(a, ps);
    }
}


// ---- The Two Towers' Graphics tab, laid out like ROTK's (tab_graphics): Quality,
// Image, View, Texture packs, Movies. Only the settings the port really has are shown.

static bool tt_has(const App &a, const char *sec, const char *key) { return tt_setting(a, sec, key) != nullptr; }

static void tt_tab_graphics(App &a)
{
    std::vector<std::string> done;   // section.key shown here; anything else goes under "More"
    auto mark = [&](const char *sec, const char *key) { done.push_back(std::string(sec) + "." + key); };

    if (!a.tt.presets.empty()) {
        section("Quality");
        std::string help;
        for (auto &p : a.tt.presets) {
            const char *d = p.name == "Original" ? "as it looked on the Xbox in 2003: 4:3, 30 fps, no anti-aliasing, filtering or sharpening."
                          : p.name == "Balanced" ? "widescreen, light anti-aliasing and filtering, light sharpening."
                          : p.name == "High"     ? "widescreen, 4x anti-aliasing, 16x texture filtering, sharper textures and sharpening."
                          : p.name == "Ultra"    ? "8x anti-aliasing, and drawn at 150% resolution for an even cleaner picture."
                          : "";
            help += p.name + (*d ? std::string(": ") + d : std::string()) + "\n";
        }
        help += "Custom: shown when you change individual settings.\nOnly Original changes the frame rate; the others keep the one chosen on the Display tab.";
        // reuse the preset row, with ROTK's wording
        int cur = -1;
        for (size_t i = 0; i < a.tt.presets.size() && cur < 0; i++) {
            bool all = true;
            for (auto &kv : a.tt.presets[i].values) {
                size_t dot = kv.first.find('.');
                const PortSetting *ps = tt_setting(a, kv.first.substr(0, dot).c_str(), kv.first.substr(dot + 1).c_str());
                if (!ps || !tt_value_equal(tt_val(a, *ps), kv.second)) { all = false; break; }
            }
            if (all) cur = (int)i;
        }
        row_begin("Preset", help.c_str());
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
        if (ImGui::BeginCombo("##ttpreset", cur >= 0 ? a.tt.presets[cur].name.c_str() : "Custom")) {
            for (size_t i = 0; i < a.tt.presets.size(); i++)
                if (ImGui::Selectable(a.tt.presets[i].name.c_str(), (int)i == cur))
                    for (auto &kv : a.tt.presets[i].values) {
                        size_t dot = kv.first.find('.');
                        a.ttFile.set(kv.first.substr(0, dot), kv.first.substr(dot + 1), kv.second);
                    }
            ImGui::EndCombo();
        }
        row_end();
    }

    bool image = tt_has(a, "graphics", "anti_aliasing") || tt_has(a, "graphics", "msaa") || tt_has(a, "graphics", "colour_look") ||
                 tt_has(a, "graphics", "anisotropic_filtering") || tt_has(a, "graphics", "lod_bias") ||
                 tt_has(a, "graphics", "sharpening") || tt_has(a, "graphics", "brightness");
    if (image) section("Image");
    // Anti-aliasing first, as in ROTK
    for (const char *k : { "anti_aliasing", "msaa" }) {
        const PortSetting *ps = tt_setting(a, "graphics", k);
        if (!ps || ps->type != "choice") continue;
        mark("graphics", k);
        std::string cur = toml_unquote(tt_val(a, *ps));
        row_begin("Anti-aliasing", "Smooths jagged edges (MSAA). 8x is heavy: at high frame rates 4x looks almost the same and stays smooth.");
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
        if (ImGui::BeginCombo((std::string("##tt") + k).c_str(), tt_choice_name(*ps, cur).c_str())) {
            for (auto &c : ps->choices)
                if (ImGui::Selectable(tt_choice_name(*ps, c).c_str(), c == cur)) a.ttFile.set(ps->section, ps->key, toml_quote(c));
            ImGui::EndCombo();
        }
        row_end();
        break;
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "anisotropic_filtering")) {
        mark("graphics", "anisotropic_filtering");
        std::string cur = toml_unquote(tt_val(a, *ps));
        row_begin("Texture filtering", "Anisotropic filtering: keeps ground and walls sharp at a distance.");
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
        if (ImGui::BeginCombo("##ttaf", tt_choice_name(*ps, cur).c_str())) {
            for (auto &c : ps->choices)
                if (ImGui::Selectable(tt_choice_name(*ps, c).c_str(), c == cur)) a.ttFile.set(ps->section, ps->key, toml_quote(c));
            ImGui::EndCombo();
        }
        row_end();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "lod_bias")) {
        mark("graphics", "lod_bias");
        int ts = (int)lroundf(-(float)atof(tt_val(a, *ps).c_str()) * 100.0f);
        int lo = (int)lround(-ps->max * 100), hi = (int)lround(-ps->min * 100);   // -1.00..0 -> 0..100%
        row_begin("Texture sharpness", "Shows textures at higher detail sooner. Too high can shimmer.");
        if (ImGui::SliderInt("##ttlod", &ts, lo, hi, ts == 0 ? "Game default" : "%d%%")) {
            char b[32]; snprintf(b, sizeof b, "%.2f", -ts / 100.0f); a.ttFile.set(ps->section, ps->key, b);
        }
        row_end();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "sharpening")) {
        mark("graphics", "sharpening");
        int v = atoi(tt_val(a, *ps).c_str());
        row_begin("Sharpening", "Crisper picture. 0 turns it off. While playing, [ switches it on and off to compare.");
        if (ImGui::SliderInt("##ttsharp", &v, (int)ps->min, (int)ps->max, v == 0 ? "Off" : "%d")) a.ttFile.set(ps->section, ps->key, std::to_string(v));
        row_end();
    }
    // the port's own (no ROTK Fix counterpart): right after Sharpening; presets set it
    if (const PortSetting *ps = tt_setting(a, "graphics", "smooth_2d")) {
        mark("graphics", "smooth_2d");
        bool v = tt_val(a, *ps) == "true";
        row_begin(ps->label.empty() ? "Smooth menus and HUD" : ps->label.c_str(),
                  ps->help.empty() ? "Smooths menus, text and the HUD when they are drawn larger than the console's screen." : ps->help.c_str());
        if (ImGui::Checkbox("##ttsmooth2d", &v)) a.ttFile.set(ps->section, ps->key, v ? "true" : "false");
        row_end();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "colour_look")) {
        mark("graphics", "colour_look");
        std::string cur = toml_unquote(tt_val(a, *ps));
        row_begin("Colour look", "Original: the game's own colours.\n"
                                 "Film: richer contrast, cool shadows and warm highlights.\n"
                                 "Vivid: stronger colour.\n"
                                 "Dark fantasy: cooler and moodier, with less colour.\n"
                                 "While playing, ] switches between them.");
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
        if (ImGui::BeginCombo("##ttlook", tt_choice_name(*ps, cur).c_str())) {
            for (auto &c : ps->choices)
                if (ImGui::Selectable(tt_choice_name(*ps, c).c_str(), c == cur)) a.ttFile.set(ps->section, ps->key, toml_quote(c));
            ImGui::EndCombo();
        }
        row_end();
        // strength only matters with a look chosen, as in ROTK
        if (const PortSetting *pst = tt_setting(a, "graphics", "look_strength")) {
            mark("graphics", "look_strength");
            if (cur != "original") {
                int v = atoi(tt_val(a, *pst).c_str());
                row_begin("Look strength", "How strongly the colour look is applied.");
                if (ImGui::SliderInt("##ttlookpct", &v, (int)pst->min, (int)pst->max, "%d%%")) a.ttFile.set(pst->section, pst->key, std::to_string(v));
                row_end();
            }
        }
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "brightness")) {
        mark("graphics", "brightness");
        float v = (float)atof(tt_val(a, *ps).c_str());
        row_begin("Brightness", "Lifts the darker parts of the picture (dark cutscenes, caves) without washing out white.");
        if (ImGui::SliderFloat("##ttbright", &v, (float)ps->min, (float)ps->max, fabsf(v - 1.0f) < 0.005f ? "Game's own" : "%.2f")) {
            char b[32]; snprintf(b, sizeof b, "%.2f", v); a.ttFile.set(ps->section, ps->key, b);
        }
        row_end();
    }

    if (const PortSetting *ps = tt_setting(a, "graphics", "hdr")) {   // The Two Towers only
        mark("graphics", "hdr");
        bool v = tt_val(a, *ps) == "true";
        row_begin("HDR", ps->help.c_str());
        if (ImGui::Checkbox("##tthdr", &v)) a.ttFile.set(ps->section, ps->key, v ? "true" : "false");
        row_end();
    }

    const PortSetting *pws = tt_setting(a, "graphics", "widescreen");
    if (pws || tt_has(a, "graphics", "fov") || tt_has(a, "graphics", "hud_edges")) section("View");
    bool ws = pws ? tt_val(a, *pws) == "true" : false;
    if (pws) {
        mark("graphics", "widescreen");
        row_begin("Widescreen correction", "Shows more at the sides instead of stretching the 4:3 picture.");
        if (ImGui::Checkbox("##ttws", &ws)) a.ttFile.set(pws->section, pws->key, ws ? "true" : "false");
        row_end();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "hud_edges")) {
        mark("graphics", "hud_edges");
        bool v = tt_val(a, *ps) == "true";
        ImGui::BeginDisabled(!ws);
        row_begin("HUD at the screen edges", "With widescreen correction on: health bars and other HUD pieces sit at the screen's edges "
                                             "instead of in the middle 4:3 area.");
        if (ImGui::Checkbox("##tthud", &v)) a.ttFile.set(ps->section, ps->key, v ? "true" : "false");
        row_end();
        ImGui::EndDisabled();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "fov")) {
        mark("graphics", "fov");
        int v = atoi(tt_val(a, *ps).c_str());
        row_begin("Field of view", "How wide the camera sees, compared with the game's own. Wider shows more of the battle "
                                   "but makes characters look further away.");
        if (ImGui::SliderInt("##ttfov", &v, (int)ps->min, (int)ps->max, v == 100 ? "100%%  (game's own)" : "%d%%"))
            a.ttFile.set(ps->section, ps->key, std::to_string(v));
        row_end();
    }

    const PortSetting *prep = tt_setting(a, "textures", "replace"), *pdump = tt_setting(a, "textures", "dump");
    if (prep || pdump) {
        section("Texture packs");
        std::string dir = tt_data_dir() + "\\textures";
        if (prep) {
            mark("textures", "replace");
            bool v = tt_val(a, *prep) == "true";
            std::string h = "Shows sharper or remade pictures from texture packs. A pack goes in its own folder inside "
                            "ROTKFix\\TwoTowers\\textures\\replace; remove the folder to go back to the original pictures.";
            row_begin("Use texture packs", h.c_str());
            if (ImGui::Checkbox("##tttexrepl", &v)) a.ttFile.set(prep->section, prep->key, v ? "true" : "false");
            row_end();
        }
        if (pdump) {
            mark("textures", "dump");
            bool v = tt_val(a, *pdump) == "true";
            row_begin("Save game textures", "For making texture packs: every picture the game uses is saved as a PNG in "
                                            "ROTKFix\\TwoTowers\\textures\\dump as you play. Levels load a little slower the first time. "
                                            "Turn it off when you have what you need.");
            if (ImGui::Checkbox("##tttexdump", &v)) a.ttFile.set(pdump->section, pdump->key, v ? "true" : "false");
            row_end();
        }
        row_begin("Textures folder", "Opens ROTKFix\\TwoTowers\\textures, where the replace and dump folders are.");
        if (ImGui::Button("Open folder##tttex")) tt_open_textures();
        row_end();
    }

    // Movies, in ROTK's order: Movie display, Sharper cutscenes, Skip any video, Skip opening logos, Cutscene sound delay
    {
        const PortSetting *pmd = tt_setting(a, "graphics", "movie_display"), *pms = tt_setting(a, "graphics", "movie_sharpening"),
                          *psa = tt_setting(a, "graphics", "skip_all_videos"), *psl = tt_setting(a, "graphics", "skip_logos"),
                          *pad = tt_setting(a, "graphics", "cutscene_audio_delay");
        if (pmd || pms || psa || psl || pad) section("Movies");
        if (pmd && pmd->type == "choice") {
            mark("graphics", "movie_display");
            std::string cur = toml_unquote(tt_val(a, *pmd));
            row_begin("Movie display", pmd->help.c_str());
            ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
            if (ImGui::BeginCombo("##ttmd", tt_choice_name(*pmd, cur).c_str())) {
                for (auto &c : pmd->choices)
                    if (ImGui::Selectable(tt_choice_name(*pmd, c).c_str(), c == cur)) a.ttFile.set(pmd->section, pmd->key, toml_quote(c));
                ImGui::EndCombo();
            }
            row_end();
        }
        if (pms) {
            mark("graphics", "movie_sharpening");
            int v = atoi(tt_val(a, *pms).c_str());
            row_begin("Sharper cutscenes", pms->help.c_str());
            if (ImGui::SliderInt("##ttmvsharp", &v, (int)pms->min, (int)pms->max, v == 0 ? "Off" : "%d")) a.ttFile.set(pms->section, pms->key, std::to_string(v));
            row_end();
        }
        if (psa) {
            mark("graphics", "skip_all_videos");
            bool v = tt_val(a, *psa) == "true";
            row_begin("Skip any video", psa->help.c_str());
            if (ImGui::Checkbox("##ttskipall", &v)) a.ttFile.set(psa->section, psa->key, v ? "true" : "false");
            row_end();
        }
        if (psl) {
            mark("graphics", "skip_logos");
            bool v = tt_val(a, *psl) == "true";
            row_begin("Skip opening logos", psl->help.c_str());
            if (ImGui::Checkbox("##ttlogos", &v)) a.ttFile.set(psl->section, psl->key, v ? "true" : "false");
            row_end();
        }
        if (pad) {
            mark("graphics", "cutscene_audio_delay");
            int v = atoi(tt_val(a, *pad).c_str());
            row_begin("Cutscene sound delay", pad->help.c_str());
            if (ImGui::SliderInt("##ttadelay", &v, (int)pad->min, (int)pad->max, v == 0 ? "Off" : "%d ms")) a.ttFile.set(pad->section, pad->key, std::to_string(v));
            row_end();
        }
    }

    // anything else on this tab that the port adds later, with its own label
    bool more = false;
    for (auto &ps : a.tt.settings) {
        if (tt_tab_of_setting(ps) != 1 || tt_hidden(ps)) continue;
        if (std::find(done.begin(), done.end(), ps.section + "." + ps.key) != done.end()) continue;
        if (!more) { section("More"); more = true; }
        tt_setting_row(a, ps);
    }
}


// ---- The Two Towers' Controls tab, laid out and worked like ROTK's: Controller, Buttons
// (click a move, press the button; it moves on by itself), Controller options.
// The port's [controls] settings say which button on the player's controller does each
// Xbox button; their values are the same names ROTK uses (A, B, X, Y, LB, RB, LT, RT, LS, RS).
static const char *const k_ttPhys[PAD_PHYS_COUNT] = { "A", "B", "X", "Y", "LB", "RB", "", "", "LS", "RS", "LT", "RT" };
static int tt_phys_of(const std::string &v)
{
    for (int i = 0; i < PAD_PHYS_COUNT; i++) if (k_ttPhys[i][0] && v == k_ttPhys[i]) return i;
    return -1;   // "none"
}
static std::vector<const PortSetting *> tt_buttons(App &a)
{
    std::vector<const PortSetting *> v;
    for (auto &ps : a.tt.settings) if (ps.section == "controls" && ps.type == "choice") v.push_back(&ps);
    return v;
}
static int tt_button_count(App &a) { return (int)tt_buttons(a).size(); }
// As on ROTK: the move that had the button gets this move's old one.
static void tt_assign_button(App &a, int idx, int ph)
{
    auto b = tt_buttons(a);
    if (idx < 0 || idx >= (int)b.size() || ph < 0 || !k_ttPhys[ph][0]) return;
    std::string nv = k_ttPhys[ph], old = toml_unquote(tt_val(a, *b[idx]));
    if (std::find(b[idx]->choices.begin(), b[idx]->choices.end(), nv) == b[idx]->choices.end()) return;
    for (int i = 0; i < (int)b.size(); i++)
        if (i != idx && toml_unquote(tt_val(a, *b[i])) == nv) a.ttFile.set(b[i]->section, b[i]->key, toml_quote(old));
    a.ttFile.set(b[idx]->section, b[idx]->key, toml_quote(nv));
}
// "Speed Attack / Fire" -> "Speed attack / fire", as ROTK writes its moves; "Xbox B" stays
static std::string tt_move_name(const std::string &l)
{
    std::string r = l;
    bool first = true, afterXbox = false;
    for (size_t i = 0; i < r.size(); i++) {
        if (r[i] == ' ' || r[i] == '/') continue;
        size_t e = r.find_first_of(" /", i);
        if (e == std::string::npos) e = r.size();
        std::string w = r.substr(i, e - i);
        bool keep = first || afterXbox || w == "Xbox" || (w.size() <= 2 && isupper((unsigned char)w[0]));   // "Xbox Black" is a button's name
        afterXbox = w == "Xbox";
        if (!keep && isupper((unsigned char)w[0]) && (w.size() < 2 || islower((unsigned char)w[1]))) r[i] = (char)tolower((unsigned char)r[i]);
        first = false;
        i = e;
    }
    return r;
}
static void tt_slider_row(App &a, const char *key, const char *label, const char *help, const char *id, const char *zero)
{
    const PortSetting *ps = tt_setting(a, "input", key);
    if (!ps) return;
    int v = atoi(tt_val(a, *ps).c_str());
    row_begin(label, help);
    if (ImGui::SliderInt(id, &v, (int)ps->min, (int)ps->max, v == 0 ? zero : "%d%%")) a.ttFile.set(ps->section, ps->key, std::to_string(v));
    row_end();
}
static void tt_tab_controls(App &a)
{
    section("Controller");
    row_begin("Use a controller", "Always on: the controller is the game's own input, as on the Xbox.");
    ImGui::BeginDisabled();
    bool on = true;
    ImGui::Checkbox("##ttpad", &on);
    ImGui::EndDisabled();
    ImGui::SameLine(0, S(16));
    ImGui::AlignTextToFramePadding();
    if (a.controllers > 1) ImGui::TextColored(C_GOOD, "%d controllers connected", a.controllers);
    else if (a.controllers) ImGui::TextColored(C_GOOD, "Connected");
    else ImGui::TextColored(C_WARN, "None connected");
    row_end();

    auto btn = tt_buttons(a);
    int style = label_style(a);
    if (!a.captureTT) a.capture = -1;
    a.captureTT = true;
    if (a.capture >= (int)btn.size()) a.capture = -1;
    if (a.capture >= 0 && (ImGui::GetTime() - a.captureAt > 8.0 || !a.controllers)) a.capture = -1;
    if (!btn.empty()) {
        section("Buttons");
        float slotW = S(170);
        ImGui::BeginGroup();
        for (int m = 0; m < (int)btn.size(); m++) {
            const PortSetting &ps = *btn[m];
            ImGui::PushID(m);
            ImGui::AlignTextToFramePadding();
            std::string name = tt_move_name(ps.label.empty() ? ps.key : ps.label);
            ImGui::TextUnformatted(name.c_str());
            ImGui::SameLine(g_labelW);
            bool waiting = a.capture == m;
            int ph = tt_phys_of(toml_unquote(tt_val(a, ps)));
            char lab[64];
            snprintf(lab, sizeof lab, "%s##slot", waiting ? "Press a button..." : ph < 0 ? "Nothing" : phys_label(ph, style));
            if (waiting) {
                float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 6.0);
                ImGui::PushStyleColor(ImGuiCol_Button, rgb(110 + (int)(40 * pulse), 82 + (int)(30 * pulse), 36));
                ImGui::PushStyleColor(ImGuiCol_Border, C_GOLD_H);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, rgb(34, 27, 20));
                ImGui::PushStyleColor(ImGuiCol_Border, C_LINE);
            }
            if (ImGui::Button(lab, ImVec2(slotW, 0))) {
                if (waiting) a.capture = -1;
                else if (a.controllers) { a.capture = m; a.captureAt = ImGui::GetTime(); }
                else ImGui::OpenPopup("pick");
            }
            ImGui::PopStyleColor(2);
            if (ImGui::BeginPopup("pick")) {
                static const int choices[] = { PH_A, PH_B, PH_X, PH_Y, PH_LB, PH_RB, PH_LT, PH_RT, PH_LS, PH_RS };
                for (int c : choices)
                    if (ImGui::Selectable(phys_label(c, style), ph == c)) tt_assign_button(a, m, c);
                if (std::find(ps.choices.begin(), ps.choices.end(), "none") != ps.choices.end() &&
                    ImGui::Selectable("Nothing", ph < 0)) a.ttFile.set(ps.section, ps.key, toml_quote("none"));
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndGroup();
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::PushTextWrapPos(0);
        if (a.capture >= 0)
            ImGui::TextColored(C_GOLD_H, "Press the button for \"%s\". Start, Back or Esc stops.",
                               tt_move_name(btn[a.capture]->label.empty() ? btn[a.capture]->key : btn[a.capture]->label).c_str());
        else if (a.controllers)
            ImGui::TextColored(C_DIM, "Click a move, then press the button you want on the controller. It moves on to the next move by itself.");
        else
            ImGui::TextColored(C_DIM, "Connect a controller to set buttons by pressing them, or click a move to pick from a list.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::BeginDisabled(!a.controllers);
        if (ImGui::Button("Set all, one by one")) { a.capture = 0; a.captureAt = ImGui::GetTime(); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        bool isDefault = true;
        for (auto *ps : btn) if (toml_unquote(tt_val(a, *ps)) != toml_unquote(ps->def)) isDefault = false;
        ImGui::BeginDisabled(isDefault);
        if (ImGui::Button("Original layout")) { for (auto *ps : btn) a.ttFile.set(ps->section, ps->key, ps->def); a.capture = -1; }
        ImGui::EndDisabled();
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_DIM, "%s pauses and skips movies, %s is Back. The D-pad and left stick move through menus. One layout is used for every controller.",
                           phys_label(PH_START, style), phys_label(PH_BACK, style));
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }

    section("Controller options");
    tt_slider_row(a, "vibration", "Vibration", "Controller rumble from the original Xbox version. 0 turns it off.", "##ttrumble", "Off");
    tt_slider_row(a, "stick_deadzone", "Stick deadzone", "How far a stick must move before it counts. 0 keeps the game's own, as on the Xbox.", "##ttdz", "Game's own");
    tt_slider_row(a, "trigger_threshold", "Trigger sensitivity", "How far the triggers must be pulled to count as pressed. 0 keeps the game's own, as on the Xbox.", "##tttrig", "Game's own");
    // anything else the port puts on this tab
    bool more = false;
    for (auto &ps : a.tt.settings) {
        if (tt_tab_of_setting(ps) != 2 || tt_hidden(ps) || ps.section == "controls") continue;
        if (ps.section == "input" && (ps.key == "vibration" || ps.key == "stick_deadzone" || ps.key == "trigger_threshold")) continue;
        if (!more) { section("More"); more = true; }
        tt_setting_row(a, ps);
    }
}

// The settings on one tab, in the port's own order, grouped by section
static void tt_settings_on_tab(App &a, int tab)
{
    const PortSetting *pw = tt_setting(a, "display", "width"), *ph = tt_setting(a, "display", "height");
    std::string sec = "\x01";
    if (tab == 1 && !a.tt.presets.empty()) { section("Preset"); tt_preset_row(a); }
    for (auto &ps : a.tt.settings) {
        if (tt_tab_of_setting(ps) != tab) continue;
        // the frame rate joins the Display tab's screen settings, without starting a Graphics heading there
        std::string psec = tab == 0 && ps.section == "graphics" ? std::string("display") : ps.section;
        if (psec != sec) {
            if (sec == "textures" && ImGui::Button("Open texture folder")) tt_open_textures();
            sec = psec;
            const char *t = tt_section_title(sec);
            section(t ? t : cap_first(sec).c_str());
        }
        if (pw && ph && &ps == pw) {
            tt_resolution_row(a, *pw, *ph);
            if (const PortSetting *pr = tt_setting(a, "display", "refresh")) if (pr->type == "int") tt_refresh_row(a, *pr);   // right under it
            continue;
        }
        if (pw && ph && &ps == ph) continue;
        if (tt_hidden(ps)) continue;
        if (ps.section == "display" && ps.key == "refresh" && ps.type == "int") { if (!(pw && ph)) tt_refresh_row(a, ps); continue; }
        // the HUD-edge setting only matters with widescreen on
        bool off = false;
        if (ps.key == "hud_edges") if (const PortSetting *ws = tt_setting(a, "graphics", "widescreen")) off = tt_val(a, *ws) != "true";
        ImGui::BeginDisabled(off);
        tt_setting_row(a, ps);
        ImGui::EndDisabled();
    }
    if (sec == "textures" && ImGui::Button("Open texture folder")) tt_open_textures();
}

static void tt_about(App &a)
{
    const LibGame &g = a.lib[a.cur];
    section(game_kind_name(g.kind));
    row_begin("Location", nullptr);
    ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(g.path.c_str()); ImGui::PopTextWrapPos();
    row_end();
    row_begin("Version", nullptr);
    ImGui::Text("Xbox, %s copy, %s", game_region_name(g), g.iso ? "disc image" : "extracted files");
    if (g.titleId) { ImGui::SameLine(); ImGui::TextColored(C_DIM, "(title ID %08X)", (unsigned)g.titleId); }
    row_end();
    section("Port");
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "%s  -  %s", a.tt.version.c_str(), a.tt.exe.c_str());
    ImGui::TextColored(C_DIM, "Settings, texture packs and the log: %s", tt_data_dir().c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (ImGui::Button("Open settings folder")) {
        SHCreateDirectoryExA(NULL, tt_data_dir().c_str(), NULL);
        ShellExecuteA(NULL, "open", tt_data_dir().c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("Open screenshots")) {
        std::string d = exe_dir() + "\\Screenshots";
        SHCreateDirectoryExA(NULL, d.c_str(), NULL);
        ShellExecuteA(NULL, "open", d.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults")) {
        a.ttFile = PortSettingsFile();
        tt_save(a);
        set_message(a, "The Two Towers settings are back to their defaults", false);
    }
    games_section(a);
}

// The Two Towers' page when its port is ready: tabs like ROTK's
static void tt_tabs(App &a, const LibGame &)
{
    static const char *names[] = { "  Display  ", "  Graphics  ", "  Controls  ", "  Audio  ", "  Network  " };
    bool has[5] = {};
    for (auto &ps : a.tt.settings) has[tt_tab_of_setting(ps)] = true;
    has[1] = true;   // the preset lives there
    ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
    if (ImGui::BeginTabBar("tttabs")) {
        static int startTab = getenv("ROTKFIX_TAB") ? atoi(getenv("ROTKFIX_TAB")) : -1;
        int shown = 0;
        for (int t = 0; t < 6; t++) {
            if (t < 5 && !has[t]) continue;
            ImGuiTabItemFlags tf = shown++ == startTab ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(t < 5 ? names[t] : "  About  ", nullptr, tf)) {
                ImGui::PopFont();
                ImGui::BeginChild("tttab", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
                if (t == 0) tt_tab_display(a); else if (t == 1) tt_tab_graphics(a); else if (t == 2) tt_tab_controls(a);
                else if (t < 5) tt_settings_on_tab(a, t); else tt_about(a);
                if (getenv("ROTKFIX_SCROLL_END")) ImGui::SetScrollY(ImGui::GetScrollMaxY());   // testing
                ImGui::Dummy(ImVec2(0, S(6)));
                ImGui::EndChild();
                ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
        startTab = -1;
    }
    ImGui::PopFont();
}

// One line for the bottom bar
static bool tt_value_equal(const std::string &x, const std::string &y);
static std::string tt_summary(const App &a)
{
    if (!tt_ready(a)) return "The Two Towers  |  port not found";
    std::string s;
    const PortSetting *p;
    int w = 0, h = 0;
    if ((p = tt_setting(a, "display", "width"))) w = atoi(tt_val(a, *p).c_str());
    if ((p = tt_setting(a, "display", "height"))) h = atoi(tt_val(a, *p).c_str());
    if (w <= 0 || h <= 0) { w = tt_monitor(a).deskW; h = tt_monitor(a).deskH; }
    s = std::to_string(w) + " x " + std::to_string(h);
    if ((p = tt_setting(a, "display", "mode"))) s += "  |  " + cap_first(toml_unquote(tt_val(a, *p)));
    if ((p = tt_setting(a, "graphics", "fps"))) s += "  |  " + std::to_string(atoi(toml_unquote(tt_val(a, *p)).c_str())) + " fps";
    // the preset, as ROTK's line ends with it
    if (!a.tt.presets.empty()) {
        std::string name = "Custom";
        for (auto &pr : a.tt.presets) {
            bool all = true;
            for (auto &kv : pr.values) {
                size_t dot = kv.first.find('.');
                const PortSetting *ps = tt_setting(a, kv.first.substr(0, dot).c_str(), kv.first.substr(dot + 1).c_str());
                if (!ps || !tt_value_equal(tt_val(a, *ps), kv.second)) { all = false; break; }
            }
            if (all) { name = pr.name; break; }
        }
        s += "  |  " + name;
    }
    return s;
}

// ---------------------------------------------------------------------------
// The Return of the King (Xbox), played through its PC port, ROTKXbox.exe. It reads
// ROTK Fix's own ROTKFix.ini, so it shares the PC version's settings page.
// The port is where the player pointed to it (ROTKFix\ports.ini, [Ports] ROTKXbox=), or
// else next to the launcher (ROTKXbox\ROTKXbox.exe). Its log, saves and its own
// texture packs (Xbox textures differ from the PC ones) live in ROTKFix\ROTKXbox.

static std::string rx_data_dir() { return exe_dir() + "\\ROTKFix\\ROTKXbox"; }
static std::string rx_log_path() { return rx_data_dir() + "\\rotkx.log"; }

static void rx_find(App &a)
{
    a.rxLooked = true;
    a.rx = Port();
    std::string cand[2];
    // ports.ini first (a path the player set, e.g. a development build), then the
    // ROTKXbox folder next to the launcher (release copies)
    char b[MAX_PATH * 2] = "";
    GetPrivateProfileStringA("Ports", "ROTKXbox", "", b, sizeof b, ports_ini().c_str());
    cand[0] = b;
    cand[1] = exe_dir() + "\\ROTKXbox\\ROTKXbox.exe";
    for (auto &c : cand) if (!c.empty() && file_there(c)) { a.rx.exe = c; break; }
    if (a.rx.exe.empty()) return;
    std::string out;
    if (run_capture(a.rx.exe, "--version", out, 15000) == 0) {
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
        a.rx.version = out;
    }
}
static bool rx_ready(const App &a) { return !a.rx.exe.empty(); }

static void rx_play(App &a, const LibGame &g)
{
    std::string d = rx_data_dir();
    SHCreateDirectoryExA(NULL, (d + "\\textures\\replace").c_str(), NULL);
    SHCreateDirectoryExA(NULL, (d + "\\saves").c_str(), NULL);
    std::string args = "--game \"" + g.path + "\" --ini \"" + a.dir + "\\ROTKFix.ini\" --log \"" + rx_log_path() +
                       "\" --saves \"" + d + "\\saves\" --screenshots \"" + exe_dir() + "\\Screenshots\" --textures \"" + d +
                       "\\textures\" --config \"" + d + "\\settings.toml\" --shader-cache \"" + d + "\\shadercache\" --no-dialogs";
    std::string err;
    HANDLE h = port_start(a.rx.exe, args, err);
    if (!h) { set_message(a, err, true); return; }
    a.running = h;
    a.runningLog = rx_log_path();
    a.runningGame = "The Return of the King";
    SDL_HideWindow(a.win);
}

static void rx_locate_button(App &a)
{
    if (ImGui::Button("Locate ROTKXbox.exe...")) {
        std::string f;
        if (pick_file(f, L"Choose ROTKXbox.exe", L"The Return of the King Xbox port (ROTKXbox.exe)", L"ROTKXbox.exe")) {
            SHCreateDirectoryExA(NULL, (exe_dir() + "\\ROTKFix").c_str(), NULL);
            WritePrivateProfileStringA("Ports", "ROTKXbox", f.c_str(), ports_ini().c_str());
            rx_find(a);
        }
    }
}

// About tab on the Xbox version's page
static void rx_about(App &a)
{
    const LibGame &g = a.lib[a.cur];
    section("The Return of the King  -  Xbox");
    row_begin("Location", nullptr);
    ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(g.path.c_str()); ImGui::PopTextWrapPos();
    row_end();
    row_begin("Version", nullptr);
    ImGui::Text("Xbox, %s copy, %s", game_region_name(g), g.iso ? "disc image" : "extracted files");
    if (g.titleId) { ImGui::SameLine(); ImGui::TextColored(C_DIM, "(title ID %08X)", (unsigned)g.titleId); }
    row_end();
    section("Port");
    ImGui::PushTextWrapPos(0);
    if (rx_ready(a)) {
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::TextColored(C_DIM, "%s  -  %s", a.rx.version.empty() ? "ROTKXbox" : a.rx.version.c_str(), a.rx.exe.c_str());
        if (a.game.exeFound) ImGui::TextColored(C_DIM, "Settings are shared with the PC version (%s\\ROTKFix.ini).", a.dir.c_str());
        else ImGui::TextColored(C_DIM, "Settings: %s\\ROTKFix.ini", a.dir.c_str());
        ImGui::TextColored(C_DIM, "Log, saves and Xbox texture packs: %s", rx_data_dir().c_str());
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Open folder##rx")) {
            SHCreateDirectoryExA(NULL, (rx_data_dir() + "\\textures\\replace").c_str(), NULL);
            ShellExecuteA(NULL, "open", rx_data_dir().c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
        ImGui::SameLine();
        rx_locate_button(a);
    } else {
        ImGui::TextColored(C_WARN, "The Return of the King Xbox port (ROTKXbox.exe) wasn't found.");
        ImGui::TextUnformatted("It is normally in a ROTKXbox folder next to this launcher. If you have it somewhere else, show the launcher where.");
        ImGui::PopTextWrapPos();
        rx_locate_button(a);
    }
    games_section(a);
}


// Shown for a game other than this folder's PC game
static void draw_other_game(App &a)
{
    LibGame &g = a.lib[a.cur];
    section(game_kind_name(g.kind));
    row_begin("Location", nullptr);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(g.path.c_str());
    ImGui::PopTextWrapPos();
    row_end();
    if (g.kind != GK_ROTK_PC) {
        row_begin("Version", nullptr);
        ImGui::Text("Xbox, %s copy, %s", game_region_name(g), g.iso ? "disc image" : "extracted files");
        if (g.titleId) { ImGui::SameLine(); ImGui::TextColored(C_DIM, "(title ID %08X)", (unsigned)g.titleId); }
        row_end();
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        if (!game_region_supported(g)) {
            ImGui::TextColored(C_WARN, "This is a %s copy. Only the US version is supported for now.", game_region_name(g));
            ImGui::PopTextWrapPos();
            return;
        }
        if (g.kind == GK_TT_XBOX) { ImGui::PopTextWrapPos(); draw_tt(a, g); return; }
        ImGui::TextColored(C_WARN, "Recognised, but not playable from here yet.");
        ImGui::TextUnformatted("The Xbox Return of the King needs its own PC port, which is still to be built. When it's ready, "
                               "its settings will appear here and Play will start it with your game files.");
        ImGui::PopTextWrapPos();
        return;
    }
    // the PC game in another folder: its own copy of ROTK Fix keeps its own settings there
    if (a.otherFor != g.path) { a.other = inspect_game(g.path); a.otherFor = g.path; }
    const GameInfo &o = a.other;
    row_begin("Version", nullptr);
    ImGui::TextUnformatted(o.versionText.c_str());
    row_end();
    ImGui::Spacing();
    if (o.hasDll && o.hasProxyD3D8) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("ROTK Fix is installed there, with its own settings.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Open the launcher there")) { if (start_launcher(g.path, false, "")) a.quit = true; }
    } else {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("ROTK Fix isn't installed in that folder yet.");
        ImGui::PopTextWrapPos();
        if (gold_button("Set up ROTK Fix there", ImVec2(S(240), S(36)), F_BODY)) { setup_here(a, g.path); a.otherFor.clear(); }
        if (!a.setupErr.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextColored(C_BAD, "%s", a.setupErr.c_str()); ImGui::PopTextWrapPos(); }
    }
}

static void draw_setup(App &a)
{
    if (!a.searched) { a.found = find_game_folders(); a.searched = true; }
    section("Let's find your games");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("This launcher plays your own copies of The Return of the King (PC or Xbox) and The Two Towers (Xbox). "
                           "Show it where your games are; it remembers them.");
    ImGui::PopTextWrapPos();

    // Xbox games: a folder with the extracted game, or a disc image
    section("Xbox games");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("Choose the folder you extracted an Xbox game to (the one with default.xbe), or its disc image (.iso). "
                           "Nothing is copied or changed. Only US copies are supported for now.");
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Add a game folder...##setup")) {
        std::string d;
        if (pick_folder(d, L"Choose the folder with your extracted Xbox game (default.xbe)")) add_game_from(a, d);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add an Xbox disc image...##setup")) {
        std::string f;
        if (pick_disc_image(f)) add_game_from(a, f);
    }

    section("The Return of the King  -  PC");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("For the PC version, ROTK Fix has to be in the game's own folder (the one with rotk.exe), "
                           "and this launcher isn't there. Choose your game below and ROTK Fix will be set up there for you.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (a.found.empty()) {
        ImGui::TextColored(C_WARN, "No installed copy of the game was found automatically.");
    } else {
        ImGui::TextColored(C_DIM, a.found.size() == 1 ? "Found on this PC:" : "Found on this PC (supported versions first):");
        for (size_t i = 0; i < a.found.size() && i < 6; i++) {
            const FoundGame &f = a.found[i];
            ImGui::PushID((int)i);
            ImGui::Spacing();
            ImGui::TextUnformatted(f.dir.c_str());
            ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
            ImGui::TextColored(f.info.versionOk ? C_GOOD : C_WARN, "%s%s", f.info.versionText.c_str(),
                               f.info.hasDll ? "  -  ROTK Fix already installed there" : "");
            ImGui::PopFont();
            if (f.info.hasDll && f.info.hasProxyD3D8) {
                if (ImGui::Button("Open the launcher there")) { if (start_launcher(f.dir, false, "")) a.quit = true; }
            } else if (gold_button("Set up ROTK Fix here", ImVec2(S(240), S(36)), F_BODY)) {
                setup_here(a, f.dir);
            }
            ImGui::PopID();
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("Choose the folder myself...")) {
        std::string d;
        if (pick_folder(d)) {
            GameInfo g = inspect_game(d);
            if (!g.exeFound) a.setupErr = "There is no rotk.exe in " + d + ". Choose the folder the game is installed in.";
            else { a.found.insert(a.found.begin(), FoundGame{ d, g }); a.setupErr.clear(); }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Search again")) a.searched = false;
    if (!a.setupErr.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_BAD, "%s", a.setupErr.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!a.deniedDir.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("The game is in a protected folder. Windows will ask for permission once to copy ROTK Fix there.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Set up with administrator permission")) {
            if (start_launcher(a.dir, true, "--install-to \"" + a.deniedDir + "\"")) a.quit = true;
            else a.setupErr = "Permission was not given.";
        }
    }
    if (!a.lib.empty()) {
        ImGui::Dummy(ImVec2(0, S(10)));
        games_section(a);
    }
}

// ---------------------------------------------------------------------------
// Main screen

static void draw_ui(App &a, ImVec2 size, SDL_Renderer *ren)
{
    // change background every 15 s
    backgrounds_poll(a.bg, ren);
    if (a.bg.files.size() > 1 && a.bg.cur.tex && ImGui::GetTime() - a.bg.changedAt > 15.0) backgrounds_next(a.bg);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    draw_background(ImGui::GetBackgroundDrawList(), size, a.bg);

    float pad = S(30), bannerH = S(142), barH = S(66);
    const GameInfo &g = a.game;

    // ---- banner: title on the left, status on the right
    ImGui::SetCursorPos(ImVec2(pad, S(28)));
    ImGui::PushFont(F_TITLE, F_TITLE->LegacySize);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 tp = ImGui::GetCursorScreenPos();
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            if (dx || dy) dl->AddText(F_TITLE, F_TITLE->LegacySize, ImVec2(tp.x + dx * S(2), tp.y + dy * S(2)), IM_COL32(0, 0, 0, 110), "ROTK FIX");
    ImGui::TextColored(C_GOLD_H, "ROTK FIX");
    ImGui::PopFont();
    ImGui::SetCursorPosX(pad + S(2));
    ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
    // The Return of the King is one entry whatever versions the player has (PC, Xbox or
    // both); with more than one, a second menu picks the version. Other games are listed as they are.
    auto isRotk = [](GameKind k) { return k == GK_ROTK_PC || k == GK_ROTK_XBOX; };
    std::vector<int> rotkIdx;
    for (size_t i = 0; i < a.lib.size(); i++) if (isRotk(a.lib[i].kind)) rotkIdx.push_back((int)i);
    bool curRotk = a.cur < 0 || isRotk(a.lib[a.cur].kind);
    std::string curName = curRotk ? (rotkIdx.size() > 1 ? "The Return of the King" : (a.cur >= 0 ? game_kind_name(a.lib[a.cur].kind) : "The Return of the King  -  PC"))
                                  : game_kind_name(a.lib[a.cur].kind);
    if (a.cur < 0 && !g.exeFound) curName = "Welcome";   // the welcome screen: no game chosen yet
    int entries = (rotkIdx.empty() ? 0 : 1) + (int)(a.lib.size() - rotkIdx.size());
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    if (entries > 1) {
        // more than one game: the subtitle picks which one is shown
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(curName.c_str()).x + ImGui::GetFrameHeight() + S(24));
        if (ImGui::BeginCombo("##game", curName.c_str())) {
            bool rotkListed = false;
            for (size_t i = 0; i < a.lib.size(); i++) {
                ImGui::PushID((int)i);
                if (isRotk(a.lib[i].kind)) {
                    if (!rotkListed) {
                        rotkListed = true;
                        const char *name = rotkIdx.size() > 1 ? "The Return of the King" : game_kind_name(a.lib[i].kind);
                        if (ImGui::Selectable(name, curRotk && a.cur >= 0) && (!curRotk || a.cur < 0)) a.cur = (int)i;
                    }
                } else {
                    bool sameKind = false;
                    for (size_t j = 0; j < a.lib.size(); j++) if (j != i && a.lib[j].kind == a.lib[i].kind) sameKind = true;
                    std::string label = game_kind_name(a.lib[i].kind);
                    if (sameKind) label += "  (" + a.lib[i].path + ")";     // two copies of one version: say which
                    if (ImGui::Selectable(label.c_str(), (int)i == a.cur)) a.cur = (int)i;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextColored(C_TEXT, "%s", curName.c_str());
    }
    // the version picker: PC / Xbox (with the folder when there are two copies of one)
    if (curRotk && rotkIdx.size() > 1) {
        auto verName = [&](int i) {
            const LibGame &lg = a.lib[i];
            std::string v = lg.kind == GK_ROTK_PC ? "PC" : "Xbox";
            int same = 0;
            for (int j : rotkIdx) if (a.lib[j].kind == lg.kind) same++;
            if (same > 1) v += "  (" + lg.path + ")";
            return v;
        };
        int shown = a.cur >= 0 ? a.cur : rotkIdx[0];
        std::string vcur = verName(shown);
        ImGui::SameLine(0, S(4));
        ImGui::TextColored(C_DIM, "-");
        ImGui::SameLine(0, S(4));
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(vcur.c_str()).x + ImGui::GetFrameHeight() + S(24));
        if (ImGui::BeginCombo("##version", vcur.c_str())) {
            for (int i : rotkIdx) {
                ImGui::PushID(i);
                if (ImGui::Selectable(verName(i).c_str(), i == shown)) a.cur = i;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::PopStyleColor(2);
    ImGui::PopFont();
    const bool own = own_game(a);
    // The Two Towers shown, and its port ready: Play and Save work for it
    if (!own && a.lib[a.cur].kind == GK_TT_XBOX && !a.ttLooked) tt_find(a);
    const bool ttShown = !own && a.lib[a.cur].kind == GK_TT_XBOX && game_region_supported(a.lib[a.cur]) && a.ttLooked && tt_ready(a);
    // The Return of the King on Xbox: the same settings page as the PC version
    const bool rxShown = !own && a.lib[a.cur].kind == GK_ROTK_XBOX && game_region_supported(a.lib[a.cur]);
    if (rxShown && !a.rxLooked) rx_find(a);
    g_xboxPage = rxShown;

    // status (top right, one per line)
    struct St { bool ok, warn; std::string label, tip; };
    std::vector<St> st;
    bool filesOk = g.hasDll && g.hasProxyD3D8 && g.hasProxyDinput && g.hasDxvk;
    st.push_back({ g.exeFound && g.versionOk, g.exeFound, g.exeFound ? (g.versionOk ? "Game v1.0" : g.discProtected ? "Disc version (protected)" : "Unknown game version") : "Game not found",
                   g.exeFound ? g.versionText : "Put the launcher in the game folder (next to rotk.exe)." });
    st.push_back({ filesOk, false, filesOk ? "ROTK Fix ready" : "Files missing",
                   filesOk ? "ROTK Fix and the Vulkan renderer are installed." : "Some ROTK Fix files are missing - see the About tab." });
    if (g.otherD3D8 || g.otherDinput) {
        std::string f = g.otherD3D8 && g.otherDinput ? "d3d8.dll and dinput8.dll" : g.otherD3D8 ? "d3d8.dll" : "dinput8.dll";
        st.push_back({ false, true, "Another mod found",
                       "The " + f + " in the game folder belongs to another mod (for example dgVoodoo, DXVK on its own, "
                       "or a fix that came with a repack). ROTK Fix can't load while it's there.\n\n"
                       "Move it out of the game folder (keep it in case you want it back), then copy ROTK Fix's " + f + " in again." });
    }
    if (own && !g.exeFound) st.clear();   // the welcome screen: nothing is wrong, nothing to report yet
    if (!own) {   // another game is shown: just say what it is
        st.clear();
        const LibGame &lg = a.lib[a.cur];
        if (rxShown) {
            bool r = rx_ready(a);
            st.push_back({ r, true, r ? "Ready" : "Port not found", r ? a.rx.exe : "ROTKXbox.exe wasn't found - see the About tab." });
            if (a.s.padEnabled)
                st.push_back({ a.controllers > 0, true, a.controllers > 1 ? std::to_string(a.controllers) + " controllers" : a.controllers ? "Controller" : "No controller",
                               a.controllers ? a.controller : "Connect a controller - it will be picked up automatically." });
        } else {
        bool tt = lg.kind == GK_TT_XBOX;
        if (tt && !a.ttLooked) tt_find(a);
        bool ready = game_region_supported(lg) && (tt ? tt_ready(a) : game_kind_playable(lg.kind));
        st.push_back({ ready, true, ready ? "Ready" : !game_region_supported(lg) ? "Not supported" : tt ? "Port not found" : "Not playable yet", lg.path });
        }
    }
    if (own && a.s.padEnabled)
        st.push_back({ a.controllers > 0 && g.hasSdl, true, a.controllers > 1 ? std::to_string(a.controllers) + " controllers" : a.controllers ? "Controller" : "No controller",
                       a.controllers ? a.controller : "Connect a controller - it will be picked up automatically." });
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    float colW = 0;
    for (auto &x : st) colW = std::max(colW, ImGui::CalcTextSize(x.label.c_str()).x + S(15));
    float sy = S(34), lh = ImGui::GetTextLineHeight() + S(4);
    for (auto &x : st) {
        ImGui::SetCursorPos(ImVec2(size.x - pad - colW, sy));
        status_dot(x.ok, x.warn, x.label.c_str(), x.tip.c_str());
        sy += lh;
    }
    ImGui::PopFont();

    // ---- settings panel
    // an elven frame around it; the panel itself sits just inside the frame
    const float frameT = S(9);
    ImVec2 panelA = ImGui::GetWindowPos() + ImVec2(pad, bannerH);
    ImVec2 panelB = panelA + ImVec2(size.x - pad * 2, size.y - bannerH - barH - S(30));
    orn_panel_fill(ImGui::GetWindowDrawList(), panelA, panelB, ImGui::GetColorU32(rgb(15, 14, 13, 0.86f)));
    const float inset = frameT * 0.5f + S(5);   // clear of the frame's lines
    ImGui::SetCursorPos(ImVec2(pad + inset, bannerH + inset));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("panel", ImVec2(size.x - pad * 2 - inset * 2, size.y - bannerH - barH - S(30) - inset * 2), ImGuiChildFlags_AlwaysUseWindowPadding);
    g_labelW = std::max(S(230), ImGui::GetContentRegionAvail().x * 0.36f);
    if (ttShown) {
        if (!a.ttLoaded) tt_load_settings(a);
        tt_tabs(a, a.lib[a.cur]);
    } else if (!own && !rxShown) {
        ImGui::BeginChild("other", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        draw_other_game(a);
        ImGui::Dummy(ImVec2(0, S(10)));
        games_section(a);
        ImGui::EndChild();
    } else if (!rxShown && !g.exeFound) {
        ImGui::BeginChild("setup", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        draw_setup(a);
        ImGui::EndChild();
    } else {
    ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
    if (ImGui::BeginTabBar("tabs")) {
        struct T { const char *name; void (*fn)(App &); } tabs[] = {
            { "  Display  ", tab_display }, { "  Graphics  ", tab_graphics }, { "  Controls  ", tab_controls },
            { "  Audio  ", tab_audio }, { "  About  ", tab_about } };
        // ROTKFIX_TAB=n (testing): open on tab n
        static int startTab = getenv("ROTKFIX_TAB") ? atoi(getenv("ROTKFIX_TAB")) : -1;
        int ti = 0;
        for (auto &t : tabs) {
            ImGuiTabItemFlags tf = ti++ == startTab ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(t.name, nullptr, tf)) {
                ImGui::PopFont();
                ImGui::BeginChild("tab", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
                t.fn(a);
                ImGui::Dummy(ImVec2(0, S(6)));
                if (getenv("ROTKFIX_SCROLL_END")) ImGui::SetScrollY(ImGui::GetScrollMaxY());   // testing
                else if (const char *sy = getenv("ROTKFIX_SCROLL_Y")) ImGui::SetScrollY(std::min((float)atof(sy) * g_scale, ImGui::GetScrollMaxY()));   // testing
                ImGui::EndChild();
                ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
        startTab = -1;
    }
    ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    orn_panel_frame(ImGui::GetWindowDrawList(), panelA, panelB);

    // ---- bottom bar: summary on the left, buttons on the right
    float by = size.y - barH - S(16);
    int w, h; effective_res(a, w, h);
    static const char *dm[] = { "Borderless", "Fullscreen", "Windowed" };
    static const char *pn[] = { "Original", "Balanced", "High", "Ultra", "Custom" };
    int fps = a.s.vsync ? std::min(a.s.targetFps, effective_refresh(a)) : a.s.targetFps;
    float playW = S(170), playH = S(54), btnH = S(40), saveW = S(100);
    float bx = size.x - pad - playW;
    // summary and messages on a small parchment card, readable over any background
    {
        float cardW = bx - saveW - S(24) - pad;
        bool msg = !a.message.empty() && ImGui::GetTime() - a.messageAt < (a.messageBad ? 30.0 : 4.0);
        float cardH = S(32);
        if (msg) {   // room for the message, however many lines it wraps to
            ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
            cardH += ImGui::CalcTextSize(a.message.c_str(), nullptr, false, cardW - S(18)).y + S(2);
            ImGui::PopFont();
        }
        ImVec2 c0 = ImGui::GetWindowPos() + ImVec2(pad, by + (playH - cardH) * 0.5f), c1 = c0 + ImVec2(cardW, cardH);
        orn_plate(ImGui::GetWindowDrawList(), c0, c1, ImGui::GetColorU32(rgb(15, 14, 13, 0.86f)), false);
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::SetCursorPos(ImVec2(pad + S(10), by + (playH - cardH) * 0.5f + S(5)));
        ImGui::PushTextWrapPos(pad + cardW - S(8));
        if (own && !g.exeFound) ImGui::TextColored(C_DIM, "Add a game to begin");
        else if (own || rxShown) ImGui::TextColored(C_DIM, "%d x %d  |  %s  |  %d fps  |  %s", w, h, dm[a.s.displayMode], fps, pn[detect_preset(a.s)]);
        else if (ttShown) ImGui::TextColored(C_DIM, "%s", tt_summary(a).c_str());
        else ImGui::TextColored(C_DIM, "%s  |  %s", game_kind_name(a.lib[a.cur].kind),
                                game_kind_playable(a.lib[a.cur].kind) ? "settings in its own launcher" : "not playable yet");
        if (msg) {
            ImGui::SetCursorPosX(pad + S(10));
            ImGui::TextColored(a.messageBad ? C_BAD : C_GOOD, "%s", a.message.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }

    // ROTKFIX_PLAY (testing): press Play once at start-up
    // ROTKFIX_TT_SET="graphics.fps=180,..." / ROTKFIX_TT_PRESET=name / ROTKFIX_TT_SAVE (testing):
    // change Two Towers settings the way the menus do, then save
    static bool ttTest = true;
    if (ttTest && ttShown) {
        ttTest = false;
        if (!a.ttLoaded) tt_load_settings(a);
        if (const char *e = getenv("ROTKFIX_TT_PRESET"))
            for (auto &pr : a.tt.presets) if (pr.name == e)
                for (auto &kv : pr.values) { size_t d = kv.first.find('.'); a.ttFile.set(kv.first.substr(0, d), kv.first.substr(d + 1), kv.second); }
        if (const char *e = getenv("ROTKFIX_TT_SET")) {
            std::string all = e;
            size_t st = 0;
            while (st < all.size()) {
                size_t en = all.find(',', st); if (en == std::string::npos) en = all.size();
                std::string item = all.substr(st, en - st);
                size_t d = item.find('.'), q = item.find('=');
                if (d != std::string::npos && q != std::string::npos)
                    tt_put(a, item.substr(0, d).c_str(), item.substr(d + 1, q - d - 1).c_str(), item.substr(q + 1));
                st = en + 1;
            }
        }
        if (getenv("ROTKFIX_TT_SAVE")) tt_save(a);
    }
    static bool autoPlay = getenv("ROTKFIX_PLAY") != nullptr;
    if (autoPlay && ttShown) { autoPlay = false; tt_play(a, a.lib[a.cur]); }
    if (autoPlay && rxShown && rx_ready(a)) { autoPlay = false; if (save(a)) rx_play(a, a.lib[a.cur]); }
    ImGui::SetCursorPos(ImVec2(bx, by));
    bool playOk = !(ttShown ? a.running != NULL : rxShown ? (!rx_ready(a) || a.running != NULL) : (!own || !g.exeFound || g_removeSelf));
    if (orn_arch_button("PLAY", ImVec2(playW, playH), F_PLAY, playOk)) {
        if (ttShown) tt_play(a, a.lib[a.cur]);
        else if (rxShown) { if (save(a)) rx_play(a, a.lib[a.cur]); }
        else if (save(a)) {
            std::string err;
            if (launch_game(a.dir, err)) a.quit = true;
            else set_message(a, err, true);
        }
    }
    bx -= saveW + S(12);
    ImGui::SetCursorPos(ImVec2(bx, by + (playH - btnH) * 0.5f));
    bool saveOk = ttShown || rxShown || !(!own || !g.exeFound || g_removeSelf);
    if (orn_plate_button("Save", ImVec2(saveW, btnH), saveOk)) { if (ttShown) tt_save(a); else save(a); }

    ImGui::End();
}

// ---------------------------------------------------------------------------

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR cmdLine, int)
{
    // started with administrator rights to copy ROTK Fix into a protected game folder
    if (const char *arg = strstr(cmdLine ? cmdLine : "", "--install-to ")) {
        std::string dst = arg + 13;
        while (!dst.empty() && (dst[0] == '"' || dst[0] == ' ')) dst.erase(0, 1);
        while (!dst.empty() && (dst.back() == '"' || dst.back() == ' ')) dst.pop_back();
        std::string err; bool denied = false;
        if (!install_to(exe_dir(), dst, err, denied)) { MessageBoxA(NULL, err.c_str(), "ROTK Fix", MB_ICONERROR); return 1; }
        start_launcher(dst, false, "");
        return 0;
    }
    // A game installed in a protected folder (Program Files - where the disc
    // installs by default): settings and the controller layout can only be
    // saved there with administrator rights. Restart once with Windows'
    // permission prompt; if it is declined, carry on and explain.
    bool protectedDir = false;
    {
        std::string d = exe_dir();
        if (GetFileAttributesA((d + "\\rotk.exe").c_str()) != INVALID_FILE_ATTRIBUTES && !folder_writable(d)) {
            if (!strstr(cmdLine ? cmdLine : "", "--elevated") && start_launcher(d, true, "--elevated")) return 0;
            protectedDir = true;
        }
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        MessageBoxA(NULL, SDL_GetError(), "ROTK Fix Launcher", MB_ICONERROR);
        return 1;
    }
    g_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (g_scale <= 0.0f) g_scale = 1.0f;
    int ww = (int)(720 * g_scale), wh = (int)(800 * g_scale);
    const SDL_DisplayMode *dmode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (dmode && (ww > dmode->w * 0.95f || wh > dmode->h * 0.9f)) {
        float f = std::min(dmode->w * 0.95f / ww, dmode->h * 0.9f / wh);
        g_scale *= f; ww = (int)(ww * f); wh = (int)(wh * f);
    }
    SDL_Window *win = SDL_CreateWindow("ROTK Fix", ww, wh, 0);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, NULL) : nullptr;
    if (!ren) { MessageBoxA(NULL, SDL_GetError(), "ROTK Fix Launcher", MB_ICONERROR); return 1; }
    SDL_SetRenderVSync(ren, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImFontConfig fc; fc.FontDataOwnedByAtlas = false; fc.OversampleH = 2;
    F_BODY  = io.Fonts->AddFontFromMemoryTTF((void *)font_garamond_data, sizeof font_garamond_data, S(21), &fc);
    F_SMALL = io.Fonts->AddFontFromMemoryTTF((void *)font_garamond_data, sizeof font_garamond_data, S(18), &fc);
    F_HEAD  = io.Fonts->AddFontFromMemoryTTF((void *)font_cinzel_data, sizeof font_cinzel_data, S(19), &fc);
    F_TITLE = io.Fonts->AddFontFromMemoryTTF((void *)font_cinzel_data, sizeof font_cinzel_data, S(60), &fc);
    F_PLAY  = io.Fonts->AddFontFromMemoryTTF((void *)font_cinzel_data, sizeof font_cinzel_data, S(34), &fc);
    io.FontDefault = F_BODY;
    ImGui::GetStyle().FontSizeBase = F_BODY->LegacySize;   // 1.92: size of the default font
    setup_style();
    orn_set_scale(g_scale);
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

    App a;
    a.win = win;
    a.dir = exe_dir();
    a.game = inspect_game(a.dir);
    // the player's games: this folder's PC game first, then the remembered ones
    a.lib = library_load(a.dir);
    if (a.game.exeFound) {
        LibGame own;
        if (detect_game_at(a.dir, own) == GK_ROTK_PC) {
            for (size_t i = 0; i < a.lib.size(); i++)
                if (!_stricmp(a.lib[i].path.c_str(), own.path.c_str())) { a.lib.erase(a.lib.begin() + i); break; }
            a.lib.insert(a.lib.begin(), own);
            a.cur = 0;
            if (!protectedDir) library_save(a.dir, a.lib);
        }
    }
    // Not in a PC game's folder, but the player has Xbox games: open on the first of them
    // (The Return of the King first) instead of the PC setup screen.
    if (a.cur < 0)
        for (GameKind k : { GK_ROTK_XBOX, GK_TT_XBOX })
            for (size_t i = 0; i < a.lib.size() && a.cur < 0; i++)
                if (a.lib[i].kind == k && game_region_supported(a.lib[i])) a.cur = (int)i;
    if (const char *t = getenv("ROTKFIX_GAME")) { int n = atoi(t); if (n >= 0 && n < (int)a.lib.size()) a.cur = n; }   // testing
    a.monitors = enum_monitors();
    bool hadIni = load_settings(a.dir, a.s);
    if (!hadIni) apply_preset(a.s, PRESET_HIGH);
    a.s.renderer = 0;   // Vulkan always; the DLL falls back to Direct3D 8 by itself if Vulkan is unavailable
    // the saved screen by name (the order of screens can change)
    for (size_t i = 0; i < a.monitors.size(); i++) {
        const Monitor &m = a.monitors[i];
        bool match = _stricmp(a.s.screen.c_str(), "main") ? m.device.size() > 4 && !_stricmp(m.device.c_str() + 4, a.s.screen.c_str())
                                                          : m.primary;
        if (match) { a.s.monitor = (int)i; break; }
    }
    if (a.s.monitor >= (int)a.monitors.size()) a.s.monitor = 0;
    // a saved fullscreen size this screen doesn't offer (any more): back to the screen's own
    if (a.s.displayMode == 1 && a.s.width > 0 && !mon(a).find(a.s.width, a.s.height)) a.s.width = a.s.height = 0;
    refresh_controllers(a);

    list_pictures(a.dir + "\\ROTKFix\\backgrounds", a.bg.files, 100);
    // (screenshots as backgrounds: turned off for now)
    backgrounds_next(a.bg);
    if (getenv("ROTKFIX_SAVE_AND_EXIT")) { save(a); return 0; }     // testing
    if (!hadIni) set_message(a, "Welcome! Settings are chosen for your PC - press Play, or adjust anything first.", false);
    if (protectedDir) set_message(a, "This game folder is protected by Windows, so settings can't be saved. "
                                     "Open the launcher again and allow the permission prompt.", true);

    while (!a.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) a.quit = true;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED) refresh_controllers(a);
            capture_event(a, e);
        }
        if (a.running) {   // a port is playing: wait, hidden, for it to close
            if (WaitForSingleObject(a.running, 100) == WAIT_OBJECT_0) {
                DWORD code = 0;
                GetExitCodeProcess(a.running, &code);
                CloseHandle(a.running);
                port_finished(a, code);
            }
            continue;
        }
        if (SDL_GetWindowFlags(win) & SDL_WINDOW_MINIMIZED) { SDL_Delay(50); continue; }
        // while mapping (and just after), controller presses must not also click things here
        if (a.capture >= 0 || ImGui::GetTime() - a.captureAt < 0.5) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        else io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        draw_ui(a, io.DisplaySize, ren);
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 10, 8, 6, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
        SDL_RenderPresent(ren);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (g_removeSelf) remove_launcher_after_exit();
    return 0;
}
