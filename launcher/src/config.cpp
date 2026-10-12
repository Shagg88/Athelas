// ROTK Fix Launcher - settings, game files and system information
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "config.h"
#include "gamedata.h"

// ---------------------------------------------------------------------------
// Presets (graphics quality only; display, frame rate and FOV are left alone)

struct PresetValues { int msaa; bool edges; int aniso; float lod; int sharpen; int shadows; bool charHigh; int sfx; };
static const PresetValues k_presets[4] = {
    /* Original */ { 0, false,  0,  0.0f,  0, 2, true, 2 },
    /* Balanced */ { 2, false,  8,  0.0f, 40, 2, true, 2 },
    /* High     */ { 4, true,  16, -0.5f, 60, 2, true, 2 },
    /* Ultra    */ { 8, true,  16, -0.5f, 80, 2, true, 2 },
};

void apply_preset(Settings &s, int p)
{
    if (p < 0 || p > 3) return;
    const PresetValues &v = k_presets[p];
    s.msaa = v.msaa; s.alphaEdges = v.edges; s.aniso = v.aniso; s.lodBias = v.lod; s.sharpening = v.sharpen;
    s.shadows = v.shadows; s.charDetailHigh = v.charHigh; s.sfx = v.sfx;
    s.preset = p;
}

int detect_preset(const Settings &s)
{
    for (int p = 0; p < 4; p++) {
        const PresetValues &v = k_presets[p];
        if (s.msaa == v.msaa && s.alphaEdges == v.edges && s.aniso == v.aniso && s.lodBias == v.lod &&
            s.sharpening == v.sharpen && s.shadows == v.shadows && s.charDetailHigh == v.charHigh && s.sfx == v.sfx)
            return p;
    }
    return PRESET_CUSTOM;
}

// ---------------------------------------------------------------------------
// Monitors

int Monitor::maxHz() const
{
    int m = deskHz;
    for (auto &md : modes) for (int h : md.hz) m = std::max(m, h);
    return m;
}

const Mode *Monitor::find(int w, int h) const
{
    for (auto &m : modes) if (m.w == w && m.h == h) return &m;
    return nullptr;
}

std::vector<Monitor> enum_monitors()
{
    std::vector<Monitor> out;
    DISPLAY_DEVICEA dd; dd.cb = sizeof dd;
    for (DWORD i = 0; EnumDisplayDevicesA(NULL, i, &dd, 0); i++, dd.cb = sizeof dd) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        Monitor m;
        m.device = dd.DeviceName;
        m.primary = (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
        DISPLAY_DEVICEA mon; mon.cb = sizeof mon;
        m.name = EnumDisplayDevicesA(dd.DeviceName, 0, &mon, 0) && mon.DeviceString[0] ? mon.DeviceString : dd.DeviceString;
        DEVMODEA dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
        if (EnumDisplaySettingsA(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
            m.deskW = dm.dmPelsWidth; m.deskH = dm.dmPelsHeight; m.deskHz = dm.dmDisplayFrequency;
        }
        if (m.deskW <= 0 || m.deskH <= 0) { m.deskW = 1920; m.deskH = 1080; }
        if (m.deskHz <= 1) m.deskHz = 60;          // 0/1 = "hardware default" on some drivers
        for (DWORD k = 0; ; k++) {
            memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
            if (!EnumDisplaySettingsA(dd.DeviceName, k, &dm)) break;
            if (dm.dmBitsPerPel != 32 || dm.dmPelsWidth < 800 || dm.dmPelsHeight < 600) continue;
            Mode *md = nullptr;
            for (auto &x : m.modes) if (x.w == (int)dm.dmPelsWidth && x.h == (int)dm.dmPelsHeight) md = &x;
            if (!md) { m.modes.push_back(Mode{ (int)dm.dmPelsWidth, (int)dm.dmPelsHeight, {} }); md = &m.modes.back(); }
            int hz = (int)dm.dmDisplayFrequency;
            if (hz > 1 && std::find(md->hz.begin(), md->hz.end(), hz) == md->hz.end()) md->hz.push_back(hz);
        }
        for (auto &x : m.modes) std::sort(x.hz.begin(), x.hz.end());
        std::sort(m.modes.begin(), m.modes.end(), [](const Mode &a, const Mode &b) {
            return a.w != b.w ? a.w < b.w : a.h < b.h; });
        out.push_back(std::move(m));
    }
    if (out.empty()) {
        Monitor m; m.name = "Display"; m.deskW = 1920; m.deskH = 1080; m.deskHz = 60; m.primary = true;
        m.modes.push_back(Mode{ 1920, 1080, { 60 } });
        out.push_back(m);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Game folder

static bool file_exists(const std::string &p)
{
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool read_file(const std::string &p, std::string &out)
{
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    bool ok = n == 0 || fread(&out[0], 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}

static bool write_file(const std::string &p, const std::string &data)
{
    FILE *f = fopen(p.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    return fclose(f) == 0 && ok;
}

std::string exe_dir()
{
    char p[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, p, MAX_PATH);
    std::string s(p, n);
    size_t k = s.find_last_of("\\/");
    return k == std::string::npos ? "." : s.substr(0, k);
}

static std::string documents_dir()
{
    PWSTR w = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, NULL, &w)) && w) {
        char buf[MAX_PATH * 2];
        int n = WideCharToMultiByte(CP_ACP, 0, w, -1, buf, sizeof buf, NULL, NULL);
        if (n > 0) out = buf;
    }
    if (w) CoTaskMemFree(w);
    return out;
}

// Map a virtual address in rotk.exe to a file offset (PE sections).
static long va_to_offset(const std::string &pe, DWORD va)
{
    if (pe.size() < 0x400 || pe[0] != 'M' || pe[1] != 'Z') return -1;
    DWORD lfanew = *(const DWORD *)&pe[0x3C];
    if (lfanew + 0x200 > pe.size()) return -1;
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)&pe[lfanew];
    if (nt->Signature != IMAGE_NT_SIGNATURE) return -1;
    DWORD rva = va - nt->OptionalHeader.ImageBase;
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        DWORD size = std::max(sec->Misc.VirtualSize, sec->SizeOfRawData);
        if (rva >= sec->VirtualAddress && rva < sec->VirtualAddress + size)
            return (long)(rva - sec->VirtualAddress + sec->PointerToRawData);
    }
    return -1;
}

static bool sig_at(const std::string &pe, DWORD va, const unsigned char *sig, size_t n)
{
    long o = va_to_offset(pe, va);
    return o >= 0 && (size_t)o + n <= pe.size() && memcmp(&pe[o], sig, n) == 0;
}

GameInfo inspect_game(const std::string &dir)
{
    GameInfo g;
    g.dir = dir;
    std::string exe = dir + "\\rotk.exe", pe;
    g.exeFound = file_exists(exe) && read_file(exe, pe);
    if (g.exeFound) {
        static const unsigned char s1[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8 };              // frame pacer
        static const unsigned char s2[] = { 0x39, 0x1D, 0xDC, 0x2A, 0x56, 0x00 };              // frame cap
        static const unsigned char s3[] = { 0x83, 0xEC, 0x18, 0xD9, 0x05, 0x4C, 0x5F, 0x53 };  // gamma ramp
        g.versionOk = sig_at(pe, 0x4828E0, s1, sizeof s1) && sig_at(pe, 0x482907, s2, sizeof s2) &&
                      sig_at(pe, 0x4A5010, s3, sizeof s3);
        bool laa = false;
        if (pe.size() > 0x200) {
            DWORD lfanew = *(const DWORD *)&pe[0x3C];
            if (lfanew + sizeof(IMAGE_NT_HEADERS32) < pe.size())
                laa = (((const IMAGE_NT_HEADERS32 *)&pe[lfanew])->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
        }
        /* the original disc exe keeps SafeDisc's encrypted sections (named
         * "stxt...") - its code cannot be checked on disk; on Windows 10/11
         * SafeDisc needs SafeDiscShim (github.com/RibShark/SafeDiscShim) */
        bool safedisc = false;
        if (pe.size() > 0x200) {
            DWORD lfanew = *(const DWORD *)&pe[0x3C];
            if (lfanew + sizeof(IMAGE_NT_HEADERS32) < pe.size()) {
                const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)&pe[lfanew];
                const IMAGE_SECTION_HEADER *sec = (const IMAGE_SECTION_HEADER *)((const BYTE *)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader);
                for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
                    if ((const char *)&sec[i + 1] > pe.data() + pe.size()) break;
                    if (!memcmp(sec[i].Name, "stxt", 4)) safedisc = true;
                }
            }
        }
        g.discProtected = safedisc && !g.versionOk;
        g.versionText = g.versionOk ? (laa ? "Version 1.0 (large-address patched) - supported" : "Version 1.0 - supported")
                      : safedisc ? "Original disc version - on Windows 10/11 it needs SafeDiscShim and the game disc in the drive"
                                 : "Unrecognised version - enhancements may not apply";
    }
    std::string d8;
    g.hasDll = file_exists(dir + "\\ROTKFix.dll");
    g.hasProxyD3D8 = read_file(dir + "\\d3d8.dll", d8) && d8.find("ROTKFix.RF_Direct3DCreate8") != std::string::npos;
    std::string di;
    g.hasProxyDinput = read_file(dir + "\\dinput8.dll", di) && di.find("ROTKFix.RF_DirectInput8Create") != std::string::npos;
    g.otherD3D8 = !g.hasProxyD3D8 && file_exists(dir + "\\d3d8.dll");
    g.otherDinput = !g.hasProxyDinput && file_exists(dir + "\\dinput8.dll");
    g.hasSdl = file_exists(dir + "\\SDL3.dll");
    g.hasDxvk = file_exists(dir + "\\ROTKFix\\dxvk\\d3d8.dll") && file_exists(dir + "\\ROTKFix\\dxvk\\d3d9.dll");
    // The game keeps ROTK.ini in a Documents subfolder named by string resource 1007 of rotk.exe
    // ("LOTR The Return of the King (tm) Data" in English copies, translated in others).
    std::string docs = documents_dir();
    std::string sub = "LOTR The Return of the King (tm) Data";
    if (g.exeFound) {
        HMODULE h = LoadLibraryExA(exe.c_str(), NULL, LOAD_LIBRARY_AS_DATAFILE);
        char b[160];
        if (h && LoadStringA(h, 1007, b, sizeof b) > 0) sub = b;
        if (h) FreeLibrary(h);
    }
    if (!docs.empty()) { g.rotkIniDir = docs + "\\" + sub; g.rotkIniPath = g.rotkIniDir + "\\ROTK.ini"; }
    g.rotkIniExists = !g.rotkIniPath.empty() && file_exists(g.rotkIniPath);
    std::string inp;
    g.inputProfileInstalled = read_file(dir + "\\Input.ini", inp) && inp.find("[inputmap_ROTKFixPad_BASE]") != std::string::npos
                              && inp.find("[inputmap_ROTKFixPad2_BASE]") != std::string::npos;
    return g;
}

// ---------------------------------------------------------------------------
// ROTKFix.ini

const char *const k_padPhysId[PAD_PHYS_COUNT] = { "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LS", "RS", "LT", "RT" };
const char *const k_padActionKey[PAD_ACTION_COUNT] = { "SpeedAttack", "PhysicalAttack", "Parry", "FierceAttack", "JumpBack",
                                                       "Action", "Special", "RangedAttack", "KillingMove" };
const int k_padActionDefault[PAD_ACTION_COUNT] = { PH_A, PH_B, PH_X, PH_Y, PH_LB, PH_RB, PH_LS, PH_LT, PH_RT };

// Player 2's layout: the same as player 1's, for "ROTK Fix Gamepad 2", without keyboard and mouse lines
static std::string pad2_profile()
{
    std::string out, line, src = k_inputProfile;
    size_t i = 0;
    while (i < src.size()) {
        size_t e = src.find('\n', i);
        line = src.substr(i, e == std::string::npos ? std::string::npos : e - i + 1);
        i = e == std::string::npos ? src.size() : e + 1;
        if (line.find("KEYID_") != std::string::npos || line.find("MOUSE") != std::string::npos) continue;
        for (size_t k = 0; (k = line.find("ROTKFixPad", k)) != std::string::npos; k += 11) line.replace(k, 10, "ROTKFixPad2");
        size_t q = line.find("\"ROTK Fix Gamepad\"");
        if (q != std::string::npos) line.replace(q, 18, "\"ROTK Fix Gamepad 2\"");
        size_t c = line.find("; ROTK Fix Gamepad (");
        if (c != std::string::npos) line = "; ROTK Fix Gamepad 2: player 2 in co-op, same layout as player 1\r\n";
        out += line;
    }
    return out;
}

static int geti(const char *ini, const char *sec, const char *key, int def) { return (int)GetPrivateProfileIntA(sec, key, def, ini); }
static std::string gets(const char *ini, const char *sec, const char *key, const char *def)
{
    char b[128]; GetPrivateProfileStringA(sec, key, def, b, sizeof b, ini); return b;
}
static float getf(const char *ini, const char *sec, const char *key, float def)
{
    char d[32]; snprintf(d, sizeof d, "%g", def);
    return (float)atof(gets(ini, sec, key, d).c_str());
}
static bool puts_(const char *ini, const char *sec, const char *key, const std::string &v) { return WritePrivateProfileStringA(sec, key, v.c_str(), ini) != 0; }
static bool puti(const char *ini, const char *sec, const char *key, int v) { return puts_(ini, sec, key, std::to_string(v)); }

static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

bool load_settings(const std::string &dir, Settings &s)
{
    std::string p = dir + "\\ROTKFix.ini";
    const char *ini = p.c_str();
    bool exists = file_exists(p);
    s.targetFps = clampi(geti(ini, "Frame", "TargetFPS", 60), 20, 240);
    s.padEnabled = geti(ini, "Gamepad", "Enabled", 1) != 0;
    s.player2 = geti(ini, "Gamepad", "SecondController", 1) != 0;
    s.vibration = clampi(geti(ini, "Gamepad", "Vibration", 100), 0, 100);
    s.hideRealPads = geti(ini, "Gamepad", "HideRealControllers", 1) != 0;
    s.deadzone = clampi(geti(ini, "Gamepad", "StickDeadzone", 12), 0, 50);
    s.triggerThreshold = clampi(geti(ini, "Gamepad", "TriggerThreshold", 30), 5, 95);
    {
        std::string bn = gets(ini, "Gamepad", "ButtonNames", "auto");
        static const char *names[] = { "auto", "xbox", "playstation", "nintendo", "original" };
        s.buttonNames = 0;
        for (int i = 0; i < 5; i++) if (!_stricmp(bn.c_str(), names[i])) s.buttonNames = i;
        for (int pl = 0; pl < 2; pl++)
            for (int a = 0; a < PAD_ACTION_COUNT; a++) {
                // player 2 without a layout of its own starts from player 1's
                int def = pl ? s.padMap[0][a] : k_padActionDefault[a];
                std::string v = gets(ini, pl ? "GamepadButtons2" : "GamepadButtons", k_padActionKey[a], k_padPhysId[def]);
                s.padMap[pl][a] = def;
                for (int p = 0; p < PAD_PHYS_COUNT; p++) if (!_stricmp(v.c_str(), k_padPhysId[p])) s.padMap[pl][a] = p;
            }
    }
    s.skipAllVideos = geti(ini, "Video", "AllowSkippingAllVideos", 1) != 0;
    std::string md = gets(ini, "Video", "MovieDisplay", "auto");
    s.movieDisplay = !_stricmp(md.c_str(), "stretch") ? 1 : !_stricmp(md.c_str(), "4:3") ? 2 : 0;
    s.skipLogos = geti(ini, "Video", "SkipOpeningLogos", 0) != 0;
    s.audioDelayMs = clampi(geti(ini, "Video", "CutsceneAudioDelay", 0), 0, 1000);
    s.sharperMovies = clampi(geti(ini, "Video", "SharperMovies", 70), 0, 100);
    s.renderer = !_stricmp(gets(ini, "Graphics", "Renderer", "dxvk").c_str(), "native") ? 1 : 0;
    s.width = geti(ini, "Graphics", "Width", 0);
    s.height = geti(ini, "Graphics", "Height", 0);
    std::string dm = gets(ini, "Graphics", "DisplayMode", "borderless");
    s.displayMode = !_stricmp(dm.c_str(), "fullscreen") ? 1 : !_stricmp(dm.c_str(), "windowed") ? 2 : 0;
    s.refresh = geti(ini, "Graphics", "RefreshRate", 0);
    s.vsync = geti(ini, "Graphics", "VSync", 1) != 0;
    s.msaa = geti(ini, "Graphics", "MSAA", 4);
    if (s.msaa != 0 && s.msaa != 2 && s.msaa != 4 && s.msaa != 8) s.msaa = 4;
    s.alphaEdges = geti(ini, "Graphics", "AlphaEdgeSmoothing", 1) != 0;
    s.aniso = geti(ini, "Graphics", "AnisotropicFiltering", 16);
    s.widescreenFix = geti(ini, "Graphics", "WidescreenFix", 1) != 0;
    s.fovPct = clampi(geti(ini, "Graphics", "FieldOfView", 100), 70, 150);
    s.lodBias = std::min(0.0f, std::max(-1.0f, getf(ini, "Graphics", "TextureLODBias", -0.5f)));
    s.brightness = std::min(2.0f, std::max(0.5f, getf(ini, "Graphics", "Brightness", 1.0f)));
    s.sharpening = clampi(geti(ini, "Graphics", "Sharpening", 60), 0, 100);
    {
        std::string lk = gets(ini, "Graphics", "ColourLook", "original");
        static const char *looks[] = { "original", "film", "vivid", "dark" };
        s.colourLook = 0;
        for (int i = 0; i < 4; i++) if (!_stricmp(lk.c_str(), looks[i])) s.colourLook = i;
        s.lookStrength = clampi(geti(ini, "Graphics", "ColourLookStrength", 100), 0, 100);
    }
    {
        std::string sm = gets(ini, "Sound", "Mode", "speakers");
        s.sound3d = !_stricmp(sm.c_str(), "off") ? 0 : !_stricmp(sm.c_str(), "headphones") ? 2 : 1;
    }
    s.monitor = std::max(0, geti(ini, "Launcher", "Monitor", 0));

    GameInfo g = inspect_game(dir);
    if (g.rotkIniExists) {
        const char *r = g.rotkIniPath.c_str();
        int sh = geti(r, "Video", "EnableShadows", 1), simple = geti(r, "Video", "EnableSimpleShadows", 0);
        s.shadows = !sh ? 0 : simple ? 1 : 2;
        s.charDetailHigh = geti(r, "Video", "LowResTexture", 0) == 0;
        s.sfx = clampi(geti(r, "Video", "SfxMode", 2), 0, 2);
        s.altCameras = geti(r, "Video", "AlternateCameras", 0) != 0;
        s.subtitles = geti(r, "GamePlay", "Subtitles", 2) != 0;
        s.volMusic = clampi(geti(r, "Audio", "Volume_Music", 100), 0, 100);
        s.volVoice = clampi(geti(r, "Audio", "Volume_Voice", 100), 0, 100);
        s.volSfx = clampi(geti(r, "Audio", "Volume_Sfx", 100), 0, 100);
    }
    s.preset = detect_preset(s);
    return exists;
}

// ---------------------------------------------------------------------------
// ROTKFix.ini is rewritten as a whole in one fixed, commented layout, so it
// stays readable in Notepad. Settings the launcher has no control for keep
// the value already in the file (or their default); keys and sections the
// player added by hand are kept at the end of their section / the file.

struct IniKey { const char *key, *def, *comment; };
struct IniSection { const char *name, *title; std::vector<IniKey> keys; };

static const std::vector<IniSection> &ini_layout()
{
    static const std::vector<IniSection> L = {
        { "Graphics", "Screen and picture", {
            { "DisplayMode", "borderless", "borderless, fullscreen or windowed" },
            { "Width", "0", "Resolution; 0 = the screen's own" },
            { "Height", "0", nullptr },
            { "RefreshRate", "0", "Fullscreen only; 0 = the screen's own" },
            { "VSync", "1", "1 = no tearing" },
            { "Renderer", "dxvk", "dxvk = Vulkan (recommended), native = Windows' own Direct3D 8" },
            { "DxvkFolder", "dxvk", "Which DXVK inside the ROTKFix folder draws the game" },
            { "MSAA", "4", "Anti-aliasing: 0, 2, 4 or 8" },
            { "AlphaEdgeSmoothing", "1", "1 = also smooth leaves, grass and fences (needs MSAA; costs speed)" },
            { "AnisotropicFiltering", "16", "Texture filtering: 0, 2, 4, 8 or 16" },
            { "TextureLODBias", "-0.50", "Texture sharpness: 0 = game's own, down to -1.0 = sharpest" },
            { "Sharpening", "60", "0-100, 0 = off (also turns off the automatic HUD sharpening). [ switches it on and off while playing" },
            { "Brightness", "1.00", "Added to the game's own brightness option (0.5-2.0)" },
            { "ColourLook", "original", "original, film, vivid or dark. ] switches while playing" },
            { "ColourLookStrength", "100", "0-100" },
            { "WidescreenFix", "1", "1 = see more at the sides instead of a stretched picture" },
            { "FieldOfView", "100", "Percent of the game's own view angle (70-150)" },
            { "DrawDistance", "100", "Percent of the game's own (50-800)" },
            { "UseGameBrightness", nullptr, "1 = the game's own brightness option works in every display mode" },
        } },
        { "Frame", "Frame rate", {
            { "TargetFPS", "60", "Frames per second (the game was made for 60)" },
            { "Limiter", "1", "1 = ROTK Fix's smooth limiter, 0 = the game's own (62 fps, busy CPU)" },
            { "SpinMicroseconds", "1000", "Fine timing window; leave as it is" },
        } },
        { "Video", "Movies", {
            { "MovieDisplay", "auto", "auto = cutscenes fill the width, stretch, or 4:3" },
            { "SharperMovies", "70", "Cutscene sharpening 0-100, 0 = off" },
            { "CutsceneAudioDelay", "0", "Holds the picture back (ms) for Bluetooth sound, 0 = off" },
            { "AllowSkippingAllVideos", "1", "1 = every movie can be skipped" },
            { "SkipOpeningLogos", "0", "1 = go straight to the menus" },
        } },
        { "Sound", "Sound", {
            { "Mode", "speakers", "3D sound and EAX reverb: speakers, headphones, or off" },
        } },
        { "Gamepad", "Controllers", {
            { "Enabled", "1", "1 = use modern controllers (as \"ROTK Fix Gamepad\")" },
            { "SecondController", "1", "1 = a second controller plays player 2 in co-op" },
            { "ButtonNames", "auto", "Button pictures in on-screen hints: auto, xbox, playstation, nintendo; original = the game's own text" },
            { "HideMousePointer", "1", "1 = hide the game's menu pointer while a controller is used (moving the mouse shows it)" },
            { "Vibration", "100", "Rumble strength 0-100, 0 = off" },
            { "StickDeadzone", "12", "Percent" },
            { "TriggerThreshold", "30", "How far a trigger is pulled to count as pressed, percent" },
            { "HideRealControllers", "1", "1 = the game only sees the ROTK Fix controllers" },
            { "LogInput", nullptr, "1 = log controller details" },
        } },
        { "GamepadButtons", "Player 1 buttons (A B X Y LB RB LT RT LS RS, by Xbox position)", {
            { "SpeedAttack", "A", nullptr }, { "PhysicalAttack", "B", nullptr }, { "Parry", "X", nullptr },
            { "FierceAttack", "Y", nullptr }, { "JumpBack", "LB", nullptr }, { "Action", "RB", nullptr },
            { "Special", "LS", nullptr }, { "RangedAttack", "LT", nullptr }, { "KillingMove", "RT", nullptr },
        } },
        { "GamepadButtons2", "Player 2 buttons (co-op)", {
            { "SpeedAttack", "A", nullptr }, { "PhysicalAttack", "B", nullptr }, { "Parry", "X", nullptr },
            { "FierceAttack", "Y", nullptr }, { "JumpBack", "LB", nullptr }, { "Action", "RB", nullptr },
            { "Special", "LS", nullptr }, { "RangedAttack", "LT", nullptr }, { "KillingMove", "RT", nullptr },
        } },
        { "Textures", "Texture packs", {
            { "Dump", "0", "1 = save every texture the game uses as a PNG in ROTKFix\\textures\\dump (for making texture packs)" },
            { "Replace", "1", "1 = use replacement PNGs from ROTKFix\\textures\\replace (texture packs)" },
        } },
        { "Extras", "Extras", {
            { "DeveloperButtons", "0", "1 = show the hidden developer menu buttons (testing)" },
        } },
        { "Log", "Log (ROTKFix.log)", {
            { "Enabled", "1", nullptr },
            { "Detailed", "0", "1 = much more detail, for tracking down a problem" },
            { "IntervalSeconds", nullptr, "Seconds between frame-rate lines" },
        } },
        { "Launcher", "Launcher", {
            { "Monitor", "0", nullptr },
            { "CreatedGameSettings", nullptr, "1 = the launcher made the game's ROTK.ini (removed again when uninstalling)" },
        } },
    };
    return L;
}

struct IniFile {
    struct Sec { std::string name; std::vector<std::pair<std::string, std::string>> kv; };
    std::vector<Sec> secs;
    Sec *find(const char *n) { for (auto &s : secs) if (!_stricmp(s.name.c_str(), n)) return &s; return nullptr; }
    const std::string *get(const char *sec, const char *key)
    {
        Sec *s = find(sec);
        if (s) for (auto &e : s->kv) if (!_stricmp(e.first.c_str(), key)) return &e.second;
        return nullptr;
    }
};

static std::string trim(const std::string &v)
{
    size_t a = v.find_first_not_of(" \t\r\n"), b = v.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : v.substr(a, b - a + 1);
}

static IniFile parse_ini(const std::string &text)
{
    IniFile f;
    IniFile::Sec *cur = nullptr;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        std::string line = trim(text.substr(pos, e == std::string::npos ? std::string::npos : e - pos));
        pos = e == std::string::npos ? text.size() : e + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            size_t close = line.find(']');
            std::string n = trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1));
            cur = f.find(n.c_str());
            if (!cur) { f.secs.push_back({ n, {} }); cur = &f.secs.back(); }
            continue;
        }
        size_t eq = line.find('=');
        if (!cur || eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k.empty()) continue;
        bool dup = false;
        for (auto &kv : cur->kv) if (!_stricmp(kv.first.c_str(), k.c_str())) { kv.second = v; dup = true; }
        if (!dup) cur->kv.push_back({ k, v });
    }
    return f;
}

static std::string fmtf(float v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); return b; }

static bool write_rotkfix_ini(const std::string &p, const Settings &s, bool createdGameSettings, std::string &err)
{
    std::string oldText;
    read_file(p, oldText);
    IniFile old = parse_ini(oldText);

    // values the launcher controls
    IniFile mine;
    auto set = [&](const char *sec, const char *key, const std::string &v) {
        IniFile::Sec *x = mine.find(sec);
        if (!x) { mine.secs.push_back({ sec, {} }); x = &mine.secs.back(); }
        x->kv.push_back({ key, v });
    };
    static const char *dm[] = { "borderless", "fullscreen", "windowed" };
    static const char *looks[] = { "original", "film", "vivid", "dark" };
    static const char *names[] = { "auto", "xbox", "playstation", "nintendo", "original" };
    static const char *sm[] = { "off", "speakers", "headphones" };
    set("Graphics", "DisplayMode", dm[s.displayMode]);
    set("Graphics", "Width", std::to_string(s.width));
    set("Graphics", "Height", std::to_string(s.height));
    set("Graphics", "RefreshRate", std::to_string(s.refresh));
    set("Graphics", "VSync", std::to_string((int)s.vsync));
    set("Graphics", "Renderer", s.renderer ? "native" : "dxvk");
    set("Graphics", "MSAA", std::to_string(s.msaa));
    set("Graphics", "AlphaEdgeSmoothing", std::to_string((int)s.alphaEdges));
    set("Graphics", "AnisotropicFiltering", std::to_string(s.aniso));
    set("Graphics", "TextureLODBias", fmtf(s.lodBias));
    set("Graphics", "Sharpening", std::to_string(s.sharpening));
    set("Graphics", "Brightness", fmtf(s.brightness));
    set("Graphics", "ColourLook", looks[s.colourLook]);
    set("Graphics", "ColourLookStrength", std::to_string(s.lookStrength));
    set("Graphics", "WidescreenFix", std::to_string((int)s.widescreenFix));
    set("Graphics", "FieldOfView", std::to_string(s.fovPct));
    set("Frame", "TargetFPS", std::to_string(s.targetFps));
    set("Frame", "Limiter", "1");
    set("Video", "MovieDisplay", s.movieDisplay == 1 ? "stretch" : s.movieDisplay == 2 ? "4:3" : "auto");
    set("Video", "SharperMovies", std::to_string(s.sharperMovies));
    set("Video", "CutsceneAudioDelay", std::to_string(s.audioDelayMs));
    set("Video", "AllowSkippingAllVideos", std::to_string((int)s.skipAllVideos));
    set("Video", "SkipOpeningLogos", std::to_string((int)s.skipLogos));
    set("Sound", "Mode", sm[s.sound3d]);
    set("Gamepad", "Enabled", std::to_string((int)s.padEnabled));
    set("Gamepad", "SecondController", std::to_string(s.player2));
    set("Gamepad", "ButtonNames", names[s.buttonNames]);
    set("Gamepad", "Vibration", std::to_string(s.vibration));
    set("Gamepad", "StickDeadzone", std::to_string(s.deadzone));
    set("Gamepad", "TriggerThreshold", std::to_string(s.triggerThreshold));
    set("Gamepad", "HideRealControllers", std::to_string((int)s.hideRealPads));
    for (int pl = 0; pl < 2; pl++)
        for (int a = 0; a < PAD_ACTION_COUNT; a++)
            set(pl ? "GamepadButtons2" : "GamepadButtons", k_padActionKey[a], k_padPhysId[s.padMap[pl][a]]);
    set("Launcher", "Monitor", std::to_string(s.monitor));
    if (createdGameSettings) set("Launcher", "CreatedGameSettings", "1");   // so uninstalling removes it again

    // Compact layout: one aligned "Name = value" line per setting; what each
    // one does is explained in README.txt.
    static const char *retired[] = { "KeepHUDProportions", "DrawDistanceHotkey", "FoliageEdgeCutoff",
                                     "FoliageEdgeCutoffDXT3", "ShadowResolution", "ButtonPictures", "SharperHUD" };
    auto isRetired = [&](const std::string &k) {
        for (const char *r : retired) if (!_stricmp(r, k.c_str())) return true;
        return false;
    };
    std::string out =
        "; ROTK Fix settings - easiest to change with ROTKFixLauncher.exe.\r\n"
        "; Edits made here are kept. README.txt explains every setting.\r\n";
    std::vector<std::pair<std::string, std::string>> rows;
    auto flush = [&]() {
        size_t w = 0;
        for (auto &r : rows) w = std::max(w, r.first.size());
        for (auto &r : rows) out += r.first + std::string(w - r.first.size() + 1, ' ') + "= " + r.second + "\r\n";
        rows.clear();
    };
    for (auto &sec : ini_layout()) {
        out += "\r\n[" + std::string(sec.name) + "]\r\n";
        for (auto &k : sec.keys) {
            const std::string *v = mine.get(sec.name, k.key);
            if (!v) v = old.get(sec.name, k.key);
            if (!v && !k.def) continue;          // optional: only written when set
            rows.push_back({ k.key, v ? *v : k.def });
        }
        // the player's own additions to this section
        if (IniFile::Sec *os = old.find(sec.name))
            for (auto &kv : os->kv) {
                bool known = isRetired(kv.first);
                for (auto &k : sec.keys) if (!_stricmp(k.key, kv.first.c_str())) known = true;
                if (!known) rows.push_back(kv);
            }
        flush();
    }
    // whole sections the launcher does not know
    for (auto &os : old.secs) {
        bool known = false;
        for (auto &sec : ini_layout()) if (!_stricmp(sec.name, os.name.c_str())) known = true;
        if (known) continue;
        out += "\r\n[" + os.name + "]\r\n";
        for (auto &kv : os.kv) rows.push_back(kv);
        flush();
    }
    // write next to it, then swap in, so a failed write never leaves half a file
    std::string tmp = p + ".new";
    if (!write_file(tmp, out) || !MoveFileExA(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(tmp.c_str());
        err = "Could not write ROTKFix.ini (is the game folder read-only?).";
        return false;
    }
    return true;
}

// Fullscreen: run the screen at the refresh rate that suits the frame rate best -
// the same number if available, else the lowest exact multiple, else the highest.
int pick_refresh(const std::vector<int> &hz, int fps)
{
    if (hz.empty()) return 0;
    for (int h : hz) if (h == fps) return h;
    for (int h : hz) if (h > fps && h % fps == 0) return h;
    return hz.back();
}

bool save_settings(const std::string &dir, const Settings &s, std::string &err)
{
    std::string p = dir + "\\ROTKFix.ini";
    const char *ini = p.c_str();
    bool ok = true;
    // The game's own settings file in Documents
    GameInfo g = inspect_game(dir);
    if (g.rotkIniPath.empty()) { err = "Could not find your Documents folder for the game's ROTK.ini."; return false; }
    bool created = geti(ini, "Launcher", "CreatedGameSettings", 0) != 0;
    if (!g.rotkIniExists) {
        CreateDirectoryA(g.rotkIniDir.c_str(), NULL);
        if (!write_file(g.rotkIniPath, k_defaultRotkIni)) { err = "Could not create the game's ROTK.ini in Documents."; return false; }
        created = true;
    }
    // ROTKFix.ini is only ever written here, as a whole
    if (!write_rotkfix_ini(p, s, created, err)) return false;
    if (g.rotkIniExists && !created) {
        // keep the player's own settings file as it was, for uninstalling
        if (!file_exists(g.rotkIniPath + ".before-rotkfix"))
            CopyFileA(g.rotkIniPath.c_str(), (g.rotkIniPath + ".before-rotkfix").c_str(), TRUE);
    }
    const char *r = g.rotkIniPath.c_str();
    ok &= puti(r, "Video", "AdapterNumber", s.monitor);
    ok &= puti(r, "Video", "EnableShadows", s.shadows > 0);
    ok &= puti(r, "Video", "EnableSimpleShadows", s.shadows == 1);
    ok &= puti(r, "Video", "LowResTexture", !s.charDetailHigh);
    ok &= puti(r, "Video", "SfxMode", s.sfx);
    ok &= puti(r, "Video", "AlternateCameras", s.altCameras);
    ok &= puti(r, "Video", "Enable32Bit", 1);
    ok &= puti(r, "GamePlay", "Subtitles", s.subtitles ? 2 : 0);
    ok &= puti(r, "Audio", "Volume_Music", s.volMusic);
    ok &= puti(r, "Audio", "Volume_Voice", s.volVoice);
    ok &= puti(r, "Audio", "Volume_Sfx", s.volSfx);
    if (s.padEnabled) {
        // Player 1 uses the ROTK Fix Gamepad; forget saved layouts made for other controllers.
        ok &= puts_(r, "input0", "map", "ROTKFixPad");
        ok &= puts_(r, "input0", "mode", "JS");
        // Player 2 (co-op): the second controller, or the game's keyboard layout
        ok &= puts_(r, "input1", "map", s.player2 ? "ROTKFixPad2" : "ROTKDefault");
        ok &= puts_(r, "input1", "mode", s.player2 ? "JS" : "KB");
        char names[8192];
        DWORD n = GetPrivateProfileSectionNamesA(names, sizeof names, r);
        std::vector<std::string> stale;
        for (const char *q = names; q < names + n && *q; q += strlen(q) + 1) {
            if (_strnicmp(q, "input_JS_BASE_", 14)) continue;
            std::string root = gets(r, q, "ROOT_MAP", "");
            if (_stricmp(root.c_str(), "ROTKFixPad") && _stricmp(root.c_str(), "ROTKFixPad2")) stale.push_back(q);
        }
        for (auto &sec : stale) WritePrivateProfileStringA(sec.c_str(), NULL, NULL, r);
    }
    if (!ok) { err = "Could not update ROTK.ini in Documents."; return false; }
    if (!g.inputProfileInstalled && !install_input_profile(dir, err)) return false;
    return true;
}

// Add the ROTK Fix Gamepad profile to the game's Input.ini (once; original kept as Input.ini.original)
// A controller profile has two parts: button names for the game's [input_alias_list]
// (which runs up to the first [inputmap_ section) and the layouts themselves.
static void split_profile(const std::string &prof, std::string &aliases, std::string &maps)
{
    size_t m = prof.find("[inputmap_");
    aliases = prof.substr(0, m);
    maps = m == std::string::npos ? "" : prof.substr(m);
}

bool install_input_profile(const std::string &dir, std::string &err)
{
    std::string p = dir + "\\Input.ini", text;
    if (!read_file(p, text)) { err = "Could not read Input.ini in the game folder."; return false; }
    bool has1 = text.find("[inputmap_ROTKFixPad_BASE]") != std::string::npos;
    bool has2 = text.find("[inputmap_ROTKFixPad2_BASE]") != std::string::npos;
    if (has1 && has2) return true;
    if (!has1 && !file_exists(dir + "\\Input.ini.original") && !write_file(dir + "\\Input.ini.original", text)) {
        err = "Could not back up Input.ini."; return false;
    }
    size_t at = text.find("\n[inputmap_");
    if (at == std::string::npos) { err = "Input.ini does not look like the game's controller file."; return false; }
    std::string a1, m1, a2, m2;
    split_profile(k_inputProfile, a1, m1);
    split_profile(pad2_profile(), a2, m2);
    // new button names at the end of the alias list, new layouts in front of the game's own
    std::string add = (has1 ? "" : a1) + (has2 ? "" : a2) + (has1 ? "" : m1) + (has2 ? "" : m2);
    text.insert(at + 1, add);
    if (!write_file(p, text)) { err = "Could not write Input.ini."; return false; }
    return true;
}

bool launch_game(const std::string &dir, std::string &err)
{
    std::string exe = dir + "\\rotk.exe";
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    std::string cmd = "\"" + exe + "\"";
    if (!CreateProcessA(exe.c_str(), &cmd[0], NULL, NULL, FALSE, 0, NULL, dir.c_str(), &si, &pi)) {
        DWORD e = GetLastError();
        if (e == ERROR_ELEVATION_REQUIRED) {
            // the original disc exe asks to run as administrator: start it the
            // way Explorer does, which shows Windows' usual permission prompt
            SHELLEXECUTEINFOA sei; memset(&sei, 0, sizeof sei);
            sei.cbSize = sizeof sei;
            sei.fMask = SEE_MASK_NOASYNC;
            sei.lpVerb = "open";
            sei.lpFile = exe.c_str();
            sei.lpDirectory = dir.c_str();
            sei.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExA(&sei)) return true;
            e = GetLastError();
            if (e == ERROR_CANCELLED) { err = "The game was not started (the Windows permission prompt was declined)."; return false; }
        }
        err = "Could not start rotk.exe (error " + std::to_string(e) + ").";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ---------------------------------------------------------------------------
// Uninstall

static bool delete_tree(const std::string &path)
{
    std::string from = path; from.push_back('\0');           // double-null terminated
    SHFILEOPSTRUCTA op; memset(&op, 0, sizeof op);
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationA(&op) == 0 && !op.fAnyOperationsAborted;
}

bool uninstall(const std::string &dir, std::vector<std::string> &done, std::string &err)
{
    GameInfo g = inspect_game(dir);
    std::string ini = dir + "\\ROTKFix.ini";
    bool createdRotkIni = GetPrivateProfileIntA("Launcher", "CreatedGameSettings", 0, ini.c_str()) != 0;

    // 1. Files that load ROTK Fix go first: if the game is running they are locked, and nothing else is touched.
    struct F { const char *name; bool ours; } loaders[] = {
        { "dinput8.dll", g.hasProxyDinput }, { "d3d8.dll", g.hasProxyD3D8 }, { "ROTKFix.dll", g.hasDll } };
    for (auto &f : loaders) {
        if (!f.ours) continue;
        std::string p = dir + "\\" + f.name;
        if (!DeleteFileA(p.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
            err = std::string("Could not remove ") + f.name + ". Close the game and try again.";
            return false;
        }
        done.push_back(std::string("Removed ") + f.name);
        // put back another mod's file that ROTK Fix replaced, if it was kept
        std::string keep = p + ".before-rotkfix";
        if (file_exists(keep) && MoveFileA(keep.c_str(), p.c_str())) done.push_back(std::string("Restored the previous ") + f.name);
    }

    // 2. Controller profile in the game's Input.ini
    std::string inp = dir + "\\Input.ini", orig = inp + ".original";
    if (file_exists(orig)) {
        if (CopyFileA(orig.c_str(), inp.c_str(), FALSE)) { DeleteFileA(orig.c_str()); done.push_back("Restored the game's Input.ini"); }
        else done.push_back("Could not restore Input.ini (Input.ini.original is still there)");
    } else {
        std::string text;
        size_t at;
        bool changed = false;
        std::string parts[4];
        split_profile(k_inputProfile, parts[0], parts[1]);
        split_profile(pad2_profile(), parts[2], parts[3]);
        if (read_file(inp, text)) {
            if ((at = text.find(k_inputProfile)) != std::string::npos) { text.erase(at, strlen(k_inputProfile)); changed = true; }
            for (auto &part : parts)
                if (!part.empty() && (at = text.find(part)) != std::string::npos) { text.erase(at, part.size()); changed = true; }
        }
        if (changed && write_file(inp, text)) done.push_back("Removed the controller profiles from Input.ini");
    }

    // 3. The game's own settings in Documents
    if (!g.rotkIniPath.empty()) {
        std::string before = g.rotkIniPath + ".before-rotkfix";
        if (file_exists(before)) {
            if (CopyFileA(before.c_str(), g.rotkIniPath.c_str(), FALSE)) { DeleteFileA(before.c_str()); done.push_back("Restored your game settings (ROTK.ini)"); }
        } else if (createdRotkIni && g.rotkIniExists) {
            DeleteFileA(g.rotkIniPath.c_str());
            done.push_back("Removed the game settings file ROTK Fix created (the game makes a new one)");
        } else if (g.rotkIniExists) {
            // no copy of the old file: point player 1 back at the game's keyboard layout
            const char *r = g.rotkIniPath.c_str();
            if (_stricmp(gets(r, "input0", "map", "").c_str(), "ROTKFixPad") == 0) {
                WritePrivateProfileStringA("input0", "map", "ROTKDefault", r);
                WritePrivateProfileStringA("input0", "mode", "KB", r);
                done.push_back("Set player 1 back to keyboard in ROTK.ini");
            }
        }
    }

    // 4. Everything else ROTK Fix added (Screenshots are kept: they are the player's)
    // SDL3.dll is in use by this launcher; it goes with the launcher when it closes
    for (const char *f : { "ROTKFix.ini", "ROTKFix.log" })
        if (file_exists(dir + "\\" + f) && DeleteFileA((dir + "\\" + f).c_str())) done.push_back(std::string("Removed ") + f);
    if (GetFileAttributesA((dir + "\\ROTKFix").c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (delete_tree(dir + "\\ROTKFix")) done.push_back("Removed the ROTKFix folder");
        else done.push_back("Could not remove the ROTKFix folder completely");
    }

    // 5. The launcher itself: removed by remove_launcher_after_exit() when it closes
    done.push_back("The launcher removes itself when it closes");
    return true;
}

void remove_launcher_after_exit()
{
    char self[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, self, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    std::string exe = self, dir = exe.substr(0, exe.find_last_of("\\/"));
    std::string q = "\"" + exe + "\"", sdl = "\"" + dir + "\\SDL3.dll\"";
    // retry for up to ~30 s in case closing takes a moment
    std::string cmd = "cmd.exe /c for /l %i in (1,1,30) do (ping -n 2 127.0.0.1 >nul & del /f /q " + q + " " + sdl +
                      " 2>nul & if not exist " + q + " if not exist " + sdl + " exit)";
    char sysdir[MAX_PATH] = "";
    GetSystemDirectoryA(sysdir, MAX_PATH);
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    if (CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, sysdir[0] ? sysdir : NULL, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
}

// ---------------------------------------------------------------------------
// Finding the game and setting ROTK Fix up there

static std::string lower(std::string v) { for (auto &c : v) c = (char)tolower((unsigned char)c); return v; }
static std::string trim_dir(std::string d)
{
    while (!d.empty() && (d.back() == '\\' || d.back() == '/' || d.back() == '"' || d.back() == ' ')) d.pop_back();
    while (!d.empty() && (d[0] == '"' || d[0] == ' ')) d.erase(0, 1);
    return d;
}
static std::string dir_of(std::string path)
{
    path = trim_dir(path);
    size_t comma = path.find(','); if (comma != std::string::npos) path = path.substr(0, comma);   // "x.exe,0"
    path = trim_dir(path);
    size_t k = path.find_last_of("\\/");
    return k == std::string::npos ? "" : path.substr(0, k);
}

static void add_candidate(std::vector<std::string> &out, std::string d)
{
    d = trim_dir(d);
    if (d.empty() || !file_exists(d + "\\rotk.exe")) return;
    char full[MAX_PATH];
    if (GetFullPathNameA(d.c_str(), MAX_PATH, full, NULL)) d = full;
    for (auto &o : out) if (lower(o) == lower(d)) return;
    out.push_back(d);
}

static std::string reg_str(HKEY k, const char *name)
{
    char b[1024]; DWORD sz = sizeof b, type = 0;
    if (RegQueryValueExA(k, name, NULL, &type, (BYTE *)b, &sz) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return "";
    b[sizeof b - 1] = 0;
    return b;
}

static bool looks_like_rotk(const std::string &name)
{
    std::string n = lower(name);
    return n.find("return of the king") != std::string::npos || n.find("rotk") != std::string::npos;
}

static void scan_uninstall(HKEY root, REGSAM view, std::vector<std::string> &out)
{
    HKEY k;
    if (RegOpenKeyExA(root, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view, &k) != ERROR_SUCCESS) return;
    char sub[256];
    for (DWORD i = 0; ; i++) {
        DWORD n = sizeof sub;
        if (RegEnumKeyExA(k, i, sub, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        HKEY e;
        if (RegOpenKeyExA(k, sub, 0, KEY_READ | view, &e) != ERROR_SUCCESS) continue;
        if (looks_like_rotk(reg_str(e, "DisplayName"))) {
            add_candidate(out, reg_str(e, "InstallLocation"));
            add_candidate(out, reg_str(e, "Inno Setup: App Path"));
            add_candidate(out, dir_of(reg_str(e, "DisplayIcon")));
            add_candidate(out, dir_of(reg_str(e, "UninstallString")));
        }
        RegCloseKey(e);
    }
    RegCloseKey(k);
}

// Publisher keys (EA's installers record the install folder under various value names)
static void scan_publisher(HKEY root, REGSAM view, const char *path, int depth, std::vector<std::string> &out)
{
    HKEY k;
    if (RegOpenKeyExA(root, path, 0, KEY_READ | view, &k) != ERROR_SUCCESS) return;
    if (looks_like_rotk(path)) {
        static const char *names[] = { "Install Dir", "InstallDir", "Install Path", "InstallPath", "Folder", "Path", "InstallLocation" };
        for (const char *v : names) add_candidate(out, reg_str(k, v));
    }
    if (depth > 0) {
        char sub[256];
        for (DWORD i = 0; ; i++) {
            DWORD n = sizeof sub;
            if (RegEnumKeyExA(k, i, sub, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            scan_publisher(root, view, (std::string(path) + "\\" + sub).c_str(), depth - 1, out);
        }
    }
    RegCloseKey(k);
}

// One level of subfolders of 'base' that contain rotk.exe
static void scan_folder(const std::string &base, std::vector<std::string> &out)
{
    add_candidate(out, base);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((base + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        add_candidate(out, base + "\\" + fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

std::vector<FoundGame> find_game_folders()
{
    std::vector<std::string> dirs;
    for (REGSAM view : { (REGSAM)KEY_WOW64_32KEY, (REGSAM)KEY_WOW64_64KEY }) {
        scan_uninstall(HKEY_LOCAL_MACHINE, view, dirs);
        scan_uninstall(HKEY_CURRENT_USER, view, dirs);
        for (const char *pub : { "SOFTWARE\\Electronic Arts", "SOFTWARE\\EA GAMES", "SOFTWARE\\EA Games" })
            scan_publisher(HKEY_LOCAL_MACHINE, view, pub, 2, dirs);
    }
    // the usual places, on each local drive
    DWORD drives = GetLogicalDrives();
    for (int d = 0; d < 26; d++) {
        if (!(drives & (1u << d))) continue;
        std::string root = std::string(1, (char)('A' + d)) + ":";
        if (GetDriveTypeA((root + "\\").c_str()) != DRIVE_FIXED) continue;
        for (const char *sub : { "\\Games", "\\EA Games", "\\Program Files (x86)\\EA Games", "\\Program Files\\EA Games",
                                 "\\Program Files (x86)", "\\Program Files", "\\GOG Games" })
            scan_folder(root + sub, dirs);
    }
    std::vector<FoundGame> out;
    for (auto &d : dirs) out.push_back({ d, inspect_game(d) });
    // supported versions first
    std::stable_sort(out.begin(), out.end(), [](const FoundGame &x, const FoundGame &y) { return x.info.versionOk && !y.info.versionOk; });
    return out;
}

bool pick_folder(std::string &out)
{
    bool ok = false;
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog *dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, (void **)&dlg))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dlg->SetTitle(L"Choose the folder with rotk.exe");
        if (SUCCEEDED(dlg->Show(GetActiveWindow()))) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR w = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &w))) {
                    char b[MAX_PATH * 2];
                    if (WideCharToMultiByte(CP_ACP, 0, w, -1, b, sizeof b, NULL, NULL) > 0) { out = b; ok = true; }
                    CoTaskMemFree(w);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return ok;
}

static bool copy_tree(const std::string &from, const std::string &toDir, DWORD &error)
{
    std::string f = from; f.push_back('\0');
    std::string t = toDir; t.push_back('\0');
    SHFILEOPSTRUCTA op; memset(&op, 0, sizeof op);
    op.wFunc = FO_COPY; op.pFrom = f.c_str(); op.pTo = t.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI | FOF_SILENT;
    int r = SHFileOperationA(&op);
    error = (DWORD)r;
    return r == 0 && !op.fAnyOperationsAborted;
}

bool install_to(const std::string &src, const std::string &dst, std::string &err, bool &accessDenied)
{
    accessDenied = false;
    static const char *files[] = { "ROTKFixLauncher.exe", "ROTKFix.dll", "d3d8.dll", "dinput8.dll", "SDL3.dll" };
    for (const char *f : files)
        if (!file_exists(src + "\\" + f)) { err = std::string("ROTK Fix is incomplete here: ") + f + " is missing. Unzip the whole download again."; return false; }
    if (lower(src) == lower(dst)) { err = "That is already this folder."; return false; }
    // check we can write there at all
    std::string probe = dst + "\\rotkfix-write-test.tmp";
    HANDLE h = CreateFileA(probe.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        accessDenied = GetLastError() == ERROR_ACCESS_DENIED;
        err = accessDenied ? "Windows needs administrator permission to change that folder."
                           : "Could not write to that folder (error " + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(h);
    // keep another mod's loader files so uninstalling can put them back
    GameInfo g = inspect_game(dst);
    if (g.otherD3D8) MoveFileExA((dst + "\\d3d8.dll").c_str(), (dst + "\\d3d8.dll.before-rotkfix").c_str(), MOVEFILE_REPLACE_EXISTING);
    if (g.otherDinput) MoveFileExA((dst + "\\dinput8.dll").c_str(), (dst + "\\dinput8.dll.before-rotkfix").c_str(), MOVEFILE_REPLACE_EXISTING);
    for (const char *f : files) {
        if (!CopyFileA((src + "\\" + f).c_str(), (dst + "\\" + f).c_str(), FALSE)) {
            err = std::string("Could not copy ") + f + (GetLastError() == ERROR_SHARING_VIOLATION ? " (is the game running?)" : "") + ".";
            return false;
        }
    }
    for (const char *f : { "README.txt", "ROTKFix.ini" })
        if (file_exists(src + "\\" + f) && !file_exists(dst + "\\" + f)) CopyFileA((src + "\\" + f).c_str(), (dst + "\\" + f).c_str(), TRUE);
    if (GetFileAttributesA((src + "\\ROTKFix").c_str()) != INVALID_FILE_ATTRIBUTES) {
        DWORD e = 0;
        if (!copy_tree(src + "\\ROTKFix", dst, e)) { err = "Could not copy the ROTKFix folder (error " + std::to_string(e) + ")."; return false; }
    }
    return true;
}

// Can we create files in this folder? (Program Files needs administrator rights.)
bool folder_writable(const std::string &dir)
{
    std::string probe = dir + "\\rotkfix-write-test.tmp";
    HANDLE h = CreateFileA(probe.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() != ERROR_ACCESS_DENIED;
    CloseHandle(h);
    return true;
}

bool start_launcher(const std::string &dir, bool elevated, const std::string &args)
{
    std::string exe = dir + "\\ROTKFixLauncher.exe";
    HINSTANCE r = ShellExecuteA(NULL, elevated ? "runas" : "open", exe.c_str(), args.empty() ? NULL : args.c_str(), dir.c_str(), SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}
