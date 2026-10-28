/*
 * ROTK Fix - post-processing on the Direct3D 9 level
 *
 * DXVK's d3d8.dll is a front end for its d3d9.dll: every Direct3D 8 call ends
 * up on a Direct3D 9 device. ROTK Fix redirects d3d8.dll's import of
 * Direct3DCreate9 to itself, which gives access to that device. Just before
 * each frame is shown (Present), a full-screen pass runs with real Direct3D 9
 * pixel shaders.
 *
 * Brightness: the game sets its brightness with a gamma ramp, which DXVK only
 * applies in exclusive fullscreen. The ramp is captured here and applied by a
 * shader instead, in every display mode, with an optional extra Brightness
 * adjustment from ROTKFix.ini.
 *
 * Every Direct3D 9 state touched is saved and restored (state block plus the
 * render target and depth buffer), so the d3d8 layer above never notices.
 */
#define CINTERFACE
#include "rotkfix.h"
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "shaders/post.h"

static float g_brightness = 1.0f;   /* extra gamma on top of the game's: >1 brighter */
static int   g_useGameGamma = 1;
static int   g_postEnabled = 1;
static int   g_sharpenPct = 60;       /* 0-100 */
static int   g_sharpenOn = 1;         /* toggled with [ */

/* Colour looks, applied through the same colour table as brightness */
enum { LOOK_ORIGINAL, LOOK_FILM, LOOK_VIVID, LOOK_DARK, LOOK_COUNT };
static const char *const k_lookName[LOOK_COUNT] = { "original", "film", "vivid", "dark" };
static int g_look = LOOK_ORIGINAL;
static int g_lookPct = 100;
static int g_lutDirty = 1;

void post_load_settings(const char *ini)
{
    char buf[32];
    g_postEnabled = GetPrivateProfileIntA("Graphics", "PostProcessing", 1, ini);
    GetPrivateProfileStringA("Graphics", "Brightness", "1.0", buf, sizeof buf, ini);
    g_brightness = (float)atof(buf);
    if (g_brightness < 0.5f) g_brightness = 0.5f;
    if (g_brightness > 2.0f) g_brightness = 2.0f;
    g_useGameGamma = GetPrivateProfileIntA("Graphics", "UseGameBrightness", 1, ini);
    g_sharpenPct = GetPrivateProfileIntA("Graphics", "Sharpening", 60, ini);
    GetPrivateProfileStringA("Graphics", "ColourLook", "original", buf, sizeof buf, ini);
    for (int i = 0; i < LOOK_COUNT; i++) if (!lstrcmpiA(buf, k_lookName[i])) g_look = i;
    g_lookPct = GetPrivateProfileIntA("Graphics", "ColourLookStrength", 100, ini);
    if (g_lookPct < 0) g_lookPct = 0;
    if (g_lookPct > 100) g_lookPct = 100;
    if (g_sharpenPct < 0) g_sharpenPct = 0;
    if (g_sharpenPct > 100) g_sharpenPct = 100;
}

static void patch_vt(void **vt, int idx, void *hook, void **orig)
{
    if (vt[idx] == hook) return;
    DWORD old;
    if (orig && !*orig) *orig = vt[idx];
    VirtualProtect(&vt[idx], sizeof(void *), PAGE_EXECUTE_READWRITE, &old);
    vt[idx] = hook;
    VirtualProtect(&vt[idx], sizeof(void *), old, &old);
}

/* ------------------------------------------------------------------ */
/* State                                                                */

typedef HRESULT (WINAPI *Present9_t)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT (WINAPI *Reset9_t)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
typedef void    (WINAPI *SetGamma9_t)(IDirect3DDevice9 *, UINT, DWORD, const D3DGAMMARAMP *);
typedef HRESULT (WINAPI *CreateDev9_t)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef IDirect3D9 *(WINAPI *Create9_t)(UINT);

static Create9_t    g_realCreate9;
static CreateDev9_t g_realCreateDev9;
static Present9_t   g_realPresent9;
static Reset9_t     g_realReset9;
static SetGamma9_t  g_realSetGamma9;

static IDirect3DPixelShader9 *g_psPost;
static IDirect3DTexture9     *g_sceneTex;      /* copy of the finished frame */
static UINT                   g_sceneW, g_sceneH;
static D3DFORMAT              g_sceneFmt;
#define LUT_N 64
static IDirect3DVolumeTexture9 *g_lutTex;      /* LUT_N^3 colour curve (brightness) */
static IDirect3DStateBlock9  *g_sb;
static int                    g_failed;        /* stop trying: the shader was rejected, or errors kept coming */
static int                    g_waitFrames;    /* after a passing error (e.g. video memory full during alt-tab): frames to skip */
static int                    g_errors;        /* errors in a row */

/* A resource could not be made right now. Skip the effect for a while and try
 * again; give up only if it keeps failing. */
static void transient_error(const char *what)
{
    g_errors++;
    if (g_errors >= 10) { logf_("post: %s kept failing; post-processing off", what); g_failed = 1; return; }
    logf_("post: %s failed; trying again shortly", what);
    g_waitFrames = 120 * g_errors;
}

static WORD  g_gameRamp[3][256];
static int   g_haveGameRamp;
static int   g_lutIdentity = 1;

/* ------------------------------------------------------------------ */
/* Brightness curve                                                      */

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/* One colour through the chosen look. Values are display (gamma) values 0..1.
 * Tuned for this game's dark scenes: contrast pivots low (0.30) so shadows
 * are not crushed, and the shadow tint fades out toward pure black. */
typedef struct { float contrast, pivot, sat, vibrance, gamma, shadow[3], light[3]; } Look;
static const Look k_looks[LOOK_COUNT] = {
    { 1.00f, 0.30f, 1.00f, 0.00f, 1.00f, {  0.000f, 0.000f,  0.000f }, { 0.000f, 0.000f,  0.000f } },  /* original */
    { 1.12f, 0.30f, 1.06f, 0.15f, 0.97f, { -0.012f, 0.006f,  0.028f }, { 0.045f, 0.018f, -0.030f } },  /* film */
    { 1.08f, 0.30f, 1.30f, 0.35f, 0.93f, {  0.000f, 0.000f,  0.000f }, { 0.010f, 0.005f,  0.000f } },  /* vivid */
    { 1.15f, 0.30f, 0.72f, 0.00f, 1.05f, { -0.020f, 0.000f,  0.025f }, { 0.012f, 0.006f, -0.006f } },  /* dark */
};

static void grade(float c[3])
{
    if (g_look == LOOK_ORIGINAL || g_lookPct == 0) return;
    const Look *L = &k_looks[g_look];
    float in[3] = { c[0], c[1], c[2] }, x[3];
    for (int i = 0; i < 3; i++) {
        float v = powf(clamp01(c[i]), L->gamma);
        v = L->pivot + (v - L->pivot) * L->contrast;
        if (v < 0.0f) v = 0.0f;
        if (v > 0.9f) v = 0.9f + 0.1f * tanhf((v - 0.9f) / 0.1f);     /* soft shoulder */
        x[i] = v;
    }
    float l = 0.2126f * x[0] + 0.7152f * x[1] + 0.0722f * x[2];
    float mx = fmaxf(x[0], fmaxf(x[1], x[2])), mn = fminf(x[0], fminf(x[1], x[2]));
    float curSat = (mx - mn) / (mx + 1e-4f);
    float sat = L->sat * (1.0f + L->vibrance * (1.0f - curSat));    /* vibrance: dull colours gain most */
    float ws = (1.0f - l) * fminf(l * 6.0f, 1.0f);
    float k = g_lookPct / 100.0f;
    for (int i = 0; i < 3; i++) {
        float v = l + (x[i] - l) * sat + ws * L->shadow[i] + l * L->light[i];
        c[i] = clamp01(in[i] + (clamp01(v) - in[i]) * k);
    }
}

static void build_lut(IDirect3DDevice9 *dev)
{
    if (!g_lutTex) {
        if (FAILED(IDirect3DDevice9_CreateVolumeTexture(dev, LUT_N, LUT_N, LUT_N, 1, 0, D3DFMT_A16B16G16R16,
                                                        D3DPOOL_MANAGED, &g_lutTex, NULL))) {
            g_lutTex = NULL;
            transient_error("creating the colour curve texture");
            return;
        }
    }
    /* one curve per channel, sampled at LUT_N points */
    WORD curve[3][LUT_N];
    int identity = 1;
    float ex = 1.0f / g_brightness;
    for (int c = 0; c < 3; c++) {
        for (int i = 0; i < LUT_N; i++) {
            float x = (float)i / (LUT_N - 1);
            if (g_useGameGamma && g_haveGameRamp) {      /* game ramp, interpolated */
                float f = x * 255.0f;
                int a = (int)f; if (a > 254) a = 254;
                float t = f - a;
                x = (g_gameRamp[c][a] * (1.0f - t) + g_gameRamp[c][a + 1] * t) / 65535.0f;
            }
            float v = powf(x, ex);
            WORD w = (WORD)(v * 65535.0f + 0.5f);
            WORD idw = (WORD)((float)i / (LUT_N - 1) * 65535.0f + 0.5f);
            if (w > idw + 64 || w + 64 < idw) identity = 0;
            curve[c][i] = w;
        }
    }
    D3DLOCKED_BOX lb;
    if (FAILED(IDirect3DVolumeTexture9_LockBox(g_lutTex, 0, &lb, NULL, 0))) return;
    int graded = g_look != LOOK_ORIGINAL && g_lookPct > 0;
    if (graded) identity = 0;
    for (int b = 0; b < LUT_N; b++)          /* depth = blue */
        for (int g = 0; g < LUT_N; g++) {    /* rows = green */
            WORD *p = (WORD *)((BYTE *)lb.pBits + b * lb.SlicePitch + g * lb.RowPitch);
            for (int r = 0; r < LUT_N; r++) {  /* columns = red; A16B16G16R16 = R,G,B,A in memory */
                if (graded) {
                    float c[3] = { curve[0][r] / 65535.0f, curve[1][g] / 65535.0f, curve[2][b] / 65535.0f };
                    grade(c);
                    p[r * 4 + 0] = (WORD)(c[0] * 65535.0f + 0.5f);
                    p[r * 4 + 1] = (WORD)(c[1] * 65535.0f + 0.5f);
                    p[r * 4 + 2] = (WORD)(c[2] * 65535.0f + 0.5f);
                } else {
                    p[r * 4 + 0] = curve[0][r];
                    p[r * 4 + 1] = curve[1][g];
                    p[r * 4 + 2] = curve[2][b];
                }
                p[r * 4 + 3] = 0xFFFF;
            }
        }
    IDirect3DVolumeTexture9_UnlockBox(g_lutTex, 0);
    g_lutIdentity = identity;
    g_lutDirty = 0;
    static int nlog;
    if (nlog++ < 6) dlogf_("post: brightness curve %s (game ramp %s, Brightness %.2f): 25%% -> %.3f, 50%% -> %.3f, 75%% -> %.3f",
          identity ? "is neutral" : "active",
          g_haveGameRamp ? (g_useGameGamma ? "used" : "ignored") : "not set",
          g_brightness, curve[1][LUT_N / 4] / 65535.0f, curve[1][LUT_N / 2] / 65535.0f, curve[1][LUT_N * 3 / 4] / 65535.0f);
}

static void WINAPI hook_SetGamma9(IDirect3DDevice9 *dev, UINT sc, DWORD flags, const D3DGAMMARAMP *r)
{
    (void)dev; (void)flags;
    if (!r || sc != 0) return;
    if (!g_haveGameRamp || memcmp(g_gameRamp, r, sizeof g_gameRamp)) {
        memcpy(g_gameRamp[0], r->red, sizeof g_gameRamp[0]);
        memcpy(g_gameRamp[1], r->green, sizeof g_gameRamp[1]);
        memcpy(g_gameRamp[2], r->blue, sizeof g_gameRamp[2]);
        static int n;
        if (n++ < 5) dlogf_("post: game set its brightness ramp (25%% -> %.3f, 50%% -> %.3f, 75%% -> %.3f)",
                            r->green[64] / 65535.0f, r->green[128] / 65535.0f, r->green[192] / 65535.0f);
        g_haveGameRamp = 1;
        g_lutDirty = 1;
    }
    /* not passed on: DXVK would apply it in fullscreen and we apply it ourselves */
}

/* The game keeps its gamma / contrast / brightness at 0x569c2c and builds
 * the ramp with its own function at 0x4a5010, but it only sends the ramp to
 * Direct3D when it believes it is in fullscreen. Read the three values every
 * frame and build the ramp with the game's own function when they change. */
#define GAME_GAMMA_VALUES  ((volatile float *)0x569c2c)
#define GAME_RAMP_BUILDER  0x4a5010
static const BYTE k_rampSig[] = { 0x83, 0xEC, 0x18, 0xD9, 0x05, 0x4C, 0x5F, 0x53, 0x00, 0xA1, 0x2C, 0x9C, 0x56, 0x00 };
static int   g_rampSigOk = -1;
static float g_lastGameVals[3] = { -1.0f, -1.0f, -1.0f };

static int ramp_valid(const WORD *r)
{
    if (r[0] >= r[255]) return 0;
    for (int i = 1; i < 256; i++) if (r[i] < r[i - 1]) return 0;
    return 1;
}

static void poll_game_gamma(void)
{
    if (g_rampSigOk < 0) {
        g_rampSigOk = !IsBadReadPtr((void *)GAME_RAMP_BUILDER, sizeof k_rampSig)
                      && !memcmp((void *)GAME_RAMP_BUILDER, k_rampSig, sizeof k_rampSig);
        logf_("post: game brightness code %s", g_rampSigOk ? "found" : "not recognised (game's brightness option unavailable)");
    }
    if (!g_rampSigOk) return;
    float v[3] = { GAME_GAMMA_VALUES[0], GAME_GAMMA_VALUES[1], GAME_GAMMA_VALUES[2] };
    if (!memcmp(v, g_lastGameVals, sizeof v)) return;
    memcpy(g_lastGameVals, v, sizeof v);
    static WORD buf[3][256];
    ((void (__cdecl *)(void *))GAME_RAMP_BUILDER)(buf);
    if (!ramp_valid(buf[1])) {
        logf_("post: game brightness values %.3f %.3f %.3f give an unusable curve; ignored", v[0], v[1], v[2]);
        return;
    }
    memcpy(g_gameRamp, buf, sizeof g_gameRamp);
    g_haveGameRamp = 1;
    g_lutDirty = 1;
    static int n;
    if (n++ < 20) dlogf_("post: game brightness gamma %.3f contrast %.3f brightness %.3f (windowed flag %lu)",
                        v[0], v[1], v[2], (unsigned long)*(volatile DWORD *)0x5c2338);
}

/* ------------------------------------------------------------------ */
/* The pass                                                             */

static void release_default_pool(void)
{
    if (g_sceneTex) { IDirect3DTexture9_Release(g_sceneTex); g_sceneTex = NULL; }
    if (g_sb) { IDirect3DStateBlock9_Release(g_sb); g_sb = NULL; }
}

typedef struct { float x, y, z, rhw; float uv[5][2]; } QuadVtx;

static void run_post(IDirect3DDevice9 *dev)
{
    if (!g_postEnabled || g_failed) return;
    if (g_waitFrames > 0) { g_waitFrames--; return; }
    if (g_useGameGamma) poll_game_gamma();
    if (g_lutDirty) build_lut(dev);
    if (g_failed || !g_lutTex) return;
    if (input_keyboard_seen() && input_take_key_presses(0x1A) > 0) {     /* [ toggles sharpening */
        g_sharpenOn = !g_sharpenOn;
        logf_("post: [ -> sharpening %s", g_sharpenOn ? "on" : "off");
    }
    if (input_keyboard_seen() && input_take_key_presses(0x1B) > 0) {   /* ] next look */
        g_look = (g_look + 1) % LOOK_COUNT;
        g_lutDirty = 1;
        logf_("post: ] -> colour look %s", k_lookName[g_look]);
        build_lut(dev);
        if (!g_lutTex) return;
    }
    int sharpen = g_sharpenOn && g_sharpenPct > 0;
    if (g_lutIdentity && !sharpen) return;

    IDirect3DSurface9 *bb = NULL, *oldRT = NULL, *oldZ = NULL, *sceneSurf = NULL;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    D3DSURFACE_DESC bd;
    IDirect3DSurface9_GetDesc(bb, &bd);

    if (!g_psPost && FAILED(IDirect3DDevice9_CreatePixelShader(dev, ps_post, &g_psPost))) {
        logf_("post: post-processing shader was rejected; post-processing off");
        g_failed = 1;
        goto out;
    }
    if (g_sceneTex && (g_sceneW != bd.Width || g_sceneH != bd.Height || g_sceneFmt != bd.Format)) {
        IDirect3DTexture9_Release(g_sceneTex); g_sceneTex = NULL;
    }
    if (!g_sceneTex) {
        if (FAILED(IDirect3DDevice9_CreateTexture(dev, bd.Width, bd.Height, 1, D3DUSAGE_RENDERTARGET, bd.Format,
                                                  D3DPOOL_DEFAULT, &g_sceneTex, NULL))) {
            transient_error("creating the frame copy");
            goto out;
        }
        g_sceneW = bd.Width; g_sceneH = bd.Height; g_sceneFmt = bd.Format;
        logf_("post: running at %ux%u (format %d, multisample %d), sharpening %d%%",
              bd.Width, bd.Height, (int)bd.Format, (int)bd.MultiSampleType, g_sharpenPct);
    }
    if (!g_sb && FAILED(IDirect3DDevice9_CreateStateBlock(dev, D3DSBT_ALL, &g_sb))) { g_sb = NULL; transient_error("saving the render state"); goto out; }

    IDirect3DStateBlock9_Capture(g_sb);
    IDirect3DDevice9_GetRenderTarget(dev, 0, &oldRT);
    IDirect3DDevice9_GetDepthStencilSurface(dev, &oldZ);

    /* copy the frame (this also resolves MSAA) */
    IDirect3DTexture9_GetSurfaceLevel(g_sceneTex, 0, &sceneSurf);
    if (FAILED(IDirect3DDevice9_StretchRect(dev, bb, NULL, sceneSurf, NULL, D3DTEXF_NONE))) {
        static int warned;
        if (!warned++) logf_("post: could not copy the frame");
        goto restore;
    }

    IDirect3DDevice9_SetRenderTarget(dev, 0, bb);
    IDirect3DDevice9_SetDepthStencilSurface(dev, NULL);
    D3DVIEWPORT9 vp = { 0, 0, bd.Width, bd.Height, 0.0f, 1.0f };
    IDirect3DDevice9_SetViewport(dev, &vp);

    static const D3DRENDERSTATETYPE off[] = {
        D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_FOGENABLE,
        D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_CLIPPLANEENABLE,
        D3DRS_SEPARATEALPHABLENDENABLE,
    };
    for (size_t i = 0; i < sizeof off / sizeof off[0]; i++) IDirect3DDevice9_SetRenderState(dev, off[i], FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_FILLMODE, D3DFILL_SOLID);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_COLORWRITEENABLE, 0xF);

    /* stages 0-4: the frame (0 exact, 1-4 bilinear), stage 5: colour curve */
    for (DWORD s = 0; s < 6; s++) {
        IDirect3DDevice9_SetTexture(dev, s, s < 5 ? (IDirect3DBaseTexture9 *)g_sceneTex : (IDirect3DBaseTexture9 *)g_lutTex);
        DWORD filt = s == 0 ? D3DTEXF_POINT : D3DTEXF_LINEAR;
        IDirect3DDevice9_SetSamplerState(dev, s, D3DSAMP_MINFILTER, filt);
        IDirect3DDevice9_SetSamplerState(dev, s, D3DSAMP_MAGFILTER, filt);
        IDirect3DDevice9_SetSamplerState(dev, s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        IDirect3DDevice9_SetSamplerState(dev, s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        IDirect3DDevice9_SetSamplerState(dev, s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        IDirect3DDevice9_SetSamplerState(dev, s, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
        IDirect3DDevice9_SetSamplerState(dev, s, D3DSAMP_SRGBTEXTURE, FALSE);
        IDirect3DDevice9_SetSamplerState(dev, s, D3DSAMP_MAXANISOTROPY, 1);
        IDirect3DDevice9_SetSamplerState(dev, s, D3DSAMP_MIPMAPLODBIAS, 0);
        IDirect3DDevice9_SetTextureStageState(dev, s, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        IDirect3DDevice9_SetTextureStageState(dev, s, D3DTSS_TEXCOORDINDEX, s < 5 ? s : 0);
    }
    for (DWORD s = 6; s < 8; s++) IDirect3DDevice9_SetTexture(dev, s, NULL);

    IDirect3DDevice9_SetVertexShader(dev, NULL);
    IDirect3DDevice9_SetPixelShader(dev, g_psPost);
    IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX5);
    const float a = (LUT_N - 1.0f) / LUT_N, b = 0.5f / LUT_N;
    float amt = sharpen ? g_sharpenPct / 100.0f : 0.0f;     /* ps 1.x constants are limited to [-1, 1] */
    float c[20] = { a, a, a, a,  b, b, b, b,  0, 0, 0, 0,  -0.25f, -0.25f, -0.25f, -0.25f,  amt, amt, amt, amt };
    IDirect3DDevice9_SetPixelShaderConstantF(dev, 0, c, 5);

    /* texcoords: the pixel itself, then half a pixel diagonally each way */
    float w = (float)bd.Width - 0.5f, h = (float)bd.Height - 0.5f;
    float du = 0.5f / bd.Width, dv = 0.5f / bd.Height;
    static const float off4[4][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
    QuadVtx q[4] = {
        { -0.5f, -0.5f, 0.0f, 1.0f, { { 0 } } },
        {  w,    -0.5f, 0.0f, 1.0f, { { 0 } } },
        { -0.5f,  h,    0.0f, 1.0f, { { 0 } } },
        {  w,     h,    0.0f, 1.0f, { { 0 } } },
    };
    for (int v = 0; v < 4; v++) {
        float u0 = (v & 1) ? 1.0f : 0.0f, v0 = (v & 2) ? 1.0f : 0.0f;
        q[v].uv[0][0] = u0; q[v].uv[0][1] = v0;
        for (int k = 0; k < 4; k++) { q[v].uv[k + 1][0] = u0 + off4[k][0] * du; q[v].uv[k + 1][1] = v0 + off4[k][1] * dv; }
    }
    IDirect3DDevice9_BeginScene(dev);
    IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof q[0]);
    IDirect3DDevice9_EndScene(dev);
    g_errors = 0;

restore:
    IDirect3DDevice9_SetRenderTarget(dev, 0, oldRT);
    IDirect3DDevice9_SetDepthStencilSurface(dev, oldZ);
    IDirect3DStateBlock9_Apply(g_sb);
out:
    if (sceneSurf) IDirect3DSurface9_Release(sceneSurf);
    if (oldRT) IDirect3DSurface9_Release(oldRT);
    if (oldZ) IDirect3DSurface9_Release(oldZ);
    if (bb) IDirect3DSurface9_Release(bb);
}

/* ------------------------------------------------------------------ */
/* Hooks                                                                */

void shot_take(IDirect3DDevice9 *dev);
void shot_release(void);

static HRESULT WINAPI hook_Present9(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND w, const RGNDATA *r)
{
    run_post(dev);
    achieve_draw(dev);   /* "Achievement earned", over the picture and under nothing */
    RF_RunRenderHooks(dev); /* External mod visual render hooks (auras, HUD, effects) */
    RF_RunFrameHooks();  /* External mod frame hooks / updates */
    /* Print Screen (DIK_SYSRQ) or \ (DIK_BACKSLASH): screenshot of the finished frame */
    if (input_keyboard_seen() && (input_take_key_presses(0xB7) + input_take_key_presses(0x2B)) > 0)
        shot_take(dev);
    return g_realPresent9(dev, a, b, w, r);
}

static HRESULT WINAPI hook_Reset9(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp)
{
    release_default_pool();
    shot_release();
    achieve_release();
    RF_RunResetHooks(dev);
    return g_realReset9(dev, pp);
}

static HRESULT WINAPI hook_CreateDev9(IDirect3D9 *d3d, UINT ad, D3DDEVTYPE t, HWND h, DWORD fl,
                                      D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    HRESULT hr = g_realCreateDev9(d3d, ad, t, h, fl, pp, out);
    if (SUCCEEDED(hr) && out && *out) {
        /* objects made on an earlier device can't be used on this one */
        release_default_pool();
        shot_release();
        achieve_release();
        RF_RunResetHooks(NULL);   /* the mods' objects too (a mod's reset hook is told the device is gone) */
        if (g_psPost) { IDirect3DPixelShader9_Release(g_psPost); g_psPost = NULL; }
        if (g_lutTex) { IDirect3DVolumeTexture9_Release(g_lutTex); g_lutTex = NULL; }
        g_lutDirty = 1; g_errors = 0; g_waitFrames = 0;
        void **vt = *(void ***)*out;
        patch_vt(vt, 16, (void *)hook_Reset9, (void **)&g_realReset9);
        patch_vt(vt, 17, (void *)hook_Present9, (void **)&g_realPresent9);
        patch_vt(vt, 21, (void *)hook_SetGamma9, (void **)&g_realSetGamma9);
        logf_("post: Direct3D 9 device ready for post-processing");
    }
    return hr;
}

static IDirect3D9 *WINAPI hook_Create9(UINT sdk)
{
    IDirect3D9 *d = g_realCreate9(sdk);
    if (d) patch_vt(*(void ***)d, 16, (void *)hook_CreateDev9, (void **)&g_realCreateDev9);
    return d;
}

/* Point d3d8.dll's import of d3d9.dll!Direct3DCreate9 at hook_Create9. */
int post_attach_dxvk_d3d8(HMODULE d3d8)
{
    if (!g_postEnabled || !d3d8) return 0;
    BYTE *base = (BYTE *)d3d8;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); imp->Name; imp++) {
        if (lstrcmpiA((char *)(base + imp->Name), "d3d9.dll")) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA *iat = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME *n = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((char *)n->Name, "Direct3DCreate9")) continue;
            DWORD old;
            g_realCreate9 = (Create9_t)(void *)iat->u1.Function;
            VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            iat->u1.Function = (ULONG_PTR)(void *)hook_Create9;
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            logf_("post: attached to DXVK's Direct3D 9 layer");
            return 1;
        }
    }
    logf_("post: d3d8.dll does not import Direct3DCreate9; post-processing unavailable");
    return 0;
}
