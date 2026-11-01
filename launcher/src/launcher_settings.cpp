// Athelas - the launcher's own settings: its window and the games it knows.
#include "app.h"

// Athelas' own window (as COGBox has it): fullscreen with F11 or LT + RT together, from any screen, a borderless window
// with its own buttons; its size and choices are remembered. Not the games' display settings.
bool launcher_ini_get(const char *k, int def) { return GetPrivateProfileIntA("Launcher", k, def, ports_ini().c_str()) != 0; }

void launcher_ini_put(const char *k, int v) { WritePrivateProfileStringA("Launcher", k, std::to_string(v).c_str(), ports_ini().c_str()); }

bool g_borderless;

SDL_Window *g_mainWin;   // (for the maker's mark: under the borderless window's own buttons)

void set_fullscreen(App &a, bool on)
{
    SDL_SetWindowFullscreen(a.win, on);
    launcher_ini_put("Fullscreen", on);
}

static void set_borderless(App &a, bool on)
{
    g_borderless = on;
    SDL_SetWindowBordered(a.win, !on);
    launcher_ini_put("Borderless", on);
}

static void window_section(App &a)
{
    section("This window");
    bool fs = (SDL_GetWindowFlags(a.win) & SDL_WINDOW_FULLSCREEN) != 0;
    if (ImGui::Checkbox("Fullscreen (F11, or LT + RT together)", &fs)) set_fullscreen(a, fs);
    bool bl = g_borderless;
    if (ImGui::Checkbox("Borderless window (no title bar)", &bl)) set_borderless(a, bl);
    row_begin("Language", "Athelas' own language. The games' languages are on their Gameplay tabs.\n"
                          "Translations are written by the project; corrections are welcome on GitHub (the lang folder).");
    language_combo();
    row_end();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "Athelas grows with its window: drag a corner, or maximise it. Borderless: Athelas draws its own "
                              "minimise, maximise and close buttons in the top-right corner; drag the strip beside them to move it. "
                              "These are for Athelas itself - each game's display settings are on its Display tab.");
    ImGui::PopTextWrapPos();
}

void games_section(App &a)
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

    shortcuts_section(a);
    window_section(a);

    section("Background pictures");
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "Shown behind the launcher, changing every 10 seconds. Game art is read from your own copy of "
                              "each game when the launcher shows it (for The Return of the King, the PC version has the most); nothing is copied. "
                              "My pictures are any PNG or JPG files you put in the backgrounds folder.");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(S(280));
    if (ImGui::BeginCombo("##bgshow", k_bgShowName[g_bgShow])) {
        for (int i = 0; i < BG_SHOW_COUNT; i++)
            if (ImGui::Selectable(k_bgShowName[i], i == g_bgShow) && i != g_bgShow) {
                g_bgShow = i;
                bg_show_save();
                a.bg.group = -1;           // list again on the next frame
            }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Open backgrounds folder")) {
        std::string d = exe_dir() + "\\ROTKFix\\backgrounds";
        SHCreateDirectoryExA(NULL, d.c_str(), NULL);
        ShellExecuteA(NULL, "open", d.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("Show new pictures")) a.bg.group = -1;   // list again on the next frame
}
