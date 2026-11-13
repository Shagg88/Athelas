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

// A crash dump is a snapshot of the game's memory: unlike the logs, the Windows user name in its folder names can't
// be taken out. The player chooses whether it goes in the zip ([Reports] CrashDump in Athelas.ini, on by default).
static bool report_with_dump()
{
    return GetPrivateProfileIntA("Reports", "CrashDump", 1, ports_ini().c_str()) != 0;
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
    bool anyDump = false;
    if (report_with_dump()) for (auto &d : dirs) {
        std::string dmp = report_newest_dump(d.second + "\\CrashDumps", 14);
        if (!dmp.empty()) { files.push_back({ d.first + "/" + dmp.substr(dmp.find_last_of('\\') + 1), dmp, false }); anyDump = true; }
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
                       "It has the logs and settings, with your Windows user name taken out" +
                       std::string(anyDump ? " (the crash file in it is a memory snapshot and may still hold it)" : "") + ". -->\n";
    if (title.empty()) title = report_game_name(a) + ": ";
    // GitHub's new-issue form is filled from its address; the text also goes on the clipboard, in case a long report
    // is cut short by the browser
    SDL_SetClipboardText(("Title: " + title + "\n\n" + body).c_str());
    g_reportUrl = std::string(kTicketsNew) + "?labels=bug&title=" + url_encode(title) + "&body=" + url_encode(body);
    if (g_reportUrl.size() > 2000) g_reportUrl = std::string(kTicketsNew) + "?labels=bug&title=" + url_encode(title);
    ShellExecuteA(NULL, "open", "explorer.exe", ("/select,\"" + g_reportZip + "\"").c_str(), NULL, SW_SHOWNORMAL);
    ShellExecuteA(NULL, "open", g_reportUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return true;
}

// On each About page
void report_section(App &a)
{
    section("Problems");
    if (ImGui::Button("Report a problem")) {
        if (!make_report(a, "")) set_message(a, g_reportErr, true);
    }
    tip("Puts the logs, the settings and a description of this PC into one zip, opens a new issue on GitHub with the report "
        "filled in (it's on the clipboard too): drag the zip in. "
        "Nothing is sent by itself; your Windows user name is taken out of the logs and settings.");
    ImGui::SameLine();
    if (ImGui::Button("Open the tickets")) ShellExecuteA(NULL, "open", kTickets, NULL, NULL, SW_SHOWNORMAL);
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
    {
        bool dump = report_with_dump();
        if (ImGui::Checkbox("Include the newest crash file", &dump))
            WritePrivateProfileStringA("Reports", "CrashDump", dump ? "1" : "0", ports_ini().c_str());
        tip("After a crash, a game saves a snapshot of its memory (a .dmp file); the newest one from the last two weeks goes "
            "in the zip, and it is what most helps to find a crash. Unlike the logs it can't be cleaned: it can hold folder "
            "names, including your Windows user name. Untick this to leave it out.");
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
                           "and opens the report page on GitHub, for you to attach it." +
                    std::string(report_with_dump() ? "\n\nThe zip includes the crash file (a snapshot of the game's memory, which can hold folder "
                                                     "names with your Windows user name). About > Problems has a switch to leave it out." : "");
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
            SDL_SetClipboardText(title.c_str());
            ShellExecuteA(NULL, "open", kTicketsNew, NULL, NULL, SW_SHOWNORMAL);
        }
    }
}
