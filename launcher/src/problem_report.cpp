// Athelas - Report a problem: the report zip, and the box shown when a game crashes.
#include "app.h"

// ---- A problem report: every log and settings file Athelas knows of and a description of the PC in one zip
// (report.cpp), then GitHub's new-issue page with what the zip can't say filled in. Athelas sends nothing: the
// player reads the page, writes what happened and drags the zip in, or doesn't.
static std::string g_reportZip, g_reportUrl, g_reportErr;

static std::string report_game_name(const App &a)
{
    std::string n = a.cur >= 0 && a.cur < (int)a.lib.size() ? game_kind_name(a.lib[a.cur].kind) : "The Return of the King - PC";
    for (size_t at; (at = n.find("  ")) != std::string::npos;) n.erase(at, 1);   // the page's wide "  -  " as " - "
    return n;
}

static std::string report_versions(const App &a)
{
    std::string s = "Athelas " LAUNCHER_VERSION;
    std::string arch = exe_machine(exe_dir() + "\\Athelas.exe");
    std::string pcfix = a.game.hasDll ? fix_dll_version(a.dir + "\\ROTKFix.dll") : "";
    if (!pcfix.empty()) s += ", ROTK Fix " + pcfix;
    for (int who = 0; who < k_tomlPortCount; who++) {   // the TOML ports: the one in the tt fields, and the others
        const Port &p = who == a.ttWho ? a.tt : a.ttPark[who];
        if (p.exe.empty()) continue;
        std::string m = exe_machine(p.exe), name = k_tomlPorts[who].exe;
        s += ", " + (p.version.empty() ? name.substr(0, name.size() - 4) : p.version) + (m.empty() ? "" : " " + m);
    }
    return s;
}

static bool make_report(App &a, std::string title)
{
    g_reportErr.clear();
    // the ports aren't asked again here (a port that doesn't answer held the window up for minutes, GitHub #42): the
    // versions known so far, and each port's port-query.log, go in the report
    std::vector<ReportFile> files;
    files.push_back({ "Athelas.ini", ports_ini() });
    if (!a.dir.empty()) {
        files.push_back({ "ROTK-PC/ROTKFix.log", a.dir + "\\ROTKFix.log" });
        files.push_back({ "ROTK-PC/ROTKFix.ini", a.dir + "\\ROTKFix.ini" });
        if (!a.game.rotkIniPath.empty()) files.push_back({ "ROTK-PC/ROTK.ini", a.game.rotkIniPath });
    }
    std::vector<std::pair<std::string, std::string>> dirs;   // each port's folder: name in the zip, where
    for (int who = 0; who < k_tomlPortCount; who++) {
        g_ttWho = who;   // tt_ paths only (the fields stay as they are)
        std::string d = tp().dir;
        files.push_back({ d + "/" + tp().log, tt_log_path() });
        files.push_back({ d + "/settings.toml", tt_settings_path() });
        files.push_back({ d + "/port-query.log", tt_data_dir() + "\\port-query.log" });
        dirs.push_back({ d, tt_data_dir() });
    }
    g_ttWho = a.ttWho;
    // the newest crash dump of each port, when it is from the last two weeks (an old one is another problem)
    for (auto &d : dirs) {
        std::string dmp = report_newest_dump(d.second + "\\CrashDumps", 14);
        if (!dmp.empty()) files.push_back({ d.first + "/" + dmp.substr(dmp.find_last_of('\\') + 1), dmp, false });
    }
    std::string sys = report_system_text();
    std::string versions = report_versions(a);
    GpuInfo gi = gpu_info();
    std::string vk = gi.state == GPU_VULKAN_OK ? "Vulkan: " + gi.name + (gi.software ? " (software)" : "")
                   : gi.state == GPU_VULKAN_TOO_OLD ? "Vulkan: too old for DXVK (" + gi.name + ")"
                   : gi.state == GPU_NO_VULKAN ? "Vulkan: none" : "Vulkan: still checking";
    std::string pads;
    for (SDL_Gamepad *p : a.pads) {
        const char *n = SDL_GetGamepadName(p);
        if (n) pads += (pads.empty() ? "" : ", ") + std::string(n);
    }
    std::string extra = "Versions: " + versions + "\r\n" + vk + "\r\nControllers: " + (pads.empty() ? "none" : pads) + "\r\n";
    for (auto &g : a.lib)
        extra += std::string("Game: ") + game_kind_name(g.kind) + (g.iso ? ", disc image" : ", folder") +
                 (g.titleId ? ", " + std::string(game_region_name(g)) + " copy" : "") + " - " + g.path + "\r\n";
    if (!report_write(files, sys + extra, g_reportZip, g_reportErr)) return false;

    // the issue: the template's headings, with what is known filled in
    std::string pcLines;
    for (size_t at = 0; at < sys.size();) {
        size_t e = sys.find("\r\n", at);
        std::string l = sys.substr(at, e - at);
        if (!l.rfind("Windows:", 0) || !l.rfind("Processor:", 0) || !l.rfind("Memory:", 0) || !l.rfind("Graphics:", 0) || !l.rfind("Display:", 0))
            pcLines += "- " + l + "\n";
        if (e == std::string::npos) break;
        at = e + 2;
    }
    pcLines += "- Controllers: " + (pads.empty() ? std::string("none connected") : pads) + "\n";
    std::string zipName = g_reportZip.substr(g_reportZip.find_last_of('\\') + 1);
    std::string body = "**Which game**\n" + report_game_name(a) + "\n\n**Athelas version**\n" + versions +
                       "\n\n**What happened**\n<!-- What you saw, and what you did just before. Screenshots help. -->\n\n\n"
                       "**Your PC**\n" + report_anonymise(pcLines) +
                       "\n**The log**\n<!-- Drag \"" + zipName + "\" from the folder Athelas just opened into this box. "
                       "It has the logs and settings, with your Windows user name taken out. -->\n";
    if (title.empty()) title = report_game_name(a) + ": ";
    g_reportUrl = "https://github.com/Shagg88/Athelas/issues/new?labels=bug&title=" + url_encode(title) + "&body=" + url_encode(body);
    ShellExecuteA(NULL, "open", "explorer.exe", ("/select,\"" + g_reportZip + "\"").c_str(), NULL, SW_SHOWNORMAL);
    ShellExecuteA(NULL, "open", g_reportUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return true;
}

// On each About page
void report_section(App &a)
{
    section("Report a problem");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted("Something wrong? This puts the logs, the settings and a description of this PC into one zip, "
                           "then opens a new report on GitHub with the details filled in. Write what happened and drag "
                           "the zip in.");
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::TextColored(C_DIM, "Athelas doesn't send anything itself: you see the report and choose what to post. Your "
                              "Windows user name is taken out of the files; the folders your games are in stay.");
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Make a problem report")) {
        if (!make_report(a, "")) set_message(a, g_reportErr, true);
    }
    if (!g_reportZip.empty() && g_reportErr.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Show the zip")) ShellExecuteA(NULL, "open", "explorer.exe", ("/select,\"" + g_reportZip + "\"").c_str(), NULL, SW_SHOWNORMAL);
        ImGui::SameLine();
        if (ImGui::Button("Open the report page")) ShellExecuteA(NULL, "open", g_reportUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_GOOD, "Saved: %s", g_reportZip.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
}

// After a crash: the log and a bug report a click away (the report opens the repository's form, its title filled in)
void crash_box(App &a, const std::string &text, const std::string &title)
{
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Close" },
        { 0, 1, "Open log folder" },
        { 0, 2, "Report a problem" },
    };
    std::string t = text + "\n\nIf it keeps happening, please report it: \"Report a problem\" puts the logs into one zip "
                           "and opens the report page on GitHub, for you to attach it.";
    SDL_MessageBoxData box = { SDL_MESSAGEBOX_ERROR, NULL, "Athelas", t.c_str(), 3, buttons, NULL };
    int pressed = 0;
    if (!SDL_ShowMessageBox(&box, &pressed)) return;
    if (pressed == 1) {
        if (GetFileAttributesA(a.runningLog.c_str()) != INVALID_FILE_ATTRIBUTES)
            ShellExecuteA(NULL, "open", "explorer.exe", ("/select,\"" + a.runningLog + "\"").c_str(), NULL, SW_SHOWNORMAL);
        else
            ShellExecuteA(NULL, "open", a.runningLog.substr(0, a.runningLog.find_last_of('\\')).c_str(), NULL, NULL, SW_SHOWNORMAL);
    } else if (pressed == 2) {
        if (!make_report(a, title)) {   // the report couldn't be written: the form on its own
            std::string url = "https://github.com/Shagg88/Athelas/issues/new?template=bug_report.md&title=" + url_encode(title);
            ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    }
}
