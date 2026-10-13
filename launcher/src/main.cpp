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
    st.WindowRounding = 0; st.ChildRounding = S(6); st.FrameRounding = S(4); st.PopupRounding = S(4);
    st.GrabRounding = S(3); st.TabRounding = S(4); st.ScrollbarRounding = S(6);
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
    ImU32 top = IM_COL32(26, 24, 28, 255), mid = IM_COL32(58, 46, 36, 255);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, size.y * 0.62f), top, top, mid, mid);
    dl->AddRectFilled(ImVec2(0, size.y * 0.62f), size, IM_COL32(14, 11, 9, 255));
    ImVec2 moon(size.x * 0.74f, size.y * 0.2f);
    for (int i = 10; i > 0; i--) dl->AddCircleFilled(moon, S(26) + i * S(14), IM_COL32(230, 210, 170, 5));
    dl->AddCircleFilled(moon, S(26), IM_COL32(236, 224, 196, 200));
    struct L { float base, amp, seed; ImU32 col; } layers[] = {
        { 0.40f, 0.16f, 1.3f, IM_COL32(52, 44, 40, 255) },
        { 0.50f, 0.13f, 4.1f, IM_COL32(38, 32, 28, 255) },
        { 0.62f, 0.10f, 7.7f, IM_COL32(24, 19, 15, 255) },
        { 0.74f, 0.07f, 2.9f, IM_COL32(14, 11, 9, 255) } };
    float step = S(6);
    for (auto &l : layers) {
        for (float x = 0; x < size.x; x += step) {
            float y0 = size.y * (l.base - l.amp * ridge(x / g_scale, l.seed));
            float y1 = size.y * (l.base - l.amp * ridge((x + step) / g_scale, l.seed));
            dl->AddQuadFilled(ImVec2(x, y0), ImVec2(x + step + 1, y1), ImVec2(x + step + 1, size.y), ImVec2(x, size.y), l.col);
        }
        // mist above each ridge line
        float my = size.y * l.base;
        dl->AddRectFilledMultiColor(ImVec2(0, my - size.y * 0.06f), ImVec2(size.x, my + size.y * 0.02f),
                                    IM_COL32(150, 140, 130, 0), IM_COL32(150, 140, 130, 0), IM_COL32(150, 140, 130, 22), IM_COL32(150, 140, 130, 22));
    }
}

static void draw_background(ImDrawList *dl, ImVec2 size, Backgrounds &b)
{
    if (b.cur.tex) {
        float t = (float)(ImGui::GetTime() - b.changedAt), fade = std::min(1.0f, t / 1.5f);
        dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(10, 8, 6, 255));
        if (fade < 1.0f) {
            if (b.prev.tex) draw_cover(dl, b.prev, size, 1.0f);
            else draw_landscape(dl, size);      // the first picture fades in over the drawn backdrop
        }
        draw_cover(dl, b.cur, size, fade);
    } else {
        draw_landscape(dl, size);
    }
    // darken for legibility: lighter at the top (banner), darker behind the settings
    ImU32 a0 = IM_COL32(8, 6, 4, 50), a1 = IM_COL32(8, 6, 4, 165);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), size, a0, a0, a1, a1);
    float m = S(8);
    dl->AddRect(ImVec2(m, m), ImVec2(size.x - m, size.y - m), IM_COL32(170, 134, 66, 150), 0.0f, S(1));
    dl->AddRect(ImVec2(m + S(4), m + S(4)), ImVec2(size.x - m - S(4), size.y - m - S(4)), IM_COL32(170, 134, 66, 64), 0.0f, S(1));
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

static void section(const char *title)
{
    ImGui::Spacing();
    ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
    ImGui::TextColored(C_GOLD, "%s", title);
    ImGui::PopFont();
    ImGui::Separator();
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
    ImGui::PushFont(font, font->LegacySize);
    ImGui::PushStyleColor(ImGuiCol_Button, rgb(158, 118, 48));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(192, 146, 64));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(124, 92, 38));
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(24, 16, 8));
    ImGui::PushStyleColor(ImGuiCol_Border, C_GOLD_H);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(6));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    ImGui::PopFont();
    return r;
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
    if (!save_settings(a.dir, a.s, err)) { set_message(a, err, true); return false; }
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
    row_begin("Frame rate", "The game was made for 60. Higher rates were tested and play at normal speed.\n"
                            "In fullscreen the screen switches to the matching refresh rate. In borderless and windowed, "
                            "rates that divide evenly into your screen's refresh rate are the smoothest.");
    static const int rates[] = { 60, 90, 120, 144, 165, 180, 240 };
    char c[64];
    snprintf(c, sizeof c, "%d fps", s.targetFps);
    if (ImGui::BeginCombo("##fps", c)) {
        for (int r : rates) {
            if (s.vsync && r > maxHz) continue;
            snprintf(c, sizeof c, "%d fps%s", r, r == 60 ? "  (original)" : "");
            if (ImGui::Selectable(c, s.targetFps == r)) s.targetFps = r;
        }
        if (std::find(std::begin(rates), std::end(rates), maxHz) == std::end(rates) && maxHz > 60) {
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
    char fov[48];
    snprintf(fov, sizeof fov, s.fovPct == 100 ? "%.0f%s  (game's own)" : "%.0f%s", hfov_deg(a, s.fovPct), "\xC2\xB0");
    row_begin("Field of view", "How wide the camera sees, in horizontal degrees. Wider shows more of the battle but makes characters look further away.");
    ImGui::SliderInt("##fov", &s.fovPct, 70, 150, fov);
    row_end();

    section("Game detail");
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
    row_begin("Skip opening logos", "Go straight to the menus at startup.");
    ImGui::Checkbox("##logos", &s.skipLogos);
    row_end();
    row_begin("Cutscene sound delay", "Bluetooth speakers and headphones play sound late. This holds the picture back to match (150-300 ms is typical). 0 = off.");
    ImGui::SliderInt("##adelay", &s.audioDelayMs, 0, 500, s.audioDelayMs == 0 ? "Off" : "%d ms");
    row_end();
    row_begin("Subtitles", nullptr);
    ImGui::Checkbox("##subs", &s.subtitles);
    row_end();
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
    assign_button(a.s, a.padPlayer, a.capture, ph);
    a.capture = a.capture + 1 < PAD_ACTION_COUNT ? a.capture + 1 : -1;
    a.captureAt = ImGui::GetTime();
}

static void tab_controls(App &a)
{
    Settings &s = a.s;
    section("Controller");
    row_begin("Use a controller", "Xbox, PlayStation, Switch and most other controllers work, wired or wireless.");
    ImGui::Checkbox("##pad", &s.padEnabled);
    if (s.padEnabled) {
        ImGui::SameLine(0, S(16));
        ImGui::AlignTextToFramePadding();
        if (a.controllers > 1) ImGui::TextColored(C_GOOD, "%d controllers connected", a.controllers);
        else if (a.controllers) ImGui::TextColored(C_GOOD, "Connected");
        else ImGui::TextColored(C_WARN, "None connected");
    }
    row_end();
    ImGui::BeginDisabled(!s.padEnabled);
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
    row_begin("Button names", "Which controller's buttons the game's hints and tutorials show, as pictures.\n"
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
    row_begin("3D sound and reverb", "Brings back the game's 3D sound and EAX reverb (echoes in caves and halls), "
                                     "which Windows stopped supporting after XP. Uses DSOAL and OpenAL Soft.\n"
                                     "Headphones adds virtual surround (HRTF): you can hear sounds above, behind and around you.");
    segmented("s3d", &s.sound3d, s3, 3);
    row_end();
    static int vcrt = -1;
    if (vcrt < 0) {
        // the game is 32-bit: look in SysWOW64 on 64-bit Windows, System32 on 32-bit Windows
        char sys[MAX_PATH] = "";
        if (!GetSystemWow64DirectoryA(sys, MAX_PATH)) GetSystemDirectoryA(sys, MAX_PATH);
        vcrt = GetFileAttributesA((std::string(sys) + "\\msvcp140.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    if (s.sound3d && !vcrt) {
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::SetCursorPosX(g_labelW);
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_WARN, "Needs the Microsoft Visual C++ Redistributable (x86) from Microsoft's website. Without it the game uses its original sound.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
    section("Volume");
    row_begin("Music", nullptr);  ImGui::SliderInt("##vm", &s.volMusic, 0, 100, "%d%%"); row_end();
    row_begin("Voices", nullptr); ImGui::SliderInt("##vv", &s.volVoice, 0, 100, "%d%%"); row_end();
    row_begin("Effects", nullptr); ImGui::SliderInt("##vs", &s.volSfx, 0, 100, "%d%%"); row_end();
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
}

static void tab_about(App &a)
{
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
// The port is found next to the launcher (TwoTowers\TwoTowers.exe), or where the
// player pointed to it (remembered in ROTKFix\ports.ini). Its settings, texture
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
    cand[0] = exe_dir() + "\\TwoTowers\\TwoTowers.exe";
    char b[MAX_PATH * 2] = "";
    GetPrivateProfileStringA("Ports", "TwoTowers", "", b, sizeof b, ports_ini().c_str());
    cand[1] = b;
    for (auto &c : cand) if (!c.empty() && file_there(c)) { a.tt.exe = c; break; }
    if (a.tt.exe.empty()) return;
    port_query(a.tt);
    a.ttLoaded = false;
}

static bool tt_ready(const App &a) { return !a.tt.exe.empty() && a.tt.err.empty() && !a.tt.settings.empty(); }

// The value of a setting: from the file, else its default
static std::string tt_val(const App &a, const PortSetting &ps) { return a.ttFile.get(ps.section, ps.key, ps.def); }
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

static bool tt_save(App &a)
{
    SHCreateDirectoryExA(NULL, tt_data_dir().c_str(), NULL);
    // the frame limit follows the frame rate
    if (const PortSetting *cap = tt_setting(a, "display", "fps_cap"))
        if (const PortSetting *fps = tt_setting(a, "graphics", "fps"))
            a.ttFile.set(cap->section, cap->key, std::to_string(atoi(toml_unquote(tt_val(a, *fps)).c_str())));
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
    a.runningGame = game_kind_name(g.kind);
    SDL_HideWindow(a.win);
}

// The port has closed: back to the launcher, saying why if it wasn't a normal quit
static void port_finished(App &a, DWORD code)
{
    std::string why = port_log_reason(a.runningLog);
    std::string logDir = a.runningLog.substr(0, a.runningLog.find_last_of('\\'));
    std::string m;
    switch (code) {
    case 0: break;
    case 1: m = "The game couldn't start" + (why.empty() ? std::string(".") : ": " + why + ".") + " Details are in tt.log in " + logDir + "."; break;
    case 2: m = "The game files couldn't be found there, or they aren't The Two Towers. Check the location on this page."; break;
    case 3: m = "This copy isn't the US release. Only the US version is supported for now."; break;
    case 4: m = "Direct3D 12 couldn't start. Update your graphics driver; the port needs a DirectX 12 graphics card."; break;
    case 5: m = "The settings file is damaged" + (why.empty() ? std::string("") : " (" + why + ")") + ". Use \"Reset to defaults\" below, then Play again."; break;
    default: {
        char b[32]; snprintf(b, sizeof b, "0x%08lX", (unsigned long)code);
        m = std::string("The game closed unexpectedly (") + b + "). Details are in tt.log in " + logDir + ".";
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
    if (ps.key == "anisotropic_filtering") return c == "0" ? "As the game asks" : c + "x";
    if (ps.key == "fps") return c + " fps";
    if (ps.key == "msaa") return c == "0" ? "Off" : c + "x";
    return cap_first(c);
}

// Screen size: one menu with the main screen's sizes (width and height are two settings in the file)
static void tt_resolution_row(App &a, const PortSetting &pw, const PortSetting &ph)
{
    int w = atoi(tt_val(a, pw).c_str()), h = atoi(tt_val(a, ph).c_str());
    const Monitor &m = a.monitors[0];
    char cur[64];
    if (w <= 0 || h <= 0) snprintf(cur, sizeof cur, "Desktop (%d x %d)", m.deskW, m.deskH);
    else snprintf(cur, sizeof cur, "%d x %d", w, h);
    row_begin("Resolution", "The size of the picture on screen. Desktop uses the main screen's own size.");
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
        for (size_t i = 0; i < ps.choices.size(); i++) { names.push_back(tt_choice_name(ps, ps.choices[i])); if (ps.choices[i] == cur) idx = (int)i; }
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
        if (ImGui::BeginCombo("##c", idx >= 0 ? names[idx].c_str() : cur.c_str())) {
            for (size_t i = 0; i < names.size(); i++)
                if (ImGui::Selectable(names[i].c_str(), (int)i == idx)) a.ttFile.set(ps.section, ps.key, toml_quote(ps.choices[i]));
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
        if (pw && ph && &ps == pw) { tt_resolution_row(a, *pw, *ph); continue; }
        if (pw && ph && &ps == ph) continue;
        if (tt_hidden(ps)) continue;
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
                if (t < 5) tt_settings_on_tab(a, t); else tt_about(a);
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
static std::string tt_summary(const App &a)
{
    if (!tt_ready(a)) return "The Two Towers  |  port not found";
    std::string s;
    const PortSetting *p;
    int w = 0, h = 0;
    if ((p = tt_setting(a, "display", "width"))) w = atoi(tt_val(a, *p).c_str());
    if ((p = tt_setting(a, "display", "height"))) h = atoi(tt_val(a, *p).c_str());
    if (w <= 0 || h <= 0) { w = a.monitors[0].deskW; h = a.monitors[0].deskH; }
    s = std::to_string(w) + " x " + std::to_string(h);
    if ((p = tt_setting(a, "display", "mode"))) s += "  |  " + cap_first(toml_unquote(tt_val(a, *p)));
    if ((p = tt_setting(a, "graphics", "fps"))) s += "  |  " + toml_unquote(tt_val(a, *p)) + " fps";
    if ((p = tt_setting(a, "graphics", "widescreen"))) s += tt_val(a, *p) == "true" ? "  |  Widescreen" : "  |  4:3";
    return s;
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
    section("Let's find your game");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("ROTK Fix has to be in the same folder as the game (the folder with rotk.exe), "
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
    ImGui::Dummy(ImVec2(0, S(10)));
    games_section(a);
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
    const char *curName = a.cur >= 0 ? game_kind_name(a.lib[a.cur].kind) : "The Return of the King  -  PC";
    if (a.lib.size() > 1) {
        // more than one game: the subtitle picks which one is shown
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(curName).x + ImGui::GetFrameHeight() + S(24));
        if (ImGui::BeginCombo("##game", curName)) {
            for (size_t i = 0; i < a.lib.size(); i++) {
                ImGui::PushID((int)i);
                bool sameKind = false;
                for (size_t j = 0; j < a.lib.size(); j++) if (j != i && a.lib[j].kind == a.lib[i].kind) sameKind = true;
                std::string label = game_kind_name(a.lib[i].kind);
                if (sameKind) label += "  (" + a.lib[i].path + ")";     // two copies of one version: say which
                if (ImGui::Selectable(label.c_str(), (int)i == a.cur)) a.cur = (int)i;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::TextColored(C_TEXT, "%s", curName);
    }
    ImGui::PopFont();
    const bool own = own_game(a);
    // The Two Towers shown, and its port ready: Play and Save work for it
    if (!own && a.lib[a.cur].kind == GK_TT_XBOX && !a.ttLooked) tt_find(a);
    const bool ttShown = !own && a.lib[a.cur].kind == GK_TT_XBOX && game_region_supported(a.lib[a.cur]) && a.ttLooked && tt_ready(a);

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
    if (!own) {   // another game is shown: just say what it is
        st.clear();
        const LibGame &lg = a.lib[a.cur];
        bool tt = lg.kind == GK_TT_XBOX;
        if (tt && !a.ttLooked) tt_find(a);
        bool ready = game_region_supported(lg) && (tt ? tt_ready(a) : game_kind_playable(lg.kind));
        st.push_back({ ready, true, ready ? "Ready" : !game_region_supported(lg) ? "Not supported" : tt ? "Port not found" : "Not playable yet", lg.path });
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
    ImGui::SetCursorPos(ImVec2(pad, bannerH));
    ImGui::BeginChild("panel", ImVec2(size.x - pad * 2, size.y - bannerH - barH - S(30)), ImGuiChildFlags_Borders);
    g_labelW = std::max(S(230), ImGui::GetContentRegionAvail().x * 0.36f);
    if (ttShown) {
        if (!a.ttLoaded) tt_load_settings(a);
        tt_tabs(a, a.lib[a.cur]);
    } else if (!own) {
        ImGui::BeginChild("other", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        draw_other_game(a);
        ImGui::Dummy(ImVec2(0, S(10)));
        games_section(a);
        ImGui::EndChild();
    } else if (!g.exeFound) {
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
        ImGui::GetWindowDrawList()->AddRectFilled(c0, c1, ImGui::GetColorU32(C_PANEL), S(8));
        ImGui::GetWindowDrawList()->AddRect(c0, c1, ImGui::GetColorU32(C_LINE), S(8), 1.0f);
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::SetCursorPos(ImVec2(pad + S(10), by + (playH - cardH) * 0.5f + S(5)));
        ImGui::PushTextWrapPos(pad + cardW - S(8));
        if (own) ImGui::TextColored(C_DIM, "%d x %d  |  %s  |  %d fps  |  %s", w, h, dm[a.s.displayMode], fps, pn[detect_preset(a.s)]);
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
    static bool autoPlay = getenv("ROTKFIX_PLAY") != nullptr;
    if (autoPlay && ttShown) { autoPlay = false; tt_play(a, a.lib[a.cur]); }
    ImGui::SetCursorPos(ImVec2(bx, by));
    ImGui::BeginDisabled(ttShown ? a.running != NULL : (!own || !g.exeFound || g_removeSelf));
    if (gold_button("PLAY", ImVec2(playW, playH), F_PLAY)) {
        if (ttShown) tt_play(a, a.lib[a.cur]);
        else if (save(a)) {
            std::string err;
            if (launch_game(a.dir, err)) a.quit = true;
            else set_message(a, err, true);
        }
    }
    ImGui::EndDisabled();
    bx -= saveW + S(12);
    ImGui::SetCursorPos(ImVec2(bx, by + (playH - btnH) * 0.5f));
    ImGui::BeginDisabled(ttShown ? false : (!own || !g.exeFound || g_removeSelf));
    if (ImGui::Button("Save", ImVec2(saveW, btnH))) { if (ttShown) tt_save(a); else save(a); }
    ImGui::EndDisabled();

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
