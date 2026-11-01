// Athelas - desktop and Start menu shortcuts.
#include "app.h"

// "Your games" (About tab): the list, adding and removing
// Shortcuts to Athelas: on the desktop and in the Start menu, made on request (nothing is made unasked)
static std::wstring known_folder(REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, NULL, &p)) && p) r = p;
    if (p) CoTaskMemFree(p);
    return r;
}

static std::wstring shortcut_path(bool desktop)
{
    static const std::wstring paths[2] = {
        [] { std::wstring d = known_folder(FOLDERID_Desktop); return d.empty() ? d : d + L"\\Athelas.lnk"; }(),
        [] { std::wstring d = known_folder(FOLDERID_Programs); return d.empty() ? d : d + L"\\Athelas.lnk"; }() };
    return paths[desktop ? 0 : 1];
}

static bool make_shortcut(const std::wstring &lnk, const std::wstring &args = L"",
                          const std::wstring &desc = L"Athelas - The Lord of the Rings games")
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    std::wstring dir = exe;
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IShellLinkW *sl = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void **)&sl))) {
        sl->SetPath(exe);
        sl->SetWorkingDirectory(dir.c_str());
        sl->SetIconLocation(exe, 0);
        sl->SetDescription(desc.c_str());
        if (!args.empty()) sl->SetArguments(args.c_str());
        IPersistFile *pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void **)&pf))) {
            ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
            pf->Release();
        }
        sl->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}

void shortcuts_section(App &a)
{
    section("Shortcuts");
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "Start Athelas from the desktop or the Start menu.");
    ImGui::PopTextWrapPos();
    for (int k = 0; k < 2; k++) {
        bool desktop = k == 0;
        std::wstring lnk = shortcut_path(desktop);
        bool there = !lnk.empty() && GetFileAttributesW(lnk.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (k) ImGui::SameLine();
        ImGui::PushID(k);
        const char *label = desktop ? (there ? "Desktop shortcut  (made)" : "Make a desktop shortcut")
                                    : (there ? "Start menu shortcut  (made)" : "Add to the Start menu");
        ImGui::BeginDisabled(lnk.empty());
        if (ImGui::Button(label)) {
            // made again when it's there: it then points to this copy of Athelas, wherever it is now
            if (make_shortcut(lnk)) set_message(a, desktop ? "Shortcut made on the desktop." : "Athelas added to the Start menu.", false);
            else set_message(a, "The shortcut couldn't be made.", true);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    // one for each game too (as COGBox has): it opens Athelas and starts that game, with its settings and fixes
    static const GameKind kinds[] = { GK_FOTR_XBOX, GK_TT_XBOX, GK_ROTK_XBOX, GK_ROTK_PC, GK_TA_XBOX };
    static const char *keys[] = { "fotr", "tt", "rotk-xbox", "rotk-pc", "ta" };
    std::wstring desk = known_folder(FOLDERID_Desktop);
    for (int k = 0; k < 5; k++) {
        bool have = false;
        for (const LibGame &g : a.lib) if (g.kind == kinds[k]) have = true;
        if (!have || desk.empty()) continue;
        std::string name = game_kind_name(kinds[k]);
        std::string label = "Desktop shortcut: " + name;
        ImGui::PushID(10 + k);
        if (ImGui::Button(label.c_str())) {
            std::wstring wname;
            for (char c : name) wname += strchr("\\/:*?\"<>|", c) ? L'-' : (wchar_t)(unsigned char)c;
            std::wstring args = L"--play " + std::wstring(keys[k], keys[k] + strlen(keys[k]));
            if (make_shortcut(desk + L"\\" + wname + L".lnk", args, L"Play " + wname + L" through Athelas"))
                set_message(a, "A " + name + " shortcut is on your desktop.", false);
            else set_message(a, "The shortcut couldn't be made.", true);
        }
        ImGui::PopID();
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "To pin one to the taskbar or Start, right-click the shortcut and choose Pin "
                              "(Windows doesn't let programs do that themselves).");
    ImGui::PopTextWrapPos();
}
