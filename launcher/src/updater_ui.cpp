// Athelas - the update question and the About tab's update section (update.cpp does the work).
#include "app.h"

// [Updates] Check in Athelas.ini: whether the launcher asks GitHub at start for the newest release's number (on by default)
bool updates_on() { return GetPrivateProfileIntA("Updates", "Check", 1, ports_ini().c_str()) != 0; }

// "Update Athelas?": asked from the About tab or the header's button, drawn by the main window each frame
bool g_askUpdate;

void update_popup(App &a)
{
    std::string v = update_newer();
    if (g_askUpdate) { g_askUpdate = false; if (!v.empty()) ImGui::OpenPopup("Update Athelas?"); }
    // the question, in the launcher's own look: the settings panel's frame and fill, a heading, plate buttons
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(S(560), 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, rgb(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, rgb(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, rgb(0, 0, 0, 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(30), S(26)));
    if (ImGui::BeginPopupModal("Update Athelas?", NULL,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetWindowPos(), p1 = p0 + ImGui::GetWindowSize();
        dl->PushClipRectFullScreen();
        orn_panel_fill(dl, p0, p1, ImGui::GetColorU32(rgb(18, 17, 15, 0.97f)));
        orn_panel_frame(dl, p0, p1);
        dl->PopClipRect();
        section("Update Athelas");
        ImGui::PushTextWrapPos(0);
        ImGui::Text("Download Athelas %s (about 125 MB) and install it here?", v.c_str());
        ImGui::TextColored(C_DIM, "The launcher starts again when it's done. Your settings, saves and achievements are kept.");
        if (a.running) ImGui::TextColored(C_WARN, "Close the game first.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, S(8)));
        float bw = S(150), bh = S(40);
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - S(30) - bw * 2 - S(12));
        if (orn_plate_button("Update", ImVec2(bw, bh), a.running == NULL)) { update_install_start(v); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(0, S(12));
        if (orn_plate_button("Not now", ImVec2(bw, bh), true) || dialog_back()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

// About, both ROTK pages: a newer Athelas (installed from here, or its page), and the switch for looking
void update_section(App &a)
{
    (void)a;
    section("Athelas");
    std::string v = update_newer();
    float prog = 0;
    std::string err;
    int st = update_install_state(&prog, &err);
    ImGui::PushTextWrapPos(0);
    if (st == 1 || st == 2) {
        ImGui::TextColored(C_GOLD_H, st == 1 ? "Downloading Athelas %s..." : "Installing Athelas %s...", v.empty() ? LAUNCHER_VERSION : v.c_str());
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, rgb(176, 138, 62));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, rgb(30, 27, 23));
        ImGui::ProgressBar(prog, ImVec2(-1, S(8)), "");
        ImGui::PopStyleColor(2);
        ImGui::TextColored(C_DIM, "%d%%", (int)(prog * 100 + 0.5f));
    } else if (st == 3) {
        ImGui::TextColored(C_GOOD, "Athelas %s is installed. Start Athelas again to use it.", v.empty() ? LAUNCHER_VERSION : v.c_str());
    } else if (v.empty() && native_arm64() && !arm_files_current()) {
        // a Windows on ARM PC without this release's ARM64 builds: the same version's zip (which has them), installed over it
        ImGui::TextColored(C_GOLD_H, "This PC can run the ARM64 builds of the Xbox ports, which are faster here. "
                                     "They aren't installed for Athelas " LAUNCHER_VERSION " yet.");
        if (st == -1) ImGui::TextColored(C_BAD, "%s", err.c_str());
        else ImGui::TextUnformatted("Your settings, saves and achievements are kept.");
        if (ImGui::Button("Install the ARM64 builds")) update_install_start(LAUNCHER_VERSION);
    } else if (!v.empty()) {
        ImGui::TextColored(C_GOLD_H, "Athelas %s is out (you have " LAUNCHER_VERSION ").", v.c_str());
        if (st == -1) ImGui::TextColored(C_BAD, "%s", err.c_str());
        else ImGui::TextUnformatted("Your settings, saves and achievements are kept.");
        if (ImGui::Button("Update now")) g_askUpdate = true;
        ImGui::SameLine();
        if (ImGui::Button("Open the download page")) ShellExecuteA(NULL, "open", update_page(), NULL, NULL, SW_SHOWNORMAL);
    } else {
        // (update_checked: GitHub answered, or couldn't be reached - as COGBox says it)
        if (!updates_on()) status_dot(true, true, "Athelas " LAUNCHER_VERSION " - checking for updates is off", nullptr);
        else if (!update_checked()) status_dot(false, true, "Athelas " LAUNCHER_VERSION " - checking for updates...", nullptr);
        else status_dot(true, false, "Athelas " LAUNCHER_VERSION " - the latest (or GitHub and GitLab couldn't be reached)", nullptr);
    }
    ImGui::PopTextWrapPos();
}
