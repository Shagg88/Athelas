/*
 * ROTK Fix - core (test build 82)
 *
 * Loaded into The Lord of the Rings: The Return of the King (PC, 2003)
 * through the d3d8.dll and dinput8.dll forwarders in the game folder (the
 * game delay-loads both by name).
 *
 * This file:
 *   - Verifies it is running inside the expected rotk.exe build. Every patch
 *     checks the original bytes first; anything that does not match is left
 *     alone.
 *   - Settings (ROTKFix.ini next to rotk.exe, created with defaults if
 *     missing) and the log (ROTKFix.log, capped at 4 MB).
 *   - Frame limiter: replaces the game's busy-wait 62 fps cap with a
 *     high-resolution waitable timer plus a short final spin on a fixed
 *     schedule at TargetFPS.
 *   - Movies: skippable videos, optional skipping of the opening logos,
 *     the cutscene sound delay, and the movie display mode.
 *   - Optional developer menu buttons ([Extras] DeveloperButtons).
 *   - Calls the other parts' patches (input.c, sound.c) and graphics/post
 *     set-up.
 *
 * Only this project's own code. No game code, no third-party code.
 */
#include "rotkfix.h"
#include <shlobj.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

/* ---- Addresses in rotk.exe (1,703,936 bytes, PE timestamp 2003-10-05) ---- */
#define ADDR_FRAME_FUNC   0x004828E0u  /* per-frame timing function           */
#define ADDR_FRAME_RESUME 0x004828E6u  /* first instruction after prologue    */
#define ADDR_FPS_CAP      0x00562ADCu  /* int: built-in cap, 62 in this build */
#define ADDR_CAP_CHECK    0x00482907u  /* cmp dword ptr [ADDR_FPS_CAP], ebx   */

static const unsigned char SIG_FRAME_FUNC[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8 };
static const unsigned char SIG_CAP_CHECK[6]  = { 0x39, 0x1D, 0xDC, 0x2A, 0x56, 0x00 };

/* ---- Settings ---- */
static int  g_limiterMode = 1;   /* 1 = ours, 0 = leave the game's own limiter */
static int  g_targetFps   = 60;
static int  g_logEnabled  = 1;
int         g_detailLog   = 0;     /* [Log] Detailed: testing diagnostics */
static long g_logBytes;
#define LOG_LIMIT (4L * 1024 * 1024)
static int  g_logInterval = 5;   /* seconds between stats lines */
static int  g_spinUs      = 1000;/* final busy-wait window, microseconds */
static int  g_skipAllVideos = 1; /* make every video skippable */

/* ---- State ---- */
char                 g_dir[MAX_PATH];
static FILE         *g_log;
static LARGE_INTEGER g_freq;
static LONGLONG      g_period;       /* ticks per frame */
static LONGLONG      g_deadline;     /* next frame deadline (ticks) */
static HANDLE        g_timer;
static int           g_timerHiRes;
static DWORD         g_frameThread;
enum { MD_AUTO, MD_STRETCH, MD_43 };
static int  g_movieDisplay = MD_AUTO;   /* [Video] MovieDisplay */
static int  g_skipLogos = 0;            /* [Video] SkipOpeningLogos */
static int  g_audioDelayMs = 0;         /* [Video] CutsceneAudioDelay */
static int  g_devButtons = 0;           /* [Extras] DeveloperButtons */
static int  g_movieOverrides = 1;       /* [Video] ReplacementMovies */
static volatile float g_movieHold;      /* seconds of movie clock still to hold back */
static unsigned char *g_trampoline;
static int           g_patched;

/* frame stats */
#define MAX_SAMPLES 16384   /* 60 s at 240 fps fits */
static LONGLONG g_lastFrame;
static LONGLONG g_windowStart;
static unsigned g_samples[MAX_SAMPLES]; /* frame intervals, microseconds */
static int      g_nSamples;
static int      g_nFrames;   /* all frames in the window (samples may stop at MAX_SAMPLES) */
static int      g_lateFrames;   /* frames that missed their deadline (build 2) */

/* ---------------------------------------------------------------------- */

void logf_(const char *fmt, ...)
{
    if (!g_log || g_logBytes > LOG_LIMIT) return;
    SYSTEMTIME st; GetLocalTime(&st);
    int n = fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); n += vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log);
    g_logBytes += n > 0 ? n + 1 : 1;
    if (g_logBytes > LOG_LIMIT) fputs("(log size limit reached; nothing more is written this session)\n", g_log);
    fflush(g_log);
}

void path_in_dir(char *out, const char *name)
{
    snprintf(out, MAX_PATH, "%s\\%s", g_dir, name);
}

static void load_settings(void)
{
    char ini[MAX_PATH];
    path_in_dir(ini, "ROTKFix.ini");
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = fopen(ini, "w");
        if (f) {
            fputs("; ROTK Fix settings - easiest to change with ROTKFixLauncher.exe.\n"
                  "; Edits made here are kept. README.txt explains every setting.\n"
                  "\n"
                  "[Graphics]\n"
                  "Screen               = main\n"
                  "DisplayMode          = borderless\n"
                  "Width                = 0\n"
                  "Height               = 0\n"
                  "RefreshRate          = 0\n"
                  "VSync                = 1\n"
                  "Renderer             = dxvk\n"
                  "DxvkFolder           = dxvk\n"
                  "MSAA                 = 4\n"
                  "AlphaEdgeSmoothing   = 1\n"
                  "AnisotropicFiltering = 16\n"
                  "TextureLODBias       = -0.50\n"
                  "Sharpening           = 60\n"
                  "Brightness           = 1.00\n"
                  "ColourLook           = original\n"
                  "ColourLookStrength   = 100\n"
                  "WidescreenFix        = 1\n"
                  "FieldOfView          = 100\n"
                  "DrawDistance         = 100\n"
                  "\n"
                  "[Frame]\n"
                  "TargetFPS        = 60\n"
                  "Limiter          = 1\n"
                  "SpinMicroseconds = 1000\n"
                  "\n"
                  "[Video]\n"
                  "MovieDisplay           = auto\n"
                  "SharperMovies          = 70\n"
                  "CutsceneAudioDelay     = 0\n"
                  "AllowSkippingAllVideos = 1\n"
                  "SkipOpeningLogos       = 0\n"
                  "\n"
                  "[Sound]\n"
                  "Mode = speakers\n"
                  "\n"
                  "[Gamepad]\n"
                  "Enabled             = 1\n"
                  "SecondController    = 1\n"
                  "ButtonNames         = auto\n"
                  "HideMousePointer    = 1\n"
                  "Vibration           = 100\n"
                  "StickDeadzone       = 12\n"
                  "TriggerThreshold    = 30\n"
                  "HideRealControllers = 1\n"
                  "\n"
                  "[GamepadButtons]\n"
                  "SpeedAttack    = A\n"
                  "PhysicalAttack = B\n"
                  "Parry          = X\n"
                  "FierceAttack   = Y\n"
                  "JumpBack       = LB\n"
                  "Action         = RB\n"
                  "Special        = LS\n"
                  "RangedAttack   = LT\n"
                  "KillingMove    = RT\n"
                  "\n"
                  "[GamepadButtons2]\n"
                  "SpeedAttack    = A\n"
                  "PhysicalAttack = B\n"
                  "Parry          = X\n"
                  "FierceAttack   = Y\n"
                  "JumpBack       = LB\n"
                  "Action         = RB\n"
                  "Special        = LS\n"
                  "RangedAttack   = LT\n"
                  "KillingMove    = RT\n"
                  "\n"
                  "[Textures]\n"
                  "Dump    = 0\n"
                  "Replace = 1\n"
                  "\n"
                  "[Extras]\n"
                  "DeveloperButtons = 0\n"
                  "\n"
                  "[Log]\n"
                  "Enabled  = 1\n"
                  "Detailed = 0\n"
                  "\n"
                  "[Launcher]\n"
                  "Monitor = 0\n", f);
            fclose(f);
        }
    }
    g_limiterMode = GetPrivateProfileIntA("Frame", "Limiter", 1, ini);
    g_targetFps   = GetPrivateProfileIntA("Frame", "TargetFPS", 60, ini);
    g_spinUs      = GetPrivateProfileIntA("Frame", "SpinMicroseconds", 1000, ini);
    g_logEnabled  = GetPrivateProfileIntA("Log", "Enabled", 1, ini);
    g_detailLog   = GetPrivateProfileIntA("Log", "Detailed", 0, ini);
    /* frame-time summary: every few seconds in a detailed log, once a minute otherwise */
    g_logInterval = GetPrivateProfileIntA("Log", "IntervalSeconds", g_detailLog ? 5 : 60, ini);
    if (g_targetFps < 20)  g_targetFps = 20;
    if (g_targetFps > 240) g_targetFps = 240;
    if (g_spinUs < 0)      g_spinUs = 0;
    if (g_spinUs > 4000)   g_spinUs = 4000;
    if (g_logInterval < 1) g_logInterval = 1;
    g_skipAllVideos = GetPrivateProfileIntA("Video", "AllowSkippingAllVideos", 1, ini);
    {
        char mb[32];
        GetPrivateProfileStringA("Video", "MovieDisplay", "auto", mb, sizeof mb, ini);
        g_movieDisplay = !lstrcmpiA(mb, "stretch") ? MD_STRETCH : !lstrcmpiA(mb, "4:3") ? MD_43 : MD_AUTO;
    }
    g_skipLogos = GetPrivateProfileIntA("Video", "SkipOpeningLogos", 0, ini);
    g_movieOverrides = GetPrivateProfileIntA("Video", "ReplacementMovies", 1, ini);
    g_audioDelayMs = GetPrivateProfileIntA("Video", "CutsceneAudioDelay", 0, ini);
    if (g_audioDelayMs < 0) g_audioDelayMs = 0;
    if (g_audioDelayMs > 1000) g_audioDelayMs = 1000;
    input_load_settings(ini);
    icons_load_settings(ini);
    sound_load_settings(ini);
    textures_load_settings(ini);
    g_devButtons = GetPrivateProfileIntA("Extras", "DeveloperButtons", 0, ini);
    graphics_load_settings(ini);
    post_load_settings(ini);
}

static LONGLONG now_ticks(void)
{
    LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart;
}

/* ---------------------------------------------------------------------- */
/* Frame statistics                                                        */

static int cmp_uint(const void *a, const void *b)
{
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
    return (x > y) - (x < y);
}

/* With V-Sync on, frames cannot come faster than the screen refreshes, so a
 * higher target only makes the limiter fight V-Sync. Cap to the refresh rate. */
void limiter_cap_to_refresh(unsigned hz)
{
    if (hz < 30 || g_targetFps <= (int)hz) return;   /* 0/1 = driver default; ignore odd values */
    logf_("limiter: %d fps is above the screen's %u Hz with V-Sync on; capping at %u fps", g_targetFps, hz, hz);
    g_targetFps = (int)hz;
    if (g_freq.QuadPart) g_period = g_freq.QuadPart / g_targetFps;
}

static int g_hitchLogged, g_slowFrames;   /* slow-frame diagnostics, below */

static void flush_stats(LONGLONG now)
{
    if (g_nSamples == 0) return;
    unsigned long long sum = 0;
    unsigned mn = 0xFFFFFFFFu, mx = 0;
    unsigned target = (unsigned)(1000000 / g_targetFps);
    int off2ms = 0;
    for (int i = 0; i < g_nSamples; i++) {
        unsigned v = g_samples[i];
        sum += v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        if (g_limiterMode && (v > target + 2000 || v + 2000 < target)) off2ms++;
    }
    qsort(g_samples, g_nSamples, sizeof g_samples[0], cmp_uint);
    unsigned p99 = g_samples[(g_nSamples * 99) / 100 < g_nSamples ? (g_nSamples * 99) / 100 : g_nSamples - 1];
    unsigned avg = (unsigned)(sum / g_nSamples);
    double secs = (double)(now - g_windowStart) / (double)g_freq.QuadPart;
    logf_("frames=%d fps=%.2f avg=%.3fms min=%.3fms max=%.3fms p99=%.3fms off-by-2ms=%d late=%d slow=%d",
          g_nFrames, g_nFrames / secs, avg / 1000.0, mn / 1000.0, mx / 1000.0, p99 / 1000.0, off2ms, g_lateFrames, g_slowFrames);
    if (g_hitchLogged >= 20 && g_slowFrames > 20) logf_("(%d more slow frames not listed)", g_slowFrames - 20);
    g_hitchLogged = 0; g_slowFrames = 0;
    g_lateFrames = 0;
    g_nSamples = 0;
    g_nFrames = 0;
    g_windowStart = now;
    /* memory: the game is a 32-bit program (2 GB of address space, shared
     * with the Vulkan renderer); log use about once a minute */
    static LONGLONG lastMem;
    if (!lastMem || now - lastMem >= 60 * g_freq.QuadPart) {
        MEMORYSTATUSEX ms; ms.dwLength = sizeof ms;
        if (GlobalMemoryStatusEx(&ms)) {
            unsigned long long used = ms.ullTotalVirtual - ms.ullAvailVirtual;
            logf_("memory: %llu MB of %llu MB address space in use", used >> 20, ms.ullTotalVirtual >> 20);
            if (ms.ullAvailVirtual < (300ull << 20)) logf_("memory: WARNING less than 300 MB of address space left");
        }
        lastMem = now;
    }
}

/* ---- slow-frame diagnostics ----
 * Each frame that takes more than twice the target (and at least 9 ms) is
 * logged with a breakdown: game work before Present, Present itself (waiting
 * for the graphics card / V-Sync), and what happened during the frame (files
 * opened and read, draws, new textures and vertex buffers). At most 20 lines
 * per stats window, so a long slow stretch does not flood the log. */
volatile LONG g_hitch[HITCH_N];
static LONGLONG g_presT0, g_presT1;
void hitch_present(LONGLONG t0, LONGLONG t1) { g_presT0 = t0; g_presT1 = t1; }

typedef HANDLE (WINAPI *CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL (WINAPI *ReadFile_t)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static CreateFileA_t g_realCreateFileA;
static ReadFile_t g_realReadFile;
static char g_lastFile[96];
/* Level files: Data\Game\<lvl>\<lvl>NN.scw. Each new one opened is logged, so the
 * rest of the log can be read level by level. */
volatile LONG g_levelSeq;
static void note_level_file(const char *n)
{
    static char cur[32];
    const char *b = n;
    for (const char *q = n; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
    size_t len = strlen(b);
    if (len < 5 || len >= sizeof cur || lstrcmpiA(b + len - 4, ".scw") != 0) return;
    int inGame = 0;                                                  /* Data\Game\..., not Data\Player */
    for (const char *q = n; *q && !inGame; q++)                       /* a folder named exactly Game ("C:\\Games" is not) */
        inGame = (*q == '/' || *q == '\\') && !_strnicmp(q + 1, "game", 4) && (q[5] == '/' || q[5] == '\\');
    if (!inGame) return;
    if (!lstrcmpiA(b, "GlobScen.scw") || !lstrcmpiA(b, "LoadOnce.scw")) return;
    if (!lstrcmpiA(b, cur)) return;
    lstrcpynA(cur, b, sizeof cur);
    InterlockedIncrement(&g_levelSeq);
    logf_("level: loading %s", b);
}
static HANDLE WINAPI hook_CreateFileA(LPCSTR n, DWORD a, DWORD sh, LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fl, HANDLE t)
{
    InterlockedIncrement(&g_hitch[HITCH_FILES]);
    if (n && !IsBadStringPtrA(n, MAX_PATH)) note_level_file(n);
    if (n) { const char *b = strrchr(n, '\\'); lstrcpynA(g_lastFile, b ? b + 1 : n, sizeof g_lastFile); }
    return g_realCreateFileA(n, a, sh, sa, cd, fl, t);
}
static BOOL WINAPI hook_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov)
{
    BOOL r = g_realReadFile(h, buf, n, got, ov);
    static DWORD rem;
    DWORD b = (r && got) ? *got : n;
    rem += b;
    if (rem >= 1024) { InterlockedExchangeAdd(&g_hitch[HITCH_READKB], (LONG)(rem / 1024)); rem %= 1024; }
    return r;
}
static void *iat_hook(const char *dll, const char *fn, void *hook)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); imp->Name; imp++) {
        if (lstrcmpiA((char *)(base + imp->Name), dll)) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *n = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((char *)n->Name, fn)) continue;
            void *orig = (void *)iat->u1.Function;
            DWORD old;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) return NULL;
            iat->u1.Function = (ULONG_PTR)hook;
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            return orig;
        }
    }
    return NULL;
}
/* Documents with a name the 2003 game can't hold. The game asks for My Documents
 * with SHGetSpecialFolderPathA and saves ROTK.ini and its progress below it. When
 * the real name has characters outside the system's code page (a Cyrillic or
 * Japanese user name on English Windows), the ANSI answer is mangled and nothing
 * saves. Give it the folder's short 8.3 name instead, which is plain ASCII. */
typedef BOOL (WINAPI *SHGetSpecialFolderPathA_t)(HWND, LPSTR, int, BOOL);
static SHGetSpecialFolderPathA_t g_realSHGetSpecialFolderPathA;
int safe_folder_path(int csidl, char *out, size_t n)
{
    wchar_t w[MAX_PATH], sh[MAX_PATH];
    if (!SHGetSpecialFolderPathW(NULL, w, csidl, FALSE)) return 0;
    BOOL lossy = FALSE;
    char buf[MAX_PATH * 2];
    int k = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, buf, sizeof buf, NULL, &lossy);
    if (k <= 0 || lossy) {
        DWORD m = GetShortPathNameW(w, sh, MAX_PATH);
        lossy = FALSE;
        k = m && m < MAX_PATH ? WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, sh, -1, buf, sizeof buf, NULL, &lossy) : 0;
        if (k <= 0 || lossy) return 0;
    }
    if ((size_t)k > n) return 0;
    memcpy(out, buf, (size_t)k);
    return 1;
}
static BOOL WINAPI hook_SHGetSpecialFolderPathA(HWND h, LPSTR path, int csidl, BOOL create)
{
    BOOL r = g_realSHGetSpecialFolderPathA(h, path, csidl, create);
    if (!path) return r;
    wchar_t w[MAX_PATH];
    if (!SHGetSpecialFolderPathW(h, w, csidl, create)) return r;
    BOOL lossy = FALSE;
    char tmp[MAX_PATH * 2];
    int k = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, tmp, sizeof tmp, NULL, &lossy);
    if (k > 0 && !lossy) return r;                        /* the game's own answer is fine */
    char safe[MAX_PATH];
    static int logged;
    if (safe_folder_path(csidl & 0xff, safe, sizeof safe)) {
        lstrcpynA(path, safe, MAX_PATH);
        if (!logged++) logf_("files: the Documents folder's name has characters the game can't use; giving it the short name %s", safe);
        return TRUE;
    }
    if (!logged++) logf_("files: WARNING the Documents folder's name has characters the game can't use, and it has no short name; "
                         "the game may not save its settings and progress");
    return r;
}

static void hitch_install(void)
{
    g_realCreateFileA = (CreateFileA_t)iat_hook("kernel32.dll", "CreateFileA", (void *)hook_CreateFileA);
    g_realReadFile = (ReadFile_t)iat_hook("kernel32.dll", "ReadFile", (void *)hook_ReadFile);
    logf_("diagnostics: slow frames logged (file reads %s)", g_realCreateFileA && g_realReadFile ? "counted" : "not counted");
}

/* ---- stall sampler ([Log] Detailed=1) ----
 * A helper thread watches the game's main thread. When a frame has run for
 * more than 20 ms it pauses the thread for a moment every ~2 ms and notes
 * where it is (instruction address, plus the first few return addresses
 * into rotk.exe found on its stack). Slow frames then list where the time
 * went. While the thread is paused the sampler only reads registers and
 * memory (no locks, no heap), so it cannot deadlock the game. */
#define STALL_MAX 256
typedef struct { DWORD eip, ret[3]; } StallSample;
static StallSample   g_stall[STALL_MAX];
static volatile LONG g_nStall;
static volatile LONG g_frameStartMs, g_frameSeq;
static int g_stallLogged;
int readable(const void *addr, size_t n);

static LONG qpc_ms(LONGLONG t) { return (LONG)(t * 1000 / g_freq.QuadPart); }

static DWORD WINAPI stall_sampler(LPVOID unused)
{
    (void)unused;
    HANDLE th = NULL; DWORD tid = 0;
    HANDLE tm = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    for (;;) {
        LARGE_INTEGER due; due.QuadPart = -20000;   /* 2 ms */
        if (tm && SetWaitableTimer(tm, &due, 0, NULL, NULL, FALSE)) WaitForSingleObject(tm, 50); else Sleep(2);
        DWORD ft = g_frameThread;
        if (!ft) continue;
        if (ft != tid) {
            if (th) CloseHandle(th);
            tid = ft;
            th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, tid);
        }
        if (!th) continue;
        LONG seq = g_frameSeq, start = g_frameStartMs;
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        if (qpc_ms(q.QuadPart) - start < 20) continue;
        LONG n = g_nStall;
        if (n < 0 || n >= STALL_MAX) continue;
        if (SuspendThread(th) == (DWORD)-1) continue;
        StallSample smp = { 0, { 0, 0, 0 } };
        CONTEXT c; memset(&c, 0, sizeof c); c.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(th, &c)) {
            smp.eip = c.Eip;
            const DWORD *sp = (const DWORD *)(DWORD_PTR)c.Esp;
            int k = 0;
            for (int i = 0; i < 512 && k < 3; i += 64) {
                if (!readable(sp + i, 64 * sizeof(DWORD))) break;
                for (int j = i; j < i + 64 && k < 3; j++)
                    if (sp[j] >= 0x00401000u && sp[j] < 0x00535000u) smp.ret[k++] = sp[j];
            }
        }
        ResumeThread(th);
        if (seq == g_frameSeq && g_nStall == n) { g_stall[n] = smp; g_nStall = n + 1; }
    }
    return 0;
}

static void addr_name(DWORD a, char *out, size_t n)
{
    HMODULE m = NULL; char path[MAX_PATH];
    if (a && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(DWORD_PTR)a, &m) && m
        && GetModuleFileNameA(m, path, sizeof path)) {
        const char *b = strrchr(path, '\\');
        b = b ? b + 1 : path;
        if (m == GetModuleHandleA(NULL)) snprintf(out, n, "game 0x%06lx", (unsigned long)a);
        else snprintf(out, n, "%s+0x%lx", b, (unsigned long)(a - (DWORD)(DWORD_PTR)m));
    } else snprintf(out, n, "0x%08lx", (unsigned long)a);
}

static void stall_report(void)
{
    LONG n = g_nStall;
    if (n <= 0 || n > STALL_MAX || g_stallLogged >= 15) return;
    g_stallLogged++;
    /* group by (instruction, first game return address) */
    int used[STALL_MAX] = { 0 };
    for (int pass = 0; pass < 4; pass++) {
        int best = -1, bestN = 0;
        for (int i = 0; i < n; i++) {
            if (used[i]) continue;
            int cnt = 0;
            for (int j = i; j < n; j++)
                if (!used[j] && g_stall[j].eip == g_stall[i].eip && g_stall[j].ret[0] == g_stall[i].ret[0]) cnt++;
            if (cnt > bestN) { bestN = cnt; best = i; }
        }
        if (best < 0) break;
        StallSample b = g_stall[best];
        for (int j = 0; j < n; j++)
            if (!used[j] && g_stall[j].eip == b.eip && g_stall[j].ret[0] == b.ret[0]) used[j] = 1;
        char e[96], r0[48], r1[48], r2[48];
        addr_name(b.eip, e, sizeof e);
        snprintf(r0, sizeof r0, "0x%06lx", (unsigned long)b.ret[0]);
        snprintf(r1, sizeof r1, "0x%06lx", (unsigned long)b.ret[1]);
        snprintf(r2, sizeof r2, "0x%06lx", (unsigned long)b.ret[2]);
        logf_("   stalled in %s (%d of %ld samples) <- %s %s %s", e, bestN, (long)n, r0, r1, r2);
    }
}

static void stall_start(void)
{
    HANDLE h = CreateThread(NULL, 0, stall_sampler, NULL, 0, NULL);
    if (h) { SetThreadPriority(h, THREAD_PRIORITY_TIME_CRITICAL); CloseHandle(h); logf_("diagnostics: stall sampler running"); }
}

static void hitch_frame(LONGLONG now, LONGLONG us)
{
    LONG c[HITCH_N];
    for (int i = 0; i < HITCH_N; i++) c[i] = InterlockedExchange(&g_hitch[i], 0);
    unsigned periodUs = (unsigned)(1000000 / (g_targetFps ? g_targetFps : 60));
    if (us < 9000 || us < 2 * (LONGLONG)periodUs) { g_presT0 = g_presT1 = 0; return; }
    g_slowFrames++;
    if (g_hitchLogged >= 20) { g_presT0 = g_presT1 = 0; return; }
    g_hitchLogged++;
    double f = 1000.0 / (double)g_freq.QuadPart;
    double work = 0, pres = 0, after = 0;
    if (g_presT0 > g_lastFrame && g_presT1 >= g_presT0 && g_presT1 <= now) {
        work = (g_presT0 - g_lastFrame) * f; pres = (g_presT1 - g_presT0) * f; after = (now - g_presT1) * f;
    }
    logf_("slow frame %.1f ms: game %.1f, present %.1f, after %.1f | files %ld (%s) read %ld KB | draws %ld (3D %ld) | new textures %ld, new buffers %ld | 2D copies %ld KB",
          us / 1000.0, work, pres, after, (long)c[HITCH_FILES], c[HITCH_FILES] ? g_lastFile : "-", (long)c[HITCH_READKB],
          (long)c[HITCH_DRAWS], (long)c[HITCH_DRAWS3D], (long)c[HITCH_TEXNEW], (long)c[HITCH_VBNEW], (long)c[HITCH_VBKB]);
    if (g_detailLog && us >= 30000 && us < 2000000) stall_report();
    g_presT0 = g_presT1 = 0;
}

static void record_frame(LONGLONG now)
{
    if (g_lastFrame) {
        LONGLONG us = (now - g_lastFrame) * 1000000 / g_freq.QuadPart;
        if (us > 0xFFFFFFF) us = 0xFFFFFFF;
        if (g_logEnabled) hitch_frame(now, us);
        g_nFrames++;
        if (g_nSamples < MAX_SAMPLES) g_samples[g_nSamples++] = (unsigned)us;
    } else {
        g_windowStart = now;
    }
    g_lastFrame = now;
    g_frameSeq++;
    g_nStall = 0;
    g_frameStartMs = qpc_ms(now);
    if (g_logEnabled && now - g_windowStart >= (LONGLONG)g_logInterval * g_freq.QuadPart)
        flush_stats(now);
}

/* ---------------------------------------------------------------------- */
/* Limiter                                                                 */

static void wait_until(LONGLONG deadline)
{
    LONGLONG spinTicks = g_freq.QuadPart * g_spinUs / 1000000;
    for (;;) {
        LONGLONG remaining = deadline - now_ticks();
        if (remaining <= spinTicks) break;
        LONGLONG sleepTicks = remaining - spinTicks;
        /* due time in 100 ns units, negative = relative */
        LARGE_INTEGER due;
        due.QuadPart = -(sleepTicks * 10000000 / g_freq.QuadPart);
        if (due.QuadPart >= 0) break;
        if (g_timer && SetWaitableTimer(g_timer, &due, 0, NULL, NULL, FALSE))
            WaitForSingleObject(g_timer, 100);
        else
            Sleep(1);
    }
    while (now_ticks() < deadline)
        YieldProcessor();
}

/* Called at the start of every game frame-timing call (game's main thread). */
static volatile LONG g_paceCount;

void __cdecl rotkfix_on_frame(void)
{
    g_paceCount++;
    DWORD tid = GetCurrentThreadId();
    if (!g_frameThread) {
        g_frameThread = tid;
        dlogf_("first frame on thread %lu", (unsigned long)tid);
    } else if (tid != g_frameThread) {
        /* Unexpected caller thread: do not pace it, just note it once. */
        static int warned;
        if (!warned) { warned = 1; logf_("note: frame function called from another thread %lu", (unsigned long)tid); }
        return;
    }

    if (g_limiterMode) {
        LONGLONG now = now_ticks();
        LONGLONG tolerance = g_freq.QuadPart / 2000;   /* 0.5 ms */
        if (!g_deadline || now > g_deadline + tolerance) {
            /* first frame, or this frame ran late (load, heavy scene, video):
             * start a fresh schedule from now instead of rushing the next
             * frames to catch up (build 1 did that, causing 10/23 ms swings) */
            if (g_deadline) g_lateFrames++;
            g_deadline = now + g_period;
        } else {
            if (now < g_deadline)
                wait_until(g_deadline);
            g_deadline += g_period;   /* on time: keep a fixed, drift-free schedule */
        }
    }
    record_frame(now_ticks());
}

/* Some screens (legal text, loading) run a loop that never calls the game's
 * frame function, so nothing paces them. Called from Present: if no frame was
 * paced since the previous Present, wait here instead. */
void limiter_present_backstop(void)
{
    static LONG lastSeen = -1;
    static LONGLONG next;
    LONG c = g_paceCount;
    if (c != lastSeen) { lastSeen = c; next = 0; return; }
    if (!g_limiterMode || !g_period) return;
    LONGLONG now = now_ticks();
    if (!next || now > next + g_period) next = now + g_period;
    else { wait_until(next); next += g_period; }
}

/* ---------------------------------------------------------------------- */
/* Patching                                                                */

int write_code(void *addr, const void *bytes, size_t n)
{
    DWORD old;
    if (!VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(addr, bytes, n);
    VirtualProtect(addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), addr, n);
    return 1;
}

/* Code written at start-up (trampolines) lives in one page that is made
 * read+execute (not writable) once patching is done. */
static BYTE  *g_codePage;
static size_t g_codeUsed;
static int    g_codeSealed;
#define CODE_PAGE_SIZE 4096
void *code_alloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (g_codeSealed || n > CODE_PAGE_SIZE) return NULL;
    if (!g_codePage) {
        g_codePage = (BYTE *)VirtualAlloc(NULL, CODE_PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_codePage) return NULL;
        memset(g_codePage, 0xCC, CODE_PAGE_SIZE);      /* int3 between pieces */
    }
    if (g_codeUsed + n > CODE_PAGE_SIZE) return NULL;
    void *r = g_codePage + g_codeUsed;
    g_codeUsed += n;
    return r;
}
static void code_seal(void)
{
    DWORD old;
    g_codeSealed = 1;
    if (!g_codePage) return;
    FlushInstructionCache(GetCurrentProcess(), g_codePage, CODE_PAGE_SIZE);
    if (!VirtualProtect(g_codePage, CODE_PAGE_SIZE, PAGE_EXECUTE_READ, &old)) logf_("note: could not seal the code page");
}

int readable(const void *addr, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(addr, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (const char *)addr + n <= (const char *)mbi.BaseAddress + mbi.RegionSize;
}

/* Movie table in rotk.exe: { const char *name; int type; } pairs ending with a
 * NULL name whose type is the default for any video not listed.
 * type 0 = never skippable (opening logos), 1 = skippable,
 * 2 = skippable only after it has been watched once (default for story videos). */
#define ADDR_MOVIE_TABLE 0x00572E00u
/* ------------------------------------------------------------------ */
/* Movies: which one is playing                                          */
/*
 * 0x50abd0 starts a movie; its second argument is the file path
 * ("Data/Movies/helmfmv1.avi" etc.). Hooked to remember the name, so the
 * graphics layer can choose how to fit the picture on a widescreen.
 * Story cutscenes ("...fmv...", except the attract video) and the opening
 * logos are widescreen film with black bars baked into a 4:3 frame; everything
 * else is full-frame 4:3.
 */
#define ADDR_MOVIE_START  0x50abd0
#define ADDR_MOVIE_CALLEE 0x50a240
static char g_movieName[64];
static volatile int g_movieFit;      /* MOVIE_FIT_* for the current movie */

int movie_fit(void) { return g_movieFit; }

/* Replacement movies: ROTKFix\movies\<same name>.avi is played instead of
 * the game's own file when it exists (for example, a sharper version made
 * from the player's own copy). The game's files are never changed. */
static char g_moviePath[MAX_PATH];

static const char *__cdecl on_movie_start(const char *path)
{
    char name[64] = "";
    if (path && !IsBadStringPtrA(path, 260)) {
        const char *b = path;
        for (const char *q = path; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
        lstrcpynA(name, b, sizeof name);
    }
    CharLowerA(name);
    lstrcpynA(g_movieName, name, sizeof g_movieName);
    /* the opening logos fill the width too: the New Line part is letterboxed
     * film like the cutscenes, and the EA logo sits in the middle of its frame */
    int film = (strstr(name, "fmv") != NULL && strncmp(name, "attr", 4) != 0) || !strncmp(name, "logos", 5);
    g_movieFit = g_movieDisplay == MD_STRETCH ? MOVIE_FIT_STRETCH
               : g_movieDisplay == MD_43 ? MOVIE_FIT_43
               : film ? MOVIE_FIT_FILM : MOVIE_FIT_43;
    static const char *const fitName[] = { "stretched", "film: fill width, thin bars", "4:3 with side bars" };
    logf_("video: playing %s -> %s", name[0] ? name : "(unknown)", fitName[g_movieFit]);
    g_movieHold = g_audioDelayMs / 1000.0f;
    if (g_skipLogos && !strncmp(name, "logos", 5)) {
        input_inject_key_press(0x1C /* DIK_RETURN */, 400);
        logf_("video: skipping the opening logos (SkipOpeningLogos=1)");
    }
    if (g_movieOverrides && name[0] && !strchr(name, ':') && !strstr(name, "..")) {
        char rel[MAX_PATH], full[MAX_PATH];
        /* relative, in the same form as the game's own "Data/Movies/x.avi" */
        snprintf(rel, sizeof rel, "ROTKFix/movies/%s", name);
        snprintf(full, sizeof full, "%s\\ROTKFix\\movies\\%s", g_dir, name);
        if (GetFileAttributesA(full) != INVALID_FILE_ATTRIBUTES) {
            lstrcpynA(g_moviePath, rel, sizeof g_moviePath);
            logf_("video: playing the replacement %s", rel);
            return g_moviePath;
        }
    }
    return path;
}

/* Movie clock: 0x50c488 adds the frame time to the movie's clock
 * ([movie+0x11a4]); the picture shows whatever frame that clock has reached.
 * Rerouted so the first CutsceneAudioDelay ms of every movie are held back. */
#define ADDR_MOVIE_CLOCK 0x50c488
static const BYTE k_movieClockSig[18] = { 0xD9, 0x44, 0x24, 0x18, 0x33, 0xFF, 0xD8, 0x86, 0xA4, 0x11, 0x00, 0x00,
                                          0xD9, 0x9E, 0xA4, 0x11, 0x00, 0x00 };
static float __cdecl movie_clock_add(float clock, float dt)
{
    if (g_movieHold > 0.0f && dt > 0.0f) {
        float use = dt < g_movieHold ? dt : g_movieHold;
        g_movieHold -= use;
        dt -= use;
    }
    return clock + dt;
}

static void patch_movie_clock(void)
{
    if (!g_audioDelayMs) return;
    if (!readable((void *)ADDR_MOVIE_CLOCK, sizeof k_movieClockSig) ||
        memcmp((void *)ADDR_MOVIE_CLOCK, k_movieClockSig, sizeof k_movieClockSig)) {
        logf_("video: movie clock not recognised; CutsceneAudioDelay off");
        return;
    }
    /* pushad; pushfd; push [esp+0x3C] (dt); push [esi+0x11A4] (clock); call movie_clock_add;
     * add esp,8; popfd; popad; xor edi,edi; fstp [esi+0x11A4]; jmp 0x50c49a */
    BYTE *t = (BYTE *)code_alloc(64);
    if (!t) return;
    BYTE *p = t;
    *p++ = 0x60; *p++ = 0x9C;
    *p++ = 0xFF; *p++ = 0x74; *p++ = 0x24; *p++ = 0x3C;
    *p++ = 0xFF; *p++ = 0xB6; *p++ = 0xA4; *p++ = 0x11; *p++ = 0x00; *p++ = 0x00;
    *p++ = 0xE8; *(LONG *)p = (LONG)((DWORD_PTR)&movie_clock_add - ((DWORD_PTR)p + 4)); p += 4;
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x08;
    *p++ = 0x9D; *p++ = 0x61;
    *p++ = 0x33; *p++ = 0xFF;
    *p++ = 0xD9; *p++ = 0x9E; *p++ = 0xA4; *p++ = 0x11; *p++ = 0x00; *p++ = 0x00;
    *p++ = 0xE9; *(LONG *)p = (LONG)((ADDR_MOVIE_CLOCK + 18) - ((DWORD_PTR)p + 4));
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    BYTE jmp[18];
    memset(jmp, 0x90, sizeof jmp);
    jmp[0] = 0xE9;
    *(LONG *)(jmp + 1) = (LONG)((DWORD_PTR)t - (ADDR_MOVIE_CLOCK + 5));
    if (write_code((void *)ADDR_MOVIE_CLOCK, jmp, sizeof jmp))
        logf_("video: movie pictures held back %d ms for late sound (CutsceneAudioDelay)", g_audioDelayMs);
}

static void patch_movie_start(void)
{
    const BYTE *f = (const BYTE *)ADDR_MOVIE_START;
    if (!readable(f, 7)) { logf_("video: movie start not readable; movie fitting off"); return; }
    LONG rel = *(const LONG *)(f + 1);
    if (f[0] != 0xE8 || ADDR_MOVIE_START + 5 + rel != ADDR_MOVIE_CALLEE || f[5] != 0x84 || f[6] != 0xC0) {
        logf_("video: movie start not recognised; movie fitting off");
        return;
    }
    /* pushad; pushfd; push [esp+44] (path, arg 2); call on_movie_start; add esp,4;
     * mov [esp+44],eax (the path to play); popfd; popad; call 0x50a240; jmp 0x50abd5 */
    BYTE *t = (BYTE *)code_alloc(64);
    if (!t) return;
    BYTE *p = t;
    *p++ = 0x60; *p++ = 0x9C;
    *p++ = 0xFF; *p++ = 0x74; *p++ = 0x24; *p++ = 0x2C;          /* push dword [esp+0x2C] */
    *p++ = 0xE8; *(LONG *)p = (LONG)((DWORD_PTR)&on_movie_start - ((DWORD_PTR)p + 4)); p += 4;
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;                        /* add esp,4 */
    *p++ = 0x89; *p++ = 0x44; *p++ = 0x24; *p++ = 0x2C;          /* mov [esp+0x2C],eax */
    *p++ = 0x9D; *p++ = 0x61;
    *p++ = 0xE8; *(LONG *)p = (LONG)(ADDR_MOVIE_CALLEE - ((DWORD_PTR)p + 4)); p += 4;
    *p++ = 0xE9; *(LONG *)p = (LONG)((ADDR_MOVIE_START + 5) - ((DWORD_PTR)p + 4));
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    BYTE jmp[5];
    jmp[0] = 0xE9;
    *(LONG *)(jmp + 1) = (LONG)((DWORD_PTR)t - (ADDR_MOVIE_START + 5));
    if (write_code((void *)ADDR_MOVIE_START, jmp, 5)) logf_("video: movie fitting ready (MovieDisplay=%s)",
        g_movieDisplay == MD_STRETCH ? "stretch" : g_movieDisplay == MD_43 ? "4:3" : "auto");
}

/* The released game still has three developer buttons in its menus - "Debug
 * Game" on the New/Load screen, "Win Game" and "Restart Game" on the pause
 * screen - and hides them each time those screens open (0x4f6f90 clears a
 * field of each button). With [Extras] DeveloperButtons=1 they stay visible. */
static void patch_dev_buttons(void)
{
    static const DWORD at[3] = { 0x004F6FE5u, 0x004F6FF2u, 0x004F6FFFu };
    static const BYTE hide[4] = { 0x66, 0x89, 0x58, 0x74 };   /* mov word [eax+74h], bx */
    static const BYTE nop4[4] = { 0x0F, 0x1F, 0x40, 0x00 };   /* 4-byte nop */
    if (!g_devButtons) return;
    for (int i = 0; i < 3; i++)
        if (!readable((void *)at[i], 4) || memcmp((void *)at[i], hide, 4)) { logf_("extras: developer buttons code not recognised"); return; }
    for (int i = 0; i < 3; i++) write_code((void *)at[i], nop4, 4);
    logf_("extras: developer buttons shown (Debug Game, Win Game, Restart Game) - testing");
}

static void patch_video_skipping(void)
{
    if (!g_skipAllVideos) { logf_("video: AllowSkippingAllVideos=0, keeping the game's rules"); return; }
    DWORD *t = (DWORD *)ADDR_MOVIE_TABLE;
    if (!readable(t, 8 * 32)) { logf_("video: movie table not readable"); return; }
    /* sanity check: first entry must be "LOGOS" with type 0..2 */
    const char *first = (const char *)t[0];
    if (!readable(first, 6) || memcmp(first, "LOGOS", 6) != 0) { logf_("video: movie table not recognised, not patching"); return; }
    int changed = 0, i;
    for (i = 0; i < 32; i++) {
        DWORD type = t[i * 2 + 1];
        if (type > 2) { logf_("video: unexpected movie type %lu at entry %d, stopping", (unsigned long)type, i); return; }
        if (type != 1) {
            DWORD one = 1;
            if (write_code(&t[i * 2 + 1], &one, 4)) changed++;
        }
        if (t[i * 2] == 0) break;   /* terminator: its type is the default */
    }
    logf_("video: all videos skippable (%d table entries changed, %d entries)", changed, i + 1);
}

/* The game's mode enumeration (0x4a4230) keeps only 4:3 modes:
 *   lea eax,[edx+edx*2] ; shr eax,2 ; cmp eax,ebx ; jne skip
 * Removing the jne lets widescreen modes through. */
#define ADDR_43_CHECK 0x004A443Au
static void patch_mode_filter(void)
{
    static const unsigned char sig[10] = { 0x8D, 0x04, 0x52, 0xC1, 0xE8, 0x02, 0x3B, 0xC3, 0x75, 0x7E };
    if (!graphics_enabled()) return;
    if (!readable((void *)ADDR_43_CHECK, 10) || memcmp((void *)ADDR_43_CHECK, sig, 10) != 0) {
        logf_("graphics: 4:3-only mode filter not found, not patching");
        return;
    }
    static const unsigned char nops[2] = { 0x90, 0x90 };
    if (write_code((void *)(ADDR_43_CHECK + 8), nops, 2))
        logf_("graphics: removed the game's 4:3-only display mode filter");
}

/* The game rebuilds its DirectInput devices on every screen change and
 * movie start (0x4b13f0), then waits Sleep(50) before creating them again:
 * a visible 55 ms hitch each time. Modern input needs no settling time, so
 * the wait becomes Sleep(0). Only the push immediate is changed. */
#define ADDR_INPUT_SLEEP 0x004B149Au   /* mov [0x8cb22c],ebx; pop edi; push 0x32 */
static void patch_input_sleep(void)
{
    static const BYTE sig[9] = { 0x89, 0x1D, 0x2C, 0xB2, 0x8C, 0x00, 0x5F, 0x6A, 0x32 };
    BYTE *at = (BYTE *)ADDR_INPUT_SLEEP;
    if (!readable(at, sizeof sig) || memcmp(at, sig, sizeof sig)) { logf_("input: rebuild pause not recognised; left as is"); return; }
    BYTE zero = 0;
    if (write_code(at + 8, &zero, 1)) logf_("input: removed the 50 ms pause on screen changes");
}

static void apply_patches(void)
{
    HMODULE exe = GetModuleHandleA(NULL);
    if ((DWORD_PTR)exe != 0x00400000u) {
        logf_("not patching: unexpected image base %p", (void *)exe);
        return;
    }
    if (!readable((void *)ADDR_FRAME_FUNC, 6) || !readable((void *)ADDR_CAP_CHECK, 6) ||
        !readable((void *)ADDR_FPS_CAP, 4)) {
        logf_("not patching: expected game memory not present");
        return;
    }
    if (memcmp((void *)ADDR_FRAME_FUNC, SIG_FRAME_FUNC, 6) != 0 ||
        memcmp((void *)ADDR_CAP_CHECK, SIG_CAP_CHECK, 6) != 0) {
        logf_("not patching: unsupported rotk.exe version (signature mismatch)");
        return;
    }
    int cap = *(volatile int *)ADDR_FPS_CAP;
    logf_("rotk.exe recognised; built-in frame cap = %d", cap);

    /* Trampoline:
     *   pushad; pushfd; call rotkfix_on_frame; popfd; popad;
     *   push ebp; mov ebp,esp; and esp,-8;   (original prologue)
     *   jmp ADDR_FRAME_RESUME
     */
    g_trampoline = (unsigned char *)code_alloc(64);
    if (!g_trampoline) { logf_("not patching: no memory for code"); return; }
    unsigned char *p = g_trampoline;
    *p++ = 0x60;                                    /* pushad */
    *p++ = 0x9C;                                    /* pushfd */
    *p++ = 0xE8;                                    /* call rel32 */
    *(LONG *)p = (LONG)((DWORD_PTR)&rotkfix_on_frame - ((DWORD_PTR)p + 4)); p += 4;
    *p++ = 0x9D;                                    /* popfd */
    *p++ = 0x61;                                    /* popad */
    memcpy(p, SIG_FRAME_FUNC, 6); p += 6;           /* original prologue */
    *p++ = 0xE9;                                    /* jmp rel32 */
    *(LONG *)p = (LONG)(ADDR_FRAME_RESUME - ((DWORD_PTR)p + 4));
    FlushInstructionCache(GetCurrentProcess(), g_trampoline, 64);

    unsigned char jmp[6];
    jmp[0] = 0xE9;
    *(LONG *)(jmp + 1) = (LONG)((DWORD_PTR)g_trampoline - (ADDR_FRAME_FUNC + 5));
    jmp[5] = 0x90;                                  /* nop: pad to 6 bytes */
    if (!write_code((void *)ADDR_FRAME_FUNC, jmp, 6)) { logf_("not patching: hook write failed"); return; }

    if (g_limiterMode) {
        int zero = 0;
        if (!write_code((void *)ADDR_FPS_CAP, &zero, 4)) { logf_("warning: could not disable built-in cap"); }
        else logf_("built-in busy-wait limiter disabled; ROTK Fix limiter at %d fps", g_targetFps);
    } else {
        logf_("Limiter=0: keeping the game's own limiter (%d fps), measuring only", cap);
    }
    patch_video_skipping();
    patch_movie_start();
    patch_movie_clock();
    patch_mode_filter();
    input_patch_prompts();
    icons_patch();
    input_patch_rumble();
    sound_patch();
    patch_dev_buttons();
    patch_input_sleep();
    g_patched = 1;
}

/* ---------------------------------------------------------------------- */
/* dinput8 forwarding                                                      */

typedef HRESULT (WINAPI *DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID *, LPVOID);
typedef HRESULT (WINAPI *Simple_t)(void);
typedef HRESULT (WINAPI *GetClassObj_t)(REFCLSID, REFIID, LPVOID *);
static HMODULE g_realDinput;

static HMODULE real_dinput(void)
{
    if (!g_realDinput) {
        char sys[MAX_PATH];
        GetSystemDirectoryA(sys, MAX_PATH);   /* SysWOW64 for a 32-bit process */
        strncat(sys, "\\dinput8.dll", MAX_PATH - strlen(sys) - 1);
        g_realDinput = LoadLibraryA(sys);
        if (!g_realDinput) logf_("error: could not load system dinput8.dll (%lu)", GetLastError());
    }
    return g_realDinput;
}

__declspec(dllexport) HRESULT WINAPI RF_DirectInput8Create(HINSTANCE h, DWORD v, REFIID r, LPVOID *o, LPVOID u)
{
    return input_wrap_dinput8((void *)GetProcAddress(real_dinput(), "DirectInput8Create"), h, v, r, o, u);
}
__declspec(dllexport) HRESULT WINAPI RF_DllCanUnloadNow(void)
{
    Simple_t f = (Simple_t)GetProcAddress(real_dinput(), "DllCanUnloadNow");
    return f ? f() : S_FALSE;
}
__declspec(dllexport) HRESULT WINAPI RF_DllGetClassObject(REFCLSID c, REFIID r, LPVOID *o)
{
    GetClassObj_t f = (GetClassObj_t)GetProcAddress(real_dinput(), "DllGetClassObject");
    return f ? f(c, r, o) : E_FAIL;
}
__declspec(dllexport) HRESULT WINAPI RF_DllRegisterServer(void)   { return E_NOTIMPL; }
__declspec(dllexport) HRESULT WINAPI RF_DllUnregisterServer(void) { return E_NOTIMPL; }

/* ---------------------------------------------------------------------- */

/* If the game crashes, write where, so a tester's log says whether ROTK Fix,
 * the game or a driver was involved. Then let Windows handle it as usual. */
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter;
/* The game installs its own crash handler over ours, so a crash shows only
 * its "RoTK cannot continue" box. A call to address 0 (a missing function)
 * is logged here first, with where it was called from. */
static LONG WINAPI null_call_logger(EXCEPTION_POINTERS *ep)
{
    static volatile LONG logged;
    if (ep && ep->ExceptionRecord && ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->ExceptionAddress == NULL && ep->ContextRecord && InterlockedIncrement(&logged) <= 3) {
        const DWORD *sp = (const DWORD *)(DWORD_PTR)ep->ContextRecord->Esp;
        DWORD ret = readable(sp, 4) ? sp[0] : 0;
        HMODULE mod = NULL; char name[MAX_PATH] = "?";
        if (ret && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(DWORD_PTR)ret, &mod) && mod)
            GetModuleFileNameA(mod, name, sizeof name);
        const char *b = strrchr(name, '\\');
        logf_("crash: call to address 0 from 0x%08lx (%s+0x%lx)", (unsigned long)ret, b ? b + 1 : name,
              (unsigned long)(ret - (DWORD)(DWORD_PTR)mod));
        if (g_log) fflush(g_log);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    static volatile LONG once;
    if (InterlockedExchange(&once, 1) == 0 && ep && ep->ExceptionRecord) {
        void *at = ep->ExceptionRecord->ExceptionAddress;
        char mod[MAX_PATH] = "?";
        HMODULE h = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &h) && h) {
            GetModuleFileNameA(h, mod, MAX_PATH);
            char *b = strrchr(mod, '\\'); if (b) memmove(mod, b + 1, strlen(b));
        }
        g_logBytes = 0;   /* always room for this */
        logf_("CRASH: exception %#lx at %p (%s+%#lx)", (unsigned long)ep->ExceptionRecord->ExceptionCode, at, mod,
              (unsigned long)((DWORD_PTR)at - (DWORD_PTR)h));
    }
    return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(NULL, g_dir, MAX_PATH);
        char *slash = strrchr(g_dir, '\\');
        if (slash) *slash = 0;

        load_settings();
        if (g_logEnabled) {
            char lp[MAX_PATH]; path_in_dir(lp, "ROTKFix.log");
            g_log = fopen(lp, "w");
        }
        logf_("ROTK Fix test build 82 loaded (Limiter=%d TargetFPS=%d Spin=%dus)",
              g_limiterMode, g_targetFps, g_spinUs);

        QueryPerformanceFrequency(&g_freq);
        g_period = g_freq.QuadPart / g_targetFps;

        g_timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (g_timer) g_timerHiRes = 1;
        else g_timer = CreateWaitableTimerW(NULL, TRUE, NULL);
        logf_("timer: %s, QPC frequency %lld Hz",
              g_timerHiRes ? "high-resolution waitable timer" : "standard waitable timer",
              (long long)g_freq.QuadPart);

        g_prevFilter = SetUnhandledExceptionFilter(crash_filter);
        AddVectoredExceptionHandler(1, null_call_logger);
        apply_patches();
        g_realSHGetSpecialFolderPathA = (SHGetSpecialFolderPathA_t)iat_hook("shell32.dll", "SHGetSpecialFolderPathA",
                                                                             (void *)hook_SHGetSpecialFolderPathA);
        if (g_logEnabled) hitch_install();
        if (g_logEnabled && g_detailLog) stall_start();
        code_seal();
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_log) {
            if (g_patched) flush_stats(now_ticks());
            logf_("unloading");
            fclose(g_log);
            g_log = NULL;
        }
    }
    return TRUE;
}
