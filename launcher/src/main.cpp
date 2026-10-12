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

#define LAUNCHER_VERSION "0.7 (test)"

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
// Backgrounds: public-domain paintings in ROTKFix\backgrounds and the player's
// own screenshots, shown one at a time with a slow cross-fade. Without any
// pictures, an original drawn landscape of misty mountains is used.

struct Picture { SDL_Texture *tex = nullptr; int w = 0, h = 0; };

struct Backgrounds {
    std::vector<std::string> files;
    int index = -1;
    Picture cur, prev;
    double changedAt = 0;
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
    return n == 0 || chroma / n < 6.0 || light / n < 12.0;
}

static Picture load_picture(SDL_Renderer *ren, const std::string &path)
{
    Picture p;
    int w, h, n;
    unsigned char *px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) return p;
    if (path.find("\\Screenshots\\") != std::string::npos && looks_like_text_screen(px, w, h)) { stbi_image_free(px); return p; }
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px, w * 4);
    if (s) {
        p.tex = SDL_CreateTextureFromSurface(ren, s);
        if (p.tex) { p.w = w; p.h = h; SDL_SetTextureScaleMode(p.tex, SDL_SCALEMODE_LINEAR); }
        SDL_DestroySurface(s);
    }
    stbi_image_free(px);
    return p;
}

static void backgrounds_next(Backgrounds &b, SDL_Renderer *ren)
{
    if (b.files.empty()) return;
    if (b.prev.tex) SDL_DestroyTexture(b.prev.tex);
    b.prev = b.cur;
    b.cur = Picture();
    for (size_t tries = 0; tries < b.files.size() && !b.cur.tex; tries++) {
        b.index = (b.index + 1) % (int)b.files.size();
        b.cur = load_picture(ren, b.files[b.index]);
    }
    b.changedAt = ImGui::GetTime();
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
        if (b.prev.tex && fade < 1.0f) draw_cover(dl, b.prev, size, 1.0f);
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

    section("Movies");
    static const char *md[] = { "Fit", "Stretch", "4:3" };
    row_begin("Movie display", "Fit: cutscenes fill the width with thin bars and correct proportions; logos and extras are shown 4:3.\n"
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

static void tab_about(App &a)
{
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
}

// ---------------------------------------------------------------------------
// Main screen

static void draw_ui(App &a, ImVec2 size, SDL_Renderer *ren)
{
    // change background every 15 s
    if (a.bg.files.size() > 1 && ImGui::GetTime() - a.bg.changedAt > 15.0) backgrounds_next(a.bg, ren);

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
    ImGui::TextColored(C_TEXT, "The Return of the King  -  PC");
    ImGui::PopFont();

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
    if (a.s.padEnabled)
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
    if (!g.exeFound) {
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
        float cardH = S(msg ? 54 : 32);
        ImVec2 c0 = ImGui::GetWindowPos() + ImVec2(pad, by + (playH - cardH) * 0.5f), c1 = c0 + ImVec2(cardW, cardH);
        ImGui::GetWindowDrawList()->AddRectFilled(c0, c1, ImGui::GetColorU32(C_PANEL), S(8));
        ImGui::GetWindowDrawList()->AddRect(c0, c1, ImGui::GetColorU32(C_LINE), S(8), 1.0f);
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::SetCursorPos(ImVec2(pad + S(10), by + (playH - cardH) * 0.5f + S(5)));
        ImGui::PushTextWrapPos(pad + cardW - S(8));
        ImGui::TextColored(C_DIM, "%d x %d  |  %s  |  %d fps  |  %s", w, h, dm[a.s.displayMode], fps, pn[detect_preset(a.s)]);
        if (msg) {
            ImGui::SetCursorPosX(pad + S(10));
            ImGui::TextColored(a.messageBad ? C_BAD : C_GOOD, "%s", a.message.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }

    ImGui::SetCursorPos(ImVec2(bx, by));
    ImGui::BeginDisabled(!g.exeFound || g_removeSelf);
    if (gold_button("PLAY", ImVec2(playW, playH), F_PLAY)) {
        if (save(a)) {
            std::string err;
            if (launch_game(a.dir, err)) a.quit = true;
            else set_message(a, err, true);
        }
    }
    ImGui::EndDisabled();
    bx -= saveW + S(12);
    ImGui::SetCursorPos(ImVec2(bx, by + (playH - btnH) * 0.5f));
    ImGui::BeginDisabled(!g.exeFound || g_removeSelf);
    if (ImGui::Button("Save", ImVec2(saveW, btnH))) save(a);
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
    a.dir = exe_dir();
    a.game = inspect_game(a.dir);
    a.monitors = enum_monitors();
    bool hadIni = load_settings(a.dir, a.s);
    if (!hadIni) apply_preset(a.s, PRESET_HIGH);
    a.s.renderer = 0;   // Vulkan always; the DLL falls back to Direct3D 8 by itself if Vulkan is unavailable
    if (a.s.monitor >= (int)a.monitors.size()) a.s.monitor = 0;
    // a saved fullscreen size this screen doesn't offer (any more): back to the screen's own
    if (a.s.displayMode == 1 && a.s.width > 0 && !mon(a).find(a.s.width, a.s.height)) a.s.width = a.s.height = 0;
    refresh_controllers(a);

    list_pictures(a.dir + "\\ROTKFix\\backgrounds", a.bg.files, 100);
    list_pictures(a.dir + "\\Screenshots", a.bg.files, 30);
    backgrounds_next(a.bg, ren);
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
