// Athelas - the Xbox ports: finding each port's program, its settings file, starting it, and a game closing.
#include "app.h"

// ---------------------------------------------------------------------------
// The Two Towers (Xbox), played through its PC port, TwoTowers.exe.
// The port is where the player pointed to it (remembered in ROTKFix\ports.ini), or
// else next to the launcher (TwoTowers\TwoTowers.exe). Its settings, texture
// packs and log live in ROTKFix\TwoTowers next to the launcher.

// The Xbox ports keep their settings, saves, logs and texture packs in their own folders beside Athelas
// (TT-Xbox, ROTK-Xbox). Earlier test builds kept them in ROTKFix\TwoTowers and ROTKFix\ROTKXbox: those are
// carried over once, when the new folder has no settings yet.
std::string port_data_dir(const char *dir, const char *oldDir)
{
    std::string d = exe_dir() + "\\" + dir;
    if (!oldDir) return d;   // a port that had no test builds before
    std::string old = exe_dir() + "\\ROTKFix\\" + oldDir;
    static bool moved[2];
    bool &done = moved[dir[1] == 'T' ? 0 : 1];   // "TT-Xbox" / "ROTK-Xbox"
    if (!done) {
        done = true;
        bool fresh = GetFileAttributesA((d + "\\settings.toml").c_str()) == INVALID_FILE_ATTRIBUTES;
        if (fresh && GetFileAttributesA(old.c_str()) != INVALID_FILE_ATTRIBUTES) copy_folder(old, d);
    }
    return d;
}

int g_ttWho = 0;                // App::ttWho, for the functions without the App

const TomlPortId &tp() { return k_tomlPorts[g_ttWho]; }

int tp_of(GameKind k) { return k == GK_FOTR_XBOX ? 1 : k == GK_ROTK_XBOX ? 2 : k == GK_TA_XBOX ? 3 : 0; }

bool tp_kind(GameKind k) { return k == GK_TT_XBOX || k == GK_FOTR_XBOX || k == GK_ROTK_XBOX || k == GK_TA_XBOX; }

// The tt fields hold this port's state from here on: the one there is parked in its place, this one taken out of its
void tp_use(App &a, int who)
{
    if (a.ttWho == who) return;
    int was = a.ttWho;
    std::swap(a.tt, a.ttPark[was]);
    std::swap(a.ttLooked, a.ttParkLooked[was]);
    std::swap(a.ttFile, a.ttParkFile[was]);
    std::swap(a.ttLoaded, a.ttParkLoaded[was]);
    std::swap(a.tt, a.ttPark[who]);
    std::swap(a.ttLooked, a.ttParkLooked[who]);
    std::swap(a.ttFile, a.ttParkFile[who]);
    std::swap(a.ttLoaded, a.ttParkLoaded[who]);
    a.ttWho = g_ttWho = who;
}

std::string tt_data_dir() { return port_data_dir(tp().dir, tp().oldDir); }

std::string tt_settings_path() { return tt_data_dir() + "\\settings.toml"; }

std::string tt_log_path() { return tt_data_dir() + "\\" + tp().log; }

std::string ports_ini() { return exe_dir() + "\\" ATHELAS_INI; }

// [Ports] in Athelas.ini, or the older ROTKFix\ports.ini
static std::string port_override(const char *key)
{
    char b[MAX_PATH * 2] = "";
    GetPrivateProfileStringA("Ports", key, "", b, sizeof b, ports_ini().c_str());
    if (!b[0]) GetPrivateProfileStringA("Ports", key, "", b, sizeof b, (exe_dir() + "\\ROTKFix\\ports.ini").c_str());
    return b;
}

static bool file_there(const std::string &p) { DWORD a = GetFileAttributesA(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

// A Windows on ARM PC runs a port's ARM64 build (<name>-arm64.exe beside the x64 one) natively; the x64 build only
// through Windows' translation, far slower. The launcher is an x86 program, so it asks for the machine's own CPU.
bool native_arm64()
{
    static int v = -1;
    if (v < 0) {
        typedef BOOL(WINAPI * Wow64Process2)(HANDLE, USHORT *, USHORT *);
        Wow64Process2 f = (Wow64Process2)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process2");
        USHORT process = 0, machine = 0;
        v = f && f(GetCurrentProcess(), &process, &machine) && machine == 0xAA64;   // IMAGE_FILE_MACHINE_ARM64
    }
    return v != 0;
}

// The ARM64 builds are this release's when Athelas's zip put them there: it carries ARM64.txt with its version.
// Otherwise (the regular zip was unpacked over an ARM install, as an update from 1.1.2 does) they are an older
// release's, and the x64 builds run until the ARM files are installed again (update_section offers it).
bool arm_files_current()
{
    std::string v;
    FILE *f = fopen((exe_dir() + "\\ARM64.txt").c_str(), "rb");
    if (!f) return false;
    char b[64] = {};
    size_t n = fread(b, 1, sizeof b - 1, f);
    fclose(f);
    v.assign(b, n);
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
    return v == LAUNCHER_VERSION;
}

static std::string arm64_twin(const std::string &exe)
{
    if (!native_arm64() || exe.size() < 4) return exe;
    std::string t = exe.substr(0, exe.size() - 4) + "-arm64.exe";
    // (one the launcher built on this PC from the ARM64 kit carries its .build stamp)
    return file_there(t) && (arm_files_current() || file_there(t + ".build")) ? t : exe;
}

// The Return of the King on Xbox read the PC version's ROTKFix.ini up to Athelas 1.5. The first time its own page
// looks for the port, the port brings that file's values (and button layout) into its settings.toml and exits;
// [Xbox] ROTKSettingsImported in Athelas.ini says it's done. No PC game, or no ROTKFix.ini: nothing to bring.
static void rx_import(App &a)
{
    if (tp().kind != GK_ROTK_XBOX || a.tt.exe.empty()) return;
    if (GetPrivateProfileIntA("Xbox", "ROTKSettingsImported", 0, ports_ini().c_str())) return;
    std::string ini = a.dir.empty() ? std::string() : a.dir + "\\ROTKFix.ini";
    if (!ini.empty() && file_there(ini)) {
        SHCreateDirectoryExA(NULL, tt_data_dir().c_str(), NULL);
        std::string out, args = "--import-ini \"" + ini + "\" --config \"" + tt_settings_path() + "\" --log \"" +
                                tt_data_dir() + "\\rotkx-import.log\" --no-dialogs";
        if (!run_capture(a.tt.exe, args, out, 30000).ok()) return;   // tried again the next time
    }
    WritePrivateProfileStringA("Xbox", "ROTKSettingsImported", "1", ports_ini().c_str());
}

void tt_find(App &a)
{
    a.ttLooked = true;
    a.tt = Port();
    // [Ports] first (a program the player chose, e.g. a development build), then the TT-Xbox
    // folder next to the launcher (the release copy; TwoTowers in earlier test builds)
    std::string cand[3] = { port_override(tp().iniKey), exe_dir() + "\\" + tp().dir + "\\" + tp().exe,
                            tp().oldDir ? exe_dir() + "\\" + tp().oldDir + "\\" + tp().exe : std::string() };
    for (int i = 0; i < 3; i++)
        if (!cand[i].empty() && file_there(cand[i])) { a.tt.exe = i ? arm64_twin(cand[i]) : cand[i]; break; }
    if (a.tt.exe.empty()) return;
    rx_import(a);
    SHCreateDirectoryExA(NULL, tt_data_dir().c_str(), NULL);
    port_query(a.tt, tt_data_dir() + "\\port-query.log");
    // movies "fit" (the film filling the width) for a new settings file, as ROTK's MovieDisplay = auto; the port's
    // own default is the console's 4:3
    for (auto &ps : a.tt.settings)
        if (ps.section == "graphics" && ps.key == "movie_display") ps.def = "\"fit\"";
    // Frame rate is the player's own choice on the Display tab: only the Original preset
    // (the game as it was, 30 fps) sets it. Other presets are picture quality only, as in ROTK.
    for (auto &pr : a.tt.presets) {
        if (pr.name == "Original") continue;
        for (size_t i = 0; i < pr.values.size(); )
            if (pr.values[i].first == "graphics.fps") pr.values.erase(pr.values.begin() + i); else i++;
    }
    a.ttLoaded = false;
}

bool tt_ready(const App &a) { return !a.tt.exe.empty() && a.tt.err.empty() && !a.tt.settings.empty(); }

// The value of a setting: from the file, else its default
std::string tt_val(const App &a, const PortSetting &ps)
{
    std::string v = a.ttFile.get(ps.section, ps.key, ps.def);
    if (ps.type == "choice" && !ps.choices.empty()) {   // a choice the port no longer offers (an old "unlimited"): its default, as the port does
        std::string u = toml_unquote(v);
        for (auto &c : ps.choices) if (c == u) return v;
        return ps.def;
    }
    return v;
}

const PortSetting *tt_setting(const App &a, const char *sec, const char *key)
{
    for (auto &ps : a.tt.settings) if (ps.section == sec && ps.key == key) return &ps;
    return nullptr;
}

void tt_load_settings(App &a)
{
    a.ttLoaded = true;
    a.ttFile = PortSettingsFile();
    settings_file_load(tt_settings_path(), a.ttFile);   // missing: every setting at its default
}

bool tt_save(App &a)
{
    SHCreateDirectoryExA(NULL, tt_data_dir().c_str(), NULL);
    tt_apply_refresh(a);   // fullscreen: the refresh rate that suits the frame rate, as ROTK does
    // the old frame limit is gone from the port: take it out of the file
    if (!tt_setting(a, "display", "fps_cap")) a.ttFile.remove("display", "fps_cap");
    // every setting is written, so the file is complete even before the first run
    for (auto &ps : a.tt.settings) a.ttFile.set(ps.section, ps.key, tt_val(a, ps));
    if (!settings_file_save(tt_settings_path(), a.ttFile)) { set_message(a, "Could not save " + tt_settings_path(), true); return false; }
    set_message(a, "Settings saved", false);
    return true;
}

void tt_play(App &a, const LibGame &g)
{
    if (!tt_save(a)) return;
    std::string tex = tt_data_dir() + "\\textures";
    SHCreateDirectoryExA(NULL, (tex + "\\replace").c_str(), NULL);
    // save games: kept with the launcher. The first time, bring over any the port
    // already has in its own folder (hdd\ beside TwoTowers.exe), so no progress is lost.
    std::string saves = tt_data_dir() + "\\saves";
    if (GetFileAttributesA(saves.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::string old = a.tt.exe.substr(0, a.tt.exe.find_last_of('\\')) + "\\hdd";
        if (GetFileAttributesA(old.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::string from = old + "\\*";
            from.push_back('\0');                 // SHFileOperation lists end with two zeros
            std::string to = saves; to.push_back('\0');
            SHCreateDirectoryExA(NULL, saves.c_str(), NULL);
            SHFILEOPSTRUCTA op; memset(&op, 0, sizeof op);
            op.wFunc = FO_COPY; op.pFrom = from.c_str(); op.pTo = to.c_str();
            op.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;
            SHFileOperationA(&op);
        }
    }
    std::string shots = exe_dir() + "\\Screenshots";   // with ROTK's; the names start TT_
    std::string args = "--game \"" + g.path + "\" --config \"" + tt_settings_path() + "\" --textures \"" + tex +
                       "\" --log \"" + tt_log_path() + "\" --saves \"" + saves + "\" --shader-cache \"" + tt_data_dir() +
                       "\\shadercache\" --screenshots \"" + shots + "\" --no-dialogs";
    // The Return of the King: the PC version's folder, whose Data its Sharper text reads (beside its ROTKFix.ini)
    if (tp().kind == GK_ROTK_XBOX && !a.dir.empty()) args += " --ini \"" + a.dir + "\\ROTKFix.ini\"";
    // an untested release the player said works: no question at start
    if (release_confirmed(tp().dir, g)) args += " --any-release";
    std::string err;
    HANDLE h = port_start(a.tt.exe, args, err);
    if (!h) { set_message(a, err, true); return; }
    a.running = h;
    a.runningSince = GetTickCount64();
    a.runningKey = tp().kind == GK_FOTR_XBOX ? "fotr" : tp().kind == GK_TA_XBOX ? "ta" : tp().kind == GK_TT_XBOX ? "tt" : "rotk-xbox";
    a.runningExe = a.tt.exe; a.runningArgs = args;
    a.runningLog = tt_log_path();
    a.runningGame = tp().game;
    a.runningPc = false;
    a.session = Session();
    a.session.game = tp().game; a.session.exe = a.tt.exe; a.session.args = args; a.session.log = a.runningLog;
    a.session.dumpDir = tt_data_dir() + "\\CrashDumps";
    a.session.startedMs = a.runningSince; a.session.startedUnix = (long long)time(nullptr);
    SDL_HideWindow(a.win);
}

// The PC game has closed (the launcher waited, hidden, to time it): its play time and its folder in Logs, then the
// launcher closes as it always did after ROTK - unless the game crashed (an exception code), when it says so.
static void pc_finished(App &a, DWORD code)
{
    a.running = NULL;
    play_time_add("rotk-pc", (GetTickCount64() - a.runningSince) / 1000);
    bool crashed = code >= 0xC0000000;
    char c[16]; snprintf(c, sizeof c, "0x%08lX", (unsigned long)code);
    std::string m = crashed ? std::string("The Return of the King closed unexpectedly (") + c + "). Details are in ROTKFix.log in " + a.dir + "." : "";
    session_save(logs_dir(), a.session, code, crashed ? m : "closed");
    if (!crashed) { a.quit = true; return; }
    SDL_ShowWindow(a.win);
    SDL_RaiseWindow(a.win);
    set_message(a, m, true);
    crash_box(a, m, std::string("The Return of the King (PC): closed (") + c + ") - Athelas " LAUNCHER_VERSION);
}

void port_finished(App &a, DWORD code)
{
    if (a.runningPc) { pc_finished(a, code); return; }
    // (not when it didn't start: it couldn't boot, wrong files, another release, no Direct3D 12, a damaged settings
    // file, or Windows couldn't load it)
    if (!a.runningKey.empty() && !(code >= 1 && code <= 5) && code != 0xC0000135) play_time_add(a.runningKey, (GetTickCount64() - a.runningSince) / 1000);
    std::string why = port_log_reason(a.runningLog);
    std::string logDir = a.runningLog.substr(0, a.runningLog.find_last_of('\\'));
    std::string logName = a.runningLog.substr(a.runningLog.find_last_of('\\') + 1);
    std::string m;
    switch (code) {
    case 0: break;
    case 1: m = "The game couldn't start" + (why.empty() ? std::string(".") : ": " + why + ".") + " Details are in " + logName + " in " + logDir + "."; break;
    case 2:   // the port says what is wrong with the files (not found, another game, incomplete) when its log has it
        m = why.empty() ? "The game files couldn't be found there, or they aren't " + a.runningGame + ". Check the game's location in the About tab."
                        : why + (why.back() == '.' ? "" : ".");
        break;
    case 3: m = "This copy isn't the US release. Only the US version is supported for now."; break;
    case 4: m = "Direct3D 12 couldn't start. Update your graphics driver; the port needs a DirectX 12 graphics card."; break;
    case 6: m = "The graphics driver stopped responding during play" + (why.empty() ? std::string("") : " (" + why + ")") +
                ". Try a lower Render resolution, Anti-aliasing off or a lower frame rate on the Display and Graphics tabs, "
                "or update your graphics driver."; break;
    case 5: m = "The settings file is damaged" + (why.empty() ? std::string("") : " (" + why + ")") + ". Use \"Reset to defaults\" below, then Play again."; break;
    case 0xC0000135:   // a DLL is missing, before the port could write its log (older builds need the VC++ runtime)
        m = "The game couldn't start because a Windows component is missing. Installing the \"Microsoft Visual C++ "
            "Redistributable (x64)\" from Microsoft usually fixes this.";
        break;
    default: {
        char b[32]; snprintf(b, sizeof b, "0x%08lX", (unsigned long)code);
        m = std::string("The game closed unexpectedly (") + b + "). Details are in " + logName + " in " + logDir + ".";
    }
    }
    a.running = NULL;
    session_save(logs_dir(), a.session, code, code == 0 ? "closed normally" : m);
    if (code == 0) { a.quit = true; return; }       // played and quit: close, as after ROTK
    if (code == 3 && std::find(g_untested.begin(), g_untested.end(), std::string(tp().dir)) == g_untested.end())
        g_untested.push_back(tp().dir);
    if (code == 3 && a.runningArgs.find("--any-release") == std::string::npos) {
        // another release of the game (another revision, or a modified copy): the player decides, as the port asks
        // when started on its own
        const SDL_MessageBoxButtonData buttons[] = {
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Cancel" },
            { 0, 1, "Start anyway" },
        };
        std::string text = "This copy of " + a.runningGame + " is an untested and unsupported version (the port was made "
                           "from the US release). Proceed with caution: it may not start, or may have problems.\n\nStart it "
                           "anyway? Afterwards, press \"It worked\" or \"It didn't work\" on the game's page to let us know.";
        SDL_MessageBoxData box = { SDL_MESSAGEBOX_WARNING, NULL, "Athelas", text.c_str(), 2, buttons, NULL };
        int pressed = 0;
        if (SDL_ShowMessageBox(&box, &pressed) && pressed == 1) {
            std::string err;
            HANDLE h = port_start(a.runningExe, a.runningArgs + " --any-release", err);
            if (h) {
                a.running = h;
                a.runningSince = GetTickCount64();   // (not the time the question was up)
                a.runningArgs += " --any-release";
                return;
            }
            m = err;
        } else {
            m = "Not started: this copy of " + a.runningGame + " isn't the US release the port was made from.";
        }
    }
    a.ttLoaded = false;                              // the port may have rewritten its settings
    SDL_ShowWindow(a.win);
    SDL_RaiseWindow(a.win);
    set_message(a, m, true);
    // a crash or a failed start (not a setting or a game file the player can put right from the message)
    if (code == 1 || code == 6 || code > 0xFF) {
        const Port &p = a.tt;   // the port that ran (the launcher was hidden meanwhile, the page unchanged)
        std::string arch = exe_machine(a.runningExe);
        char c[16]; snprintf(c, sizeof c, "0x%08lX", (unsigned long)code);
        std::string title = a.runningGame + ": " + (code == 1 ? "doesn't start" : code == 6 ? "graphics driver stopped"
                                                                                           : std::string("closed (") + c + ")") +
                            " - Athelas " LAUNCHER_VERSION + (p.version.empty() ? "" : ", " + p.version) +
                            (arch.empty() ? "" : " " + arch);
        crash_box(a, m, title);
    }
}

// The Return of the King (Xbox) data folder (ROTK-Xbox beside Athelas; ROTKXbox in earlier test builds), for the PC
// page's Xbox-only parts (g_xboxPage, no longer set: the Xbox version has its own page, the tt_ functions')
std::string rx_data_dir() { return port_data_dir(DIR_ROTK_XBOX, "ROTKXbox"); }
