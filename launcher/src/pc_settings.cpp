// Athelas - The Return of the King (PC) page: its Display, Graphics, Controls, Audio, Gameplay and About tabs,
// and setting up ROTK Fix.
#include "app.h"

static const Monitor &mon(const App &a) { return a.monitors[std::min<size_t>(a.s.monitor, a.monitors.size() - 1)]; }

void effective_res(const App &a, int &w, int &h)
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

int effective_refresh(const App &a)
{
    return a.s.displayMode == 1 ? pick_refresh(refresh_list(a), a.s.targetFps) : mon(a).deskHz;
}

void refresh_controllers(App &a)
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

bool g_removeSelf;      // uninstalled from the launcher's own folder: delete the launcher when it closes

bool g_fixRemoved;      // ROTK Fix was just removed from the PC game shown (Play and Save are off)

bool g_xboxPage;           // the Return of the King page is showing the Xbox version (PC-only options greyed out)

// A note under a section whose options are the PC game's own and don't apply on Xbox
static void pc_only_note()
{
    if (!g_xboxPage) return;
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::TextColored(C_DIM, "PC version only.");
    ImGui::PopFont();
}

bool save(App &a)
{
    std::string err;
    a.s.refresh = a.s.displayMode == 1 ? pick_refresh(refresh_list(a), a.s.targetFps) : 0;
    {
        const Monitor &m = mon(a);
        std::string d = m.device;
        if (d.rfind("\\\\.\\", 0) == 0) d = d.substr(4);   // \\.\DISPLAY2 -> DISPLAY2
        a.s.screen = m.primary ? "main" : d;
    }
    if (!(g_xboxPage ? save_rotkfix_ini(a.dir, a.s, err) : save_settings(a.dir, a.s, err, a.controllers))) { set_message(a, err, true); return false; }
    a.game = inspect_game(a.dir);
    set_message(a, "Settings saved", false);
    return true;
}

// ---------------------------------------------------------------------------
// Tabs

void tab_display(App &a)
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
                { 3840, 1080, "  32:9 super ultrawide" }, { 2560, 720, "  32:9 super ultrawide" },
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

    // Render resolution, as on The Two Towers: the game is drawn at a share of the size above and
    // fitted to the screen. Each choice shows the size it draws at. Fullscreen shows it borderless.
    {
        int rw = s.width > 0 ? s.width : m.deskW, rh = s.width > 0 ? s.height : m.deskH;
        static const int pcts[] = { 50, 75, 100, 125, 150, 200 };
        auto sized = [&](int pct) {
            char b[64];
            // ROTK Fix keeps the picture within twice 4K's pixels (a 32-bit game); larger choices are reduced
            double cap = 3840.0 * 2160.0 * 2.0, px = (double)rw * rh * pct * pct / 10000.0;
            int use = px > cap ? (int)(100.0 * sqrt(cap / ((double)rw * rh))) : pct;
            snprintf(b, sizeof b, "%d%%%s  (%d x %d)%s", pct, pct == 100 && s.width <= 0 ? " (screen's own)" : "",
                     (int)lround(rw * use / 100.0) & ~1, (int)lround(rh * use / 100.0) & ~1, use != pct ? "  - the most this game can use" : "");
            return std::string(b);
        };
        std::string help = "The game is drawn at this share of the resolution above, then fitted to the screen. "
                           "Above 100% gives a cleaner, steadier picture (supersampling) and costs speed; below 100% is "
                           "faster but softer.";
        if (!g_xboxPage) help += "\nIn fullscreen, anything but 100% is shown borderless on the same screen.";
        row_begin("Render resolution", help.c_str());
        if (ImGui::BeginCombo("##render", sized(s.renderScale).c_str())) {
            for (int p : pcts)
                if (ImGui::Selectable(sized(p).c_str(), s.renderScale == p)) s.renderScale = p;
            ImGui::EndCombo();
        }
        row_end();
        if (s.renderScale != 100 && s.displayMode == 1 && !g_xboxPage) {   // ROTK Fix's way; the Xbox port draws its own picture
            bool own = s.width <= 0 || (s.width == m.deskW && s.height == m.deskH);
            bool ownHz = s.refresh <= 0 || s.refresh == m.deskHz;
            ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
            ImGui::SetCursorPosX(g_labelW);
            ImGui::PushTextWrapPos(0);
            if (own && ownHz) ImGui::TextColored(C_DIM, "Shown borderless at this render resolution: fullscreen can't scale the picture.");
            else ImGui::TextColored(C_WARN, "In fullscreen this only works at the screen's own resolution and refresh rate; "
                                            "otherwise the game stays at 100%%.");
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
        }
    }

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
    row_begin("Frame rate counter", g_xboxPage ? "Shows the frames per second in the top right corner of the screen."
                                               : "Shows the frames per second in the top left corner of the screen "
                                                 "(with the Vulkan renderer, the default).");
    ImGui::Checkbox("##showfps", &s.showFps);
    row_end();
    if (g_xboxPage) {   // ROTK on PC slows itself down in the background on its own
        section("In the background");
        row_begin("Pause in the background", "The game stops, picture and sound, while another window is in front "
                                             "(Alt-Tab), and carries on where it was when you come back.");
        ImGui::Checkbox("##pausebg", &s.pauseBg);
        row_end();
        row_begin("Mute in the background", "No sound while another window is in front; the game keeps running.");
        ImGui::Checkbox("##mutebg", &s.muteBg);
        row_end();
        row_begin("Controller in the background", "Controllers still play the game while another window is in front. "
                                                  "Off: only the window in front gets them.");
        ImGui::Checkbox("##bgpad", &s.bgController);
        row_end();
    }
}

static float hfov_deg(const App &a, int pct)
{
    int w, h; effective_res(a, w, h);
    float aspect = a.s.widescreenFix ? std::max((float)w / (float)h, 4.0f / 3.0f) : 4.0f / 3.0f;   // narrower screens keep the 4:3 width
    float v = 46.8f * pct / 100.0f * 3.14159265f / 180.0f;
    return 2.0f * atanf(tanf(v * 0.5f) * aspect) * 180.0f / 3.14159265f;
}

void tab_graphics(App &a)
{
    Settings &s = a.s;
    // DXVK-only options: off when the PC version will fall back to Windows' Direct3D 8
    // (no usable Vulkan driver). The Xbox port draws with its own renderer.
    GpuInfo gi = gpu_info();
    bool noVulkan = !g_xboxPage && (gi.state == GPU_NO_VULKAN || gi.state == GPU_VULKAN_TOO_OLD);
    bool vk = g_xboxPage || (s.renderer == 0 && !noVulkan);
    section("Quality");
    static const char *pnames[] = { "Original", "Balanced", "High", "Ultra", "Custom" };
    int p = detect_preset(s);
    row_begin("Preset", "Original: as it looked in 2003, no anti-aliasing or filtering.\n"
                        "Balanced: light anti-aliasing and filtering, for older graphics cards.\n"
                        "High: 4x anti-aliasing, sharp textures.\n"
                        "Ultra: 8x anti-aliasing and stronger sharpening. Heavy: at 1440p and above it can\n"
                        "stutter in foggy scenes above about 120 fps - use High for 144-240 fps.\n"
                        "Custom: shown when you change individual settings.");
    const bool en[5] = { true, true, true, true, false };
    int sel = p;
    if (segmented("preset", &sel, pnames, 5, en)) apply_preset(s, sel);
    row_end();
    if (!g_xboxPage) {
        // ROTK (PC): Balanced for built-in graphics, High otherwise, Ultra only where its heavy fog scenes stay smooth
        // (a very strong card, a screen up to 1440p at up to 120 Hz)
        const HwInfo &h = hw_info();
        int want = h.tier == 0 ? PRESET_BALANCED
                 : h.tier == 3 && h.screenW <= 2560 && h.refresh <= 120 ? PRESET_ULTRA : PRESET_HIGH;
        // Undo: the whole settings as they were (a Custom set too); offered until the preset is changed again
        static std::remove_reference_t<decltype(s)> before = s;
        static bool canUndo = false;
        static int applied = -1;
        if (canUndo && p != applied) canUndo = false;
        std::vector<std::string> changes;
        if (p != want) changes.push_back(std::string(tr("Preset")) + ": " + tr(pnames[want]));
        bool undo = false;
        if (rec_button("pc", changes, canUndo, &undo)) { before = s; apply_preset(s, want); applied = want; canUndo = true; }
        if (undo) { s = before; canUndo = false; }
    }

    section("Image");
    if (noVulkan) {
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_WARN, gi.state == GPU_VULKAN_TOO_OLD
            ? "Your graphics driver's Vulkan is too old for the Vulkan renderer (DXVK needs Vulkan 1.4), so the game uses "
              "Windows' own Direct3D 8 and the greyed-out options can't work. A newer graphics driver may fix this."
            : "No Vulkan graphics driver was found, so the game uses Windows' own Direct3D 8 and the greyed-out options "
              "can't work. Installing your graphics card maker's latest driver may fix this.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
    static const char *aa[] = { "Off", "2x", "4x", "8x" };
    int aai = s.msaa == 2 ? 1 : s.msaa == 4 ? 2 : s.msaa == 8 ? 3 : 0;
    row_begin("Anti-aliasing", "Smooths jagged edges (MSAA). 8x is heavy at high frame rates;\n"
                               "4x looks almost the same.");
    if (segmented("aa", &aai, aa, 4)) s.msaa = aai == 0 ? 0 : 1 << aai;
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
    if (g_xboxPage) {   // the Xbox port draws text, title and main menu with the PC version's larger fonts and pictures, read from the PC game's files here
        static int pcFonts = -1, pcMenus = -1;
        static std::string checkedDir;
        if (checkedDir != a.dir) { checkedDir = a.dir; pcFonts = -1; }   // per game folder
        auto there = [&](const char *rel) { DWORD at = GetFileAttributesA((a.dir + rel).c_str()); return (int)(at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_DIRECTORY)); };
        if (pcFonts < 0) { pcFonts = there("\\Data\\Game\\GlobScen.scw"); pcMenus = there("\\Data\\Game\\fro\\fro03.scw"); }
        ImGui::BeginDisabled(!pcFonts);
        row_begin("Sharper text and menus",
                  !pcFonts ? "Needs the PC version: it uses the PC game's fonts and menu pictures (Data\\Game\\GlobScen.scw and "
                             "Data\\Game\\fro\\fro03.scw in this folder)."
                  : pcMenus ? "Draws the game's text with the PC version's larger, sharper fonts, and the title and main menu "
                              "backgrounds with the PC's larger pictures."
                            : "Draws the game's text with the PC version's larger, sharper fonts. (The PC's menu pictures, "
                              "Data\\Game\\fro\\fro03.scw, weren't found, so the menus stay as on the Xbox.)");
        bool on = s.sharperText && pcFonts;
        if (ImGui::Checkbox("##sharptext", &on)) s.sharperText = on;
        row_end();
        ImGui::EndDisabled();
    }
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
    // Zoom (the 4:3 movies fill the width too, their top and bottom cut off) is the Xbox port's; ROTK Fix has the three
    static const char *md[] = { "Fit", "Stretch", "4:3", "Zoom" };
    std::string mdh = "Fit: cutscenes and the opening logos fill the width with correct proportions; the trailer is shown 4:3.\n"
                      "Stretch: the whole movie frame is stretched to the screen (thick bars, wider picture).\n"
                      "4:3: every movie in its original frame with bars at the sides.";
    if (g_xboxPage) mdh += "\nZoom: as Fit, and the trailer fills the width too, its top and bottom cut off.";
    else if (s.movieDisplay > 2) s.movieDisplay = 0;
    row_begin("Movie display", mdh.c_str());
    segmented("md", &s.movieDisplay, md, g_xboxPage ? 4 : 3);
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
    if (g_xboxPage && native_arm64()) {   // Windows on ARM only: see the README's Windows on ARM section
        section("Windows on ARM");
        row_begin("One processor core", "The game's own threads take turns on one processor core, as they did on the "
                                        "Xbox, instead of running side by side. A little slower. Try it if the game "
                                        "crashes at random on this PC.");
        ImGui::Checkbox("##onecore", &s.oneCore);
        row_end();
    }
}

// Which brand's names to show for buttons in the launcher
int label_style(const App &a)
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

// LT + RT together switches fullscreen (WinMain): the hint in the controller's own names
const char *fullscreen_hint(int ls)
{
    return ls == NAMES_PS ? "L2 + R2: fullscreen" : ls == NAMES_NINTENDO ? "ZL + ZR: fullscreen" : "LT + RT: fullscreen";
}

const char *phys_label(int ph, int style)
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
void capture_event(App &a, const SDL_Event &e)
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
        // stop (and the time it stopped, so the same press isn't taken as Play or Save once the controller moves the cursor again)
        case SDL_GAMEPAD_BUTTON_START: case SDL_GAMEPAD_BUTTON_BACK: a.capture = -1; a.captureAt = ImGui::GetTime(); return;
        default: return;
        }
    } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.value > 20000) {
        int k = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 0 : e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? 1 : -1;
        if (k < 0 || a.trigDown[k]) return;
        a.trigDown[k] = true;
        ph = k ? PH_RT : PH_LT;
    } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) { a.capture = -1; a.captureAt = ImGui::GetTime(); return; }
    else return;
    // ignore the press that started it (A on a controller clicking the slot)
    if (ImGui::GetTime() - a.captureAt < 0.25) return;
    int count = PAD_ACTION_COUNT;
    if (a.captureTT) { count = tt_button_count(a); if (!tt_assign_button(a, a.capture, ph)) return; }   // a button this move can't use: keep waiting
    else assign_button(a.s, a.padPlayer, a.capture, ph);
    a.capture = a.capture + 1 < count ? a.capture + 1 : -1;
    a.captureAt = ImGui::GetTime();
}

// ---------------------------------------------------------------------------
// Keyboard and mouse for the Xbox ports: one key or mouse button per move, set by pressing it. The names are the
// ports' own (keys.h in their SDK): what SDL calls a key here is turned into that name.

static const char *kb_name_of_scancode(SDL_Scancode sc)
{
    static char one[2];
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) { one[0] = (char)('A' + (sc - SDL_SCANCODE_A)); one[1] = 0; return one; }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) { one[0] = (char)('1' + (sc - SDL_SCANCODE_1)); one[1] = 0; return one; }
    switch (sc) {
    case SDL_SCANCODE_0: return "0";
    case SDL_SCANCODE_F1: return "F1"; case SDL_SCANCODE_F2: return "F2"; case SDL_SCANCODE_F4: return "F4";
    case SDL_SCANCODE_F5: return "F5"; case SDL_SCANCODE_F6: return "F6"; case SDL_SCANCODE_F7: return "F7";
    case SDL_SCANCODE_F8: return "F8"; case SDL_SCANCODE_F11: return "F11";  // F9: the ports' fast forward
    case SDL_SCANCODE_F12: return "F12";
    case SDL_SCANCODE_SPACE: return "Space"; case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return "Enter";
    case SDL_SCANCODE_TAB: return "Tab"; case SDL_SCANCODE_BACKSPACE: return "Backspace"; case SDL_SCANCODE_CAPSLOCK: return "CapsLock";
    case SDL_SCANCODE_LSHIFT: return "LShift"; case SDL_SCANCODE_RSHIFT: return "RShift";
    case SDL_SCANCODE_LCTRL: return "LCtrl"; case SDL_SCANCODE_RCTRL: return "RCtrl";
    case SDL_SCANCODE_LALT: return "LAlt"; case SDL_SCANCODE_RALT: return "RAlt";
    case SDL_SCANCODE_UP: return "Up"; case SDL_SCANCODE_DOWN: return "Down"; case SDL_SCANCODE_LEFT: return "Left"; case SDL_SCANCODE_RIGHT: return "Right";
    case SDL_SCANCODE_INSERT: return "Insert"; case SDL_SCANCODE_DELETE: return "Delete"; case SDL_SCANCODE_HOME: return "Home";
    case SDL_SCANCODE_END: return "End"; case SDL_SCANCODE_PAGEUP: return "PageUp"; case SDL_SCANCODE_PAGEDOWN: return "PageDown";
    case SDL_SCANCODE_KP_0: return "Numpad0"; case SDL_SCANCODE_KP_1: return "Numpad1"; case SDL_SCANCODE_KP_2: return "Numpad2";
    case SDL_SCANCODE_KP_3: return "Numpad3"; case SDL_SCANCODE_KP_4: return "Numpad4"; case SDL_SCANCODE_KP_5: return "Numpad5";
    case SDL_SCANCODE_KP_6: return "Numpad6"; case SDL_SCANCODE_KP_7: return "Numpad7"; case SDL_SCANCODE_KP_8: return "Numpad8";
    case SDL_SCANCODE_KP_9: return "Numpad9"; case SDL_SCANCODE_KP_MULTIPLY: return "NumpadStar"; case SDL_SCANCODE_KP_PLUS: return "NumpadPlus";
    case SDL_SCANCODE_KP_MINUS: return "NumpadMinus"; case SDL_SCANCODE_KP_PERIOD: return "NumpadDot"; case SDL_SCANCODE_KP_DIVIDE: return "NumpadSlash";
    case SDL_SCANCODE_MINUS: return "Minus"; case SDL_SCANCODE_EQUALS: return "Equals"; case SDL_SCANCODE_COMMA: return "Comma";
    case SDL_SCANCODE_PERIOD: return "Period"; case SDL_SCANCODE_SLASH: return "Slash"; case SDL_SCANCODE_SEMICOLON: return "Semicolon";
    case SDL_SCANCODE_APOSTROPHE: return "Apostrophe"; case SDL_SCANCODE_GRAVE: return "Grave";
    default: return nullptr;   // [ ] \ F3 F10 are the ports' own keys, and the rest aren't read
    }
}

// a key as the Controls tab shows it
static std::string kb_display(const std::string &list)
{
    static const struct { const char *from, *to; } k[] = {
        { "Mouse1", "Left mouse" }, { "Mouse2", "Right mouse" }, { "Mouse3", "Middle mouse" }, { "Mouse4", "Mouse 4" },
        { "Mouse5", "Mouse 5" }, { "LCtrl", "Left Ctrl" }, { "RCtrl", "Right Ctrl" }, { "LShift", "Left Shift" },
        { "RShift", "Right Shift" }, { "LAlt", "Left Alt" }, { "RAlt", "Right Alt" }, { "Grave", "` (left of 1)" },
        { "CapsLock", "Caps Lock" }, { "PageUp", "Page Up" }, { "PageDown", "Page Down" } };
    std::string out;
    size_t st = 0;
    while (st <= list.size()) {
        size_t c = list.find(',', st);
        std::string one = list.substr(st, c == std::string::npos ? std::string::npos : c - st);
        st = c == std::string::npos ? list.size() + 1 : c + 1;
        while (!one.empty() && one.front() == ' ') one.erase(one.begin());
        while (!one.empty() && one.back() == ' ') one.pop_back();
        if (one.empty()) continue;
        for (auto &x : k) if (!_stricmp(x.from, one.c_str())) { one = x.to; break; }
        if (!out.empty()) out += ", ";
        out += one;
    }
    return out.empty() ? "Nothing" : out;
}

// SDL events while a move waits for its key: true takes the event (it doesn't also click in the launcher)
bool kb_capture_event(App &a, const SDL_Event &e)
{
    if (a.kbCapture < 0) return false;
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) { a.kbCapture = -1; return true; }   // B: stop
    if (e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_MOUSE_BUTTON_DOWN) return false;
    if (ImGui::GetTime() - a.kbCaptureAt < 0.25) return e.type == SDL_EVENT_KEY_DOWN;   // the click that started it
    const char *name = nullptr;
    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.scancode == SDL_SCANCODE_ESCAPE) { a.kbCapture = -1; return true; }   // stop, nothing changed
        name = kb_name_of_scancode(e.key.scancode);
    } else {
        static const char *b[] = { nullptr, "Mouse1", "Mouse3", "Mouse2", "Mouse4", "Mouse5" };   // SDL: 1 left, 2 middle, 3 right
        if (e.button.button < 6) name = b[e.button.button];
    }
    if (!name) return true;   // a key the ports don't read: keep waiting
    a.kbResult = name;
    a.kbResultFor = a.kbCapture;
    a.kbCapture = -1;
    return true;
}

// The keyboard rows: enabled, player, and each move's keys (labels and defaults per game); true when changed
bool keyboard_section(App &a, bool &enabled, int &player, std::string *keys, const std::string *labels,
                             const std::string *defaults, int n)
{
    bool changed = false;
    section("Keyboard and mouse");
    row_begin("Keyboard and mouse", "Play with the keyboard and mouse as well as a controller. The keys work like "
                                    "the controller's buttons; the game's hints still show the controller's.");
    changed |= ImGui::Checkbox("##kbon", &enabled);
    row_end();
    ImGui::BeginDisabled(!enabled);
    static const char *pl[] = { "Player 1", "Player 2" };
    int p = player - 1;
    row_begin("Keyboard player", "Which player the keyboard and mouse play as. Player 2 lets one person use the "
                                 "keyboard in co-op while the other uses a controller.");
    if (segmented("kbplayer", &p, pl, 2)) { player = p + 1; changed = true; }
    row_end();
    if (a.kbResultFor >= 0 && a.kbResultFor < n) { keys[a.kbResultFor] = a.kbResult; a.kbResultFor = -1; changed = true; }
    a.kbCaptureFrame = ImGui::GetFrameCount();
    if (a.kbCapture >= 0 && ImGui::GetTime() - a.kbCaptureAt > 8.0) a.kbCapture = -1;
    float slotW = S(230);
    for (int i = 0; i < n; i++) {
        if (keys[i].empty() && defaults[i].empty()) continue;   // nothing the game does with it
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(labels[i].c_str());
        ImGui::SameLine(g_labelW);
        bool waiting = a.kbCapture == i;
        std::string lab = (waiting ? std::string("Press a key...") : kb_display(keys[i])) + "##key";
        if (waiting) {
            float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 6.0);
            ImGui::PushStyleColor(ImGuiCol_Button, rgb(110 + (int)(40 * pulse), 82 + (int)(30 * pulse), 36));
            ImGui::PushStyleColor(ImGuiCol_Border, C_GOLD_H);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, rgb(34, 27, 20));
            ImGui::PushStyleColor(ImGuiCol_Border, C_LINE);
        }
        if (ImGui::Button(lab.c_str(), ImVec2(slotW, 0))) {
            if (waiting) a.kbCapture = -1;
            else { a.kbCapture = i; a.kbCaptureAt = ImGui::GetTime(); }
        }
        ImGui::PopStyleColor(2);
        if (!keys[i].empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) { keys[i].clear(); changed = true; if (waiting) a.kbCapture = -1; }
        }
        ImGui::PopID();
    }
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(a.kbCapture >= 0 ? C_GOLD_H : C_DIM, "%s", a.kbCapture >= 0
        ? "Press the key or mouse button for it. Esc stops."
        : "Click a move, then press the key or mouse button you want. F10 opens the settings menu in the game.");
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    bool isDefault = true;
    for (int i = 0; i < n; i++) if (keys[i] != defaults[i]) isDefault = false;
    ImGui::BeginDisabled(isDefault);
    if (ImGui::Button("Default keys")) { for (int i = 0; i < n; i++) keys[i] = defaults[i]; a.kbCapture = -1; changed = true; }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    return changed;
}

void tab_controls(App &a)
{
    Settings &s = a.s;
    section("Controller");
    // on Xbox the controller is the game's own input, so this switch is the PC version's only
    bool padOn = s.padEnabled || g_xboxPage;
    // the Xbox version has no switch: a controller always works there, so the row only says whether one is in
    row_begin(g_xboxPage ? "Controller" : "Use a controller",
              g_xboxPage ? "Any controller you plug in works, as well as the keyboard and mouse."
                         : "Xbox, PlayStation, Switch and most other controllers work, wired or wireless.");
    if (!g_xboxPage) ImGui::Checkbox("##pad", &s.padEnabled);
    if (padOn) {
        if (!g_xboxPage) ImGui::SameLine(0, S(16));
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
    a.captureFrame = ImGui::GetFrameCount();
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
    static const char *bn[] = { "Automatic", "Xbox", "PlayStation", "Nintendo", "Original (\"Button 7\")", "Keyboard keys" };
    const bool bnOn[] = { true, true, true, true, true, g_xboxPage };   // key pictures: the Xbox version's keyboard play
    row_begin("Button names", g_xboxPage ? "Which controller's buttons the game's hints and tutorials show, as pictures.\n"
                                           "Automatic follows the controller you play with, or shows keyboard keys when none is plugged in. "
                                           "\"Original\" keeps the Xbox pictures."
                                         : "Which controller's buttons the game's hints and tutorials show, as pictures.\n"
                                           "Automatic follows the controller you play with. \"Original\" keeps the game's own text.");
    segmented("names", &s.buttonNames, bn, 6, bnOn);
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
    if (g_xboxPage) {   // on PC the game has keyboard controls of its own
        static std::string labels[KB_ACTION_COUNT], defaults[KB_ACTION_COUNT];
        for (int k = 0; k < KB_ACTION_COUNT; k++) { labels[k] = k_kbLabel[k]; defaults[k] = k_kbDefault[k]; }
        keyboard_section(a, s.kbEnabled, s.kbPlayer, s.kbKeys, labels, defaults, KB_ACTION_COUNT);
        section("Mouse");
        row_begin("Mouse aiming", "While the ranged attack is held, moving the mouse aims, as the left stick does on a controller.");
        ImGui::Checkbox("##mouseaim", &s.mouseAim);
        row_end();
        row_begin("Mouse sensitivity", "How far a mouse movement turns the aim (100% is the default).");
        ImGui::SliderInt("##mousesens", &s.mouseSens, 10, 400, "%d%%");
        row_end();
        row_begin("Invert mouse up/down", "Reverses up and down for mouse aiming: moving the mouse forward aims down.");
        ImGui::Checkbox("##mouseinv", &s.mouseInvertY);
        row_end();
        row_begin("Mouse in the menus", "In the game's menus the mouse wheel moves up and down, a left click chooses and a right click goes back.");
        ImGui::Checkbox("##mousemenus", &s.mouseMenus);
        row_end();
    }
}

void tab_audio(App &a)
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

// ---------------------------------------------------------------------------
// The player's games

// a.dir is the PC game folder whose ROTK Fix settings are loaded (ROTKFix.ini there), or the
// launcher's own folder when there is none. The launcher itself can be anywhere: setting up
// the PC game puts only ROTK Fix's files in the game's folder (install_to).
// Is the game shown that PC game (or no game yet)?
bool own_game(const App &a)
{
    return a.cur < 0 || (a.lib[a.cur].kind == GK_ROTK_PC && !_stricmp(a.lib[a.cur].path.c_str(), a.dir.c_str()));
}

// Load the settings of the PC game in 'dir' (or of the launcher's folder): at start, after
// setting ROTK Fix up somewhere, and when the player switches to another PC copy.
bool load_pc_settings(App &a, const std::string &dir)
{
    a.dir = dir;
    a.game = inspect_game(a.dir);
    a.s = Settings();
    bool hadIni = load_settings(a.dir, a.s);
    if (!hadIni) { apply_preset(a.s, PRESET_HIGH); a.firstRunPreset = true; }
    // (Renderer as the settings file has it: Vulkan unless the player set native by hand. ROTK Fix checks for a
    // Vulkan 1.4 device before it loads DXVK and uses Direct3D 8 without one, #27.)
    // the saved screen by name (the order of screens can change)
    for (size_t i = 0; i < a.monitors.size(); i++) {
        const Monitor &m = a.monitors[i];
        bool match = _stricmp(a.s.screen.c_str(), "main") ? m.device.size() > 4 && !_stricmp(m.device.c_str() + 4, a.s.screen.c_str())
                                                          : m.primary;
        if (match) { a.s.monitor = (int)i; break; }
    }
    if (a.s.monitor >= (int)a.monitors.size()) a.s.monitor = 0;
    // a saved fullscreen size this screen doesn't offer (any more): back to the screen's own
    if (!a.monitors.empty() && a.s.displayMode == 1 && a.s.width > 0 && !mon(a).find(a.s.width, a.s.height)) a.s.width = a.s.height = 0;
    g_fixRemoved = false;
    return hadIni;
}

bool fix_installed(const GameInfo &g) { return g.hasDll && g.hasProxyD3D8; }

// Saving a protected game folder (Program Files) needs administrator rights: start the
// launcher again with Windows' permission prompt, on that game. False if declined.
static bool restart_elevated_for(App &a, const std::string &dir)
{
    if (!start_launcher(exe_dir(), true, "--elevated --pc \"" + dir + "\"")) return false;
    a.quit = true;
    return true;
}

// Show another PC copy that has ROTK Fix: its settings are loaded from its folder.
void use_pc_game(App &a, const std::string &dir)
{
    if (!folder_writable(dir) && restart_elevated_for(a, dir)) return;
    load_pc_settings(a, dir);
    if (!folder_writable(dir)) set_message(a, "This game folder is protected by Windows, so settings can't be saved. "
                                              "Open the launcher again and allow the permission prompt.", true);
}

void add_game_from(App &a, const std::string &path)
{
    LibGame g;
    if (detect_game_at(path, g) == GK_NONE) {
        set_message(a, g.iso ? "That disc image isn't The Fellowship of the Ring, The Two Towers or The Return of the King for Xbox."
                             : "No supported game there: choose the folder with rotk.exe, or an extracted Xbox game (with default.xbe).", true);
        return;
    }
    a.cur = library_add(a.lib, g);
    library_save(exe_dir(), a.lib);
    set_message(a, std::string("Added: ") + game_kind_name(g.kind), false);
}

// ---- Gameplay: both ROTK versions read [Gameplay] (ROTK Fix on PC, the port on Xbox)
// ---------------------------------------------------------------------------
// Achievements, as each game keeps them in its achievements.txt ([unlocked] id = date, [counters], [list] id =
// title | description), shown on the Gameplay tabs. The games earn them; the launcher only reads the file.

// The games' Discord applications aren't registered yet (each needs an ID from the Discord developer portal);
// until they are, the Discord status stays out of the launcher and the games' menus.
static const bool k_discordReady = false;

struct AchItem { std::string id, title, desc, when; };

struct AchFile { std::vector<AchItem> items; unsigned long long kills = 0; bool found = false; };

static AchFile read_achievements(const std::string &path)
{
    static std::string cachedPath;
    static AchFile cached;
    static double readAt = -10;
    if (path == cachedPath && ImGui::GetTime() - readAt < 2.0) return cached;
    cachedPath = path; readAt = ImGui::GetTime();
    AchFile f;
    FILE *fp = fopen(path.c_str(), "rb");
    if (fp) {
        f.found = true;
        std::map<std::string, std::string> when;
        char line[512], sec[32] = "";
        while (fgets(line, sizeof line, fp)) {
            std::string l = line;
            while (!l.empty() && (l.back() == '\r' || l.back() == '\n' || l.back() == ' ')) l.pop_back();
            size_t a0 = l.find_first_not_of(" \t");
            if (a0 == std::string::npos || l[a0] == ';') continue;
            l = l.substr(a0);
            if (l[0] == '[') { snprintf(sec, sizeof sec, "%s", l.c_str()); continue; }
            size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string k = l.substr(0, eq), v = l.substr(eq + 1);
            while (!k.empty() && k.back() == ' ') k.pop_back();
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
            if (!strcmp(sec, "[unlocked]")) when[k] = v;
            else if (!strcmp(sec, "[counters]") && k == "enemies_defeated") f.kills = strtoull(v.c_str(), nullptr, 10);
            else if (!strcmp(sec, "[list]")) {
                size_t bar = v.find(" | ");
                f.items.push_back({ k, bar == std::string::npos ? v : v.substr(0, bar), bar == std::string::npos ? "" : v.substr(bar + 3), "" });
            }
        }
        fclose(fp);
        for (auto &it : f.items) { auto w = when.find(it.id); if (w != when.end()) it.when = w->second; }
    }
    cached = f;
    return f;
}

void achievements_section(const std::string &path, bool header)
{
    AchFile f = read_achievements(path);
    int got = 0;
    for (auto &it : f.items) got += !it.when.empty();
    char head[64];
    if (f.items.empty()) snprintf(head, sizeof head, "Achievements");
    else snprintf(head, sizeof head, "Achievements  -  %d of %d earned", got, (int)f.items.size());
    if (header) section(head);   // otherwise the list goes on under the settings' own Achievements heading
    else if (!f.items.empty()) ImGui::TextColored(C_GOLD_H, "%d of %d earned", got, (int)f.items.size());
    ImGui::PushTextWrapPos(0);
    if (!f.found || f.items.empty()) {
        ImGui::TextColored(C_DIM, "Play the game once and its achievements appear here. They're earned as you play, "
                                  "including what your saved progress already holds.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (f.kills) ImGui::TextColored(C_DIM, "Enemies defeated: %llu", f.kills);
    for (auto &it : f.items) {
        bool on = !it.when.empty();
        ImGui::TextColored(on ? C_GOLD_H : C_DIM, "%s %s", on ? "+" : "-", it.title.c_str());
        if (on) { ImGui::SameLine(); ImGui::PushFont(F_SMALL, F_SMALL->LegacySize); ImGui::TextColored(C_DIM, "  %s", it.when.c_str()); ImGui::PopFont(); }
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::Indent(S(18));
        ImGui::TextColored(C_DIM, "%s", it.desc.c_str());
        ImGui::Unindent(S(18));
        ImGui::PopFont();
    }
    ImGui::PopTextWrapPos();
}

void tab_gameplay(App &a)
{
    Settings &s = a.s;
    section("Difficulty");
    row_begin("Damage taken", "How much damage you take, compared with the game. Works with the game's own difficulty "
                              "setting: 50% halves the damage at any difficulty, 200% doubles it.");
    ImGui::SliderInt("##dmgtaken", &s.damageTaken, 10, 500, s.damageTaken == 100 ? "100%%  (game's own)" : "%d%%");
    row_end();
    row_begin("Enemy damage", "How much damage your hits do to enemies, compared with the game. Fights between other "
                              "characters, such as your allies and their enemies, stay as they are.");
    ImGui::SliderInt("##dmgenemy", &s.enemyDamage, 10, 500, s.enemyDamage == 100 ? "100%%  (game's own)" : "%d%%");
    row_end();
    section("Unlocks");
    row_begin("Unlock all levels and characters", "Every level, the secret one included, and every character can be "
                                                  "chosen from the start, as in the game's developer builds. Turning it "
                                                  "off again shows only what you've earned.");
    ImGui::Checkbox("##unlockall", &s.unlockAll);
    row_end();
    {   // both versions: the Xbox port's own hook, ROTK Fix's on PC (gameplay.c)
        row_begin("All heroes in completed levels", "In a level you've already completed, the character select offers "
                                                    "every hero, as the game does once it's beaten. A level you haven't "
                                                    "completed yet keeps the game's choice, and Helm's Deep stays "
                                                    "Gandalf's. With Unlock all levels and characters on, every hero is "
                                                    "offered everywhere but Gimli at Helm's Deep, which doesn't load.");
        ImGui::Checkbox("##allheroes", &s.allHeroes);
        row_end();
    }
    if (g_xboxPage) {   // the Xbox port's own (rotk_eowyn.cpp)
        row_begin("Eowyn instead of Faramir", "Éowyn takes Faramir's place: her figure and name on the character "
                                             "select, her face on the HUD, and her in every level he can play, with "
                                             "his moves, combos, bow and upgrades. Made from her own model in "
                                             "Pelennor Fields when the game starts (a few seconds more). She has no "
                                             "voice of her own, so she fights quietly (his shouts are left out).");
        ImGui::Checkbox("##eowyn", &s.eowyn);
        row_end();
    }
    if (g_xboxPage) {   // the Xbox port's subtitles (rotk_lang.cpp); ROTK on PC has no such hooks
        section("Language");
        row_begin("Translation subtitles", "The game's menus, prompts and spoken lines translated at the bottom of "
                                          "the screen, in place of the game's English subtitles. The game's own text "
                                          "stays as it is. Our own translations.");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##subtitles", k_subtitleNames[s.subLang >= 0 && s.subLang < 10 ? s.subLang : 0])) {
            for (int i = 0; i < 10; i++)
                if (ImGui::Selectable(k_subtitleNames[i], s.subLang == i)) s.subLang = i;
            ImGui::EndCombo();
        }
        row_end();
    }
    if (g_xboxPage) {
        section("Cheats");
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_DIM, "The game's own secret codes, without entering them on the pause screen.");
        ImGui::PopTextWrapPos();
        row_begin("Invulnerable", "Enemies can't hurt you.");
        ImGui::Checkbox("##chinv", &s.cheatInvulnerable);
        row_end();
        row_begin("Infinite missiles", "Arrows and throwing weapons never run out.");
        ImGui::Checkbox("##chmis", &s.cheatMissiles);
        row_end();
        row_begin("Always devastating", "Every attack is a devastating one.");
        ImGui::Checkbox("##chdev", &s.cheatDevastating);
        row_end();
        row_begin("Perfect mode", "The game's Perfect Mode code.");
        ImGui::Checkbox("##chperf", &s.cheatPerfect);
        row_end();
    }
    section(k_discordReady ? "Achievements and Discord" : "Achievements");
    row_begin("Achievement notes", "A note in the corner of the screen when you earn an achievement. The PC version shows it with "
                                   "the Vulkan renderer (the default).");
    ImGui::Checkbox("##achnotes", &s.achNotify);
    row_end();
    if (k_discordReady) {
    row_begin("Discord status", "Shows on your Discord profile that you're playing, and which level and hero, while Discord is "
                                "running on this PC. Nothing is sent anywhere else.");
    ImGui::Checkbox("##discord", &s.discord);
    row_end();
    }
    achievements_section(g_xboxPage ? rx_data_dir() + "\\achievements.txt" : a.dir + "\\ROTKFix\\achievements.txt", false);
}

void tab_about(App &a)
{
    games_section(a);
    section("Setup");
    const GameInfo &g = a.game;
    status_dot(g.exeFound && g.versionOk, g.exeFound, g.exeFound ? g.versionText.c_str() : "rotk.exe not found - choose your game folder on the About tab", nullptr);
    {   // the ROTK Fix in the game folder, and the one this Athelas folder installs from: both its own version
        std::string inGame = g.hasDll ? fix_dll_version(a.dir + "\\ROTKFix.dll") : "";
        std::string carried = fix_dll_version(pc_fix_source() + "\\ROTKFix.dll");
        bool files = g.hasDll && g.hasProxyD3D8 && g.hasProxyDinput;
        std::string t = "ROTK Fix files" + (inGame.empty() ? std::string() : " (" + inGame + ")");
        bool current = inGame.empty() || carried.empty() || inGame == carried;
        status_dot(files && current, files, t.c_str(), current ? nullptr : "Press Play to update ROTK Fix in the game folder.");
        if (!carried.empty() && carried != LAUNCHER_VERSION && std::string(LAUNCHER_VERSION) != "dev") {
            std::string w = "This Athelas folder's ROTK Fix is version " + carried + ", not " LAUNCHER_VERSION
                            ". Unzip the whole Athelas " LAUNCHER_VERSION " download over this folder.";
            status_dot(false, true, w.c_str(), "Its ROTK-PC\\pcfix folder is what Play and Set up copy into your game.");
        }
    }
    status_dot(g.hasDxvk, false, "Vulkan renderer (DXVK)", nullptr);
    {
        GpuInfo gi = gpu_info();
        std::string t = gi.state == GPU_CHECKING ? "Checking the graphics card..."
                      : gi.state == GPU_VULKAN_OK ? "Graphics: " + gi.name + (gi.software ? " (software)" : gi.integrated ? " (integrated)" : "")
                      : gi.state == GPU_VULKAN_TOO_OLD ? "Graphics: " + gi.name + " - Vulkan too old for DXVK; Direct3D 8 is used"
                      : "No Vulkan driver - Direct3D 8 is used";
        status_dot(gi.state == GPU_VULKAN_OK, gi.state == GPU_CHECKING, t.c_str(),
                   gi.state == GPU_VULKAN_OK ? nullptr : "Sharpening, colour looks and texture filtering need the "
                                                         "Vulkan renderer. A newer graphics driver may make it available.");
    }
    status_dot(g.hasSdl, false, "Controller support (SDL3)", nullptr);
    status_dot(g.rotkIniExists, true, g.rotkIniExists ? "Game settings file found" : "Game settings file will be created", g.rotkIniPath.c_str());
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::TextColored(C_DIM, "Game folder: %s", a.dir.c_str());
    ImGui::PopFont();
    if (ImGui::Button("Open game folder")) ShellExecuteA(NULL, "open", a.dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
    saves_section(a, "The Return of the King", a.game.rotkIniDir, "");

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
    ImGui::TextUnformatted("Athelas is an unofficial, free fan launcher. With ROTK Fix it lets owners of The Lord of the Rings: The Return of the King "
                           "(PC, 2003) play their own copy on modern PCs with modern display, graphics and controller support, and it starts "
                           "the PC ports of their own Xbox copies too.");
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::TextColored(C_DIM, "Not affiliated with or endorsed by Electronic Arts, Vivendi Universal Games, New Line Cinema, Warner Bros. or the Tolkien Estate. "
                              "No game files are included; you need your own copy of the game. Background art is read from your own copy.");
    ImGui::PopFont();
    section("Open-source components");
    static const char *lic[][2] = {
        { "DXVK", "Vulkan renderer - zlib licence" },
        { "SDL3", "Controllers and this window - zlib licence" },
        { "Dear ImGui", "Launcher interface - MIT licence" },
        { "stb_image, stb_image_write", "Pictures and screenshots - public domain / MIT" },
        { "DSOAL, OpenAL Soft", "3D sound and EAX reverb - LGPL" },
        { "Uncial Antiqua, Cinzel", "Fonts - SIL Open Font License" },
        { "EB Garamond", "Fonts - SIL Open Font License" },
        { "Liberation Sans", "Button pictures - SIL Open Font License" },
        { "XboxXBE-Recomp", "Recompiler runtime in the Xbox ports - MIT licence, (c) 2026 TGP482" },
        { "toml++", "Settings files in the Xbox ports - MIT licence" },
        { "miniz", "Unpacking updates - MIT licence" } };
    for (auto &l : lic) { ImGui::TextColored(C_GOLD, "%s", l[0]); ImGui::SameLine(S(230)); ImGui::TextUnformatted(l[1]); }
    ImGui::PopTextWrapPos();
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::TextColored(C_DIM, "Launcher version " LAUNCHER_VERSION "  -  made by Shagg, with Claude (Anthropic)");
    ImGui::PopFont();
    report_section(a);
    update_section(a);

    section("Remove ROTK Fix");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("Takes ROTK Fix out of the game folder and puts the game's own controller and settings files back. "
                           "Your saved games, screenshots, achievements and texture packs are kept.");
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
            ImGui::TextUnformatted("This removes ROTK Fix and its settings from the game folder, "
                                   "and restores the game's Input.ini and your ROTK.ini. "
                                   "The game will run as it did before ROTK Fix. Saved games, screenshots, achievements and "
                                   "texture packs are kept.");
            if (!err.empty()) ImGui::TextColored(C_BAD, "%s", err.c_str());
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, rgb(90, 36, 28));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(120, 46, 34));
            ImGui::PushStyleColor(ImGuiCol_Text, rgb(250, 228, 214));
            if (ImGui::Button("Remove", ImVec2(S(150), 0))) {
                done.clear(); err.clear();
                if (uninstall(a.dir, done, err)) {
                    finished = true; g_fixRemoved = true;
                    g_removeSelf = !_stricmp(a.dir.c_str(), exe_dir().c_str());   // the launcher was in the game folder
                }
            }
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(S(150), 0)) || dialog_back()) { err.clear(); ImGui::CloseCurrentPopup(); }
            ImGui::SetItemDefaultFocus();   // a controller's cursor starts on Cancel, not on Remove
        } else {
            ImGui::TextColored(C_GOOD, "ROTK Fix has been removed.");
            ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
            for (auto &d : done) ImGui::BulletText("%s", d.c_str());
            ImGui::PopFont();
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(S(150), 0)) || dialog_back()) {
                if (g_removeSelf) a.quit = true;
                else { ImGui::CloseCurrentPopup(); finished = false; a.game = inspect_game(a.dir); a.otherFor.clear(); }
            }
        }
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
}

// After ROTK Fix is in place in a PC game folder: remember that game and show it.
static void pc_game_ready(App &a, const std::string &dir)
{
    LibGame g;
    if (detect_game_at(dir, g) == GK_ROTK_PC) {
        a.cur = library_add(a.lib, g);
        library_save(exe_dir(), a.lib);
    }
    load_pc_settings(a, dir);
    a.otherFor.clear(); a.searched = false;
    set_message(a, "ROTK Fix is set up in " + dir + ". Choose your settings and press Play.", false);
}

// Puts ROTK Fix's files into a PC game folder (the launcher stays where it is)
void setup_here(App &a, const std::string &dst)
{
    bool denied = false;
    a.setupErr.clear(); a.deniedDir.clear();
    if (install_to(pc_fix_source(), dst, a.setupErr, denied)) pc_game_ready(a, dst);
    else if (denied) a.deniedDir = dst;
}

// The game is in a protected folder (Program Files): the launcher starts again with
// administrator rights, sets ROTK Fix up there and carries on as administrator
// (needed to save that game's settings).
void admin_setup_prompt(App &a)
{
    if (a.deniedDir.empty()) return;
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("The game is in a protected folder. Windows will ask for permission to put ROTK Fix there "
                           "(and each time you change its settings).");
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Set up with administrator permission")) {
        if (start_launcher(exe_dir(), true, "--install-to \"" + a.deniedDir + "\"")) a.quit = true;
        else a.setupErr = "Permission was not given.";
    }
}

void draw_setup(App &a)
{
    if (!a.searched) {
        a.found = find_game_folders();
        for (const LibGame &g : a.lib)          // PC copies the player added before
            if (g.kind == GK_ROTK_PC && !g.iso) {
                bool have = false;
                for (auto &f : a.found) if (!_stricmp(f.dir.c_str(), g.path.c_str())) have = true;
                if (!have) a.found.insert(a.found.begin(), FoundGame{ g.path, inspect_game(g.path) });
            }
        a.searched = true;
    }
    section("Let's find your games");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("This launcher plays your own copies of The Return of the King (PC or Xbox), The Two Towers (Xbox) and The Fellowship of the Ring (Xbox). "
                           "Show it where your games are; it remembers them.");
    ImGui::PopTextWrapPos();

    // Xbox games: a folder with the extracted game, or a disc image
    section("Xbox games");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("Choose the folder you extracted an Xbox game to (the one with default.xbe), or its disc image (.iso or .xiso). "
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
    ImGui::TextUnformatted("Choose your installed game below. ROTK Fix's files are put in the game's own folder; "
                           "the game's files aren't changed, and the launcher stays where it is.");
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
            if (fix_installed(f.info)) {
                if (gold_button("Use this game", ImVec2(S(240), S(36)), F_BODY)) {
                    if (folder_writable(f.dir) || !restart_elevated_for(a, f.dir)) pc_game_ready(a, f.dir);
                }
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
    admin_setup_prompt(a);
    if (!a.lib.empty()) {
        ImGui::Dummy(ImVec2(0, S(10)));
        games_section(a);
    }
}
