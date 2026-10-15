/*
 * ROTK Fix - graphics layer
 *
 * The game delay-loads d3d8.dll by name, so ROTK Fix's d3d8.dll is loaded
 * first. It loads a real Direct3D 8 implementation (DXVK, rendering through
 * Vulkan, or Windows' own d3d8.dll) and wraps its IDirect3D8 so that:
 *
 *  - The game sees exactly one display resolution: the one chosen in
 *    ROTKFix.ini / the launcher. The game only stores a width and takes the
 *    first listed mode with that width, so offering one mode makes the choice
 *    exact (no wrong height, no 4:3 fallback).
 *  - The chosen resolution must be a real mode of the monitor. Nothing is
 *    rendered above the monitor's resolution and scaled down.
 *  - Display mode is fullscreen (exclusive, real mode switch), borderless
 *    (fills the monitor; a lower resolution is scaled up by the GPU) or
 *    windowed.
 *  - Optional MSAA, and anisotropic filtering through DXVK's config.
 *
 * Device hooks (vtable patches) add: widescreen and field-of-view correction
 * of the 3D camera, menus/HUD kept in proportion (centred or pinned to the
 * screen edges), cutscenes fitted to wide screens and sharpened, and side
 * bars painted black. Reset() is hooked so the same rules apply after alt-tab.
 */
#define CINTERFACE
#include "rotkfix.h"
#include <d3d8.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Settings                                                             */

enum { R_DXVK, R_NATIVE };
enum { DM_BORDERLESS, DM_FULLSCREEN, DM_WINDOWED };

static int  g_renderer   = R_DXVK;
static int  g_width      = 0;      /* 0 = desktop resolution */
static int  g_height     = 0;
static int  g_dispMode   = DM_BORDERLESS;
static char g_screenName[32] = "main"; /* [Graphics] Screen: main, or DISPLAY2 etc. */
static UINT g_screenAdapter;           /* the game's adapter 0 is drawn on this one */
static int  g_refresh    = 0;      /* 0 = desktop refresh (fullscreen only) */
static int  g_vsync      = 1;
static int  g_msaa       = 0;      /* 0, 2, 4, 8 */
static int  g_aniso      = 16;     /* 0 = game default; DXVK only */
static int  g_fovPct     = 100;    /* field of view, percent of the game's own */
static float g_lodBias   = 0.0f;   /* texture LOD bias, negative = sharper (DXVK) */
static int  g_gfxEnabled = 1;
static int  g_aspectFix;
static int  g_movieSharpPct = 70;   /* [Video] SharperMovies: cutscene sharpening at movie-pixel size */
static int  g_hudSharpPct = 0;      /* HUD and menu pictures sharpened at their own pixel size; set from the resolution */
static int  g_sharpenOn = 1;        /* [Graphics] Sharpening > 0 */
static int  g_projPersp;      /* current projection is perspective (3D) */
static int  g_drawPct = 100;    /* [Graphics] DrawDistance: far clip plane and fog, percent of the game's */
static int  g_fadeReport;     /* [Log] FadeReport = 1: log how blended 3D draws are set up (for the Xbox port) */
static int  g_renderPct = 100;  /* [Graphics] RenderScale: the game draws at this share of the chosen size; DXVK fits it to the screen */

static int parse_choice(const char *v, const char *const *names, int n, int def)
{
    for (int i = 0; i < n; i++) if (!lstrcmpiA(v, names[i])) return i;
    return def;
}

int graphics_enabled(void) { return g_gfxEnabled; }
int graphics_is_windowed(void) { return g_gfxEnabled && g_dispMode == DM_WINDOWED; }

static char    g_dxvkFolder[64] = "dxvk";   /* [Graphics] DxvkFolder, inside ROTKFix\\ */

void graphics_load_settings(const char *ini)
{
    g_fadeReport = GetPrivateProfileIntA("Log", "FadeReport", 0, ini);
    char buf[64];
    static const char *const rn[] = { "dxvk", "native" };
    static const char *const dn[] = { "borderless", "fullscreen", "windowed" };
    g_gfxEnabled = GetPrivateProfileIntA("Graphics", "Enabled", 1, ini);
    GetPrivateProfileStringA("Graphics", "Renderer", "dxvk", buf, sizeof buf, ini);
    g_renderer = parse_choice(buf, rn, 2, R_DXVK);
    /* which DXVK to use: a folder inside ROTKFix\ (dxvk = the standard one,
     * e.g. dxvk3 to try a newer version). Plain folder names only. */
    GetPrivateProfileStringA("Graphics", "DxvkFolder", "dxvk", buf, sizeof buf, ini);
    int okName = buf[0] != 0;
    for (const char *c = buf; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.') || (c[0] == '.' && c[1] == '.'))
            okName = 0;
    if (okName) lstrcpynA(g_dxvkFolder, buf, sizeof g_dxvkFolder);
    GetPrivateProfileStringA("Graphics", "Screen", "main", g_screenName, sizeof g_screenName, ini);
    GetPrivateProfileStringA("Graphics", "DisplayMode", "borderless", buf, sizeof buf, ini);
    g_dispMode = parse_choice(buf, dn, 3, DM_BORDERLESS);
    g_width   = GetPrivateProfileIntA("Graphics", "Width", 0, ini);
    g_height  = GetPrivateProfileIntA("Graphics", "Height", 0, ini);
    g_refresh = GetPrivateProfileIntA("Graphics", "RefreshRate", 0, ini);
    g_vsync   = GetPrivateProfileIntA("Graphics", "VSync", 1, ini);
    g_msaa    = GetPrivateProfileIntA("Graphics", "MSAA", 0, ini);
    g_aniso   = GetPrivateProfileIntA("Graphics", "AnisotropicFiltering", 16, ini);
    g_aspectFix = GetPrivateProfileIntA("Graphics", "WidescreenFix", 1, ini);
    g_movieSharpPct = GetPrivateProfileIntA("Video", "SharperMovies", 70, ini);
    if (g_movieSharpPct < 0) g_movieSharpPct = 0;
    if (g_movieSharpPct > 100) g_movieSharpPct = 100;
    g_sharpenOn = GetPrivateProfileIntA("Graphics", "Sharpening", 60, ini) > 0;
    g_fovPct  = GetPrivateProfileIntA("Graphics", "FieldOfView", 100, ini);
    if (g_fovPct < 70) g_fovPct = 70;
    if (g_fovPct > 150) g_fovPct = 150;
    g_renderPct = GetPrivateProfileIntA("Graphics", "RenderScale", 100, ini);
    if (g_renderPct < 50) g_renderPct = 50;
    if (g_renderPct > 200) g_renderPct = 200;      /* a 32-bit game: larger pictures risk running out of memory */
    g_drawPct = GetPrivateProfileIntA("Graphics", "DrawDistance", 100, ini);
    if (g_drawPct < 50) g_drawPct = 50;
    if (g_drawPct > 800) g_drawPct = 800;
    GetPrivateProfileStringA("Graphics", "TextureLODBias", "0", buf, sizeof buf, ini);
    g_lodBias = (float)atof(buf);
    if (g_lodBias < -2.0f) g_lodBias = -2.0f;
    if (g_lodBias > 2.0f) g_lodBias = 2.0f;
    if (g_msaa != 2 && g_msaa != 4 && g_msaa != 8) g_msaa = 0;
    if (g_aniso < 0) g_aniso = 0;
    if (g_aniso > 16) g_aniso = 16;
}

/* ------------------------------------------------------------------ */
/* Backend loading                                                       */

typedef IDirect3D8 *(WINAPI *Create8_t)(UINT);
static HMODULE g_backend;
static int     g_backendIsDxvk;

static void write_dxvk_conf(const char *dir)
{
    char conf[MAX_PATH];
    snprintf(conf, sizeof conf, "%s\\dxvk.conf", dir);
    FILE *f = fopen(conf, "w");
    if (!f) return;
    fputs("# Written by ROTK Fix at every launch from ROTKFix.ini - edit ROTKFix.ini instead.\n", f);
    if (g_aniso > 0) fprintf(f, "d3d9.samplerAnisotropy = %d\n", g_aniso);
    if (g_lodBias != 0.0f) fprintf(f, "d3d9.samplerLodBias = %.2f\n", g_lodBias);
    /* V-Sync in every display mode (borderless/windowed included) */
    fprintf(f, "d3d9.presentInterval = %d\n", g_vsync ? 1 : 0);
    /* frame pacing is done by ROTK Fix; let DXVK present as soon as allowed */
    fputs("d3d9.maxFrameLatency = 1\n", f);
    fclose(f);
    SetEnvironmentVariableA("DXVK_CONFIG_FILE", conf);
    char logdir[MAX_PATH];
    snprintf(logdir, sizeof logdir, "%s\\logs", dir);
    CreateDirectoryA(logdir, NULL);
    SetEnvironmentVariableA("DXVK_LOG_PATH", logdir);
    SetEnvironmentVariableA("DXVK_LOG_LEVEL", "warn");
}

static HMODULE load_backend(void)
{
    if (g_backend) return g_backend;
    if (g_renderer == R_DXVK) {
        char dir[MAX_PATH], dll[MAX_PATH];
        snprintf(dir, sizeof dir, "%s\\ROTKFix\\%s", g_dir, g_dxvkFolder);
        snprintf(dll, sizeof dll, "%s\\d3d8.dll", dir);
        write_dxvk_conf(dir);
        /* altered search path: DXVK's d3d8.dll imports d3d9.dll from the same folder */
        g_backend = LoadLibraryExA(dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (g_backend) { g_backendIsDxvk = 1; logf_("graphics: renderer = Vulkan (DXVK) from %s", dll); post_attach_dxvk_d3d8(g_backend); return g_backend; }
        logf_("graphics: could not load DXVK from %s (error %lu); falling back to Windows Direct3D 8", dll, GetLastError());
    }
    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);
    strncat(sys, "\\d3d8.dll", MAX_PATH - strlen(sys) - 1);
    g_backend = LoadLibraryA(sys);
    if (g_backend) logf_("graphics: renderer = Windows Direct3D 8 (%s)", sys);
    else logf_("graphics: ERROR could not load %s (error %lu)", sys, GetLastError());
    return g_backend;
}

/* ------------------------------------------------------------------ */
/* Chosen mode                                                          */

static UINT g_modeW, g_modeH, g_modeRefresh;   /* the size the game draws at (its only display mode) */
static UINT g_outW, g_outH;                    /* the size shown: a window's size, or the screen's in borderless */
static UINT g_deskW, g_deskH, g_deskHz;        /* the desktop's own mode */
static D3DFORMAT g_deskFormat = D3DFMT_X8R8G8B8;
static int  g_modeReady;
static int  g_modeIsReal;   /* the monitor supports it as a real mode */

static void choose_mode(IDirect3D8 *real, UINT adapter)
{
    if (g_modeReady) return;
    g_modeReady = 1;
    D3DDISPLAYMODE desk;
    if (FAILED(IDirect3D8_GetAdapterDisplayMode(real, adapter, &desk))) { desk.Width = 1920; desk.Height = 1080; desk.RefreshRate = 60; desk.Format = D3DFMT_X8R8G8B8; }
    g_deskFormat = desk.Format;
    g_deskW = desk.Width; g_deskH = desk.Height; g_deskHz = desk.RefreshRate;
    UINT w = g_width > 0 ? (UINT)g_width : desk.Width;
    UINT h = g_height > 0 ? (UINT)g_height : desk.Height;

    /* is it a real mode of this monitor? also log what the monitor offers */
    UINT n = IDirect3D8_GetAdapterModeCount(real, adapter), best = 0;
    char list[1024] = ""; UINT lastW = 0, lastH = 0;
    for (UINT i = 0; i < n; i++) {
        D3DDISPLAYMODE m;
        if (FAILED(IDirect3D8_EnumAdapterModes(real, adapter, i, &m))) continue;
        if (m.Format != D3DFMT_X8R8G8B8) continue;
        if (m.Width == w && m.Height == h) { g_modeIsReal = 1; if (m.RefreshRate > best) best = m.RefreshRate; }
        if ((m.Width != lastW || m.Height != lastH) && m.Width >= 1024 && strlen(list) < sizeof list - 16) {
            char t[24]; snprintf(t, sizeof t, " %ux%u", m.Width, m.Height); strcat(list, t);
            lastW = m.Width; lastH = m.Height;
        }
    }
    logf_("graphics: desktop %ux%u @ %uHz; monitor modes:%s", desk.Width, desk.Height, desk.RefreshRate, list);
    if (!g_modeIsReal && g_dispMode != DM_FULLSCREEN && w <= desk.Width && h <= desk.Height && w >= 640 && h >= 480) {
        /* a window (or borderless picture) of any size that fits the screen
         * is fine; only fullscreen needs a mode the monitor really has */
        logf_("graphics: %ux%u is not a monitor mode; fine for %s", w, h, g_dispMode == DM_WINDOWED ? "a window" : "borderless");
        best = desk.RefreshRate;
    } else if (!g_modeIsReal) {
        logf_("graphics: %ux%u is not a mode this monitor supports; using the desktop resolution %ux%u", w, h, desk.Width, desk.Height);
        w = desk.Width; h = desk.Height; g_modeIsReal = 1;
        best = desk.RefreshRate;
    }
    g_modeW = w; g_modeH = h;
    g_outW = w; g_outH = h;
    /* the saved refresh rate belongs to the saved resolution: use it only
     * if this monitor really offers it at the size the game will use */
    int rateOk = 0;
    if (g_refresh > 0)
        for (UINT i = 0; i < n && !rateOk; i++) {
            D3DDISPLAYMODE m;
            if (SUCCEEDED(IDirect3D8_EnumAdapterModes(real, adapter, i, &m)) && m.Format == D3DFMT_X8R8G8B8 &&
                m.Width == w && m.Height == h && m.RefreshRate == (UINT)g_refresh) rateOk = 1;
        }
    if (g_refresh > 0 && !rateOk && g_dispMode == DM_FULLSCREEN)
        logf_("graphics: %d Hz is not offered at %ux%u; using %u Hz", g_refresh, w, h,
              w == desk.Width && h == desk.Height ? desk.RefreshRate : best);
    g_modeRefresh = rateOk ? (UINT)g_refresh : (w == desk.Width && h == desk.Height ? desk.RefreshRate : best);
    if (g_modeRefresh == 0) g_modeRefresh = desk.RefreshRate;
    /* Render resolution: the game is offered (and draws at) a larger or smaller
     * size; the picture is presented into the window or screen and scaled to
     * fit. Fullscreen can't scale, so it is shown borderless instead - but only
     * when fullscreen was at the desktop's own size and refresh rate, so nothing
     * is stretched or slowed; otherwise fullscreen stays as chosen, at 100%. */
    if (g_renderPct != 100 && g_gfxEnabled) {
        int pct = g_renderPct;
        if (g_dispMode == DM_FULLSCREEN) {
            if (w == desk.Width && h == desk.Height && g_modeRefresh == desk.RefreshRate) {
                logf_("graphics: render resolution %d%%: shown borderless instead of fullscreen (fullscreen can't scale the picture)", pct);
                g_dispMode = DM_BORDERLESS;
            } else {
                logf_("graphics: render resolution %d%% needs fullscreen at the desktop's own %ux%u @ %uHz; staying fullscreen at 100%%",
                      pct, desk.Width, desk.Height, desk.RefreshRate);
                pct = 100;
            }
        }
        if (pct != 100) {
            if (g_dispMode == DM_BORDERLESS) { g_outW = desk.Width; g_outH = desk.Height; }
            /* a 32-bit game: keep the picture within twice 4K's pixels (with MSAA, the
             * post-processing copy and screenshots it all has to fit in 2 GB) */
            const double maxPx = 3840.0 * 2160.0 * 2.0;
            double px = (double)w * h * pct * pct / 10000.0;
            if (px > maxPx) {
                int fit = (int)(100.0 * sqrt(maxPx / ((double)w * h)));
                logf_("graphics: render resolution %d%% of %ux%u is too large for this 32-bit game; using %d%%", pct, w, h, fit);
                pct = fit;
            }
            g_modeW = ((UINT)((double)w * pct / 100.0 + 0.5)) & ~1u;
            g_modeH = ((UINT)((double)h * pct / 100.0 + 0.5)) & ~1u;
            if (g_modeW < 320) g_modeW = 320;
            if (g_modeH < 240) g_modeH = 240;
            logf_("graphics: render resolution %d%%: drawn at %ux%u, shown at %ux%u", pct, g_modeW, g_modeH, g_outW, g_outH);
        }
    }
    static const char *const dn[] = { "borderless", "fullscreen", "windowed" };
    logf_("graphics: game will render at %ux%u, %s", g_modeW, g_modeH, dn[g_dispMode]);
    if (g_dispMode == DM_FULLSCREEN) logf_("graphics: fullscreen refresh rate %uHz", g_modeRefresh);
    if (g_vsync) limiter_cap_to_refresh(g_dispMode == DM_FULLSCREEN ? g_modeRefresh : desk.RefreshRate);
}

/* ------------------------------------------------------------------ */
/* Window + present parameters                                          */

static HWND g_window;

static void get_monitor_rect(IDirect3D8 *real, UINT adapter, RECT *r)
{
    MONITORINFO mi;
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    HMONITOR mon = real ? IDirect3D8_GetAdapterMonitor(real, adapter) : NULL;
    if (mon && GetMonitorInfoA(mon, &mi)) *r = mi.rcMonitor;
    else SetRect(r, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}

static void setup_window(HWND w, IDirect3D8 *real, UINT adapter)
{
    if (!w) return;
    RECT mr;
    get_monitor_rect(real, adapter, &mr);
    if (g_dispMode == DM_BORDERLESS) {
        SetWindowLongA(w, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongA(w, GWL_EXSTYLE, 0);
        SetWindowPos(w, HWND_TOP, mr.left, mr.top, mr.right - mr.left, mr.bottom - mr.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else if (g_dispMode == DM_WINDOWED) {
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
        RECT r = { 0, 0, (LONG)g_outW, (LONG)g_outH };
        AdjustWindowRect(&r, style, FALSE);
        int ww = r.right - r.left, wh = r.bottom - r.top;
        int x = mr.left + ((mr.right - mr.left) - ww) / 2, y = mr.top + ((mr.bottom - mr.top) - wh) / 2;
        SetWindowLongA(w, GWL_STYLE, style);
        SetWindowLongA(w, GWL_EXSTYLE, 0);
        SetWindowPos(w, HWND_TOP, x, y, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

static void fix_present_params(D3DPRESENT_PARAMETERS *pp, IDirect3D8 *real, UINT adapter, HWND focus)
{
    char before[160];
    snprintf(before, sizeof before, "%ux%u fmt=%d windowed=%d swap=%d ms=%d refresh=%u interval=%#x",
             pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat, pp->Windowed, pp->SwapEffect,
             pp->MultiSampleType, pp->FullScreen_RefreshRateInHz, pp->FullScreen_PresentationInterval);

    pp->BackBufferWidth = g_modeW;
    pp->BackBufferHeight = g_modeH;
    if (!pp->hDeviceWindow) pp->hDeviceWindow = focus;
    g_window = pp->hDeviceWindow;

    if (g_dispMode == DM_FULLSCREEN) {
        pp->Windowed = FALSE;
        pp->FullScreen_RefreshRateInHz = g_modeRefresh;
        pp->FullScreen_PresentationInterval = g_vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
        if (pp->BackBufferFormat == D3DFMT_UNKNOWN) pp->BackBufferFormat = D3DFMT_X8R8G8B8;
    } else {
        pp->Windowed = TRUE;
        pp->BackBufferFormat = g_deskFormat;
        pp->FullScreen_RefreshRateInHz = 0;
        pp->FullScreen_PresentationInterval = 0;   /* must be 0 when windowed */
        if (pp->SwapEffect == D3DSWAPEFFECT_FLIP) pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
    }
    if (g_msaa) {
        D3DMULTISAMPLE_TYPE t = (D3DMULTISAMPLE_TYPE)g_msaa;
        if (SUCCEEDED(IDirect3D8_CheckDeviceMultiSampleType(real, adapter, D3DDEVTYPE_HAL, pp->BackBufferFormat, pp->Windowed, t))) {
            pp->MultiSampleType = t;
            pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
            pp->Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
        } else {
            logf_("graphics: %dx MSAA not supported here, leaving it off", g_msaa);
        }
    }
    logf_("graphics: present params game=[%s] -> %ux%u windowed=%d swap=%d ms=%d refresh=%u",
          before, pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed, pp->SwapEffect, pp->MultiSampleType,
          pp->FullScreen_RefreshRateInHz);
}

/* ------------------------------------------------------------------ */
/* IDirect3DDevice8::Reset hook (vtable patch)                           */

typedef HRESULT (WINAPI *Reset_t)(IDirect3DDevice8 *, D3DPRESENT_PARAMETERS *);
static Reset_t     g_realReset;
static IDirect3D8 *g_realD3D;
static UINT        g_adapter;

static void forget_device_state(void);
static HRESULT WINAPI hook_Reset(IDirect3DDevice8 *dev, D3DPRESENT_PARAMETERS *pp)
{
    if (pp) {
        setup_window(pp->hDeviceWindow ? pp->hDeviceWindow : g_window, g_realD3D, g_adapter);
        fix_present_params(pp, g_realD3D, g_adapter, g_window);
    }
    forget_device_state();
    HRESULT hr = g_realReset(dev, pp);
    if (FAILED(hr)) logf_("graphics: Reset failed (%#lx)", (unsigned long)hr);
    return hr;
}

/* ---- SetTransform (vtable 37): widescreen camera correction ----
 * The game builds its perspective matrices for a 4:3 screen. For a wider
 * screen we scale the horizontal term (_11) so the image is not stretched:
 * same vertical view, more visible on the sides ("Hor+"). Only perspective
 * matrices (_34 = 1, _44 = 0) are touched; 2D/orthographic ones are left alone.
 * The game's assumed aspect is measured from the matrix itself (_22/_11). */
typedef HRESULT (WINAPI *SetTransform_t)(IDirect3DDevice8 *, D3DTRANSFORMSTATETYPE, const D3DMATRIX *);
static SetTransform_t g_realSetTransform;


static D3DMATRIX g_ortho2D;        /* game's own 2D matrix, unmodified */
static int g_projOrtho;           /* the current projection is the game's 2D one */
static int g_orthoFixed;           /* the current projection is a narrowed 2D one */
static void *g_tex0;               /* texture the game bound to stage 0 */
static void *g_tex0Bound;          /* what is really bound there (a replacement picture, if any) */

static float g_camVfov, g_camAspectIn, g_camAspectOut;   /* last 3D camera, for the log */
static float g_camVfovOut;
static float g_camNear, g_camFar, g_camFarOut;
static float g_camRaw33, g_camRaw43;
static float g_camSeen[16][2];   /* cameras logged in this level: vertical FOV, aspect */
static int   g_nCamSeen;
static int   g_depthUnknown;     /* a 3D camera whose depth terms the draw distance could not read */

/* Fog, as the game sets it (render states) */
typedef HRESULT (WINAPI *SetRS_t)(IDirect3DDevice8 *, D3DRENDERSTATETYPE, DWORD);
static SetRS_t g_realSetRS;
static DWORD g_fogEnable, g_fogTable, g_fogVertex, g_fogRange, g_fogColor;
static float g_fogStart, g_fogEnd, g_fogDensity;
static int   g_fogSeen;

static float dw2f(DWORD d) { float f; memcpy(&f, &d, 4); return f; }
static DWORD f2dw(float f) { DWORD d; memcpy(&d, &f, 4); return d; }

static DWORD fog_scaled(D3DRENDERSTATETYPE s, DWORD v)
{
    float k = (float)g_drawPct / 100.0f;
    if (k == 1.0f) return v;
    if (s == D3DRS_FOGSTART || s == D3DRS_FOGEND) return f2dw(dw2f(v) * k);
    if (s == D3DRS_FOGDENSITY) return f2dw(dw2f(v) / k);
    return v;
}

static HRESULT WINAPI hook_SetRS(IDirect3DDevice8 *dev, D3DRENDERSTATETYPE s, DWORD v)
{
    switch (s) {
    case D3DRS_FOGENABLE:       g_fogEnable = v; break;
    case D3DRS_FOGTABLEMODE:    g_fogTable = v; break;
    case D3DRS_FOGVERTEXMODE:   g_fogVertex = v; break;
    case D3DRS_RANGEFOGENABLE:  g_fogRange = v; break;
    case D3DRS_FOGCOLOR:        g_fogColor = v; break;
    case D3DRS_FOGSTART:        g_fogStart = dw2f(v); g_fogSeen |= 1; return g_realSetRS(dev, s, fog_scaled(s, v));
    case D3DRS_FOGEND:          g_fogEnd = dw2f(v); g_fogSeen |= 2; return g_realSetRS(dev, s, fog_scaled(s, v));
    case D3DRS_FOGDENSITY:      g_fogDensity = dw2f(v); g_fogSeen |= 4; return g_realSetRS(dev, s, fog_scaled(s, v));
    default: break;
    }
    return g_realSetRS(dev, s, v);
}

static float screen_aspect(void) { return g_modeH ? (float)g_modeW / (float)g_modeH : 4.0f / 3.0f; }

/* Widest shape menus, text and the HUD are drawn at: stretched to fill the
 * screen, but never wider than 16:9 (on ultrawide screens a full stretch
 * would make text more than twice as wide as tall; the rest is filled with
 * black). 0 = no limit. */
#define HUD_MAX_STRETCH (16.0f / 9.0f)
static float hud_aspect(void)
{
    float sa = screen_aspect();
    float t = HUD_MAX_STRETCH;
    return sa > t + 0.01f ? t : 0.0f;
}

static D3DMATRIX g_viewM, g_worldM;     /* for FadeReport: where the camera and the object being drawn are */
static unsigned  g_presentNo;           /* frames shown so far */
static HRESULT WINAPI hook_SetTransform(IDirect3DDevice8 *dev, D3DTRANSFORMSTATETYPE st, const D3DMATRIX *m)
{
    if (m && st == D3DTS_VIEW) g_viewM = *m;
    if (m && st == D3DTS_WORLD) g_worldM = *m;
    if (st != D3DTS_PROJECTION || !m) return g_realSetTransform(dev, st, m);
    g_orthoFixed = 0;
    g_projPersp = 0;
    int persp = m->_34 > 0.99f && m->_34 < 1.01f && m->_44 > -0.001f && m->_44 < 0.001f;
    int ortho = m->_34 > -0.001f && m->_34 < 0.001f && m->_44 > 0.99f && m->_44 < 1.01f;
    static int nOrtho;
    if (persp) {
        /* log each distinct camera (angle and shape) once per level, with what ROTK Fix does to it */
        float vfov = m->_22 > 0 ? 2.0f * atanf(1.0f / m->_22) * 57.29578f : 0.0f;
        float asp = m->_11 != 0 ? fabsf(m->_22 / m->_11) : 0.0f;
        int known = 0;
        /* within 5 degrees of one already logged: an animated zoom (the level-select ring), not a new camera */
        for (int i = 0; i < g_nCamSeen; i++) if (fabsf(g_camSeen[i][0] - vfov) < 5.0f && fabsf(g_camSeen[i][1] - asp) < 0.01f) known = 1;
        if (!known && g_nCamSeen < 16) {
            g_camSeen[g_nCamSeen][0] = vfov; g_camSeen[g_nCamSeen][1] = asp; g_nCamSeen++;
            float sa = screen_aspect();
            int is43 = asp > 1.30f && asp < 1.36f;
            const char *what = !g_aspectFix ? "left as is (WidescreenFix off)"
                             : is43 && fabsf(sa - asp) > 0.01f ? "widened to the screen"
                             : is43 || fabsf(sa - asp) < 0.02f ? "already the screen's shape"
                             : "NOT widened";
            logf_("graphics: camera: vertical FOV %.1f deg, aspect %.3f -> %s%s", vfov, asp, what,
                  g_fovPct != 100 && vfov <= 25.0f ? "; zoom camera, FieldOfView not applied" : "");
            if (g_aspectFix && !is43 && fabsf(sa - asp) >= 0.02f && vfov > 1.0f)
                logf_("graphics: WARNING camera at aspect %.3f is neither 4:3 nor the screen's %.3f: this view may look stretched or squeezed", asp, sa);
        }
    }
    if (ortho && nOrtho < 3) { nOrtho++; dlogf_("graphics: 2D matrix _11=%.6f _22=%.6f _41=%.4f _42=%.4f", m->_11, m->_22, m->_41, m->_42); }
    g_projPersp = persp;
    g_projOrtho = ortho;
    if (ortho) g_ortho2D = *m;   /* the game's own 2D matrix (screen positions of 2D draws) */
    float screen = screen_aspect();
    /* The gameplay camera is mirrored (_11 negative), so work with magnitudes
     * and keep the sign. */
    if (persp && m->_11 != 0 && m->_22 > 0) {
        D3DMATRIX fixed = *m;
        int changed = 0;
        float gameAspect = fabsf(m->_22 / m->_11);
        g_camVfov = 2.0f * atanf(1.0f / m->_22) * 57.29578f;
        g_camAspectIn = gameAspect;
        g_camAspectOut = gameAspect;
        if (g_aspectFix && gameAspect > 1.30f && gameAspect < 1.36f && screen > gameAspect + 0.01f) {
            fixed._11 = m->_11 * gameAspect / screen;          /* wider: same height, more at the sides */
            g_camAspectOut = screen;
            changed = 1;
        } else if (g_aspectFix && gameAspect > 1.30f && gameAspect < 1.36f && screen < gameAspect - 0.01f) {
            fixed._22 = m->_22 * screen / gameAspect;          /* narrower (5:4): same width, more above and below */
            g_camAspectOut = screen;
            changed = 1;
        }
        /* Field of view: widen the view angle by a percentage of the game's own.
         * Applied to the tangent of the half-angle, so the shape stays correct.
         * Very narrow cameras (zoomed menu shots, < 25 deg) are left alone. */
        g_camVfovOut = g_camVfov;
        if (g_fovPct != 100) {
            float vfov = 2.0f * atanf(1.0f / m->_22);
            if (vfov > 25.0f / 57.29578f) {
                float nv = vfov * (float)g_fovPct / 100.0f;
                if (nv > 150.0f / 57.29578f) nv = 150.0f / 57.29578f;
                float k = tanf(vfov * 0.5f) / tanf(nv * 0.5f);
                fixed._11 *= k;
                fixed._22 *= k;
                g_camVfovOut = nv * 57.29578f;
                changed = 1;
            }
        }
        /* Draw distance: move the far clip plane out. Standard D3D projection:
         * _33 = f/(f-n), _43 = -n*f/(f-n). */
        g_camRaw33 = m->_33; g_camRaw43 = m->_43;
        /* The gameplay camera stores depth reversed: z/w = _43/z + _33, which
         * is 1 near the camera and 0 at the far plane f = -_43/_33.
         * Moving the far plane out by k = dividing _33 by k. */
        if (m->_33 < 0.0f && m->_33 > -0.01f && m->_43 > 0.0f) {
            float f = -m->_43 / m->_33;
            g_camNear = m->_43 / (1.0f - m->_33); g_camFar = f; g_camFarOut = f;
            if (g_drawPct != 100) {
                fixed._33 = m->_33 * 100.0f / (float)g_drawPct;
                g_camFarOut = f * (float)g_drawPct / 100.0f;
                changed = 1;
            }
        } else if (m->_33 > 1.0f && m->_43 < 0.0f) {
            float n = -m->_43 / m->_33;
            float f = m->_43 / (1.0f - m->_33);
            g_camNear = n; g_camFar = f; g_camFarOut = f;
            if (g_drawPct != 100 && f > n) {
                float f2 = f * (float)g_drawPct / 100.0f;
                fixed._33 = f2 / (f2 - n);
                fixed._43 = -n * f2 / (f2 - n);
                g_camFarOut = f2;
                changed = 1;
            }
        } else if (g_drawPct != 100 && !g_depthUnknown++) {
            logf_("graphics: WARNING draw distance: a camera's depth terms were not recognised (_33=%.6f _43=%.4f); its far plane is unchanged", m->_33, m->_43);
        }
        if (changed) return g_realSetTransform(dev, st, &fixed);
    }
    if (ortho && hud_aspect() > 0.0f) {
        /* scale the horizontal output around the screen centre */
        float sx = hud_aspect() / screen;
        D3DMATRIX fixed = *m;
        fixed._11 *= sx; fixed._21 *= sx; fixed._31 *= sx; fixed._41 *= sx;
        g_ortho2D = *m;
        g_orthoFixed = 1;
        return g_realSetTransform(dev, st, &fixed);
    }
    return g_realSetTransform(dev, st, m);
}

static void patch_vtbl(void **vt, int idx, void *hook, void **orig)
{
    if (vt[idx] == hook) return;
    /* one saved original per slot: never chain a second, different vtable to
     * the first one's function (its objects would call the wrong code) */
    if (*orig && *orig != vt[idx]) { dlogf_("graphics: vtable slot %d already hooked on another vtable, skipped", idx); return; }
    DWORD old;
    if (!VirtualProtect(&vt[idx], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) return;
    *orig = vt[idx];
    vt[idx] = hook;
    VirtualProtect(&vt[idx], sizeof(void *), old, &old);
}

/* ---- 2D layer: which draws stay full width ----
 * Text, HUD and menu art keep their shape (at most 16:9). Full-screen overlays (fades,
 * the pause-menu backdrop, shading gradients) must still cover the whole
 * screen. To tell them apart we keep a copy of the game's 2D vertex buffers
 * (FVF 0x142) as it writes them, and look at each draw's on-screen extent. */
typedef HRESULT (WINAPI *SetTexture_t)(IDirect3DDevice8 *, DWORD, IDirect3DBaseTexture8 *);
typedef HRESULT (WINAPI *DrawPrim_t)(IDirect3DDevice8 *, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (WINAPI *DrawIdx_t)(IDirect3DDevice8 *, D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT);
typedef HRESULT (WINAPI *CreateVB_t)(IDirect3DDevice8 *, UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer8 **);
typedef HRESULT (WINAPI *CreateTex_t)(IDirect3DDevice8 *, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture8 **);
typedef HRESULT (WINAPI *SetStream_t)(IDirect3DDevice8 *, UINT, IDirect3DVertexBuffer8 *, UINT);
typedef HRESULT (WINAPI *SetIndices_t)(IDirect3DDevice8 *, IDirect3DIndexBuffer8 *, UINT);
typedef HRESULT (WINAPI *Present_t)(IDirect3DDevice8 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT (WINAPI *Clear_t)(IDirect3DDevice8 *, DWORD, const D3DRECT *, DWORD, D3DCOLOR, float, DWORD);
typedef HRESULT (WINAPI *SetVS_t)(IDirect3DDevice8 *, DWORD);
typedef HRESULT (WINAPI *CopyRects_t)(IDirect3DDevice8 *, IDirect3DSurface8 *, const RECT *, UINT, IDirect3DSurface8 *, const POINT *);
typedef HRESULT (WINAPI *GetContainer_t)(IDirect3DSurface8 *, REFIID, void **);
typedef HRESULT (WINAPI *VBLock_t)(IDirect3DVertexBuffer8 *, UINT, UINT, BYTE **, DWORD);
typedef HRESULT (WINAPI *VBUnlock_t)(IDirect3DVertexBuffer8 *);
typedef ULONG   (WINAPI *Release_t)(IUnknown *);
typedef HRESULT (WINAPI *GetLevelDesc_t)(IDirect3DTexture8 *, UINT, D3DSURFACE_DESC *);
typedef D3DRESOURCETYPE (WINAPI *GetType_t)(IDirect3DBaseTexture8 *);

static SetTexture_t g_realSetTexture;
static DrawPrim_t   g_realDrawPrim;
static DrawIdx_t    g_realDrawIdx;
static CreateVB_t   g_realCreateVB;
static CreateTex_t  g_realCreateTex;
static SetStream_t  g_realSetStream;
static SetIndices_t g_realSetIndices;
static Present_t    g_realPresent;
static VBLock_t     g_realVBLock;
static VBUnlock_t   g_realVBUnlock;
static Release_t    g_realVBRelease;
static Release_t    g_realTexRelease;
static SetVS_t      g_realSetVS;
static CopyRects_t  g_realCopyRects;
static DWORD        g_curFVF;        /* vertex shader handle = FVF for fixed-function draws */

#define FVF_2D 0x142
#define MAX_VB 512
typedef struct { IDirect3DVertexBuffer8 *vb; UINT size; BYTE *shadow; BYTE *lockPtr; UINT lockOff, lockSize; } TrackedVB;
static TrackedVB g_vbs[MAX_VB];
static int g_nvbs;
#define MAX_RT 64
static void *g_rtTex[MAX_RT];      /* render-target textures (screen copies: pause backdrop, blur) */
static int g_nrt;
static IDirect3DVertexBuffer8 *g_stream0;
static UINT g_stream0Stride, g_baseVertex;
static int g_draws3D;          /* perspective draws this frame */
static int g_copyDrawn;        /* a screen copy (pause backdrop) was drawn this frame */
static int g_pictureDraw;
static DWORD g_2dSeen[24];   /* full-screen 2D kinds logged in this level */
static int g_n2dSeen;      /* this 2D draw is a full-screen picture kept at 16:9 */

static TrackedVB *find_vb(IDirect3DVertexBuffer8 *vb)
{
    for (int i = 0; i < g_nvbs; i++) if (g_vbs[i].vb == vb) return &g_vbs[i];
    return NULL;
}
static int is_rt(void *t) { for (int i = 0; i < g_nrt; i++) if (g_rtTex[i] == t) return 1; return 0; }

/* The game writes 2D vertices into our ordinary-memory copy, and on Unlock
 * the locked range is copied on to the real buffer. (Reading the real
 * buffer back instead is very slow: it lives in graphics memory.) */
static HRESULT WINAPI hook_VBLock(IDirect3DVertexBuffer8 *vb, UINT off, UINT size, BYTE **pp, DWORD flags)
{
    HRESULT hr = g_realVBLock(vb, off, size, pp, flags);
    TrackedVB *t = SUCCEEDED(hr) && pp && *pp ? find_vb(vb) : NULL;
    if (t && !t->lockPtr && !(flags & D3DLOCK_READONLY) && off < t->size) {
        UINT n = size ? size : t->size - off;
        if (off + n > t->size) n = t->size - off;
        t->lockPtr = *pp; t->lockOff = off; t->lockSize = n;
        *pp = t->shadow + off;
    }
    return hr;
}
static HRESULT WINAPI hook_VBUnlock(IDirect3DVertexBuffer8 *vb)
{
    TrackedVB *t = find_vb(vb);
    if (t && t->lockPtr) {
        memcpy(t->lockPtr, t->shadow + t->lockOff, t->lockSize);
        t->lockPtr = NULL;
        /* once a minute: how much 2D vertex data passed through */
        static DWORD t0, locks; static unsigned long long bytes;
        DWORD now = GetTickCount();
        locks++; bytes += t->lockSize;
        g_hitch[HITCH_VBKB] += (LONG)(t->lockSize / 1024);
        if (!t0) t0 = now;
        else if (now - t0 >= 60000) {
            logf_("graphics: 2D vertex copies: %lu locks, %llu KB in %lu s", (unsigned long)locks, bytes / 1024, (unsigned long)((now - t0) / 1000));
            t0 = now; locks = 0; bytes = 0;
        }
    }
    return g_realVBUnlock(vb);
}
static ULONG WINAPI hook_VBRelease(IUnknown *u)
{
    IDirect3DVertexBuffer8 *vb = (IDirect3DVertexBuffer8 *)u;
    ULONG r = g_realVBRelease(u);
    if (r == 0) {
        TrackedVB *t = find_vb(vb);
        if (t) { HeapFree(GetProcessHeap(), 0, t->shadow); *t = g_vbs[--g_nvbs]; }
        if (g_stream0 == vb) g_stream0 = NULL;
    }
    return r;
}

/* Start keeping a copy of a vertex buffer's contents. The game creates its
 * buffers without a vertex format (FVF 0) and only says at draw time that a
 * buffer holds 2D vertices, so buffers are taken on when first drawn as 2D;
 * their contents are known from the next time the game fills them. */
typedef HRESULT (WINAPI *VBGetDesc_t)(IDirect3DVertexBuffer8 *, D3DVERTEXBUFFER_DESC *);
static TrackedVB *track_vb(IDirect3DVertexBuffer8 *vb)
{
    if (!vb || g_nvbs >= MAX_VB) return NULL;
    void **vt = *(void ***)vb;
    D3DVERTEXBUFFER_DESC d;
    if (FAILED(((VBGetDesc_t)vt[13])(vb, &d)) || !d.Size || d.Size > (4u << 20)) return NULL;
    BYTE *sh = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, d.Size);
    if (!sh) return NULL;
    patch_vtbl(vt, 2, (void *)hook_VBRelease, (void **)&g_realVBRelease);
    patch_vtbl(vt, 11, (void *)hook_VBLock, (void **)&g_realVBLock);
    patch_vtbl(vt, 12, (void *)hook_VBUnlock, (void **)&g_realVBUnlock);
    if (vt[11] != (void *)hook_VBLock) { HeapFree(GetProcessHeap(), 0, sh); return NULL; }
    /* start from the buffer's current contents (one slow read per buffer) */
    BYTE *cur = NULL;
    if (SUCCEEDED(g_realVBLock(vb, 0, 0, &cur, D3DLOCK_READONLY)) && cur) {
        memcpy(sh, cur, d.Size);
        g_realVBUnlock(vb);
    }
    g_vbs[g_nvbs] = (TrackedVB){ vb, d.Size, sh, NULL, 0, 0 };
    static int logged;
    if (!logged++) logf_("graphics: watching the game's 2D vertex buffers (first %u bytes)", d.Size);
    return &g_vbs[g_nvbs++];
}

static HRESULT WINAPI hook_CreateVB(IDirect3DDevice8 *dev, UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8 **out)
{
    HRESULT hr = g_realCreateVB(dev, len, usage, fvf, pool, out);
    g_hitch[HITCH_VBNEW]++;
    if (SUCCEEDED(hr) && out && *out && fvf == FVF_2D) track_vb(*out);
    return hr;
}
/* Render-target textures are remembered (2D draws using one are screen
 * copies that must cover the full width). A texture is forgotten when the
 * game releases it, so a new texture at the same address is not mistaken
 * for one. Release is hooked on the shared texture vtable. */
static ULONG WINAPI hook_TexRelease(IUnknown *u)
{
    ULONG r = g_realTexRelease(u);
    if (r == 0) {
        if (g_tex0 == (void *)u) g_tex0 = NULL;
        if (g_tex0Bound == (void *)u) g_tex0Bound = NULL;
        textures_on_release(u);
        for (int i = 0; i < g_nrt; i++) if (g_rtTex[i] == (void *)u) { g_rtTex[i] = g_rtTex[--g_nrt]; break; }
    }
    return r;
}

static HRESULT WINAPI hook_CreateTex(IDirect3DDevice8 *dev, UINT w, UINT h, UINT lv, DWORD usage, D3DFORMAT f, D3DPOOL p, IDirect3DTexture8 **out)
{
    HRESULT hr = g_realCreateTex(dev, w, h, lv, usage, f, p, out);
    g_hitch[HITCH_TEXNEW]++;
    if (SUCCEEDED(hr) && out && *out) {
        patch_vtbl(*(void ***)*out, 2, (void *)hook_TexRelease, (void **)&g_realTexRelease);
        textures_on_create(dev, (void *)g_realCreateTex, *out);
        if (usage & D3DUSAGE_RENDERTARGET) {
            if (g_nrt < MAX_RT) g_rtTex[g_nrt++] = *out;
            dlogf_("graphics: game created a %ux%u render-target texture (format %d)", w, h, (int)f);
        }
    }
    return hr;
}

/* The pause-menu backdrop is a copy of the screen: the game copies the back
 * buffer into an ordinary texture with CopyRects (0x4a4d60), so it is not a
 * render-target texture. Remember every texture CopyRects writes into. */
static HRESULT WINAPI hook_CopyRects(IDirect3DDevice8 *dev, IDirect3DSurface8 *src, const RECT *r, UINT n, IDirect3DSurface8 *dst, const POINT *pt)
{
    HRESULT hr = g_realCopyRects(dev, src, r, n, dst, pt);
    if (SUCCEEDED(hr) && dst) {
        void *tex = NULL;
        if (SUCCEEDED(((GetContainer_t)(*(void ***)dst)[7])(dst, &IID_IDirect3DTexture8, &tex)) && tex) {
            if (!is_rt(tex) && g_nrt < MAX_RT) {
                g_rtTex[g_nrt++] = tex;
                dlogf_("graphics: screen copied into a texture (pause backdrop)");
            }
            ((Release_t)(*(void ***)tex)[2])((IUnknown *)tex);
        }
    }
    return hr;
}

static HRESULT WINAPI hook_SetVS(IDirect3DDevice8 *dev, DWORD h)
{
    g_curFVF = h;
    return g_realSetVS(dev, h);
}

/* Reset (alt-tab, display change) and a new device: nothing the game had
 * bound or created in video memory survives, so drop every pointer to it. */
static void forget_device_state(void)
{
    g_tex0 = NULL;
    g_tex0Bound = NULL;
    g_stream0 = NULL;
    g_stream0Stride = 0;
    g_nrt = 0;
}

static HRESULT WINAPI hook_SetStream(IDirect3DDevice8 *dev, UINT n, IDirect3DVertexBuffer8 *vb, UINT stride)
{
    if (n == 0) { g_stream0 = vb; g_stream0Stride = stride; }
    return g_realSetStream(dev, n, vb, stride);
}
static HRESULT WINAPI hook_SetIndices(IDirect3DDevice8 *dev, IDirect3DIndexBuffer8 *ib, UINT base)
{
    g_baseVertex = base;
    return g_realSetIndices(dev, ib, base);
}
static HRESULT WINAPI hook_SetTexture(IDirect3DDevice8 *dev, DWORD stage, IDirect3DBaseTexture8 *t)
{
    IDirect3DBaseTexture8 *b = t ? (IDirect3DBaseTexture8 *)textures_bind(dev, t, is_rt(t) || icons_is_font_texture(t)) : t;
    if (stage == 0) { g_tex0 = t; g_tex0Bound = b; }   /* g_tex0: the game's texture; g_tex0Bound: what is drawn (a replacement picture, if any) */
    return g_realSetTexture(dev, stage, b);
}

static void tex_size(void *t, UINT *w, UINT *h)
{
    *w = *h = 0;
    if (!t) return;
    void **vt = *(void ***)t;
    if (((GetType_t)vt[10])((IDirect3DBaseTexture8 *)t) != D3DRTYPE_TEXTURE) return;
    D3DSURFACE_DESC d;
    if (SUCCEEDED(((GetLevelDesc_t)vt[14])((IDirect3DTexture8 *)t, 0, &d))) { *w = d.Width; *h = d.Height; }
}

/* ------------------------------------------------------------------ */
/* Movies on a widescreen                                                */
/*
 * The game draws each movie frame as two full-screen rectangles of
 * pre-transformed vertices (FVF 0x1c4, 32 bytes each, from a dynamic vertex
 * buffer it fills just before drawing). The picture is a 4:3 frame:
 *  - story cutscenes are widescreen film with black bars baked in: scale the
 *    frame up evenly until it fills the width (bars become thin, nothing is
 *    stretched; only baked-in black is cut);
 *  - other videos are full-frame 4:3: shrink the width to 4:3 and show black
 *    bars at the sides.
 */
#define FVF_MOVIE 0x1c4
static volatile DWORD g_movieDrawTick;   /* last time a movie frame was drawn */
int graphics_movie_playing(void) { return g_movieDrawTick && GetTickCount() - g_movieDrawTick < 150; }
#define FILM_ROWS_SCALE (480.0f / 352.0f)   /* frame height / film height */
static void movie_fit_quad(IDirect3DDevice8 *dev, UINT start, UINT count)
{
    int fit = movie_fit();
    if (fit == MOVIE_FIT_STRETCH || !g_stream0 || g_stream0Stride < 8 || !g_modeW || !g_modeH) return;
    float W = (float)g_modeW, H = (float)g_modeH;
    if (W / H < 4.0f / 3.0f + 0.01f) return;
    float sx = 1.0f, sy = 1.0f;
    if (fit == MOVIE_FIT_FILM) {
        sy = (W * 3.0f / 4.0f) / H;                           /* frame height grows to match the width */
        /* the film itself is rows 66..416 of the 480-row frame: on screens
         * wider than the film (ultrawide), stop when its height is filled and
         * show bars at the sides instead of cutting off the top and bottom */
        if (sy > FILM_ROWS_SCALE) { sy = FILM_ROWS_SCALE; sx = (H * sy * 4.0f / 3.0f) / W; }
    }
    else sx = (H * 4.0f / 3.0f) / W;                          /* frame width shrinks to 4:3 */
    UINT n = count + 2;                                       /* triangle strip */
    BYTE *v = NULL;
    void **vt = *(void ***)g_stream0;
    if (FAILED(((VBLock_t)vt[11])(g_stream0, start * g_stream0Stride, n * g_stream0Stride, &v, D3DLOCK_NOOVERWRITE)) || !v) return;
    float cx = W * 0.5f, cy = H * 0.5f;
    static int logged;
    for (UINT i = 0; i < n; i++) {
        float *xy = (float *)(v + i * g_stream0Stride);
        if (logged < 8) { logged++; dlogf_("video: movie vertex %u at %.1f,%.1f", i, xy[0], xy[1]); }
        xy[0] = cx + (xy[0] - cx) * sx;
        xy[1] = cy + (xy[1] - cy) * sy;
    }
    ((VBUnlock_t)vt[12])(g_stream0);
    if (sx < 0.999f) {
        /* black side bars (the game does not clear outside its picture) */
        LONG bar = (LONG)((W - W * sx) * 0.5f);
        D3DRECT rc[2] = { { 0, 0, bar, (LONG)H }, { (LONG)W - bar, 0, (LONG)W, (LONG)H } };
        ((Clear_t)(*(void ***)dev)[36])(dev, 2, rc, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);
    }
}

/* Sharper cutscenes: the movie is only 512x480 and is stretched to the
 * screen with plain bilinear filtering. Drawing it through a small shader that
 * sharpens at the movie's own pixel size (not the screen's) makes the
 * stretched picture noticeably crisper. Needs pixel shader 1.4. */
#include "shaders/movie.h"
#include "shaders/hud2x.h"
static DWORD g_moviePS, g_hud2xPS;
static int   g_hudX2;        /* the HUD picture being drawn uses MODULATE2X */
static int   g_moviePSFailed;
typedef HRESULT (WINAPI *CreatePS8_t)(IDirect3DDevice8 *, const DWORD *, DWORD *);
typedef HRESULT (WINAPI *SetPS8_t)(IDirect3DDevice8 *, DWORD);
typedef HRESULT (WINAPI *GetPS8_t)(IDirect3DDevice8 *, DWORD *);
typedef HRESULT (WINAPI *SetPSConst8_t)(IDirect3DDevice8 *, DWORD, const void *, DWORD);
typedef HRESULT (WINAPI *GetTex8_t)(IDirect3DDevice8 *, DWORD, IDirect3DBaseTexture8 **);
typedef HRESULT (WINAPI *GetTSS8_t)(IDirect3DDevice8 *, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD *);
typedef HRESULT (WINAPI *SetTSS8_t)(IDirect3DDevice8 *, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);

/* Draw the current primitive through the sharpening shader: unsharp mask at
 * the bound texture's own pixel size (the picture's detail, not the
 * screen's). Used for movies and for HUD pictures. idx = 1: indexed draw
 * (a..d = min index, vertex count, start index, primitive count). */
static int sharpen_draw_x(IDirect3DDevice8 *dev, int pct, int x2, D3DPRIMITIVETYPE pt, int idx, UINT a, UINT b, UINT c4, UINT d);
static int sharpen_draw(IDirect3DDevice8 *dev, int pct, D3DPRIMITIVETYPE pt, int idx, UINT a, UINT b, UINT c4, UINT d)
{
    /* HUD pictures: their texture blend (set by hud_sharpen_ok) decides the shader */
    return sharpen_draw_x(dev, pct, g_hudX2, pt, idx, a, b, c4, d);
}
static int sharpen_draw_x(IDirect3DDevice8 *dev, int pct, int x2, D3DPRIMITIVETYPE pt, int idx, UINT a, UINT b, UINT c4, UINT d)
{
    void *tex = g_tex0Bound ? g_tex0Bound : g_tex0;
    if (!pct || g_moviePSFailed || !tex || !g_realDrawPrim) return 0;
    UINT w, h;
    tex_size(tex, &w, &h);
    if (!w || !h) return 0;
    void **vt = *(void ***)dev;
    if (!g_moviePS) {
        if (FAILED(((CreatePS8_t)vt[87])(dev, k_moviePS, &g_moviePS)) || !g_moviePS) {
            g_moviePSFailed = 1;
            logf_("graphics: sharpening shader not available; movies and HUD drawn as before");
            return 0;
        }
    }
    static const D3DTEXTURESTAGESTATETYPE st[4] = { D3DTSS_ADDRESSU, D3DTSS_ADDRESSV, D3DTSS_MAGFILTER, D3DTSS_MINFILTER };
    DWORD oldPS = 0, oldTSS[5][4];
    IDirect3DBaseTexture8 *oldTex[5] = { 0 };
    ((GetPS8_t)vt[89])(dev, &oldPS);
    for (int sN = 0; sN < 5; sN++) {
        if (sN) ((GetTex8_t)vt[60])(dev, sN, &oldTex[sN]);
        for (int k = 0; k < 4; k++) ((GetTSS8_t)vt[62])(dev, sN, st[k], &oldTSS[sN][k]);
    }
    float dx = 1.0f / w, dy = 1.0f / h, amt = pct / 100.0f;
    float c[6][4] = { { dx, 0, 0, 0 }, { -dx, 0, 0, 0 }, { 0, dy, 0, 0 }, { 0, -dy, 0, 0 },
                      { -0.25f, -0.25f, -0.25f, -0.25f }, { amt, amt, amt, 0 } };
    ((SetPSConst8_t)vt[91])(dev, 0, c, 6);
    for (int sN = 0; sN < 5; sN++) {
        if (sN) g_realSetTexture(dev, sN, (IDirect3DBaseTexture8 *)tex);
        ((SetTSS8_t)vt[63])(dev, sN, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
        ((SetTSS8_t)vt[63])(dev, sN, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        ((SetTSS8_t)vt[63])(dev, sN, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
        ((SetTSS8_t)vt[63])(dev, sN, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    }
    DWORD ps = g_moviePS;
    if (x2) {
        if (!g_hud2xPS && FAILED(((CreatePS8_t)vt[87])(dev, k_hud2xPS, &g_hud2xPS))) g_hud2xPS = 0;
        if (!g_hud2xPS) ps = 0;
        else ps = g_hud2xPS;
    }
    if (!ps) {   /* restore what we changed and draw normally */
        for (int sN = 0; sN < 5; sN++) {
            for (int k = 0; k < 4; k++) ((SetTSS8_t)vt[63])(dev, sN, st[k], oldTSS[sN][k]);
            if (sN) {
                g_realSetTexture(dev, sN, oldTex[sN]);
                if (oldTex[sN]) ((ULONG (WINAPI *)(IDirect3DBaseTexture8 *))(*(void ***)oldTex[sN])[2])(oldTex[sN]);
            }
        }
        return 0;
    }
    ((SetPS8_t)vt[88])(dev, ps);
    if (idx) g_realDrawIdx(dev, pt, a, b, c4, d);
    else g_realDrawPrim(dev, pt, a, b);
    ((SetPS8_t)vt[88])(dev, oldPS);
    for (int sN = 0; sN < 5; sN++) {
        for (int k = 0; k < 4; k++) ((SetTSS8_t)vt[63])(dev, sN, st[k], oldTSS[sN][k]);
        if (sN) {
            g_realSetTexture(dev, sN, oldTex[sN]);
            if (oldTex[sN]) ((ULONG (WINAPI *)(IDirect3DBaseTexture8 *))(*(void ***)oldTex[sN])[2])(oldTex[sN]);
        }
    }
    return 1;
}

static int movie_sharpen_draw(IDirect3DDevice8 *dev, UINT start, UINT count)
{
    static int logged;
    int r = sharpen_draw_x(dev, g_movieSharpPct, 0, D3DPT_TRIANGLESTRIP, 0, start, count, 0, 0);
    if (r && !logged++) logf_("video: sharper cutscenes on (%d%%)", g_movieSharpPct);
    return r;
}

/* HUD and menu pictures are small images stretched across a large screen.
 * Sharpen them at their own pixel size, like movies. Only plain pictures:
 * the texture times the vertex colour, one texture stage, no pixel shader.
 * Text (font textures), screen copies and full-screen layers are left as
 * they are. */
/* How strongly to sharpen HUD pictures: they were made for 640x480, so the
 * larger the screen the more they are stretched and the more they need.
 * None at 480 lines, 50 at 1080p, 80 at 1440p, at most 90 (4K). Off when
 * the player has turned sharpening off. */
static int hud_sharpen_strength(void)
{
    UINT shownH = g_outH ? g_outH : g_modeH;   /* the shown size, not the render size */
    if (!g_sharpenOn || !shownH) return 0;
    int pct = (int)(40.0f * ((float)shownH / 480.0f - 1.0f));
    return pct < 0 ? 0 : pct > 90 ? 90 : pct;
}

/* For checking in the log: what happened to 2D pictures in the first
 * minutes (sharpened, or why not). */
enum { HS_DONE, HS_FVF, HS_NOTEX, HS_COPY, HS_FONT, HS_PS, HS_STATE, HS_PROJ, HS_COUNT };
static DWORD g_hsCount[HS_COUNT];
static DWORD g_hsState[4][9];   /* most recent refused stage setups: cop ca1 ca2 aop aa1 aa2 c1op count */
static int hs_note(int why) { g_hsCount[why]++; return why == HS_DONE; }
static DWORD g_hsT0; static int g_hsReports;   /* three reports per level */
static void hud_sharpen_report(void)
{
    DWORD t0 = g_hsT0; int reports = g_hsReports;
    DWORD now = GetTickCount();
    if (reports >= 3 || !g_hudSharpPct) return;
    if (!t0) { g_hsT0 = now; return; }
    if (now - t0 < 60000) return;
    g_hsT0 = now; g_hsReports = reports + 1;
    logf_("graphics: HUD pictures in the last minute: %lu sharpened; not: %lu other vertex type, %lu untextured, "
          "%lu screen copies, %lu text, %lu own shader, %lu other blending, %lu other projection",
          g_hsCount[HS_DONE], g_hsCount[HS_FVF], g_hsCount[HS_NOTEX], g_hsCount[HS_COPY], g_hsCount[HS_FONT],
          g_hsCount[HS_PS], g_hsCount[HS_STATE], g_hsCount[HS_PROJ]);
    for (int i = 0; i < 4; i++)
        if (g_hsState[i][7])
            logf_("graphics:   blending not sharpened: colour op %lu (%lu,%lu) alpha op %lu (%lu,%lu) stage 2 op %lu - %lu draws",
                  g_hsState[i][0], g_hsState[i][1], g_hsState[i][2], g_hsState[i][3], g_hsState[i][4], g_hsState[i][5],
                  g_hsState[i][6], g_hsState[i][7]);
    memset(g_hsCount, 0, sizeof g_hsCount);
    memset(g_hsState, 0, sizeof g_hsState);
}

static int hud_sharpen_ok(IDirect3DDevice8 *dev)
{
    g_hudSharpPct = hud_sharpen_strength();
    static int logged;
    if (g_hudSharpPct && !logged++) logf_("graphics: sharper HUD on (%d%% for %u lines)", g_hudSharpPct, g_outH ? g_outH : g_modeH);
    if (g_curFVF == FVF_2D && g_tex0 && !is_rt(g_tex0) && !icons_is_font_texture(g_tex0)) textures_note_2d(g_tex0);
    if (!g_hudSharpPct || g_moviePSFailed) return 0;
    if (g_curFVF != FVF_2D) return hs_note(HS_FVF);
    if (!g_tex0) return hs_note(HS_NOTEX);
    if (is_rt(g_tex0)) return hs_note(HS_COPY);
    if (icons_is_font_texture(g_tex0)) return hs_note(HS_FONT);
    void **vt = *(void ***)dev;
    DWORD ps = 0, cop = 0, ca1 = 0, ca2 = 0, aop = 0, aa1 = 0, aa2 = 0, c1op = 0;
    ((GetPS8_t)vt[89])(dev, &ps);
    if (ps) return hs_note(HS_PS);
    ((GetTSS8_t)vt[62])(dev, 0, D3DTSS_COLOROP, &cop);
    ((GetTSS8_t)vt[62])(dev, 0, D3DTSS_COLORARG1, &ca1);
    ((GetTSS8_t)vt[62])(dev, 0, D3DTSS_COLORARG2, &ca2);
    ((GetTSS8_t)vt[62])(dev, 0, D3DTSS_ALPHAOP, &aop);
    ((GetTSS8_t)vt[62])(dev, 0, D3DTSS_ALPHAARG1, &aa1);
    ((GetTSS8_t)vt[62])(dev, 0, D3DTSS_ALPHAARG2, &aa2);
    ((GetTSS8_t)vt[62])(dev, 1, D3DTSS_COLOROP, &c1op);
    int texDiff = (ca1 == D3DTA_TEXTURE && ca2 == D3DTA_DIFFUSE) || (ca1 == D3DTA_DIFFUSE && ca2 == D3DTA_TEXTURE);
    int aTexDiff = (aa1 == D3DTA_TEXTURE && aa2 == D3DTA_DIFFUSE) || (aa1 == D3DTA_DIFFUSE && aa2 == D3DTA_TEXTURE);
    /* stage 2 off, or only passing the first stage's result through */
    int pass = c1op == D3DTOP_DISABLE;
    if (!pass && c1op == D3DTOP_SELECTARG2) {
        DWORD c1a2 = 0, a1op = 0, a1a1 = 0, a1a2 = 0;
        ((GetTSS8_t)vt[62])(dev, 1, D3DTSS_COLORARG2, &c1a2);
        ((GetTSS8_t)vt[62])(dev, 1, D3DTSS_ALPHAOP, &a1op);
        ((GetTSS8_t)vt[62])(dev, 1, D3DTSS_ALPHAARG1, &a1a1);
        ((GetTSS8_t)vt[62])(dev, 1, D3DTSS_ALPHAARG2, &a1a2);
        pass = (c1a2 & D3DTA_SELECTMASK) == D3DTA_CURRENT && !(c1a2 & ~D3DTA_SELECTMASK) &&
               (a1op == D3DTOP_DISABLE || (a1op == D3DTOP_SELECTARG2 && a1a2 == D3DTA_CURRENT) ||
                (a1op == D3DTOP_SELECTARG1 && a1a1 == D3DTA_CURRENT));
    }
    /* colour and alpha: texture x vertex colour, or the same doubled */
    if (texDiff && aTexDiff && pass && cop == aop && (cop == D3DTOP_MODULATE || cop == D3DTOP_MODULATE2X)) {
        g_hudX2 = cop == D3DTOP_MODULATE2X;
        return hs_note(HS_DONE);
    }
    DWORD k[7] = { cop, ca1, ca2, aop, aa1, aa2, c1op };
    int slot = -1;
    for (int i = 0; i < 4 && slot < 0; i++)
        if (g_hsState[i][7] && !memcmp(g_hsState[i], k, sizeof k)) slot = i;
    for (int i = 0; i < 4 && slot < 0; i++)
        if (!g_hsState[i][7]) { slot = i; memcpy(g_hsState[i], k, sizeof k); }
    if (slot >= 0) g_hsState[slot][7]++;
    return hs_note(HS_STATE);
}

/* Horizontal extent of a 2D draw in the game's own (unnarrowed) screen
 * units, -1..1, read from our copy of its vertex buffer. 0 if unknown. */
static int draw_extent(UINT first, UINT count, float *mn, float *mx, float *ymn, float *ymx)
{
    TrackedVB *t = g_stream0 ? find_vb(g_stream0) : NULL;
    if (!t && g_stream0 && g_curFVF == FVF_2D) { track_vb(g_stream0); return 0; }
    UINT stride = g_stream0Stride ? g_stream0Stride : 24;
    if (!t || count == 0 || stride < 12 || first > t->size / stride || count > t->size / stride - first) return 0;
    const D3DMATRIX *m = &g_ortho2D;
    *mn = *ymn = 1e9f; *mx = *ymx = -1e9f;
    for (UINT i = 0; i < count; i++) {
        const float *v = (const float *)(t->shadow + (first + i) * stride);
        float x = v[0] * m->_11 + v[1] * m->_21 + v[2] * m->_31 + m->_41;
        float y = v[0] * m->_12 + v[1] * m->_22 + v[2] * m->_32 + m->_42;
        if (x < *mn) *mn = x;
        if (x > *mx) *mx = x;
        if (y < *ymn) *ymn = y;
        if (y > *ymx) *ymx = y;
    }
    return 1;
}

/* Should this 2D draw cover the full screen width? */
static int draw_full_width(UINT first, UINT count)
{
    if (!g_tex0) return 1;                     /* untextured: fades, flat overlays */
    int copy = is_rt(g_tex0);                  /* screen copies (pause backdrop, blur) */
    float mn, mx, ymn, ymx;
    if (!draw_extent(first, count, &mn, &mx, &ymn, &ymx)) { if (copy) g_copyDrawn = 1; return copy; }
    int spans = mn <= -0.98f && mx >= 0.98f;
    /* the pause menu's box is also cut from the screen copy: only the
     * backdrop itself (across the whole screen) goes full width */
    if (copy) { if (spans) g_copyDrawn = 1; return spans; }
    if (!spans) {
        /* a shading band drawn in pieces (character select): a wide, thin
         * piece along the top or bottom that reaches a side edge */
        int edgeX = mn <= -0.98f || mx >= 0.98f;
        int edgeY = ymn <= -0.98f || ymx >= 0.98f;
        int piece = edgeX && edgeY && (ymx - ymn) < 0.5f && (mx - mn) >= 0.8f;
        static int loggedPiece;
        if (piece && !loggedPiece++) logf_("graphics: shading band drawn in pieces -> full width");
        return piece;
    }
    g_pictureDraw = 1;   /* cleared again below if it goes full width */
    UINT w, h;
    tex_size(g_tex0, &w, &h);
    /* small texture stretched across (gradient/overlay), or a band across
     * the screen that is not full height (the shading at the bottom of menus
     * and gameplay): full width. Full-screen pictures keep their shape. */
    int band = (ymx - ymn) < 0.5f && (ymn <= -0.98f || ymx >= 0.98f);
    int full = (w <= 128 || h <= 128) || band;
    /* log each distinct kind once (for checking on ultrawide screens) */
    DWORD key = (w << 20) ^ (h << 8) ^ (DWORD)((ymx - ymn) * 50.0f);
    int known = 0;
    for (int i = 0; i < g_n2dSeen; i++) if (g_2dSeen[i] == key) known = 1;
    if (full) g_pictureDraw = 0;
    if (!known && g_n2dSeen < 24) { g_2dSeen[g_n2dSeen++] = key; logf_("graphics: full-screen 2D draw, texture %ux%u, height %.2f -> %s", w, h, (ymx - ymn) * 0.5f, full ? "full width" : "16:9 (picture)"); }
    return full;
}

static UINT prim_verts(D3DPRIMITIVETYPE pt, UINT n)
{
    switch (pt) {
    case D3DPT_TRIANGLELIST: return n * 3;
    case D3DPT_TRIANGLESTRIP: case D3DPT_TRIANGLEFAN: return n + 2;
    case D3DPT_LINELIST: return n * 2;
    case D3DPT_LINESTRIP: return n + 1;
    default: return n;
    }
}

/* A full-screen picture drawn narrower than the screen: its texture edges
 * are clamped, so the opposite edge does not bleed in as a thin line. */
static void picture_clamp(IDirect3DDevice8 *dev, DWORD saved[2], int restore)
{
    void **vt = *(void ***)dev;
    if (!restore) {
        ((GetTSS8_t)vt[62])(dev, 0, D3DTSS_ADDRESSU, &saved[0]);
        ((GetTSS8_t)vt[62])(dev, 0, D3DTSS_ADDRESSV, &saved[1]);
        ((SetTSS8_t)vt[63])(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
        ((SetTSS8_t)vt[63])(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    } else {
        ((SetTSS8_t)vt[63])(dev, 0, D3DTSS_ADDRESSU, saved[0]);
        ((SetTSS8_t)vt[63])(dev, 0, D3DTSS_ADDRESSV, saved[1]);
    }
}

static void set_narrow(IDirect3DDevice8 *dev)
{
    D3DMATRIX f = g_ortho2D;
    float sx = hud_aspect() / screen_aspect();
    f._11 *= sx; f._21 *= sx; f._31 *= sx; f._41 *= sx;
    g_realSetTransform(dev, D3DTS_PROJECTION, &f);
}

/* ---- FadeReport: how the game draws see-through 3D things ----
 * Characters between the camera and the player fade out. The Xbox port shows them
 * either solid or gone, so this logs the exact states the PC version uses for each
 * different kind of blended 3D draw (same engine code): blend factors, where the alpha
 * comes from (texture factor, material, vertex colour, texture stages) and its value.
 * Each different set is logged once (alpha in steps of 1/16), up to 200 lines. */
typedef HRESULT (WINAPI *GetRS8_t)(IDirect3DDevice8 *, D3DRENDERSTATETYPE, DWORD *);
typedef HRESULT (WINAPI *GetMat8_t)(IDirect3DDevice8 *, D3DMATERIAL8 *);
static void fade_report(IDirect3DDevice8 *dev, UINT prims, UINT firstV, UINT numV)
{
    static DWORD seen[512]; static int nSeen, nLogged;
    void **vt = *(void ***)dev;
    GetRS8_t grs = (GetRS8_t)vt[51];
    DWORD blend = 0, atest = 0;
    grs(dev, D3DRS_ALPHABLENDENABLE, &blend);
    grs(dev, D3DRS_ALPHATESTENABLE, &atest);
    if (!blend) return;
    DWORD src = 0, dst = 0, zw = 0, aref = 0, lit = 0, dms = 0, tf = 0, cv = 0;
    grs(dev, D3DRS_SRCBLEND, &src); grs(dev, D3DRS_DESTBLEND, &dst); grs(dev, D3DRS_ZWRITEENABLE, &zw);
    grs(dev, D3DRS_ALPHAREF, &aref); grs(dev, D3DRS_LIGHTING, &lit); grs(dev, D3DRS_DIFFUSEMATERIALSOURCE, &dms);
    grs(dev, D3DRS_TEXTUREFACTOR, &tf); grs(dev, D3DRS_COLORVERTEX, &cv);
    DWORD t[2][4];
    static const D3DTEXTURESTAGESTATETYPE ts[4] = { D3DTSS_COLOROP, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2 };
    for (int st = 0; st < 2; st++) for (int k = 0; k < 4; k++) ((GetTSS8_t)vt[62])(dev, st, ts[k], &t[st][k]);
    D3DMATERIAL8 m; memset(&m, 0, sizeof m);
    ((GetMat8_t)vt[43])(dev, &m);
    int matA = (int)(m.Diffuse.a * 255.0f + 0.5f), tfA = (int)(tf >> 24);
    DWORD key = src * 131u ^ dst * 7919u ^ zw * 3u ^ atest * 5u ^ lit * 11u ^ dms * 13u ^ cv * 17u ^ (DWORD)g_curFVF * 31u;
    for (int st = 0; st < 2; st++) for (int k = 0; k < 4; k++) key = key * 33u ^ t[st][k];
    key = key * 33u ^ (DWORD)((matA >> 4) << 8);
    /* the fade itself: alpha below 255 from the texture factor (used by a stage's alpha)
     * or from the material (lighting on, diffuse from the material). Logged every frame,
     * with how far the object is from the camera, to see the fade over time and what
     * starts it. */
    int usesTF = 0;
    for (int st = 0; st < 2; st++)
        if (t[st][1] != D3DTOP_DISABLE && ((t[st][2] & 7) == D3DTA_TFACTOR || (t[st][3] & 7) == D3DTA_TFACTOR)) usesTF = 1;
    int fadeA = usesTF && tfA < 255 ? tfA : (lit && dms == D3DMCS_MATERIAL && matA < 255) ? matA : -1;
    static int nFade; static unsigned fadeFrame; static int fadeInFrame;
    if (fadeA >= 0 && nFade < 200) {
        if (fadeFrame != g_presentNo) { fadeFrame = g_presentNo; fadeInFrame = 0; }
        if (fadeInFrame++ < 4) {
            const D3DMATRIX *v = &g_viewM;
            float cx = -(v->_41 * v->_11 + v->_42 * v->_12 + v->_43 * v->_13);
            float cy = -(v->_41 * v->_21 + v->_42 * v->_22 + v->_43 * v->_23);
            float cz = -(v->_41 * v->_31 + v->_42 * v->_32 + v->_43 * v->_33);
            float dx = g_worldM._41 - cx, dy = g_worldM._42 - cy, dz = g_worldM._43 - cz;
            nFade++;
            logf_("fade frame %u: alpha %d (%s) | object at (%.1f, %.1f, %.1f), %.2f from the camera | prims %u",
                  g_presentNo, fadeA, fadeA == tfA && usesTF ? "texture factor" : "material", g_worldM._41, g_worldM._42, g_worldM._43,
                  sqrtf(dx * dx + dy * dy + dz * dz), prims);
        }
    }
    /* characters: lighting off, the game writes each vertex's colour (and alpha) itself,
     * and stage 0 multiplies the texture's alpha by it. Read the vertices of this draw:
     * their alpha range, and their centre's distance from the camera. */
    static int nVert; static unsigned vertFrame; static int vertInFrame;
    int diffuseAlpha = t[0][1] != D3DTOP_DISABLE && ((t[0][2] & 7) == D3DTA_DIFFUSE || (t[0][3] & 7) == D3DTA_DIFFUSE);
    if (diffuseAlpha && src == D3DBLEND_SRCALPHA && dst == D3DBLEND_INVSRCALPHA && nVert < 2000 && g_stream0 && g_stream0Stride &&
        g_curFVF < 0x10000 && (g_curFVF & D3DFVF_DIFFUSE) && numV > 0) {
        DWORD f = (DWORD)g_curFVF, pos = f & D3DFVF_POSITION_MASK;
        UINT off = pos == D3DFVF_XYZ ? 12 : pos == D3DFVF_XYZRHW ? 16 : pos >= D3DFVF_XYZB1 ? 12 + 4 * ((pos - D3DFVF_XYZB1) / 2 + 1) : 0;
        if (f & D3DFVF_NORMAL) off += 12;
        if (f & D3DFVF_PSIZE) off += 4;
        BYTE *p = NULL;
        if (off && off + 4 <= g_stream0Stride &&
            SUCCEEDED(IDirect3DVertexBuffer8_Lock(g_stream0, firstV * g_stream0Stride, numV * g_stream0Stride, &p, D3DLOCK_READONLY)) && p) {
            int amin = 255, amax = 0; double asum = 0, cx = 0, cy = 0, cz = 0; int n = 0;
            UINT stepV = numV > 256 ? numV / 256 : 1;
            for (UINT i = 0; i < numV; i += stepV, n++) {
                const BYTE *vx = p + (size_t)i * g_stream0Stride;
                int a = vx[off + 3];
                if (a < amin) amin = a;
                if (a > amax) amax = a;
                asum += a;
                const float *xyz = (const float *)vx;
                cx += xyz[0]; cy += xyz[1]; cz += xyz[2];
            }
            IDirect3DVertexBuffer8_Unlock(g_stream0);
            if (n && amin < 124 && g_levelSeq > 0) {   /* below the normal 128 (x2 = solid): fading; in a level */
                if (vertFrame != g_presentNo) { vertFrame = g_presentNo; vertInFrame = 0; }
                if (vertInFrame++ < 4) {
                    cx /= n; cy /= n; cz /= n;
                    /* the vertices may already be in world space; else move the centre by the world matrix */
                    const D3DMATRIX *w = &g_worldM;
                    float wx = (float)(cx * w->_11 + cy * w->_21 + cz * w->_31 + w->_41);
                    float wy = (float)(cx * w->_12 + cy * w->_22 + cz * w->_32 + w->_42);
                    float wz = (float)(cx * w->_13 + cy * w->_23 + cz * w->_33 + w->_43);
                    const D3DMATRIX *v = &g_viewM;
                    float ex = -(v->_41 * v->_11 + v->_42 * v->_12 + v->_43 * v->_13);
                    float ey = -(v->_41 * v->_21 + v->_42 * v->_22 + v->_43 * v->_23);
                    float ez = -(v->_41 * v->_31 + v->_42 * v->_32 + v->_43 * v->_33);
                    float dx = wx - ex, dy = wy - ey, dz = wz - ez;
                    nVert++;
                    logf_("fade frame %u: vertex alpha %d-%d (avg %.0f) | centre (%.1f, %.1f, %.1f), %.2f from the camera at (%.1f, %.1f, %.1f) | "
                          "zwrite=%lu alphatest=%lu ref=%lu | fvf=%#lx prims=%u verts=%u",
                          g_presentNo, amin, amax, asum / n, wx, wy, wz, sqrtf(dx * dx + dy * dy + dz * dz), ex, ey, ez,
                          (unsigned long)zw, (unsigned long)atest, (unsigned long)aref, (unsigned long)f, prims, numV);
                }
            }
        }
    }
    for (int i = 0; i < nSeen; i++) if (seen[i] == key) return;
    if (nSeen < 512) seen[nSeen++] = key;
    if (nLogged >= 200) return;
    nLogged++;
    logf_("fade: blend src=%lu dst=%lu zwrite=%lu alphatest=%lu ref=%lu | lighting=%lu colorvertex=%lu diffusesource=%lu "
          "material alpha=%d | texturefactor alpha=%d | stage0 color=%lu alpha=%lu(%lu,%lu) stage1 color=%lu alpha=%lu(%lu,%lu) | fvf=%#lx prims=%u",
          (unsigned long)src, (unsigned long)dst, (unsigned long)zw, (unsigned long)atest, (unsigned long)aref,
          (unsigned long)lit, (unsigned long)cv, (unsigned long)dms, matA, tfA,
          (unsigned long)t[0][0], (unsigned long)t[0][1], (unsigned long)t[0][2], (unsigned long)t[0][3],
          (unsigned long)t[1][0], (unsigned long)t[1][1], (unsigned long)t[1][2], (unsigned long)t[1][3],
          (unsigned long)g_curFVF, prims);
}

static HRESULT WINAPI hook_DrawPrimitive(IDirect3DDevice8 *dev, D3DPRIMITIVETYPE pt, UINT start, UINT count)
{
    g_hitch[HITCH_DRAWS]++;
    if (g_curFVF == FVF_MOVIE && pt == D3DPT_TRIANGLESTRIP && count == 2) {
        g_movieDrawTick = GetTickCount();
        movie_fit_quad(dev, start, count);
        if (movie_sharpen_draw(dev, start, count)) return D3D_OK;
    }
    if (!g_orthoFixed) {
        if (g_projPersp) { g_draws3D++; g_hitch[HITCH_DRAWS3D]++; if (g_fadeReport) fade_report(dev, count, start, prim_verts(pt, count)); }
        else if (g_projOrtho && g_sharpenOn) {            /* 2D at its normal width (16:9 or narrower) */
            g_pictureDraw = 0;
            if (!draw_full_width(start, prim_verts(pt, count)) && !g_pictureDraw && hud_sharpen_ok(dev) &&
                sharpen_draw(dev, g_hudSharpPct, pt, 0, start, count, 0, 0)) return D3D_OK;
        }
        else if (!g_projPersp && g_curFVF == FVF_2D && g_hudSharpPct) hs_note(HS_PROJ);
        return g_realDrawPrim(dev, pt, start, count);
    }
    g_pictureDraw = 0;
    int fw = draw_full_width(start, prim_verts(pt, count));
    DWORD sv[2]; int pic = g_pictureDraw;
    if (fw) g_realSetTransform(dev, D3DTS_PROJECTION, &g_ortho2D);
    if (pic) picture_clamp(dev, sv, 0);
    HRESULT hr = D3D_OK;
    if (fw || pic || !hud_sharpen_ok(dev) || !sharpen_draw(dev, g_hudSharpPct, pt, 0, start, count, 0, 0))
        hr = g_realDrawPrim(dev, pt, start, count);
    if (pic) picture_clamp(dev, sv, 1);
    if (fw) set_narrow(dev);
    return hr;
}
static HRESULT WINAPI hook_DrawIndexedPrimitive(IDirect3DDevice8 *dev, D3DPRIMITIVETYPE pt, UINT mi, UINT nv, UINT si, UINT pc)
{
    g_hitch[HITCH_DRAWS]++;
    if (!g_orthoFixed) {
        if (g_projPersp) { g_draws3D++; g_hitch[HITCH_DRAWS3D]++; if (g_fadeReport) fade_report(dev, pc, g_baseVertex + mi, nv); }
        else if (g_projOrtho && g_sharpenOn) {
            g_pictureDraw = 0;
            if (!draw_full_width(g_baseVertex + mi, nv) && !g_pictureDraw && hud_sharpen_ok(dev) &&
                sharpen_draw(dev, g_hudSharpPct, pt, 1, mi, nv, si, pc)) return D3D_OK;
        }
        else if (!g_projPersp && g_curFVF == FVF_2D && g_hudSharpPct) hs_note(HS_PROJ);
        return g_realDrawIdx(dev, pt, mi, nv, si, pc);
    }
    g_pictureDraw = 0;
    int fw = draw_full_width(g_baseVertex + mi, nv);
    DWORD sv[2]; int pic = g_pictureDraw;
    if (fw) g_realSetTransform(dev, D3DTS_PROJECTION, &g_ortho2D);
    if (pic) picture_clamp(dev, sv, 0);
    HRESULT hr = D3D_OK;
    if (fw || pic || !hud_sharpen_ok(dev) || !sharpen_draw(dev, g_hudSharpPct, pt, 1, mi, nv, si, pc))
        hr = g_realDrawIdx(dev, pt, mi, nv, si, pc);
    if (pic) picture_clamp(dev, sv, 1);
    if (fw) set_narrow(dev);
    return hr;
}

/* Menu screens with little or no 3D leave the side bars untouched, so stale
 * images from earlier screens show there. Paint them black before showing. */
static HRESULT WINAPI hook_Present(IDirect3DDevice8 *dev, const RECT *a, const RECT *b, HWND w, const RGNDATA *r)
{
    g_presentNo++;
    if (hud_aspect() > 0.0f && g_draws3D < 40 && !g_copyDrawn) {
        LONG barW = (LONG)((g_modeW - g_modeH * hud_aspect()) / 2);
        if (barW > 0) {
            D3DRECT rc[2] = { { 0, 0, barW, (LONG)g_modeH }, { (LONG)g_modeW - barW, 0, (LONG)g_modeW, (LONG)g_modeH } };
            ((Clear_t)(*(void ***)dev)[36])(dev, 2, rc, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);
        }
    }
    /* a new level: log its cameras, 2D pictures and HUD again, then a summary
     * once gameplay has been on screen for 300 frames (a few seconds) */
    static LONG levelSeq; static int summaryIn;
    if (g_levelSeq != levelSeq) {
        levelSeq = g_levelSeq;
        g_nCamSeen = 0; g_n2dSeen = 0; g_hsT0 = 0; g_hsReports = 0; g_depthUnknown = 0;
        memset(g_hsCount, 0, sizeof g_hsCount); memset(g_hsState, 0, sizeof g_hsState);
        summaryIn = 300;
    }
    if (summaryIn > 0 && g_draws3D >= 40 && g_camVfov > 0 && !graphics_movie_playing() && --summaryIn == 0) {
        logf_("level view: game camera %.1f deg vertical at %.3f -> shown %.1f deg at %.3f; near %.2f far %.0f -> %.0f (draw distance %d%%); "
              "fog %s start %.0f end %.0f density %.5f; %d 3D draws",
              g_camVfov, g_camAspectIn, g_camVfovOut, g_camAspectOut, g_camNear, g_camFar, g_camFarOut, g_drawPct,
              g_fogEnable ? (g_fogTable ? "table" : g_fogVertex ? "vertex" : "on") : "off", g_fogStart, g_fogEnd, g_fogDensity, g_draws3D);
    }
    /* every ~10 s, log the live 3D camera when a 3D scene is on screen */
    static unsigned presents;
    if (++presents % 600 == 0 && g_draws3D >= 40 && g_camVfov > 0) {
        dlogf_("graphics: live camera: game %.1f deg vertical at %.4f aspect -> shown %.1f deg at %.4f aspect; depth terms _33=%.6f _43=%.4f",
              g_camVfov, g_camAspectIn, g_camVfovOut, g_camAspectOut, g_camRaw33, g_camRaw43);
        dlogf_("graphics: live scene: draw distance %d%%, near %.2f far %.1f -> %.1f, 3D draws this frame %d, "
              "fog enable=%lu table=%lu vertex=%lu range=%lu start %.1f end %.1f density %.5f color %08lx",
              g_drawPct, g_camNear, g_camFar, g_camFarOut, g_draws3D,
              (unsigned long)g_fogEnable, (unsigned long)g_fogTable, (unsigned long)g_fogVertex,
              (unsigned long)g_fogRange, g_fogStart, g_fogEnd, g_fogDensity, (unsigned long)g_fogColor);
    }
    g_draws3D = 0;
    g_copyDrawn = 0;
    limiter_present_backstop();
    LARGE_INTEGER p0, p1;
    QueryPerformanceCounter(&p0);
    HRESULT hr = g_realPresent(dev, a, b, w, r);
    QueryPerformanceCounter(&p1);
    hitch_present(p0.QuadPart, p1.QuadPart);
    hud_sharpen_report();
    return hr;
}

static void hook_device(IDirect3DDevice8 *dev)
{
    void **vt = *(void ***)dev;
    patch_vtbl(vt, 14, (void *)hook_Reset, (void **)&g_realReset);
    patch_vtbl(vt, 15, (void *)hook_Present, (void **)&g_realPresent);
    patch_vtbl(vt, 20, (void *)hook_CreateTex, (void **)&g_realCreateTex);
    patch_vtbl(vt, 23, (void *)hook_CreateVB, (void **)&g_realCreateVB);
    patch_vtbl(vt, 28, (void *)hook_CopyRects, (void **)&g_realCopyRects);
    patch_vtbl(vt, 37, (void *)hook_SetTransform, (void **)&g_realSetTransform);
    patch_vtbl(vt, 50, (void *)hook_SetRS, (void **)&g_realSetRS);
    patch_vtbl(vt, 76, (void *)hook_SetVS, (void **)&g_realSetVS);
    patch_vtbl(vt, 61, (void *)hook_SetTexture, (void **)&g_realSetTexture);
    patch_vtbl(vt, 70, (void *)hook_DrawPrimitive, (void **)&g_realDrawPrim);
    patch_vtbl(vt, 71, (void *)hook_DrawIndexedPrimitive, (void **)&g_realDrawIdx);
    patch_vtbl(vt, 83, (void *)hook_SetStream, (void **)&g_realSetStream);
    patch_vtbl(vt, 85, (void *)hook_SetIndices, (void **)&g_realSetIndices);
}

/* ------------------------------------------------------------------ */
/* IDirect3D8 wrapper                                                    */

typedef struct {
    IDirect3D8Vtbl *lpVtbl;
    IDirect3D8 *real;
    LONG ref;
} D3D8Wrap;
#define RD(This) (((D3D8Wrap *)(This))->real)

/* The game always uses the main screen (adapter 0). When another screen is
 * chosen, swap the two so the game's "adapter 0" is that screen. */
static UINT AD(UINT a)
{
    if (!g_screenAdapter) return a;
    return a == 0 ? g_screenAdapter : a == g_screenAdapter ? 0 : a;
}
static void find_screen_adapter(IDirect3D8 *real)
{
    g_screenAdapter = 0;
    if (!_stricmp(g_screenName, "main") || !g_screenName[0]) return;
    UINT n = IDirect3D8_GetAdapterCount(real);
    for (UINT i = 0; i < n; i++) {
        MONITORINFOEXA mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
        HMONITOR m = IDirect3D8_GetAdapterMonitor(real, i);
        if (m && GetMonitorInfoA(m, (MONITORINFO *)&mi) && !_stricmp(mi.szDevice + (strncmp(mi.szDevice, "\\\\.\\", 4) ? 0 : 4), g_screenName)) {
            g_screenAdapter = i;
            logf_("graphics: game shown on %s (adapter %u)", mi.szDevice, i);
            return;
        }
    }
    logf_("graphics: screen %s not found; using the main screen", g_screenName);
}

static HRESULT WINAPI w_QueryInterface(IDirect3D8 *This, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirect3D8)) { *ppv = This; ((D3D8Wrap *)This)->ref++; return S_OK; }
    return IDirect3D8_QueryInterface(RD(This), riid, ppv);
}
static ULONG WINAPI w_AddRef(IDirect3D8 *This) { return ++((D3D8Wrap *)This)->ref; }
static ULONG WINAPI w_Release(IDirect3D8 *This)
{
    D3D8Wrap *w = (D3D8Wrap *)This;
    ULONG r = --w->ref;
    if (!r) { IDirect3D8_Release(w->real); HeapFree(GetProcessHeap(), 0, w); }
    return r;
}
static HRESULT WINAPI w_RegisterSoftwareDevice(IDirect3D8 *This, void *f) { return IDirect3D8_RegisterSoftwareDevice(RD(This), f); }
static UINT WINAPI w_GetAdapterCount(IDirect3D8 *This) { return IDirect3D8_GetAdapterCount(RD(This)); }
static HRESULT WINAPI w_GetAdapterIdentifier(IDirect3D8 *This, UINT a, DWORD f, D3DADAPTER_IDENTIFIER8 *id) { return IDirect3D8_GetAdapterIdentifier(RD(This), AD(a), f, id); }

/* The game sees one resolution, in 32-bit and 16-bit formats. */
static const D3DFORMAT g_listFormats[] = { D3DFMT_X8R8G8B8, D3DFMT_R5G6B5 };
static UINT WINAPI w_GetAdapterModeCount(IDirect3D8 *This, UINT a)
{
    choose_mode(RD(This), AD(a));
    return 2;
}
static HRESULT WINAPI w_EnumAdapterModes(IDirect3D8 *This, UINT a, UINT mode, D3DDISPLAYMODE *m)
{
    choose_mode(RD(This), AD(a));
    if (!m || mode >= 2) return D3DERR_INVALIDCALL;
    m->Width = g_modeW; m->Height = g_modeH;
    m->RefreshRate = g_dispMode == DM_FULLSCREEN ? g_modeRefresh : 0;
    m->Format = g_listFormats[mode];
    return D3D_OK;
}
static HRESULT WINAPI w_GetAdapterDisplayMode(IDirect3D8 *This, UINT a, D3DDISPLAYMODE *m) { return IDirect3D8_GetAdapterDisplayMode(RD(This), AD(a), m); }
static HRESULT WINAPI w_CheckDeviceType(IDirect3D8 *This, UINT a, D3DDEVTYPE t, D3DFORMAT df, D3DFORMAT bf, WINBOOL win)
{
    HRESULT hr = IDirect3D8_CheckDeviceType(RD(This), AD(a), t, df, bf, win);
    /* the game checks fullscreen 16/32-bit support; in borderless/windowed we
     * still run windowed, so report fullscreen support for the formats we list */
    return hr;
}
static HRESULT WINAPI w_CheckDeviceFormat(IDirect3D8 *This, UINT a, D3DDEVTYPE t, D3DFORMAT af, DWORD u, D3DRESOURCETYPE rt, D3DFORMAT cf) { return IDirect3D8_CheckDeviceFormat(RD(This), AD(a), t, af, u, rt, cf); }
static HRESULT WINAPI w_CheckDeviceMultiSampleType(IDirect3D8 *This, UINT a, D3DDEVTYPE t, D3DFORMAT f, WINBOOL win, D3DMULTISAMPLE_TYPE ms) { return IDirect3D8_CheckDeviceMultiSampleType(RD(This), AD(a), t, f, win, ms); }
static HRESULT WINAPI w_CheckDepthStencilMatch(IDirect3D8 *This, UINT a, D3DDEVTYPE t, D3DFORMAT af, D3DFORMAT rf, D3DFORMAT df) { return IDirect3D8_CheckDepthStencilMatch(RD(This), AD(a), t, af, rf, df); }
static HRESULT WINAPI w_GetDeviceCaps(IDirect3D8 *This, UINT a, D3DDEVTYPE t, D3DCAPS8 *c) { return IDirect3D8_GetDeviceCaps(RD(This), AD(a), t, c); }
static HMONITOR WINAPI w_GetAdapterMonitor(IDirect3D8 *This, UINT a) { return IDirect3D8_GetAdapterMonitor(RD(This), AD(a)); }
static HRESULT WINAPI w_CreateDevice(IDirect3D8 *This, UINT a, D3DDEVTYPE t, HWND focus, DWORD flags, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice8 **dev)
{
    a = AD(a);
    choose_mode(RD(This), a);
    g_realD3D = RD(This);
    g_adapter = a;
    if (pp) {
        setup_window(pp->hDeviceWindow ? pp->hDeviceWindow : focus, RD(This), a);
        fix_present_params(pp, RD(This), a, focus);
    }
    HRESULT hr = IDirect3D8_CreateDevice(RD(This), a, t, focus, flags, pp, dev);
    if (FAILED(hr) && pp && g_dispMode == DM_FULLSCREEN) {
        logf_("graphics: fullscreen device failed (%#lx), retrying borderless", (unsigned long)hr);
        g_dispMode = DM_BORDERLESS;
        if (g_deskW) { g_outW = g_deskW; g_outH = g_deskH; }                 /* borderless shows the whole screen */
        if (g_vsync && g_deskHz) limiter_cap_to_refresh(g_deskHz);
        setup_window(pp->hDeviceWindow, RD(This), a);
        fix_present_params(pp, RD(This), a, focus);
        hr = IDirect3D8_CreateDevice(RD(This), a, t, focus, flags, pp, dev);
    }
    if (FAILED(hr)) { logf_("graphics: CreateDevice failed (%#lx)", (unsigned long)hr); return hr; }
    logf_("graphics: device created (behavior flags %#lx)", (unsigned long)flags);
    forget_device_state();
    g_moviePS = 0; g_hud2xPS = 0; g_moviePSFailed = 0;        /* shaders belong to the old device */
    hook_device(*dev);
    return hr;
}

static IDirect3D8Vtbl g_d3d8Vtbl = {
    w_QueryInterface, w_AddRef, w_Release, w_RegisterSoftwareDevice, w_GetAdapterCount, w_GetAdapterIdentifier,
    w_GetAdapterModeCount, w_EnumAdapterModes, w_GetAdapterDisplayMode, w_CheckDeviceType, w_CheckDeviceFormat,
    w_CheckDeviceMultiSampleType, w_CheckDepthStencilMatch, w_GetDeviceCaps, w_GetAdapterMonitor, w_CreateDevice,
};

/* ------------------------------------------------------------------ */
/* Exports used by the d3d8.dll forwarder                                */

__declspec(dllexport) IDirect3D8 *WINAPI RF_Direct3DCreate8(UINT sdk)
{
    sound_late_patch();
    HMODULE b = load_backend();
    Create8_t create = b ? (Create8_t)(void *)GetProcAddress(b, "Direct3DCreate8") : NULL;
    if (!create) { logf_("graphics: ERROR backend has no Direct3DCreate8"); return NULL; }
    IDirect3D8 *real = create(sdk);
    if (!real && g_backendIsDxvk) {
        /* DXVK loaded but could not start Vulkan (old driver, or a DXVK
         * version that needs newer Vulkan than this PC has) */
        logf_("graphics: DXVK could not start on this PC (graphics driver too old for this DXVK?); falling back to Windows Direct3D 8");
        char sys[MAX_PATH];
        GetSystemDirectoryA(sys, MAX_PATH);
        strncat(sys, "\\d3d8.dll", MAX_PATH - strlen(sys) - 1);
        HMODULE n = LoadLibraryA(sys);
        create = n ? (Create8_t)(void *)GetProcAddress(n, "Direct3DCreate8") : NULL;
        if (create) { g_backend = n; g_backendIsDxvk = 0; real = create(sdk); }
    }
    if (!real) { logf_("graphics: ERROR Direct3DCreate8 failed"); return NULL; }
    if (!g_gfxEnabled) { logf_("graphics: [Graphics] Enabled=0, not changing resolution or display mode"); return real; }
    find_screen_adapter(real);
    D3D8Wrap *w = (D3D8Wrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *w);
    if (!w) return real;
    w->lpVtbl = &g_d3d8Vtbl;
    w->real = real;
    w->ref = 1;
    return (IDirect3D8 *)w;
}

typedef HRESULT (WINAPI *VPS_t)(const DWORD *, const D3DCAPS8 *, BOOL, char **);
typedef HRESULT (WINAPI *VVS_t)(const DWORD *, const DWORD *, const D3DCAPS8 *, BOOL, char **);
__declspec(dllexport) HRESULT WINAPI RF_ValidatePixelShader(const DWORD *a, const D3DCAPS8 *b, BOOL c, char **d)
{
    VPS_t f = (VPS_t)(void *)GetProcAddress(load_backend(), "ValidatePixelShader");
    return f ? f(a, b, c, d) : E_FAIL;
}
__declspec(dllexport) HRESULT WINAPI RF_ValidateVertexShader(const DWORD *a, const DWORD *b, const D3DCAPS8 *c, BOOL d, char **e)
{
    VVS_t f = (VVS_t)(void *)GetProcAddress(load_backend(), "ValidateVertexShader");
    return f ? f(a, b, c, d, e) : E_FAIL;
}
__declspec(dllexport) void WINAPI RF_DebugSetMute(void)
{
    typedef void (WINAPI *M_t)(void);
    M_t f = (M_t)(void *)GetProcAddress(load_backend(), "DebugSetMute");
    if (f) f();
}
