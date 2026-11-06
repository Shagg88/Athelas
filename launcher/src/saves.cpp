// Athelas - the Saves section: save backups, logs and shader caches.
#include "app.h"

std::string logs_dir() { return exe_dir() + "\\Logs"; }   // one folder per play (userdata.h)

static void open_folder(const std::string &d) { SHCreateDirectoryExA(NULL, d.c_str(), NULL); ShellExecuteA(NULL, "open", d.c_str(), NULL, NULL, SW_SHOWNORMAL); }

static void show_file(const std::string &f) { ShellExecuteA(NULL, "open", "explorer.exe", ("/select,\"" + f + "\"").c_str(), NULL, SW_SHOWNORMAL); }

// On each game's About page (as COGBox has it): the saves backed up to a zip on the desktop and put back from one,
// the folders, and for the Xbox ports their saved shaders cleared (set aside, not deleted)
void saves_section(App &a, const std::string &game, const std::string &saves, const std::string &shaders, const std::string &gameDir)
{
    section("Folders");
    if (!gameDir.empty()) {
        if (ImGui::Button("Game folder")) ShellExecuteA(NULL, "open", gameDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
        tip(gameDir.c_str());
        ImGui::SameLine();
    }
    std::string where = "Saves: " + (saves.empty() ? std::string("(not found)") : saves);
    bool busy = a.running != NULL || saves.empty();
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Back up saves")) {
        if (!folder_has_files(saves)) set_message(a, game + " has no saves yet.", false);
        else {
            std::string zip = backup_name(game), err;
            int n = 0;
            if (backup_folder(saves, zip, n, err, game)) {
                set_message(a, "Saves backed up (" + std::to_string(n) + (n == 1 ? " file" : " files") + ") to your desktop: " +
                               zip.substr(zip.find_last_of('\\') + 1), false);
                show_file(zip);
            } else set_message(a, "The saves couldn't be backed up: " + err, true);
        }
    }
    tip(("A copy of this game's saves in a zip on your desktop.\n" + where).c_str());
    ImGui::SameLine();
    static std::string pickZip, pickFor;
    if (ImGui::Button("Restore saves...")) {
        std::string f;
        if (pick_file(f, L"Choose a saves backup (a zip Athelas made)", L"Zip files", L"*.zip")) {
            pickZip = f; pickFor = saves;
            ImGui::OpenPopup("Restore saves?");
        }
    }
    tip("Puts a backup back. Your current saves are backed up first, so nothing is lost.");
    ImGui::SameLine();
    if (ImGui::Button("Saves")) open_folder(saves);
    tip(where.c_str());
    if (busy) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Logs")) {
        std::string n = session_newest(logs_dir());
        if (n.empty()) open_folder(logs_dir()); else show_file(n);
    }
    if (!shaders.empty()) {
        ImGui::SameLine();
        if (a.running) ImGui::BeginDisabled();
        if (ImGui::Button("Clear shaders...")) {
            if (!folder_has_files(shaders)) set_message(a, game + " has no saved shaders to clear.", false);
            else ImGui::OpenPopup("Clear shaders?");
        }
        if (a.running) ImGui::EndDisabled();
    }

    ImGui::SetNextWindowSize(ImVec2(S(560), 0));
    if (ImGui::BeginPopupModal("Restore saves?", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushTextWrapPos(0);
        ImGui::Text("Put back the saves in %s?", pickZip.substr(pickZip.find_last_of('\\') + 1).c_str());
        ImGui::TextUnformatted("The saves there now are replaced. They are backed up to a zip on your desktop first "
                               "(\"before restore\"), so you can go back.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Restore", ImVec2(S(150), 0))) {
            std::string before, err;
            if (restore_folder(pickZip, pickFor, game, before, err))
                set_message(a, before.empty() ? "Saves restored." : "Saves restored. The ones before are in " + before.substr(before.find_last_of('\\') + 1) + " on your desktop.", false);
            else set_message(a, "The saves weren't restored: " + err, true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(S(150), 0)) || dialog_back()) ImGui::CloseCurrentPopup();
        ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowSize(ImVec2(S(560), 0));
    if (ImGui::BeginPopupModal("Clear shaders?", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushTextWrapPos(0);
        ImGui::Text("Clear the saved shaders of %s?", game.c_str());
        ImGui::TextUnformatted("The next start builds them again, so expect some stutter the first time, as on a new PC. "
                               "The folder is set aside (renamed), not deleted. Your saves and settings aren't touched.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Clear", ImVec2(S(150), 0))) {
            std::string to, err;
            if (shaders_set_aside(shaders, to, err)) set_message(a, "Shaders cleared (set aside as " + to.substr(to.find_last_of('\\') + 1) + ").", false);
            else set_message(a, err, true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(S(150), 0)) || dialog_back()) ImGui::CloseCurrentPopup();
        ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }
}
