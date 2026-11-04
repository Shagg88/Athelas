// Main.cpp - entry points (the proxy's DllMain, the Athelas mod's ModInit), log, and the functions that modify the
// game's code
// Language: C++17, MSVC or mingw-w64 g++, Win32.
#include "ROTKPlus.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

static char             g_dir[MAX_PATH];   // the game folder: settings, statistics and logs (for the mod too)
static HMODULE          g_self;
static CRITICAL_SECTION g_logCs;
static FILE*            g_log;        // this session's own file: ROTKPlus_logs\ROTKPlus_<date>_<time>.log
static char             g_logStamp[40];   // its <date>_<time>[_n], for the copy of ROTK Fix's log at the end
static FILE*            g_logLatest;  // ROTKPlus.log: a copy of the latest session, always in the same place
static bool             g_detail = true;
static DWORD            g_startTick;

const char* GameDir() { return g_dir; }
bool LogDetail() { return g_detail; }

void Log(const char* fmt, ...)
{
    if (!g_log && !g_logLatest) return;
    EnterCriticalSection(&g_logCs);
    SYSTEMTIME t; GetLocalTime(&t);
    char line[1024];
    int n = sprintf_s(line, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt);
    const int room = (int)sizeof line - n - 2;
    const int m = vsnprintf(line + n, room, fmt, ap);
    va_end(ap);
    n += (m < 0 || m >= room) ? room - 1 : m;
    line[n++] = '\n';
    for (FILE* f : { g_log, g_logLatest }) if (f) { fwrite(line, 1, n, f); fflush(f); }
    LeaveCriticalSection(&g_logCs);
}

// Opens a new log file for this session without touching the earlier ones, and refreshes ROTKPlus.log.
static void OpenLogs()
{
    char dir[MAX_PATH], path[MAX_PATH];
    sprintf_s(dir, "%s\\ROTKPlus_logs", g_dir);
    CreateDirectoryA(dir, nullptr);
    SYSTEMTIME t; GetLocalTime(&t);
    for (int k = 0; k < 100 && !g_log; ++k) {
        char suffix[8] = ""; if (k) sprintf_s(suffix, "_%d", k + 1);
        sprintf_s(path, "%s\\ROTKPlus_%04d-%02d-%02d_%02d-%02d-%02d%s.log", dir, t.wYear, t.wMonth, t.wDay,
                  t.wHour, t.wMinute, t.wSecond, suffix);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) continue;   // restarted within the same second
        fopen_s(&g_log, path, "w");
        if (g_log) sprintf_s(g_logStamp, "%04d-%02d-%02d_%02d-%02d-%02d%s", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                             t.wSecond, suffix);
    }
    sprintf_s(path, "%s\\ROTKPlus.log", g_dir);
    fopen_s(&g_logLatest, path, "w");
}

// [Log] Keep=N: keeps only the N newest session files (0, the default, never deletes anything), and as many copies of
// ROTK Fix's log (see the end of the session below).
static void DeleteOldLogs(int keep, const char* prefix)
{
    if (keep <= 0) return;
    char pattern[MAX_PATH];
    sprintf_s(pattern, "%s\\ROTKPlus_logs\\%s_*.log", g_dir, prefix);
    std::vector<std::string> names;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do names.push_back(fd.cFileName); while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());   // the names start with the date: oldest first
    for (size_t i = 0; i + keep < names.size(); ++i) {
        char p[MAX_PATH]; sprintf_s(p, "%s\\ROTKPlus_logs\\%s", g_dir, names[i].c_str());
        DeleteFileA(p);
    }
}

// A copy of the statistics at every start (03/11/2026: the whole campaign was in one file on SteamOS), in
// ROTKPlus_logs as ROTKPlus_players_<date>_<time>.ini, so that copying the logs also saves them. Only when the file
// changed since the newest copy (starting the game ten times without playing must not push the real ones out); the
// newest [Log] StatsBackups (10, 0 = none) are kept.
static bool SameFile(const char* a, const char* b)
{
    FILE* fa = nullptr; FILE* fb = nullptr;
    if (fopen_s(&fa, a, "rb") || !fa) return false;
    if (fopen_s(&fb, b, "rb") || !fb) { fclose(fa); return false; }
    bool same = true;
    char ba[4096], bb[4096];
    for (;;) {
        const size_t na = fread(ba, 1, sizeof ba, fa), nb = fread(bb, 1, sizeof bb, fb);
        if (na != nb || memcmp(ba, bb, na)) { same = false; break; }
        if (!na) break;
    }
    fclose(fa); fclose(fb);
    return same;
}

static void BackupStats(int keep)
{
    if (keep <= 0) return;
    char src[MAX_PATH], pattern[MAX_PATH];
    sprintf_s(src, "%s\\ROTKPlus_players.ini", g_dir);
    if (GetFileAttributesA(src) == INVALID_FILE_ATTRIBUTES) { Log("statistics: no ROTKPlus_players.ini yet, nothing to copy"); return; }
    sprintf_s(pattern, "%s\\ROTKPlus_logs\\ROTKPlus_players_*.ini", g_dir);
    std::vector<std::string> names;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) { do names.push_back(fd.cFileName); while (FindNextFileA(h, &fd)); FindClose(h); }
    std::sort(names.begin(), names.end());   // the names start with the date: oldest first
    char p[MAX_PATH];
    if (!names.empty()) {
        sprintf_s(p, "%s\\ROTKPlus_logs\\%s", g_dir, names.back().c_str());
        if (SameFile(src, p)) { Log("statistics: unchanged since the copy %s", names.back().c_str()); return; }
    }
    char stamp[40];
    if (g_logStamp[0]) strcpy_s(stamp, g_logStamp);
    else {
        SYSTEMTIME t; GetLocalTime(&t);
        sprintf_s(stamp, "%04d-%02d-%02d_%02d-%02d-%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    }
    char dir[MAX_PATH];
    sprintf_s(dir, "%s\\ROTKPlus_logs", g_dir);
    CreateDirectoryA(dir, nullptr);
    sprintf_s(p, "%s\\ROTKPlus_players_%s.ini", dir, stamp);
    if (!CopyFileA(src, p, FALSE)) { Log("statistics: copy to %s failed (error %lu)", p, GetLastError()); return; }
    names.push_back(std::string("ROTKPlus_players_") + stamp + ".ini");
    int removed = 0;
    for (size_t i = 0; i + keep < names.size(); ++i) {
        sprintf_s(p, "%s\\%s", dir, names[i].c_str());
        if (DeleteFileA(p)) ++removed;
    }
    Log("statistics: copied to ROTKPlus_logs\\ROTKPlus_players_%s.ini (%d older copies removed, the newest %d kept)", stamp,
        removed, keep);
}

// The first lines of every session: the setup, so that a report does not need to ask for it.
static void LogSessionHeader()
{
    SYSTEMTIME t; GetLocalTime(&t);
    Log("session start %04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    typedef const char*(__cdecl* WineVer)(void);
    typedef LONG(WINAPI* RtlVer)(OSVERSIONINFOW*);
    WineVer wine = nt ? (WineVer)GetProcAddress(nt, "wine_get_version") : nullptr;
    RtlVer rtl = nt ? (RtlVer)GetProcAddress(nt, "RtlGetVersion") : nullptr;
    OSVERSIONINFOW os = { sizeof os };
    if (rtl) rtl(&os);
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    if (wine) Log("system: Wine/Proton %s (reports Windows %lu.%lu build %lu), %lu logical CPUs", wine(),
                  os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber, si.dwNumberOfProcessors);
    else      Log("system: Windows %s (version %lu.%lu build %lu), %lu logical CPUs",
                  os.dwBuildNumber >= 22000 ? "11" : os.dwMajorVersion >= 10 ? "10" : "older than 10",
                  os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber, si.dwNumberOfProcessors);
    for (const char* name : { "ROTKFix.dll", "SDL3.dll", "d3d8.dll", "dinput8.dll", "DivxMediaLib_orig.dll",
                              "ROTKFix\\mods\\ROTKPlus.dll" }) {
        char p[MAX_PATH]; sprintf_s(p, "%s\\%s", g_dir, name);
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (GetFileAttributesExA(p, GetFileExInfoStandard, &a)) {
            SYSTEMTIME w; FileTimeToSystemTime(&a.ftLastWriteTime, &w);
            Log("file: %s %lu bytes, modified %04d-%02d-%02d", name, a.nFileSizeLow, w.wYear, w.wMonth, w.wDay);
        } else Log("file: %s not present", name);
    }
}

static bool WriteCode(uintptr_t addr, const void* data, size_t n)
{
    DWORD old;
    if (!VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)addr, data, n);
    VirtualProtect((void*)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
    return true;
}

bool Patch(uintptr_t addr, const void* expect, const void* data, size_t n)
{
    if (expect && memcmp((const void*)addr, expect, n) != 0) {
        Log("ERROR: unexpected bytes at %08X, patch skipped", (unsigned)addr);
        return false;
    }
    return WriteCode(addr, data, n);
}

bool PatchDword(uintptr_t addr, uint32_t oldValue, uint32_t newValue)
{
    if (*(const uint32_t*)addr != oldValue) {
        Log("ERROR: %08X holds %08X instead of %08X, patch skipped", (unsigned)addr, *(const uint32_t*)addr, oldValue);
        return false;
    }
    return WriteCode(addr, &newValue, 4);
}

bool PatchCall(uintptr_t addr, uintptr_t oldTarget, const void* newTarget)
{
    const uint8_t op = *(const uint8_t*)addr;
    const uintptr_t cur = addr + 5 + *(const int32_t*)(addr + 1);
    if ((op != 0xE8 && op != 0xE9) || cur != oldTarget) {
        Log("ERROR: no call to %08X at %08X, patch skipped", (unsigned)oldTarget, (unsigned)addr);
        return false;
    }
    const int32_t rel = (int32_t)((uintptr_t)newTarget - (addr + 5));
    return WriteCode(addr + 1, &rel, 4);
}

bool PatchJmp(uintptr_t addr, const void* expect, size_t n, const void* target)
{
    if (expect && memcmp((const void*)addr, expect, n) != 0) {
        Log("ERROR: unexpected bytes at %08X, detour skipped", (unsigned)addr);
        return false;
    }
    uint8_t j[5] = { 0xE9 };
    *(int32_t*)(j + 1) = (int32_t)((uintptr_t)target - (addr + 5));
    return WriteCode(addr, j, 5);
}

// Checks that rotk.exe is the version the addresses were written for (version 1.0, 1,703,936 bytes):
// compile timestamp and image size in the PE header. Athelas' own signatures are not needed here, because ROTK Fix
// patches exactly the same places and the individual patches above verify the bytes they replace.
static bool GameIsSupported()
{
    const BYTE* base = (const BYTE*)0x400000;
    const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
    return nt->FileHeader.TimeDateStamp == 0x3F807074 && nt->OptionalHeader.SizeOfImage == 0x5C7000;
}

// Logs access violations (first few only) that happen in rotk.exe's code or in this DLL: address, registers and
// plausible return addresses on the stack. It does not handle them: the game carries on as it would without ROTKPlus
// (usually with its own error box).
static LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* ep)
{
    const EXCEPTION_RECORD* er = ep->ExceptionRecord;
    const CONTEXT* c = ep->ContextRecord;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    // crash in the game or in ROTKPlus (other modules, e.g. drivers and DXVK, handle their own exceptions)
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&CrashLogger, &self);
    HMODULE at = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(uintptr_t)c->Eip, &at);
    const bool inGame = c->Eip >= 0x401000 && c->Eip < 0x535000;
    if (!inGame && at != self) return EXCEPTION_CONTINUE_SEARCH;
    // A vectored handler runs before the Guard blocks: an access violation that ROTKPlus itself expects and handles
    // (reading a character or a stream that may be gone) also arrives here. So only the first three are written, the
    // line about ROTKPlus included (it used to be written every time, before the count).
    static LONG once;
    if (InterlockedIncrement(&once) > 3) return EXCEPTION_CONTINUE_SEARCH;
    if (!inGame) Log("CRASH inside ROTKPlus, at +%X from the start of the DLL (it may be one ROTKPlus handles itself)",
                     (unsigned)(c->Eip - (uintptr_t)self));
    Log("CRASH at %08X (%s %08X)", c->Eip, er->ExceptionInformation[0] ? "write" : "read",
        (unsigned)er->ExceptionInformation[1]);
    Log("    eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X",
        c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    char line[512]; int n = 0;
    line[0] = 0;   // no caller found: an empty list, not uninitialised bytes
    const uint32_t* sp = (const uint32_t*)c->Esp;
    for (int i = 0, found = 0; i < 512 && found < 16; ++i) {
        uint32_t v;
        if (!Guard([&] { v = sp[i]; })) break;
        if (v < 0x401005 || v >= 0x535000) continue;
        const uint8_t* r = (const uint8_t*)v;
        if (r[-5] == 0xE8 || (r[-6] == 0xFF && (r[-5] & 0x38) == 0x10) || (r[-2] == 0xFF && (r[-1] & 0x38) == 0x10)) {
            n += sprintf_s(line + n, sizeof line - n, " %08X(+%X)", v, i * 4);
            ++found;
        }
    }
    Log("    callers:%s", line);
    return EXCEPTION_CONTINUE_SEARCH;
}

void PvpInstall();                // Pvp.cpp
void SupportInstall(bool victoryKey);   // Support.cpp
void AudioInstall(bool on);       // Audio.cpp
void AudioBanksInstall(bool on);  // Audio.cpp

// --- how this copy was started, and which copy changes the game -------------------------------------------------------
// The proxy (DivxMediaLib.dll, the MSVC build) starts when rotk.exe loads it, at the very start of the game, a few
// milliseconds after ROTK Fix. The Athelas mod (ROTKFix\mods\ROTKPlus.dll, the g++ build) starts when ROTK Fix calls
// its ModInit, the first time the game calls Direct3DCreate8, some 60 ms later: the game has only opened its window by
// then, with no menu, level, sound or Direct3D device yet, so every patch below is still in time, and what needs the
// game's objects already waits for them in the per-frame hook (Pads.cpp, 0x4ABE67, on the game's thread: ROTK Fix's
// own frame hooks would run only with DXVK). With both installed only the first one to start changes the game: it
// leaves a note in the game process' environment (ROTKPLUS_ACTIVE = process id|its file|what it is); a copy that finds
// the note writes a line in the first one's log (through its export ROTKPlusLog) and in ROTK Fix's, and does nothing
// else (no log of its own: it would empty ROTKPlus.log). A proxy from before the note (1.2.1 or older) is recognised by
// the game's original movie library, loaded next to it.
#if defined(ROTKPLUS_MOD)
static const char kKind[] = "Athelas mod";
#else
static const char kKind[] = "DivxMediaLib.dll proxy";
#endif
static const char kActiveVar[] = "ROTKPLUS_ACTIVE";
static bool g_active;   // this copy changes the game

extern "C" __declspec(dllexport) void __cdecl ROTKPlusLog(const char* text)
{
    if (g_active && text) Log("%s", text);
}

// a line in ROTK Fix's log (ROTKFix.log), if ROTK Fix is there
static void RotkFixLog(const char* text)
{
    HMODULE fix = GetModuleHandleA("ROTKFix.dll");
    typedef void(__cdecl* RfLog)(const char*, ...);
    RfLog log = fix ? (RfLog)(void*)GetProcAddress(fix, "RF_Log") : nullptr;
    if (log) log("%s", text);
}

// another copy of ROTKPlus already at work in this game: its file (may be empty) and what it is
static bool OtherCopy(char* file, size_t fileSize, char* what, size_t whatSize)
{
    char v[MAX_PATH + 128];
    const DWORD len = GetEnvironmentVariableA(kActiveVar, v, sizeof v);
    char* rest = nullptr;
    if (len && len < sizeof v && strtoul(v, &rest, 10) == GetCurrentProcessId() && rest && *rest == '|') {
        char* bar = strchr(rest + 1, '|');
        if (bar) {
            *bar = 0;
            strncpy_s(file, fileSize, rest + 1, _TRUNCATE);
            strncpy_s(what, whatSize, bar + 1, _TRUNCATE);
            return true;
        }
    }
#if defined(ROTKPLUS_MOD)
    if (GetModuleHandleA("DivxMediaLib_orig.dll")) {
        file[0] = 0;
        strncpy_s(what, whatSize, "a DivxMediaLib.dll proxy (ROTKPlus 1.2.1 or older)", _TRUNCATE);
        return true;
    }
#endif
    return false;
}

static void Start()
{
    g_startTick = GetTickCount();
    g_detail = IniInt("Log", "Detail", 0) != 0;   // (1 until 1.2.0: the level traces are for reports, not for everybody)
    OpenLogs();
    DeleteOldLogs(IniInt("Log", "Keep", 0), "ROTKPlus");
    DeleteOldLogs(IniInt("Log", "Keep", 0), "ROTKFix");
    Log("ROTKPlus %s", ROTKPLUS_VERSION);
    Log("game folder: %s", g_dir);
    char self[MAX_PATH] = "";
    GetModuleFileNameA(g_self, self, MAX_PATH);
    Log("started as the %s: %s", kKind, self);
    LogSessionHeader();
    Log("log: detail %d, keep %d session files (0 = all)", g_detail, IniInt("Log", "Keep", 0));
    BackupStats(IniInt("Log", "StatsBackups", 10));
    if ((uintptr_t)GetModuleHandleA(NULL) != 0x400000 || !GameIsSupported()) {
        Log("rotk.exe not recognised (version 1.0, 1,703,936 bytes, is required): nothing was changed");
        return;
    }
    AddVectoredExceptionHandler(1, CrashLogger);
    LoadConfig();
    FastTextInstall(IniInt("General", "FastText", 1));
    SupportInstall(IniInt("Diag", "VictoryKey", 0) != 0);
    AudioInstall(IniInt("Diag", "AudioLog", 0) != 0);
    SimInstall(IniInt("Debug", "SimPads", 0) != 0);
    if (g_cfg.coop) {
        AudioBanksInstall(IniInt("Coop", "SoundBanks", 1) != 0);
        CoopInstall();
    }
    else Log("co-op with 3 or 4 players is off ([Coop] Enabled=0)");
    PvpInstall();
}

// the first copy changes the game; a second one says so and stops
static void Begin()
{
    char self[MAX_PATH] = "";
    GetModuleFileNameA(g_self, self, MAX_PATH);
    char file[MAX_PATH], what[160];
    if (OtherCopy(file, sizeof file, what, sizeof what)) {
        char line[MAX_PATH + 300];
        sprintf_s(line, "another copy of ROTKPlus started too, the %s %s (%s): it changes nothing, %s is at work",
                  kKind, ROTKPLUS_VERSION, self, what);
        HMODULE first = file[0] ? GetModuleHandleA(file) : nullptr;
        typedef void(__cdecl* NoteFn)(const char*);
        NoteFn note = first ? (NoteFn)(void*)GetProcAddress(first, "ROTKPlusLog") : nullptr;
        if (note) note(line);
        sprintf_s(line, "ROTKPlus %s (%s): %s is already at work in this game, this copy changes nothing", ROTKPLUS_VERSION,
                  kKind, what);
        RotkFixLog(line);
        return;
    }
    char note[MAX_PATH + 128];
    sprintf_s(note, "%lu|%s|ROTKPlus %s, the %s", GetCurrentProcessId(), self, ROTKPLUS_VERSION, kKind);
    SetEnvironmentVariableA(kActiveVar, note);
    g_active = true;
    Start();
}

#if defined(ROTKPLUS_MOD)
// Called once by ROTK Fix after it has loaded the mod (ROTKFix\mods\ROTKPlus.dll), at the game's first Direct3DCreate8.
// Settings, statistics and logs stay in the game folder, as with the proxy.
extern "C" __declspec(dllexport) void __cdecl ModInit(void)
{
    static bool started;
    if (started) return;
    started = true;
    GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
    if (char* s = strrchr(g_dir, '\\')) *s = 0;
    Begin();
}
#endif

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        g_self = mod;
        InitializeCriticalSection(&g_logCs);
#if !defined(ROTKPLUS_MOD)
        GetModuleFileNameA(mod, g_dir, MAX_PATH);
        if (char* s = strrchr(g_dir, '\\')) *s = 0;
        Begin();
#endif
    } else if (reason == DLL_PROCESS_DETACH && g_active) {
        Guard([&] {
            const DWORD s = (GetTickCount() - g_startTick) / 1000;
            Log("session end after %lu min %lu s", s / 60, s % 60);
            // ROTK Fix rewrites its log at every start: a copy next to this session's own, with the same date and time,
            // so that the two can be read together later ([Log] CopyRotkFixLog=0 turns it off)
            if (g_logStamp[0] && IniInt("Log", "CopyRotkFixLog", 1)) {
                char from[MAX_PATH], to[MAX_PATH];
                sprintf_s(from, "%s\\ROTKFix.log", g_dir);
                sprintf_s(to, "%s\\ROTKPlus_logs\\ROTKFix_%s.log", g_dir, g_logStamp);
                Log("log: copy of ROTKFix.log: %s", CopyFileA(from, to, TRUE) ? to : "not made");
            }
        });
    }
    return TRUE;
}
