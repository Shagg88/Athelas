/* Valar Boons - Core Module & Lifecycle Management
 *
 * Implements:
 *  - Global state storage & logging helpers
 *  - ROTKFix API resolution and hook registration
 *  - DLL entry points (ModInit and DllMain)
 */

#include "valar_boons.h"

/* ROTKFix Public Modding API Function Pointers */
RF_RegisterHitFilter_t   g_RegisterHitFilter   = NULL;
RF_RegisterFrameHook_t   g_RegisterFrameHook   = NULL;
RF_RegisterRenderHook_t  g_RegisterRenderHook  = NULL;
RF_RegisterResetHook_t   g_RegisterResetHook   = NULL;
RF_IsPlayer_t            g_IsPlayer            = NULL;
RF_DealHit_t             g_DealHit             = NULL;
RF_ShowNotification_t    g_ShowNotification    = NULL;
RF_ShowNotificationEx_t  g_ShowNotificationEx  = NULL;
RF_Log_t                 g_Log                 = NULL;
RF_TakeKeyPress_t        g_TakeKeyPress        = NULL;
RF_IsKeyDown_t           g_IsKeyDown           = NULL;
RF_RegisterHotkey_t      g_RegisterHotkey      = NULL;
RF_IsLogEnabled_t        g_IsLogEnabled        = NULL;

/* Boon Configurations & Runtime State */
BoonConfig g_boons[MAX_BOONS];
int g_boonCount = 0;

int g_enableAllDiks[MAX_KEYS_PER_BOON];
int g_enableAllDikCount = 0;
int g_resetDiks[MAX_KEYS_PER_BOON];
int g_resetDikCount = 0;

int g_showHud = 1;
int g_showAura = 1;

/* Group Shared Cooldown Tracking */
volatile DWORD g_groupCooldownUntil[MAX_GROUPS] = {0};
int g_groupUseCount[MAX_GROUPS] = {0};

/* Visual Hit & Attack Flash Timers */
volatile DWORD g_attackFlashUntil = 0;
volatile DWORD g_defendFlashUntil = 0;

char g_modLogPath[MAX_PATH] = "";
char g_iniPath[MAX_PATH] = "";

static volatile LONG g_stopThread = 0;
static HANDLE g_hThread = NULL;

static int is_log_enabled(void)
{
    if (g_IsLogEnabled) return g_IsLogEnabled();
    return GetPrivateProfileIntA("Log", "Enabled", 1, "ROTKFix.ini");
}

void valar_log(const char *fmt, ...)
{
    if (!is_log_enabled()) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (g_Log) {
        g_Log("%s", buf);
    }

    if (g_modLogPath[0]) {
        FILE *f = fopen(g_modLogPath, "a");
        if (f) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
            fclose(f);
        }
    }
}

void notify_ex(const char *category, const char *title, const char *desc)
{
    if (g_ShowNotificationEx) {
        g_ShowNotificationEx(category, title, desc);
    } else if (g_ShowNotification) {
        g_ShowNotification(title, desc);
    }
}

int scale_val(int amount, int pct)
{
    long long v = ((long long)amount * pct + 50) / 100;
    return v < 1 ? 1 : (int)v;
}

/* Secondary polling thread (fallback if frame hook registration is unavailable) */
static DWORD WINAPI KeyListenerThread(LPVOID param)
{
    (void)param;
    while (!g_stopThread) {
        Sleep(25);
        if (!g_RegisterFrameHook) {
            valar_frame_hook();
        }
    }
    return 0;
}

static void InitMod(void)
{
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    /* Determine path for ROTKFix\mods\ValarBoons.ini & log */
    char modPath[MAX_PATH];
    HMODULE hThis = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)InitMod, &hThis) && hThis) {
        GetModuleFileNameA(hThis, modPath, MAX_PATH);
        char *slash = strrchr(modPath, '\\');
        if (slash) {
            *slash = 0;
            snprintf(g_iniPath, sizeof g_iniPath, "%s\\ValarBoons.ini", modPath);
            snprintf(g_modLogPath, sizeof g_modLogPath, "%s\\ValarBoons.log", modPath);
        }
    }

    /* Search for ROTKFix module (or d3d8.dll / dinput8.dll proxy) */
    HMODULE hRotkFix = GetModuleHandleA("ROTKFix.dll");
    if (!hRotkFix) {
        hRotkFix = GetModuleHandleA("d3d8.dll");
        if (!hRotkFix || !GetProcAddress(hRotkFix, "RF_RegisterHitFilter")) {
            hRotkFix = GetModuleHandleA("dinput8.dll");
        }
    }

    if (hRotkFix) {
        g_RegisterHitFilter   = (RF_RegisterHitFilter_t)(void *)GetProcAddress(hRotkFix, "RF_RegisterHitFilter");
        g_RegisterFrameHook   = (RF_RegisterFrameHook_t)(void *)GetProcAddress(hRotkFix, "RF_RegisterFrameHook");
        g_RegisterRenderHook  = (RF_RegisterRenderHook_t)(void *)GetProcAddress(hRotkFix, "RF_RegisterRenderHook");
        g_RegisterResetHook   = (RF_RegisterResetHook_t)(void *)GetProcAddress(hRotkFix, "RF_RegisterResetHook");
        g_IsPlayer            = (RF_IsPlayer_t)(void *)GetProcAddress(hRotkFix, "RF_IsPlayer");
        g_DealHit             = (RF_DealHit_t)(void *)GetProcAddress(hRotkFix, "RF_DealHit");
        g_ShowNotification    = (RF_ShowNotification_t)(void *)GetProcAddress(hRotkFix, "RF_ShowNotification");
        g_ShowNotificationEx  = (RF_ShowNotificationEx_t)(void *)GetProcAddress(hRotkFix, "RF_ShowNotificationEx");
        g_Log                 = (RF_Log_t)(void *)GetProcAddress(hRotkFix, "RF_Log");
        g_IsLogEnabled        = (RF_IsLogEnabled_t)(void *)GetProcAddress(hRotkFix, "RF_IsLogEnabled");
        g_TakeKeyPress        = (RF_TakeKeyPress_t)(void *)GetProcAddress(hRotkFix, "RF_TakeKeyPress");
        g_IsKeyDown           = (RF_IsKeyDown_t)(void *)GetProcAddress(hRotkFix, "RF_IsKeyDown");
        g_RegisterHotkey      = (RF_RegisterHotkey_t)(void *)GetProcAddress(hRotkFix, "RF_RegisterHotkey");
    }

    valar_log("mods [ValarBoons]: InitMod() started. Core API connected: HitFilter=%p, FrameHook=%p, RenderHook=%p, Log=%p",
              g_RegisterHitFilter, g_RegisterFrameHook, g_RegisterRenderHook, g_Log);

    /* Load dynamic INI configurations */
    if (g_iniPath[0] && GetFileAttributesA(g_iniPath) != INVALID_FILE_ATTRIBUTES) {
        load_boon_configs(g_iniPath);
    } else {
        load_boon_configs("ROTKFix\\mods\\ValarBoons.ini");
    }

    if (g_RegisterHitFilter) {
        g_RegisterHitFilter(valar_hit_filter);
    }

    if (g_RegisterFrameHook) {
        g_RegisterFrameHook(valar_frame_hook);
    } else {
        g_hThread = CreateThread(NULL, 0, KeyListenerThread, NULL, 0, NULL);
    }

    if (g_RegisterRenderHook) {
        g_RegisterRenderHook(valar_render_hook);
    }
    if (g_RegisterResetHook) {
        g_RegisterResetHook(valar_reset_hook);
    }
}

__declspec(dllexport) void ModInit(void)
{
    InitMod();
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    (void)hinstDLL;
    (void)lpReserved;

    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        InitMod();
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        g_stopThread = 1;
        if (g_hThread) {
            WaitForSingleObject(g_hThread, 200);
            CloseHandle(g_hThread);
            g_hThread = NULL;
        }
        valar_reset_hook(NULL);
    }

    return TRUE;
}
