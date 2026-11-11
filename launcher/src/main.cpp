// Athelas - the launcher for ROTK Fix (PC) and the Xbox ports
//
// The one place to start the game from: shows whether the game and ROTK Fix
// are set up, lets the player change every setting, writes ROTKFix.ini and
// the game's own ROTK.ini, then starts rotk.exe.
//
// Dear ImGui (MIT) on SDL3 (zlib). Fonts: Uncial Antiqua, Cinzel and EB Garamond (SIL OFL).
// Pictures: stb_image (public domain / MIT).
//
// This file: the start, the window, the language and the main loop; the pages are in the files app.h lists.
#include "app.h"
#include "font_cinzel.h"
#include "font_garamond.h"
#include "font_uncial.h"

// ---------------------------------------------------------------------------

// ATHELAS_TEST_PAD=<file> (testing): a virtual controller, its presses read from that file as they are written
// ("DOWN DOWN A", names below; the file is removed once read), so controller use can be tried without one
static void test_pad_poll()
{
    static const char *file = getenv("ATHELAS_TEST_PAD");
    if (!file) return;
    static SDL_Joystick *js = nullptr;
    struct Ev { double at; int button; bool down; };
    static std::vector<Ev> queue;
    if (!js) {
        SDL_VirtualJoystickDesc d;
        SDL_INIT_INTERFACE(&d);
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT;
        d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.name = "Athelas test pad";
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
        js = id ? SDL_OpenJoystick(id) : nullptr;
        if (!js) { file = nullptr; return; }
    }
    double now = SDL_GetTicks() / 1000.0;
    if (FILE *f = fopen(file, "rb")) {
        char buf[4096];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        remove(file);
        buf[n] = 0;
        static const struct { const char *name; int b; } names[] = {
            { "A", SDL_GAMEPAD_BUTTON_SOUTH }, { "B", SDL_GAMEPAD_BUTTON_EAST }, { "X", SDL_GAMEPAD_BUTTON_WEST },
            { "Y", SDL_GAMEPAD_BUTTON_NORTH }, { "LB", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER }, { "RB", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
            { "START", SDL_GAMEPAD_BUTTON_START }, { "BACK", SDL_GAMEPAD_BUTTON_BACK }, { "UP", SDL_GAMEPAD_BUTTON_DPAD_UP },
            { "DOWN", SDL_GAMEPAD_BUTTON_DPAD_DOWN }, { "LEFT", SDL_GAMEPAD_BUTTON_DPAD_LEFT }, { "RIGHT", SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
            { "LS", SDL_GAMEPAD_BUTTON_LEFT_STICK }, { "RS", SDL_GAMEPAD_BUTTON_RIGHT_STICK } };
        double at = std::max(now, queue.empty() ? now : queue.back().at + 0.12);
        for (char *t = strtok(buf, " \r\n\t"); t; t = strtok(nullptr, " \r\n\t"))
            for (auto &nm : names)
{
                if (!_stricmp(t, "TRIGGERS")) { queue.push_back({ at, -1, true }); queue.push_back({ at + 0.15, -1, false }); at += 0.3; break; }
                if (!_stricmp(nm.name, t)) { queue.push_back({ at, nm.b, true }); queue.push_back({ at + 0.06, nm.b, false }); at += 0.18; }
            }
    }
    while (!queue.empty() && queue.front().at <= now) {
        if (queue.front().button < 0) {   // "TRIGGERS": LT and RT together
            SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, queue.front().down ? 32767 : -32768);
            SDL_SetJoystickVirtualAxis(js, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, queue.front().down ? 32767 : -32768);
        } else SDL_SetJoystickVirtualButton(js, queue.front().button, queue.front().down);
        queue.erase(queue.begin());
    }
}

// Japanese, Chinese and Korean: Windows' own font for them, merged in behind every font of the launcher (loaded
// once, the first time such a language is chosen; outside a frame, as the font atlas wants)
static bool g_langAsk;              // no language chosen yet: ask on the front screen

static std::string g_langFontPending;

static bool g_langFontsAll;         // the language list is showing: every language's font, for their names

static void lang_font_for(const std::string &code)
{
    static std::vector<std::string> loaded;
    std::string path = i18n_system_font(code);
    if (path.empty() || std::find(loaded.begin(), loaded.end(), path) != loaded.end()) return;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (64L << 20)) { fclose(f); return; }
    void *data = IM_ALLOC((size_t)n);   // kept for the program's life: the atlas reads glyphs from it as they are needed
    if (fread(data, 1, (size_t)n, f) != (size_t)n) { fclose(f); IM_FREE(data); return; }
    fclose(f);
    loaded.push_back(path);
    ImGuiIO &io = ImGui::GetIO();
    for (ImFont *ft : { F_BODY, F_SMALL, F_HEAD, F_TAB, F_TITLE, F_PLAY }) {
        ImFontConfig m; m.FontDataOwnedByAtlas = false; m.MergeMode = true; m.DstFont = ft;
        io.Fonts->AddFontFromMemoryTTF(data, (int)n, ft->LegacySize, &m);
    }
}

static void set_language(const std::string &code)
{
    WritePrivateProfileStringA("Launcher", "Language", code.c_str(), ports_ini().c_str());
    i18n_load(code);
    g_langFontPending = i18n_current();   // its font, before the next frame
    g_langAsk = false;
}

static std::string lang_label(const Language &l)   // "Deutsch (German)"; English: "English"
{
    static const char *en[][2] = { { "es", "Spanish" }, { "fr", "French" }, { "de", "German" }, { "it", "Italian" },
        { "pt-BR", "Portuguese" }, { "pl", "Polish" }, { "ru", "Russian" }, { "ja", "Japanese" },
        { "zh-Hans", "Chinese" }, { "ko", "Korean" } };
    for (auto &e : en) if (l.code == e[0]) return l.name + "  (" + e[1] + ")##" + l.code;
    return l.name + "##" + l.code;
}

void language_combo()
{
    char cur[32] = "";
    GetPrivateProfileStringA("Launcher", "Language", "auto", cur, sizeof cur, ports_ini().c_str());
    std::string autoName = std::string(tr("Automatic")) + " (" + tr("Windows' language") + ")";
    std::string shown = autoName;
    for (const Language &l : languages()) if (l.code == cur) shown = l.name;
    ImGui::SetNextItemWidth(S(320));
    // (the names stay as they are: each in its own language)
    if (ImGui::BeginCombo("##lang", shown.c_str())) {
        g_langFontsAll = true;
        if (ImGui::Selectable(autoName.c_str(), !strcmp(cur, "auto"))) set_language("auto");
        for (const Language &l : languages())
            if (ImGui::Selectable(lang_label(l).c_str(), l.code == cur)) set_language(l.code);
        ImGui::EndCombo();
    }
}

// First start: which language (on the front screen, once)
static void language_first_ask()
{
    if (!g_langAsk) return;
    if (!ImGui::IsPopupOpen("Language##first")) ImGui::OpenPopup("Language##first");
    g_langFontsAll = true;
    ImGui::SetNextWindowSize(ImVec2(S(480), 0));
    if (ImGui::BeginPopupModal("Language##first", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextColored(C_GOLD, "Athelas' language");
        ImGui::Spacing();
        std::string autoName = std::string(tr("Automatic")) + " (" + tr("Windows' language") + ")";
        if (ImGui::Selectable(autoName.c_str())) { set_language("auto"); ImGui::CloseCurrentPopup(); }
        for (const Language &l : languages())
            if (ImGui::Selectable(lang_label(l).c_str())) { set_language(l.code); ImGui::CloseCurrentPopup(); }
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_DIM, "You can change it later under This window, on any game's page.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndPopup();
    }
}

// The window's size, remembered for next time (not while fullscreen or maximised: then only that it was)
static void window_remember(SDL_Window *w)
{
    SDL_WindowFlags f = SDL_GetWindowFlags(w);
    if (f & SDL_WINDOW_FULLSCREEN) return;
    launcher_ini_put("Maximized", (f & SDL_WINDOW_MAXIMIZED) != 0);
    if (f & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)) return;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(w, &ww, &wh);
    if (ww >= 400 && wh >= 300) { launcher_ini_put("Width", ww); launcher_ini_put("Height", wh); }
}

// Everything grows and shrinks with the window: the scale follows its size against the first one
static void window_follow_size(App &a)
{
    int w = 0, h = 0;
    SDL_GetWindowSize(a.win, &w, &h);
    if (w <= 0 || h <= 0) return;
    float f = std::min((float)w / g_baseW, (float)h / g_baseH);
    f = std::max(0.6f, std::min(f, 4.0f));
    static float cur = 1.0f;
    if (fabsf(f - cur) < 0.01f) return;
    cur = f;
    g_scale = g_scale0 * f;
    ImGuiStyle &st = ImGui::GetStyle();
    st = ImGuiStyle();
    setup_style();
    st.FontSizeBase = F_BODY->LegacySize;
    st.FontScaleMain = f;
    orn_set_scale(g_scale);
}

// Borderless: the strip along the top moves the window, the edges resize it (Athelas' own buttons are left alone)
static SDL_HitTestResult SDLCALL window_hit(SDL_Window *w, const SDL_Point *p, void *)
{
    if (!g_borderless || (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN)) return SDL_HITTEST_NORMAL;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(w, &ww, &wh);
    const int e = 6;
    bool l = p->x < e, r = p->x >= ww - e, t = p->y < e, b = p->y >= wh - e;
    if (!(SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED)) {
        if (t && l) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (t && r) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (b && l) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (b && r) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (t) return SDL_HITTEST_RESIZE_TOP;
        if (b) return SDL_HITTEST_RESIZE_BOTTOM;
        if (l) return SDL_HITTEST_RESIZE_LEFT;
        if (r) return SDL_HITTEST_RESIZE_RIGHT;
    }
    float strip = S(30), buttons = S(3 * 40 + 12);
    if (p->y < strip && p->x < ww - buttons && p->x > ww * 0.35f) return SDL_HITTEST_DRAGGABLE;   // (not over the title or tabs)
    return SDL_HITTEST_NORMAL;
}

static void draw_window_buttons(App &a, ImVec2 size)
{
    if (!g_borderless || (SDL_GetWindowFlags(a.win) & SDL_WINDOW_FULLSCREEN)) return;
    const float bw = S(40), bh = S(26);
    ImGui::SetNextWindowPos(ImVec2(size.x - 3 * bw - S(6), S(2)));
    ImGui::SetNextWindowSize(ImVec2(3 * bw + S(4), bh + S(4)));
    ImGui::Begin("##winbuttons", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, rgb(20, 16, 12, 0.6f));
    bool maxed = (SDL_GetWindowFlags(a.win) & SDL_WINDOW_MAXIMIZED) != 0;
    ImGui::SetCursorPos(ImVec2(0, 0));
    if (ImGui::Button("_##min", ImVec2(bw, bh))) SDL_MinimizeWindow(a.win);
    ImGui::SameLine();
    if (ImGui::Button(maxed ? "[ ]##max" : "[  ]##max", ImVec2(bw, bh))) { if (maxed) SDL_RestoreWindow(a.win); else SDL_MaximizeWindow(a.win); }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(150, 40, 30));
    if (ImGui::Button("X##close", ImVec2(bw, bh))) a.quit = true;
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::End();
}

static DWORD WINAPI cleanup_thread(LPVOID) { builder_cleanup_old(); return 0; }

// ATHELAS_TIMING=1 (testing): how long each step of starting takes, in athelas-startup.log; it quits after the first
// frame is on screen
static LARGE_INTEGER g_st0;
static std::string g_stLog;
static void st_mark(const char *what)
{
    if (!getenv("ATHELAS_TIMING")) return;
    LARGE_INTEGER f, n;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&n);
    char b[96];
    snprintf(b, sizeof b, "%8.1f ms  %s\r\n", (n.QuadPart - g_st0.QuadPart) * 1000.0 / f.QuadPart, what);
    g_stLog += b;
}
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR cmdLine, int)
{
    QueryPerformanceCounter(&g_st0);
    if (cmdLine && strstr(cmdLine, "--vkprobe")) return gpu_probe_main();   // the background graphics check (gpu.cpp)
    // 1.7.3/1.7.4's build kits and tools, not needed since 1.7.5: removed in the background (thousands of files)
    CloseHandle(CreateThread(NULL, 0, cleanup_thread, NULL, 0, NULL));
    if (!(cmdLine && strstr(cmdLine, "--install-to "))) gpu_probe_start();
    st_mark("threads started");
    // "--install-to <dir>" (with administrator rights): put ROTK Fix into a protected game
    // folder, then carry on as the launcher for that game. "--pc <dir>": show that PC game.
    auto quoted_arg = [&](const char *name) {
        const char *at = strstr(cmdLine ? cmdLine : "", name);
        if (!at) return std::string();
        at += strlen(name);
        while (*at == ' ') at++;
        std::string v;
        if (*at == '"') { const char *e = strchr(at + 1, '"'); v.assign(at + 1, e ? e : at + strlen(at)); }
        else { const char *e = strchr(at, ' '); v.assign(at, e ? e : at + strlen(at)); }
        return v;
    };
    std::string pcArg = quoted_arg("--pc ");
    bool justInstalled = false;
    if (std::string dst = quoted_arg("--install-to "); !dst.empty()) {
        std::string err; bool denied = false;
        if (!install_to(pc_fix_source(), dst, err, denied)) { MessageBoxA(NULL, err.c_str(), "Athelas", MB_ICONERROR); return 1; }
        pcArg = dst; justInstalled = true;
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        MessageBoxA(NULL, SDL_GetError(), "Athelas", MB_ICONERROR);
        return 1;
    }
    st_mark("SDL started");
    g_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (g_scale <= 0.0f) g_scale = 1.0f;
    // landscape, so the background pictures (screenshots, the games' art) show at their own shape beside the settings
    int ww = (int)(880 * g_scale), wh = (int)(640 * g_scale);
    const SDL_DisplayMode *dmode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (dmode && (ww > dmode->w * 0.95f || wh > dmode->h * 0.9f)) {
        float f = std::min(dmode->w * 0.95f / ww, dmode->h * 0.9f / wh);
        g_scale *= f; ww = (int)(ww * f); wh = (int)(wh * f);
    }
    g_scale0 = g_scale; g_baseW = ww; g_baseH = wh;
    // the size it had last time (it can be resized, since 1.7); a small screen like the Steam Deck's: maximised and
    // borderless unless the player chose otherwise
    bool smallScreen = dmode && dmode->w <= 1280 && dmode->h <= 800;
    int lastW = GetPrivateProfileIntA("Launcher", "Width", 0, ports_ini().c_str());
    int lastH = GetPrivateProfileIntA("Launcher", "Height", 0, ports_ini().c_str());
    if (lastW >= 400 && lastH >= 300 && (!dmode || (lastW <= dmode->w && lastH <= dmode->h))) { ww = lastW; wh = lastH; }
    SDL_WindowFlags wf = SDL_WINDOW_RESIZABLE;
    if (launcher_ini_get("Maximized", smallScreen)) wf |= SDL_WINDOW_MAXIMIZED;
    g_borderless = launcher_ini_get("Borderless", smallScreen);
    if (g_borderless) wf |= SDL_WINDOW_BORDERLESS;
    if (launcher_ini_get("Fullscreen", 0)) wf |= SDL_WINDOW_FULLSCREEN;
    SDL_Window *win = SDL_CreateWindow("Athelas", ww, wh, wf);
    if (win) {
        SDL_SetWindowMinimumSize(win, 640, 460);
        SDL_SetWindowHitTest(win, window_hit, nullptr);
    }
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, NULL) : nullptr;
    if (!ren) { MessageBoxA(NULL, SDL_GetError(), "Athelas", MB_ICONERROR); return 1; }
    SDL_SetRenderVSync(ren, 1);
    st_mark("window and renderer");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImFontConfig fc; fc.FontDataOwnedByAtlas = false; fc.OversampleH = 2;
    // the body text with the accented Latin and the Cyrillic letters EB Garamond has (the subtitle languages' names)
    static const ImWchar kBodyRanges[] = { 0x0020, 0x024F, 0x0400, 0x04FF, 0x2000, 0x206F, 0x20AC, 0x20AC, 0x2122, 0x2122, 0 };
    F_BODY  = io.Fonts->AddFontFromMemoryTTF((void *)font_garamond_data, sizeof font_garamond_data, S(21), &fc, kBodyRanges);
    F_SMALL = io.Fonts->AddFontFromMemoryTTF((void *)font_garamond_data, sizeof font_garamond_data, S(18), &fc, kBodyRanges);
    F_HEAD  = io.Fonts->AddFontFromMemoryTTF((void *)font_cinzel_data, sizeof font_cinzel_data, S(19), &fc);
    F_TAB   = io.Fonts->AddFontFromMemoryTTF((void *)font_cinzel_data, sizeof font_cinzel_data, S(16.5f), &fc);
    F_TITLE = io.Fonts->AddFontFromMemoryTTF((void *)font_uncial_data, sizeof font_uncial_data, S(64), &fc);   // the Athelas title
    F_PLAY  = io.Fonts->AddFontFromMemoryTTF((void *)font_cinzel_data, sizeof font_cinzel_data, S(34), &fc);
    io.FontDefault = F_BODY;
    {   // letters the heading fonts don't have (Cyrillic, more of Latin): from EB Garamond, merged in behind them
        for (ImFont *f : { F_HEAD, F_TAB, F_TITLE, F_PLAY }) {
            ImFontConfig m; m.FontDataOwnedByAtlas = false; m.MergeMode = true; m.DstFont = f; m.OversampleH = 2;
            io.Fonts->AddFontFromMemoryTTF((void *)font_garamond_data, sizeof font_garamond_data, f->LegacySize, &m, kBodyRanges);
        }
    }
    {   // the launcher's language (since 1.7): asked on first start; "auto" is Windows' language
        char lang[32] = "";
        GetPrivateProfileStringA("Launcher", "Language", "", lang, sizeof lang, ports_ini().c_str());
        g_langAsk = !lang[0];
        i18n_load(lang[0] ? lang : "auto");
        lang_font_for(i18n_current());
    }
    ImGui::GetStyle().FontSizeBase = F_BODY->LegacySize;   // 1.92: size of the default font
    setup_style();
    orn_set_scale(g_scale);
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);
    st_mark("fonts, language, style");

    App a;
    a.win = win;
    g_mainWin = win;
    // the player's games, remembered next to the launcher (ROTKFix\games.ini)
    a.lib = library_load(exe_dir());
    st_mark("game library read");
    // Which PC game's settings to show: one named on the command line; else the launcher's own
    // folder if it is a game folder (older layout); else the first remembered PC copy with ROTK Fix.
    std::string pcDir;
    {
        LibGame own;
        if (!pcArg.empty() && detect_game_at(pcArg, own) == GK_ROTK_PC) pcDir = own.path;
        else if (detect_game_at(exe_dir(), own) == GK_ROTK_PC) pcDir = own.path;
        else
            for (const LibGame &g : a.lib)
                if (g.kind == GK_ROTK_PC && !g.iso && fix_installed(inspect_game(g.path))) { pcDir = g.path; break; }
        if (!pcDir.empty() && detect_game_at(pcDir, own) == GK_ROTK_PC) {
            a.cur = library_add(a.lib, own);
            library_save(exe_dir(), a.lib);
        }
    }
    // A game installed in a protected folder (Program Files - where the disc
    // installs by default): settings and the controller layout can only be
    // saved there with administrator rights. Restart once with Windows'
    // permission prompt; if it is declined, carry on and explain.
    bool protectedDir = false;
    if (!pcDir.empty() && !folder_writable(pcDir)) {
        if (!strstr(cmdLine ? cmdLine : "", "--elevated") && !justInstalled &&
            start_launcher(exe_dir(), true, "--elevated --pc \"" + pcDir + "\"")) return 0;
        protectedDir = true;
    }
    // Not in a PC game's folder, but the player has Xbox games: open on the first of them
    // (The Return of the King first) instead of the PC setup screen.
    if (a.cur < 0)
        for (GameKind k : { GK_ROTK_XBOX, GK_TT_XBOX, GK_FOTR_XBOX, GK_TA_XBOX })
            for (size_t i = 0; i < a.lib.size() && a.cur < 0; i++)
                if (a.lib[i].kind == k) a.cur = (int)i;
    if (const char *t = getenv("ROTKFIX_GAME")) { int n = atoi(t); if (n >= 0 && n < (int)a.lib.size()) a.cur = n; }   // testing
    a.monitors = enum_monitors();
    st_mark("PC game checked, monitors");
    bool hadIni = load_pc_settings(a, pcDir.empty() ? exe_dir() : pcDir);
    // the front screen first, unless ROTK Fix was just set up (its page says what to do next)
    a.front = !justInstalled && !getenv("ATHELAS_PAGE");   // ATHELAS_PAGE (testing): open on the page
    if (getenv("ATHELAS_JOURNEY")) { a.journey = true; a.front = false; }   // testing: open on the Journey page
    refresh_controllers(a);
    st_mark("settings, controllers");

    bg_show_load();   // the pictures themselves are listed on the first frame
    if (getenv("ROTKFIX_SAVE_AND_EXIT")) { save(a); return 0; }     // testing
    if (!hadIni) set_message(a, "Welcome! Settings are chosen for your PC - press Play, or adjust anything first.", false);
    if (justInstalled) set_message(a, "ROTK Fix is set up in " + pcDir + ". Choose your settings and press Play.", false);
    if (protectedDir) set_message(a, "This game folder is protected by Windows, so settings can't be saved. "
                                     "Open the launcher again and allow the permission prompt.", true);

    // "--play <fotr|tt|rotk-xbox|rotk-pc|ta>" (a game's desktop shortcut): start that game as its Play button would
    std::string playArg = quoted_arg("--play ");
    music_init();    // the front screen's music (nothing plays without a sound device)
    st_mark("music");
    if (int n = update_remove_leftovers())   // files an older Athelas shipped that this one doesn't
        set_message(a, std::to_string(n) + " file" + (n == 1 ? "" : "s") + " left over from an older Athelas removed "
                       "(listed in athelas-cleanup.log).", false);
    update_tidy();   // what an update left behind
    st_mark("leftovers checked");
    if (updates_on()) update_check_start(LAUNCHER_VERSION);

    bool playPending = !playArg.empty();
    while (!a.quit) {
        if (playPending && ImGui::GetFrameCount() > 1) { playPending = false; shortcut_play(a, playArg); }
        test_pad_poll();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (!kb_capture_event(a, e)) ImGui_ImplSDL3_ProcessEvent(&e);   // a key being set doesn't also click
            if (e.type == SDL_EVENT_QUIT) a.quit = true;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F11 && !e.key.repeat && a.kbCapture < 0)
                set_fullscreen(a, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
            if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && a.capture < 0 && SDL_GetKeyboardFocus() == win) {
                // LT + RT together: fullscreen on or off (once per press of both)
                static bool both;
                SDL_Gamepad *gp = SDL_GetGamepadFromID(e.gaxis.which);
                bool now = gp && SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 24000 &&
                           SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 24000;
                if (now && !both) set_fullscreen(a, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
                both = now;
            }
            if (e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_MAXIMIZED || e.type == SDL_EVENT_WINDOW_RESTORED)
                window_remember(win);
            if (e.type == SDL_EVENT_DROP_FILE && !a.running) front_drop(a, e.drop.data);   // a game dropped on the window
            if ((e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED) && !a.running) refresh_controllers(a);
            capture_event(a, e);
        }
        music_pause(a.running != NULL);
        {   // while a game plays, the launcher lets go of the controllers and comes back for them after: on a Steam
            // Deck (Proton, Steam Input) a launcher still holding them could keep them from the game (a report on
            // Reddit, 1 Nov: controllers not seen in the games since 1.5, when the launcher began staying open)
            static bool held = true;
            if (a.running && held) {
                for (SDL_Gamepad *g : a.pads) SDL_CloseGamepad(g);
                a.pads.clear();
                SDL_QuitSubSystem(SDL_INIT_GAMEPAD);   // (the devices themselves too, not only the gamepads opened)
                held = false;
            } else if (!a.running && !held) {
                SDL_InitSubSystem(SDL_INIT_GAMEPAD);
                refresh_controllers(a);
                held = true;
            }
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
        if (a.capture >= 0 && ImGui::GetFrameCount() - a.captureFrame > 2) a.capture = -1;   // left the Controls tab or the page
        if (a.kbCapture >= 0 && ImGui::GetFrameCount() - a.kbCaptureFrame > 2) a.kbCapture = -1;
        if (a.firstRunPreset) {
            // first run: High suits a gaming graphics card; integrated graphics (most laptops), software
            // Vulkan and PCs without Vulkan get Balanced instead - unless the player already changed something
            GpuInfo gi = gpu_info();
            if (gi.state != GPU_CHECKING) {
                a.firstRunPreset = false;
                bool weak = gi.integrated || gi.state != GPU_VULKAN_OK;
                if (weak && detect_preset(a.s) == PRESET_HIGH) {
                    apply_preset(a.s, PRESET_BALANCED);
                    set_message(a, "Balanced graphics chosen to suit this PC's graphics - change it on the Graphics tab", false);
                }
            }
        }
        // the controller works the launcher only while it is the window in front: its events come in the background too
        // (for setting buttons), and Start or Back there would start a game or save behind another app
        if (a.capture >= 0 || ImGui::GetTime() - a.captureAt < 0.5 || !SDL_GetKeyboardFocus())
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        else io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        window_follow_size(a);
        if (!g_langFontPending.empty()) { lang_font_for(g_langFontPending); g_langFontPending.clear(); }
        if (g_langFontsAll) { for (const char *c : { "ja", "zh-Hans", "ko" }) lang_font_for(c); }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        {   // a button just set with the controller: its cursor back where it was
            static bool padNavWas = true;
            bool on = (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;
            if (on && !padNavWas && GImGui->NavInputSource == ImGuiInputSource_Gamepad) ImGui::SetNavCursorVisible(true);
            padNavWas = on;
        }
        draw_ui(a, io.DisplaySize, ren);
        draw_window_buttons(a, io.DisplaySize);
        if (a.front && !a.running && !getenv("ATHELAS_PAGE")) language_first_ask();
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 10, 8, 6, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
        i18n_frame_end();
        SDL_RenderPresent(ren);
        if (getenv("ATHELAS_TIMING")) {
            static int frames = 0;
            char what[32]; snprintf(what, sizeof what, "frame %d on screen", ++frames);
            st_mark(what);
            if (frames == 3) {
                HANDLE h = CreateFileA((exe_dir() + "\\athelas-startup.log").c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
                if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, g_stLog.data(), (DWORD)g_stLog.size(), &w, NULL); CloseHandle(h); }
                a.quit = true;
            }
        }
    }
    music_shutdown();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (g_removeSelf) remove_launcher_after_exit();
    return 0;
}
