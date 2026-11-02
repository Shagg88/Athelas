// Athelas - the frame: the front screen (front.inc), the Journey (journey.inc) and the game pages around them.
#include "app.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#include "../third_party/stb_image.h"
#pragma GCC diagnostic pop

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
        if (tp_kind(g.kind) && release_untested(k_tomlPorts[tp_of(g.kind)].dir, g)) {
            const char *dir = k_tomlPorts[tp_of(g.kind)].dir;
            if (release_confirmed(dir, g))
                ImGui::TextColored(C_DIM, "An untested version (%s copy) you said works.", game_region_name(g));
            else {
                ImGui::TextColored(C_WARN, "This is an untested and unsupported version (%s copy). Proceed with caution: it "
                                   "may not start, or may have problems.", game_region_name(g));
                ImGui::TextUnformatted("Did it work? Let us know:");
                if (ImGui::Button("It worked")) report_version(a, dir, game_kind_name(g.kind), g, true);
                ImGui::SameLine();
                if (ImGui::Button("It didn't work")) report_version(a, dir, game_kind_name(g.kind), g, false);
            }
            ImGui::Spacing();
        }
        if (tp_kind(g.kind)) { ImGui::PopTextWrapPos(); draw_tt(a, g); return; }
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
    if (fix_installed(o)) {        // (normally switched to straight away in draw_ui)
        if (ImGui::Button("Show its settings")) use_pc_game(a, g.path);
    } else {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted("ROTK Fix isn't set up for this copy yet. Setting it up puts ROTK Fix's files in the game's folder, "
                               "next to rotk.exe (the game's own files aren't changed). Remove ROTK Fix on the About tab takes them out again.");
        ImGui::PopTextWrapPos();
        if (gold_button("Set up ROTK Fix there", ImVec2(S(240), S(36)), F_BODY)) { setup_here(a, g.path); a.otherFor.clear(); }
        if (!a.setupErr.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextColored(C_BAD, "%s", a.setupErr.c_str()); ImGui::PopTextWrapPos(); }
        admin_setup_prompt(a);
    }
}

// ---------------------------------------------------------------------------
// Main screen

#include "front.inc"

#include "journey.inc"

// a newer Athelas: a button at the top right (at y), on every page and the front screen, that asks to install it and
// shows the download
// The maker's mark, top right on every screen (as COGBox has one): "Made by Shagg" and, under it, the version of
// Athelas this is - the release's number, so nobody reads a port's own version for it. Returns the y below it.
static float maker_mark(ImVec2 size)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    float right = size.x - S(48), y = S(34);   // (inside the window's frame)
    if (g_borderless && g_mainWin && !(SDL_GetWindowFlags(g_mainWin) & SDL_WINDOW_FULLSCREEN))
        y += S(26);   // (under Athelas' own window buttons)
    const char *mark = tr("Made by Shagg");
    const std::string ver = "v" LAUNCHER_VERSION;
    float ms = F_SMALL->LegacySize * ImGui::GetStyle().FontScaleMain, vs = ms * 0.85f;
    ImVec2 m = F_SMALL->CalcTextSizeA(ms, FLT_MAX, 0, mark), v = F_SMALL->CalcTextSizeA(vs, FLT_MAX, 0, ver.c_str());
    ImVec2 mp = o + ImVec2(right - m.x, y), vp = o + ImVec2(right - v.x, y + m.y);
    dl->AddText(F_SMALL, ms, mp + ImVec2(S(1), S(1)), IM_COL32(0, 0, 0, 170), mark);
    dl->AddText(F_SMALL, ms, mp, ImGui::GetColorU32(C_GOLD), mark);
    dl->AddText(F_SMALL, vs, vp + ImVec2(S(1), S(1)), IM_COL32(0, 0, 0, 170), ver.c_str());
    dl->AddText(F_SMALL, vs, vp, ImGui::GetColorU32(C_DIM), ver.c_str());
    return y + m.y + v.y + S(8);
}

static void update_button(App &a, ImVec2 size, float sy)
{
    const float pad = S(30);
    {
        std::string nv = update_newer();
        float uprog = 0;
        int ust = update_install_state(&uprog, nullptr);
        if (!nv.empty() || ust == 1 || ust == 2) {
            char lbl[64];
            if (ust == 1 || ust == 2)   // two steps, each 0 to 100%: the download, then the files put in place
                snprintf(lbl, sizeof lbl, ust == 1 ? "Downloading  %d%%##hdrupd" : "Installing  %d%%##hdrupd", (int)(uprog * 100 + 0.5f));
            else snprintf(lbl, sizeof lbl, "Update to %s##hdrupd", nv.c_str());
            ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
            float bw = std::max(S(150), ImGui::CalcTextSize(lbl, nullptr, true).x + S(28));
            ImGui::SetCursorPos(ImVec2(size.x - pad - bw, sy + S(4)));
            ImGui::PushStyleColor(ImGuiCol_Text, C_GOLD_H);
            if (orn_plate_button(lbl, ImVec2(bw, S(30)), ust != 1 && ust != 2)) g_askUpdate = true;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ust != 1 && ust != 2)
                ImGui::SetTooltip("Athelas %s is out. Your settings, saves and achievements are kept.", nv.c_str());
            ImGui::PopFont();
        }
    }
    update_popup(a);
}

void draw_ui(App &a, ImVec2 size, SDL_Renderer *ren)
{
    // change background every 15 s
    // Background pictures: the game art comes from the player's Return of the King (PC)
    // (or, without the PC version, from an extracted Xbox copy: fewer and smaller pictures)
    if (SDL_GetAtomicInt(&g_artState) == 0) {
        std::vector<std::string> dirs;
        for (GameKind k : { GK_ROTK_PC, GK_ROTK_XBOX })
            for (const LibGame &g : a.lib)
                if (g.kind == k) dirs.push_back(g.path);     // (a folder, or an Xbox disc image)
        art_start(dirs);
    }
    {
        GameKind k = a.cur >= 0 && a.cur < (int)a.lib.size() ? a.lib[a.cur].kind : GK_NONE;
        int group = k == GK_TT_XBOX ? 1 : k == GK_FOTR_XBOX ? 2 : k == GK_TA_XBOX ? 4 : 0;   // (4: The Third Age: its film stills and the other games' art)
        std::string dir = group == 4 || group == 0 ? ta_mix_dirs(a) : a.lib[a.cur].path;
        if (a.front) front_bg_group(a, group, dir);   // the game in front of the carousel
        {   // for now every game's backgrounds are The Two Towers' gallery (39 film stills and 3 menu pictures) and
            // Fellowship's sketches, the best art on the Xbox discs, from whichever of the two the player has (group 5);
            // with neither, each game's own as before
            std::string m = ta_mix_dirs(a);
            if (m != "\n" && group != 3) { group = 5; dir = m; }
        }
        if (group != a.bg.group || (group == 0 && ((art_ready() && !a.bg.artIn) || dir != a.bg.dir)) || (group >= 1 && group <= 2 && dir != a.bg.dir) ||
            (group == 4 && (dir != a.bg.dir || (art_ready() && !a.bg.artIn))) || (group == 5 && dir != a.bg.dir)) {
            backgrounds_rebuild(a.bg, group, exe_dir(), dir);
            a.bg.dir = dir;
        }
    }
    backgrounds_poll(a.bg, ren);
    if (!a.bg.job && !a.bg.files.empty() &&
        (!a.bg.cur.tex || a.bg.fresh || (a.bg.files.size() > 1 && ImGui::GetTime() - a.bg.changedAt > 10.0))) {
        backgrounds_next(a.bg);
        if (a.bg.job) a.bg.fresh = false;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    draw_background(ImGui::GetBackgroundDrawList(), size, a.bg);
    // an installed update: the new launcher takes over (whichever page is showing: checked before the front page
    // returns, which 1.7.0 missed)
    static bool restarted = false;
    if (!restarted && update_install_state(nullptr, nullptr) == 3) {
        restarted = true;
        if (update_restart()) a.quit = true;
    }
    journey_env(a);   // Virtues (opt-in) for the game started next
    if (a.journey) {
        draw_journey(a, size, ren);
        update_button(a, size, maker_mark(size));
        ImGui::End();
        return;
    }
    if (a.front) {
        draw_front(a, size, ren);
        update_button(a, size, maker_mark(size));
        ImGui::End();
        return;
    }

    float pad = S(30), bannerH = S(142), barH = S(66);
    const GameInfo &g = a.game;

    // ---- banner: the game picker on the left (game, and below it the version), the title in
    // the middle (centred between the crest at the top and the ornament on the settings panel),
    // status on the right
    ImGui::PushFont(F_TITLE, F_TITLE->LegacySize);
    const float titleW = ImGui::CalcTextSize("Athelas").x, titleX = (size.x - titleW) * 0.5f;
    ImGui::SetCursorPos(ImVec2(titleX, S(52)));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 tp = ImGui::GetCursorScreenPos();
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            if (dx || dy) dl->AddText(F_TITLE, F_TITLE->LegacySize * ImGui::GetStyle().FontScaleMain, ImVec2(tp.x + dx * S(2), tp.y + dy * S(2)), IM_COL32(0, 0, 0, 110), "Athelas");
    ImGui::TextColored(C_GOLD_H, "Athelas");
    ImGui::PopFont();
    // the game shown, top left, as on the front screen: its box and its name (it is chosen on the carousel; the Library
    // button goes back there)
    {
        const float nameW = titleX - pad - S(18);
        float x = pad + S(4), y = S(36);
        if (!g_front.coversLooked) front_load_covers(ren);   // (a page opened first: the boxes' covers for its header)
        front_art_poll(a, ren);
        if (a.cur >= 0) {
            int e = fr_entry_of(a.lib[a.cur].kind);
            const FrontTex &ct = fr_face(a, e);
            float bh = S(92), bw = bh * 0.70f;
            ImDrawList *hd = ImGui::GetWindowDrawList();
            ImVec2 p0 = ImGui::GetWindowPos() + ImVec2(x, y), p1 = p0 + ImVec2(bw, bh);
            if (ct.tex) {
                ImVec2 uv0, uv1;
                crop_uv(ct.w, ct.h, bw, bh, uv0, uv1);
                hd->AddRectFilled(p0 + ImVec2(S(3), S(4)), p1 + ImVec2(S(3), S(4)), IM_COL32(0, 0, 0, 120));
                hd->AddImage((ImTextureID)(intptr_t)ct.tex, p0, p1, uv0, uv1);
            } else {
                hd->AddRectFilled(p0, p1, IM_COL32(18, 20, 17, 230));
                hd->AddRectFilled(p0, ImVec2(p1.x, p0.y + bh * 0.085f), IM_COL32(96, 156, 38, 255));
            }
            hd->AddRect(p0, p1, IM_COL32(150, 118, 60, 200), 0.0f, S(1));
            x += bw + S(12);
            const char *title = k_frTitle[e];
            // a dark plate behind the name, readable over any picture
            ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
            float tw = std::min(nameW - bw - S(12), ImGui::CalcTextSize(title).x);
            ImGui::PopFont();
            orn_plate(hd, ImGui::GetWindowPos() + ImVec2(x - S(8), y + S(12)), ImGui::GetWindowPos() + ImVec2(x + tw + S(12), y + S(66)),
                      IM_COL32(15, 14, 13, 200), false);
            ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
            ImGui::SetCursorPos(ImVec2(x, y + S(18)));
            ImGui::PushTextWrapPos(pad + nameW);
            ImGui::TextColored(C_GOLD_H, "%s", title);
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
            ImGui::SetCursorPosX(x);
            ImGui::TextColored(C_DIM, "%s", game_kind_short(a.lib[a.cur].kind));
            ImGui::PopFont();
        } else {
            ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
            ImGui::SetCursorPos(ImVec2(x, S(74)));
            ImGui::TextColored(C_GOLD_H, "WELCOME");
            ImGui::PopFont();
        }
    }
    // another PC copy chosen that already has ROTK Fix: show its settings
    if (a.cur >= 0 && a.lib[a.cur].kind == GK_ROTK_PC && !a.lib[a.cur].iso && _stricmp(a.lib[a.cur].path.c_str(), a.dir.c_str())) {
        if (a.otherFor != a.lib[a.cur].path) { a.other = inspect_game(a.lib[a.cur].path); a.otherFor = a.lib[a.cur].path; }
        if (fix_installed(a.other)) use_pc_game(a, a.lib[a.cur].path);
    }
    const bool own = own_game(a);
    // an Xbox game shown (The Two Towers, The Fellowship of the Ring, The Return of the King), and its port ready: Play
    // and Save work for it
    if (!own && tp_kind(a.lib[a.cur].kind)) {
        tp_use(a, tp_of(a.lib[a.cur].kind));
        if (!a.ttLooked) tt_find(a);
    }
    const bool ttShown = !own && tp_kind(a.lib[a.cur].kind) && a.ttLooked && tt_ready(a);
    g_xboxPage = false;   // The Return of the King on Xbox had the PC version's page up to 1.5; it has its own now

    // status (top right, one per line)
    struct St { bool ok, warn; std::string label, tip; };
    std::vector<St> st;
    bool filesOk = g.hasDll && g.hasProxyD3D8 && g.hasProxyDinput && g.hasDxvk;
    st.push_back({ g.exeFound && g.versionOk, g.exeFound, g.exeFound ? (g.versionOk ? "Game v1.0" : g.discProtected ? "Disc version (protected)" : "Unknown game version") : "Game not found",
                   g.exeFound ? g.versionText : "Add your game on the About tab." });
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
        bool tt = tp_kind(lg.kind);
        if (tt && !a.ttLooked) tt_find(a);
        bool ready = tt ? tt_ready(a) : game_kind_playable(lg.kind);
        st.push_back({ ready, true, ready ? "Ready" : tt ? "Port not found" : "Not playable yet", lg.path });
    }
    if (own && a.s.padEnabled)
        st.push_back({ a.controllers > 0 && g.hasSdl, true, a.controllers > 1 ? std::to_string(a.controllers) + " controllers" : a.controllers ? "Controller" : "No controller",
                       a.controllers ? a.controller : "Connect a controller - it will be picked up automatically." });
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    float colW = 0;
    for (auto &x : st) colW = std::max(colW, ImGui::CalcTextSize(x.label.c_str()).x + S(15));
    float sy = maker_mark(size), lh = ImGui::GetTextLineHeight() + S(4);
    for (auto &x : st) {
        ImGui::SetCursorPos(ImVec2(size.x - pad - colW, sy));
        status_dot(x.ok, x.warn, x.label.c_str(), x.tip.c_str());
        sy += lh;
    }
    ImGui::PopFont();
    // a newer Athelas: a button under the status, on every page, that asks to install it (and shows the download)
    update_button(a, size, sy);

    // ---- settings panel
    // an elven frame around it; the panel itself sits just inside the frame
    const float frameT = S(9);
    // on the right, the picture left of it in the clear
    const float panelW = std::min(size.x - pad * 2, S(600)), panelX = (size.x - panelW) * 0.5f;
    ImVec2 panelA = ImGui::GetWindowPos() + ImVec2(panelX, bannerH);
    ImVec2 panelB = panelA + ImVec2(panelW, size.y - bannerH - barH - S(30));
    orn_panel_fill(ImGui::GetWindowDrawList(), panelA, panelB, ImGui::GetColorU32(rgb(15, 14, 13, 0.70f)));
    const float inset = frameT * 0.5f + S(5);   // clear of the frame's lines
    ImGui::SetCursorPos(ImVec2(panelX + inset, bannerH + inset));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("panel", ImVec2(panelW - inset * 2, size.y - bannerH - barH - S(30) - inset * 2), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
    g_labelW = std::max(S(230), ImGui::GetContentRegionAvail().x * 0.36f);
    if (ttShown) {
        if (!a.ttLoaded) tt_load_settings(a);
        tt_tabs(a, a.lib[a.cur]);
    } else if (!own) {
        ImGui::BeginChild("other", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        draw_other_game(a);
        ImGui::Dummy(ImVec2(0, S(10)));
        games_section(a);
        ImGui::EndChild();
    } else if (!g.exeFound) {
        ImGui::BeginChild("setup", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        draw_setup(a);
        ImGui::EndChild();
    } else {
    ImGui::PushFont(F_TAB, F_TAB->LegacySize);
    static const char *tabNames[] = { "Display", "Graphics", "Controls", "Audio", "Gameplay", "Mods", "About" };
    static int rotkSel = 0;
    float tabW = tabs_begin(tabNames, 7, rotkSel);
    if (ImGui::BeginTabBar("tabs")) {
        struct T { const char *name; void (*fn)(App &); } tabs[] = {
            { "###display", tab_display }, { "###graphics", tab_graphics }, { "###controls", tab_controls },
            { "###audio", tab_audio }, { "###gameplay", tab_gameplay }, { "###mods", tab_mods }, { "###about", tab_about } };
        // ROTKFIX_TAB=n (testing): open on tab n
        static int startTab = getenv("ROTKFIX_TAB") ? atoi(getenv("ROTKFIX_TAB")) : -1;
        if (int step = pad_tab_step(a)) startTab = (rotkSel + step + 7) % 7;
        int ti = 0;
        for (auto &t : tabs) {
            int idx = ti;
            ImGuiTabItemFlags tf = ti++ == startTab ? ImGuiTabItemFlags_SetSelected : 0;
            ImGui::SetNextItemWidth(tabW);
            if (ImGui::BeginTabItem(t.name, nullptr, tf)) {
                rotkSel = idx;
                pad_tab_focus();
                ImGui::PopFont();
                ImGui::BeginChild("tab", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
                t.fn(a);
                ImGui::Dummy(ImVec2(0, S(6)));
                if (getenv("ROTKFIX_SCROLL_END")) ImGui::SetScrollY(ImGui::GetScrollMaxY());   // testing
                else if (const char *sye = getenv("ROTKFIX_SCROLL_Y")) ImGui::SetScrollY(std::min((float)atof(sye) * g_scale, ImGui::GetScrollMaxY()));   // testing
                ImGui::EndChild();
                ImGui::PushFont(F_TAB, F_TAB->LegacySize);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
        startTab = -1;
    }
    tabs_end();
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
        const float libW = S(130), cardX = pad + S(6) + libW + S(12);   // between the Library button and Save
        float cardW = bx - saveW - S(24) - cardX;
        bool msg = !a.message.empty() && ImGui::GetTime() - a.messageAt < (a.messageBad ? 30.0 : 4.0);
        std::string newer = update_install_state(nullptr, nullptr) > 0 ? std::string() : update_newer();   // not while it installs
        // playing with a controller: its shortcuts, in its own names, while there's no message to show
        std::string padHint;
        if (!msg && a.controllers > 0 && GImGui->NavCursorVisible && GImGui->NavInputSource == ImGuiInputSource_Gamepad) {
            int ls = label_style(a);
            padHint = ls == NAMES_PS ? "L1 / R1: tabs  \xc2\xb7  Options: Play  \xc2\xb7  Share: Save  \xc2\xb7  Circle: back"
                    : ls == NAMES_NINTENDO ? "L / R: tabs  \xc2\xb7  +: Play  \xc2\xb7  \xe2\x80\x93: Save  \xc2\xb7  B: back"
                    : "LB / RB: tabs  \xc2\xb7  Menu: Play  \xc2\xb7  View: Save  \xc2\xb7  B: back";
            padHint = std::string(tr(padHint)) + "  \xc2\xb7  " + tr(fullscreen_hint(ls));   // (each part translated)
        }
        float cardH = S(32) + (newer.empty() ? 0.0f : S(18));
        if (!padHint.empty()) {   // (it can wrap to two lines on a narrow window)
            ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
            cardH += ImGui::CalcTextSize(padHint.c_str(), nullptr, false, cardW - S(18)).y;
            ImGui::PopFont();
        }
        if (msg) {   // room for the message, however many lines it wraps to
            ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
            cardH += ImGui::CalcTextSize(a.message.c_str(), nullptr, false, cardW - S(18)).y + S(2);
            ImGui::PopFont();
        }
        ImVec2 c0 = ImGui::GetWindowPos() + ImVec2(cardX, by + (playH - cardH) * 0.5f), c1 = c0 + ImVec2(cardW, cardH);
        orn_plate(ImGui::GetWindowDrawList(), c0, c1, ImGui::GetColorU32(rgb(15, 14, 13, 0.86f)), false);
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::SetCursorPos(ImVec2(cardX + S(10), by + (playH - cardH) * 0.5f + S(5)));
        ImGui::PushTextWrapPos(cardX + cardW - S(8));
        if (own && !g.exeFound) ImGui::TextColored(C_DIM, "Add a game to begin");
        else if (own) ImGui::TextColored(C_DIM, "%d x %d  |  %s  |  %d fps  |  %s", w, h, dm[a.s.displayMode], fps, pn[detect_preset(a.s)]);
        else if (ttShown) ImGui::TextColored(C_DIM, "%s", tt_summary(a).c_str());
        else ImGui::TextColored(C_DIM, "%s  |  %s", game_kind_name(a.lib[a.cur].kind),
                                game_kind_playable(a.lib[a.cur].kind) ? "settings in its own launcher" : "not playable yet");
        if (!newer.empty()) {
            ImGui::SetCursorPosX(cardX + S(10));
            ImGui::TextColored(C_GOLD_H, "Athelas %s is out  -  press Update at the top right", newer.c_str());
        }
        if (msg) {
            ImGui::SetCursorPosX(cardX + S(10));
            ImGui::TextColored(a.messageBad ? C_BAD : C_GOOD, "%s", a.message.c_str());
        }
        if (!padHint.empty()) {
            ImGui::SetCursorPosX(cardX + S(10));
            ImGui::TextColored(C_GOLD, "%s", padHint.c_str());
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
            size_t at = 0;
            while (at < all.size()) {
                size_t en = all.find(',', at); if (en == std::string::npos) en = all.size();
                std::string item = all.substr(at, en - at);
                size_t d = item.find('.'), q = item.find('=');
                if (d != std::string::npos && q != std::string::npos)
                    tt_put(a, item.substr(0, d).c_str(), item.substr(d + 1, q - d - 1).c_str(), item.substr(q + 1));
                at = en + 1;
            }
        }
        if (getenv("ROTKFIX_TT_SAVE")) tt_save(a);
    }
    static bool autoUpdate = getenv("ROTKFIX_UPDATE_NOW") != nullptr;   // testing: install a newer release at once
    if (autoUpdate && !update_newer().empty()) { autoUpdate = false; update_install_start(update_newer()); }
    static bool autoPlay = getenv("ROTKFIX_PLAY") != nullptr;
    if (autoPlay && ttShown) { autoPlay = false; tt_play(a, a.lib[a.cur]); }
    ImGui::SetCursorPos(ImVec2(bx, by));
    bool playOk = !(ttShown ? a.running != NULL : (!own || !g.exeFound || g_fixRemoved));
    // a controller's Start is Play, and its View / Back button Save, wherever the cursor is (not with a menu open,
    // a control being changed or a button being set)
    bool padFree = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) && !ImGui::IsAnyItemActive() &&
                   a.capture < 0 && a.kbCapture < 0 && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    bool padPlay = padFree && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false);
    bool padSave = padFree && ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false);
    if (orn_arch_button("PLAY", ImVec2(playW, playH), F_PLAY, playOk) || (padPlay && playOk)) {
        if (ttShown) tt_play(a, a.lib[a.cur]);
        else pc_play(a);   // (Athelas updated since ROTK Fix was set up there: the game gets the new ROTK Fix first)
    }
    // back to the front screen (the carousel), bottom left
    ImGui::SetCursorPos(ImVec2(pad + S(6), by + (playH - btnH) * 0.5f));
    if (orn_plate_button("\xe2\x80\xb9  Library", ImVec2(S(130), btnH), true) ||
        (padFree && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)))
        back_to_front(a);
    bx -= saveW + S(12);
    ImGui::SetCursorPos(ImVec2(bx, by + (playH - btnH) * 0.5f));
    bool saveOk = ttShown || !(!own || !g.exeFound || g_fixRemoved);
    if (orn_plate_button("Save", ImVec2(saveW, btnH), saveOk) || (padSave && saveOk)) {
        if (ttShown) tt_save(a); else save(a);
    }

    draw_transition(a, size);
    ImGui::End();
}
