// Athelas - the Mods tab: plugin mods and data mods.
#include "app.h"

struct ModEntry {
    std::string id;
    std::string name;
    std::string description;
    std::string author;
    std::string version;
    std::string category;
    std::string dllName;
    std::string iniPath;
    std::string hotkeys;
    std::string folder;
    std::vector<std::string> games;
    bool enabled = true;
    bool supportsAll = false;

    bool supportsGame(const std::string &gameKey) const {
        if (supportsAll) return true;
        for (const auto &g : games) {
            if (!_stricmp(g.c_str(), gameKey.c_str()) || !_stricmp(g.c_str(), "all")) return true;
        }
        return false;
    }
};

static std::string trim_str(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> split_games(const std::string &s) {
    std::vector<std::string> res;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t next = s.find(',', pos);
        if (next == std::string::npos) next = s.size();
        std::string item = trim_str(s.substr(pos, next - pos));
        if (!item.empty()) res.push_back(item);
        pos = next + 1;
    }
    return res;
}

static const char *game_key_title(const std::string &k) {
    if (!_stricmp(k.c_str(), "rotk-pc")) return "ROTK (PC)";
    if (!_stricmp(k.c_str(), "rotk-xbox")) return "ROTK (Xbox)";
    if (!_stricmp(k.c_str(), "tt-xbox")) return "Two Towers (Xbox)";
    if (!_stricmp(k.c_str(), "fotr-xbox")) return "Fellowship (Xbox)";
    if (!_stricmp(k.c_str(), "ta-xbox")) return "The Third Age (Xbox)";
    if (!_stricmp(k.c_str(), "all")) return "All Games";
    return k.c_str();
}

static std::string get_current_game_key(const App &a) {
    if (a.cur >= 0 && a.cur < (int)a.lib.size()) {
        GameKind k = a.lib[a.cur].kind;
        if (k == GK_ROTK_PC) return "rotk-pc";
        if (k == GK_ROTK_XBOX) return "rotk-xbox";
        if (k == GK_TT_XBOX) return "tt-xbox";
        if (k == GK_FOTR_XBOX) return "fotr-xbox";
        if (k == GK_TA_XBOX) return "ta-xbox";
    }
    return "rotk-pc";
}

static std::vector<ModEntry> scan_mods_dir(const std::string &dir, const std::string &rotkFixIni) {
    std::vector<ModEntry> list;
    if (dir.empty()) return list;

    std::string pattern = dir + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return list;

    std::map<std::string, ModEntry> found;

    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;

        // (folders: not mods. ROTK Fix loads the .dll files in the mods folder itself; mods\Data holds replacement
        // game files)
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        {
            std::string fn = fd.cFileName;
            size_t dot = fn.find_last_of('.');
            if (dot == std::string::npos) continue;
            std::string ext = fn.substr(dot);
            std::string base = fn.substr(0, dot);

            if (!_stricmp(ext.c_str(), ".ini")) {
                std::string ini = dir + "\\" + fn;
                ModEntry m;
                m.iniPath = ini;
                m.id = base;
                char buf[512] = {};
                GetPrivateProfileStringA("Mod", "Name", base.c_str(), buf, sizeof buf, ini.c_str());
                m.name = buf;
                GetPrivateProfileStringA("Mod", "Description", "External standalone mod plugin.", buf, sizeof buf, ini.c_str());
                m.description = buf;
                GetPrivateProfileStringA("Mod", "Author", "Community", buf, sizeof buf, ini.c_str());
                m.author = buf;
                GetPrivateProfileStringA("Mod", "Version", "1.0", buf, sizeof buf, ini.c_str());
                m.version = buf;
                GetPrivateProfileStringA("Mod", "Category", "Gameplay", buf, sizeof buf, ini.c_str());
                m.category = buf;
                GetPrivateProfileStringA("Mod", "Hotkeys", "", buf, sizeof buf, ini.c_str());
                m.hotkeys = buf;
                GetPrivateProfileStringA("Mod", "DLL", (base + ".dll").c_str(), buf, sizeof buf, ini.c_str());
                m.dllName = buf;
                GetPrivateProfileStringA("Mod", "Games", "rotk-pc", buf, sizeof buf, ini.c_str());
                m.games = split_games(buf);
                int en = GetPrivateProfileIntA("Mod", "Enabled", 1, ini.c_str());
                m.enabled = (en != 0);
                for (const auto &g : m.games) if (!_stricmp(g.c_str(), "all")) m.supportsAll = true;
                found[base] = m;
            } else if (!_stricmp(ext.c_str(), ".dll")) {
                if (found.find(base) == found.end()) {
                    ModEntry m;
                    m.id = base;
                    m.name = base;
                    m.dllName = fn;
                    m.description = "External standalone code mod (" + fn + ").";
                    m.author = "Unknown";
                    m.version = "1.0";
                    m.category = "Gameplay";
                    m.games = { "rotk-pc" };
                    m.enabled = true;
                    found[base] = m;
                }
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    for (auto &kv : found) {
        ModEntry &m = kv.second;
        if (!rotkFixIni.empty() && GetFileAttributesA(rotkFixIni.c_str()) != INVALID_FILE_ATTRIBUTES) {
            char val[16] = {};
            if (GetPrivateProfileStringA("Mods", m.dllName.c_str(), "", val, sizeof val, rotkFixIni.c_str()) > 0) {
                m.enabled = (atoi(val) != 0);
            }
        }
        list.push_back(m);
    }
    return list;
}

static void save_mod_state(const ModEntry &m, const std::string &rotkFixIni) {
    if (!m.iniPath.empty()) {
        WritePrivateProfileStringA("Mod", "Enabled", m.enabled ? "1" : "0", m.iniPath.c_str());
    }
    if (!rotkFixIni.empty()) {
        WritePrivateProfileStringA("Mods", m.dllName.c_str(), m.enabled ? "1" : "0", rotkFixIni.c_str());
    }
}

// Data mods, one folder each in ROTK Fix's mods folder (<mod>\Data\...), in an order the player sets (as COGBox has
// it): ROTKFix.ini [ModOrder] Order=a|b|c, [DataMods] <mod>=0/1. ROTK Fix uses them top to bottom; the lower one
// wins a file both change. "Add a mod (.zip)" unpacks a data mod there, or a code mod's .dll/.ini after a warning.
static std::vector<std::string> data_mods_ordered(const std::string &dir, const std::string &ini)
{
    std::vector<std::string> on_disk, out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.' || !_stricmp(fd.cFileName, "Data")) continue;
            DWORD at = GetFileAttributesA((dir + "\\" + fd.cFileName + "\\Data").c_str());
            if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) on_disk.push_back(fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    char order[2048] = "";
    GetPrivateProfileStringA("ModOrder", "Order", "", order, sizeof order, ini.c_str());
    std::string o = order;
    size_t at = 0;
    while (at <= o.size()) {
        size_t bar = o.find('|', at);
        std::string n = o.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
        for (auto it = on_disk.begin(); it != on_disk.end(); ++it)
            if (!_stricmp(it->c_str(), n.c_str())) { out.push_back(*it); on_disk.erase(it); break; }
        if (bar == std::string::npos) break;
        at = bar + 1;
    }
    for (auto &n : on_disk) out.push_back(n);
    return out;
}

static void data_mods_store(const std::vector<std::string> &v, const std::string &ini)
{
    std::string o;
    for (auto &n : v) o += (o.empty() ? "" : "|") + n;
    WritePrivateProfileStringA("ModOrder", "Order", o.c_str(), ini.c_str());
}

static std::string mod_folder_name(const std::string &zip)
{
    std::string b = zip.substr(zip.find_last_of('\\') + 1);
    if (b.size() > 4 && !_stricmp(b.c_str() + b.size() - 4, ".zip")) b.resize(b.size() - 4);
    std::string r;
    for (char c : b) r += (isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ') ? c : '_';
    while (!r.empty() && (r.back() == ' ' || r.back() == '.')) r.pop_back();
    if (r.empty() || !_stricmp(r.c_str(), "Data")) r = "Mod";
    return r.substr(0, 60);
}

static void data_mods_section(App &a, const std::string &dir, const std::string &ini, bool &rescan)
{
    static std::string pendingZip;
    static ModZip pending;
    bool running = a.running != NULL;
    if (running) ImGui::BeginDisabled();
    if (ImGui::Button("Add a mod (.zip)...")) {
        std::string f;
        if (pick_file(f, L"Choose a mod's .zip", L"Zip files", L"*.zip")) {
            pending = mod_zip_inspect(f);
            pendingZip = f;
            if (!pending.error.empty()) set_message(a, pending.error, true);
            else if (pending.code) ImGui::OpenPopup("Add a code mod?");
            else {
                std::string name = mod_folder_name(f), err;
                int n = 0;
                if (mod_zip_install(f, dir, name, false, n, err)) {
                    std::vector<std::string> v = data_mods_ordered(dir, ini);
                    v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string &x) { return !_stricmp(x.c_str(), name.c_str()); }), v.end());
                    v.push_back(name);   // a new one goes last: it wins
                    data_mods_store(v, ini);
                    WritePrivateProfileStringA("DataMods", name.c_str(), "1", ini.c_str());
                    set_message(a, "The mod \"" + name + "\" is added and ticked (" + std::to_string(n) + " files).", false);
                } else set_message(a, "The mod couldn't be added: " + err, true);
                rescan = true;
            }
        }
    }
    if (running) ImGui::EndDisabled();
    ImGui::SetNextWindowSize(ImVec2(S(580), 0));
    if (ImGui::BeginPopupModal("Add a code mod?", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushTextWrapPos(0);
        std::string list;
        for (auto &d : pending.dlls) list += (list.empty() ? "" : ", ") + d;
        ImGui::Text("This mod is a program (%s) that runs inside the game, with the same rights as the game.", list.c_str());
        ImGui::TextColored(C_WARN, "Only add it if you trust where it came from. Athelas doesn't check what it does.");
        ImGui::TextUnformatted("It is added to ROTK Fix's mods folder and ticked; untick it below to turn it off.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Add it", ImVec2(S(150), 0))) {
            std::string err; int n = 0;
            if (mod_zip_install(pendingZip, dir, "", true, n, err)) set_message(a, "The code mod is added (" + std::to_string(n) + " files).", false);
            else set_message(a, "The mod couldn't be added: " + err, true);
            rescan = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(S(150), 0)) || dialog_back()) ImGui::CloseCurrentPopup();
        ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }

    std::vector<std::string> v = data_mods_ordered(dir, ini);
    if (v.empty()) return;
    section("Data mods");
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "Ticked mods are used top to bottom - where two change the same file, the lower one wins. "
                              "Your game's own files aren't changed. Changes apply the next time the game starts.");
    ImGui::PopTextWrapPos();
    bool changed = false;
    for (size_t i = 0; i < v.size(); i++) {
        ImGui::PushID((int)i + 9000);
        if (running) ImGui::BeginDisabled();
        bool on = GetPrivateProfileIntA("DataMods", v[i].c_str(), 1, ini.c_str()) != 0;
        if (ImGui::Checkbox("##on", &on)) WritePrivateProfileStringA("DataMods", v[i].c_str(), on ? "1" : "0", ini.c_str());
        ImGui::SameLine();
        char nm[160] = "", ver[40] = "";
        std::string mi = dir + "\\" + v[i] + "\\mod.ini";
        GetPrivateProfileStringA("Mod", "Name", v[i].c_str(), nm, sizeof nm, mi.c_str());
        GetPrivateProfileStringA("Mod", "Version", "", ver, sizeof ver, mi.c_str());
        ImGui::TextColored(on ? C_GOLD_H : C_DIM, "%d. %s%s%s", (int)i + 1, nm, ver[0] ? "  " : "", ver);
        ImGui::SameLine();
        if (i > 0 && ImGui::SmallButton("Up")) { std::swap(v[i], v[i - 1]); changed = true; }
        if (i > 0) ImGui::SameLine();
        if (i + 1 < v.size() && ImGui::SmallButton("Down")) { std::swap(v[i], v[i + 1]); changed = true; }
        if (i + 1 < v.size()) ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {   // to the Recycle Bin, so it can come back
            std::string from = dir + "\\" + v[i]; from.push_back('\0');
            SHFILEOPSTRUCTA op; memset(&op, 0, sizeof op);
            op.wFunc = FO_DELETE; op.pFrom = from.c_str();
            op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT | FOF_ALLOWUNDO;
            if (SHFileOperationA(&op) == 0 && !op.fAnyOperationsAborted) {
                WritePrivateProfileStringA("DataMods", v[i].c_str(), NULL, ini.c_str());
                set_message(a, "\"" + v[i] + "\" removed (it is in the Recycle Bin).", false);
                v.erase(v.begin() + i);
                changed = true;
            } else set_message(a, "It couldn't be removed (is the game running?).", true);
        }
        if (running) ImGui::EndDisabled();
        ImGui::PopID();
        if (changed) break;
    }
    if (changed) data_mods_store(v, ini);
    ImGui::Dummy(ImVec2(0, S(6)));
    section("Code mods");
}

void tab_mods(App &a)
{
    section("Mods & Plugins");

    std::string primaryDir = a.dir + "\\ROTKFix\\mods";
    std::string fallbackDir = pc_fix_source() + "\\ROTKFix\\mods";
    std::string localDir = exe_dir() + "\\mods";

    std::string activeDir = primaryDir;
    if (GetFileAttributesA(primaryDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (GetFileAttributesA(fallbackDir.c_str()) != INVALID_FILE_ATTRIBUTES) activeDir = fallbackDir;
        else if (GetFileAttributesA(localDir.c_str()) != INVALID_FILE_ATTRIBUTES) activeDir = localDir;
    }

    std::string rotkFixIni = a.dir + "\\ROTKFix.ini";
    if (GetFileAttributesA(rotkFixIni.c_str()) == INVALID_FILE_ATTRIBUTES) {
        rotkFixIni = exe_dir() + "\\ROTKFix.ini";
    }

    std::string curGameKey = get_current_game_key(a);

    static std::vector<ModEntry> s_mods;
    static double s_lastScan = 0;
    static bool s_forceScan = true;

    double now = ImGui::GetTime();
    if (s_forceScan || now - s_lastScan > 2.0) {
        s_mods = scan_mods_dir(activeDir, rotkFixIni);
        if (activeDir != fallbackDir && GetFileAttributesA(fallbackDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
            auto more = scan_mods_dir(fallbackDir, rotkFixIni);
            for (auto &m : more) {
                bool dup = false;
                for (auto &ex : s_mods) if (!_stricmp(ex.id.c_str(), m.id.c_str())) { dup = true; break; }
                if (!dup) s_mods.push_back(m);
            }
        }
        s_lastScan = now;
        s_forceScan = false;
    }

    int activeCount = 0;
    for (auto &m : s_mods) if (m.enabled) activeCount++;

    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "Athelas supports external code mods (.dll) and asset replacements loaded dynamically without "
                              "altering original game discs or archives. Below you can toggle each mod and inspect its properties.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, S(4)));

    // Actions
    if (ImGui::Button("Open Mods Folder")) {
        SHCreateDirectoryExA(NULL, activeDir.c_str(), NULL);
        ShellExecuteA(NULL, "open", activeDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        s_forceScan = true;
    }
    ImGui::SameLine(0, S(16));
    static char s_modSearch[64] = "";
    ImGui::SetNextItemWidth(std::max(S(120), ImGui::GetContentRegionAvail().x - S(70)));
    ImGui::InputTextWithHint("##modsearch", "Search mods...", s_modSearch, sizeof(s_modSearch));
    if (s_modSearch[0] != '\0') {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            s_modSearch[0] = '\0';
        }
    }
    ImGui::TextColored(C_GOLD, "%d mod%s found  |  %d active", (int)s_mods.size(), s_mods.size() == 1 ? "" : "s", activeCount);
    data_mods_section(a, primaryDir, rotkFixIni, s_forceScan);

    ImGui::Dummy(ImVec2(0, S(8)));

    if (s_mods.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_DIM, "No mods found in:");
        ImGui::TextColored(C_TEXT, "  %s", activeDir.c_str());
        ImGui::Spacing();
        ImGui::TextColored(C_DIM, "To install a mod, drop its .dll and .ini files into that folder and press Refresh.");
        ImGui::PopTextWrapPos();
        return;
    }

    std::string q = s_modSearch;
    for (char &c : q) c = (char)tolower(c);

    int shownCount = 0;
    for (size_t i = 0; i < s_mods.size(); i++) {
        ModEntry &m = s_mods[i];
        if (!q.empty()) {
            std::string hay = m.name + " " + m.description + " " + m.author + " " + m.category + " " + m.dllName;
            for (const auto &g : m.games) hay += " " + g;
            for (char &c : hay) c = (char)tolower(c);
            if (hay.find(q) == std::string::npos) continue;
        }
        shownCount++;
        bool comp = m.supportsGame(curGameKey);

        ImGui::PushID((int)i);
        ImGui::BeginGroup();

        if (ImGui::Checkbox("##en", &m.enabled)) {
            save_mod_state(m, rotkFixIni);
        }
        ImGui::SameLine();
        ImGui::PushFont(F_TAB, F_TAB->LegacySize);
        ImGui::TextColored(m.enabled ? C_GOLD_H : C_DIM, "%s", m.name.c_str());
        ImGui::PopFont();

        ImGui::SameLine();
        if (m.enabled) {
            ImGui::TextColored(C_GOOD, "[Active]");
        } else {
            ImGui::TextColored(C_DIM, "[Disabled]");
        }

        ImGui::Indent(S(28));
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::TextColored(C_DIM, "v%s  |  Author: %s  |  Category: %s  |  Binary: %s",
                           m.version.c_str(), m.author.c_str(), m.category.c_str(), m.dllName.c_str());
        ImGui::PopFont();

        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::TextColored(C_GOLD, "Supported Games:");
        for (const auto &g : m.games) {
            ImGui::SameLine();
            bool curMatch = (_stricmp(g.c_str(), curGameKey.c_str()) == 0 || !_stricmp(g.c_str(), "all"));
            ImGui::TextColored(curMatch ? C_GOOD : C_DIM, "[ %s%s ]", game_key_title(g), curMatch ? " (Current)" : "");
        }
        if (!comp) {
            ImGui::SameLine();
            ImGui::TextColored(C_WARN, "(Requires other LOTR game)");
        }
        ImGui::PopFont();

        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_TEXT, "%s", m.description.c_str());
        if (!m.hotkeys.empty()) {
            ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
            ImGui::TextColored(C_GOLD_H, "Controls / Hotkeys: %s", m.hotkeys.c_str());
            ImGui::PopFont();
        }
        ImGui::PopTextWrapPos();

        ImGui::Unindent(S(28));
        ImGui::EndGroup();

        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, S(6)));

        ImGui::PopID();
    }

    if (shownCount == 0 && !s_mods.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::Spacing();
        ImGui::TextColored(C_DIM, "No mods match your search query \"%s\".", s_modSearch);
        ImGui::PopTextWrapPos();
    }
}
