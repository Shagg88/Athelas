/*
 * ROTK Fix - screenshots
 *
 * Print Screen (or \) saves the finished frame - after brightness and
 * sharpening, before any overlay is drawn on top - as a PNG in the game
 * folder's Screenshots subfolder. The PNG is written on a background thread
 * so the game does not hitch.
 *
 * PNG encoding: stb_image_write by Sean Barrett (public domain / MIT).
 */
#define CINTERFACE
#include "rotkfix.h"
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "third_party/stb_image_write.h"
#pragma GCC diagnostic pop

static IDirect3DSurface9 *g_shotRT;    /* plain (non-MSAA) copy of the back buffer */
static IDirect3DSurface9 *g_shotSys;   /* system-memory copy we can read */
static UINT g_shotW, g_shotH;
static D3DFORMAT g_shotFmt;

typedef struct { BYTE *rgb; UINT w, h; char path[MAX_PATH]; } ShotJob;

static DWORD WINAPI shot_writer(LPVOID p)
{
    ShotJob *j = (ShotJob *)p;
    int ok = stbi_write_png(j->path, (int)j->w, (int)j->h, 3, j->rgb, (int)j->w * 3);
    logf_("screenshot: %s %s", ok ? "saved" : "could not save", j->path);
    free(j->rgb);
    free(j);
    return 0;
}

void shot_release(void)
{
    if (g_shotRT) { IDirect3DSurface9_Release(g_shotRT); g_shotRT = NULL; }
    if (g_shotSys) { IDirect3DSurface9_Release(g_shotSys); g_shotSys = NULL; }
}

void shot_take(IDirect3DDevice9 *dev)
{
    IDirect3DSurface9 *bb = NULL;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    D3DSURFACE_DESC d;
    IDirect3DSurface9_GetDesc(bb, &d);
    if (d.Format != D3DFMT_X8R8G8B8 && d.Format != D3DFMT_A8R8G8B8) {
        logf_("screenshot: unsupported back buffer format %d", (int)d.Format);
        IDirect3DSurface9_Release(bb);
        return;
    }
    if (g_shotRT && (g_shotW != d.Width || g_shotH != d.Height || g_shotFmt != d.Format)) shot_release();
    if (!g_shotRT) {
        if (FAILED(IDirect3DDevice9_CreateRenderTarget(dev, d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &g_shotRT, NULL)) ||
            FAILED(IDirect3DDevice9_CreateOffscreenPlainSurface(dev, d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &g_shotSys, NULL))) {
            logf_("screenshot: could not create the capture surfaces");
            shot_release();
            IDirect3DSurface9_Release(bb);
            return;
        }
        g_shotW = d.Width; g_shotH = d.Height; g_shotFmt = d.Format;
    }
    HRESULT hr = IDirect3DDevice9_StretchRect(dev, bb, NULL, g_shotRT, NULL, D3DTEXF_NONE);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_GetRenderTargetData(dev, g_shotRT, g_shotSys);
    IDirect3DSurface9_Release(bb);
    if (FAILED(hr)) { logf_("screenshot: could not read the frame (%#lx)", (unsigned long)hr); return; }

    D3DLOCKED_RECT lr;
    if (FAILED(IDirect3DSurface9_LockRect(g_shotSys, &lr, NULL, D3DLOCK_READONLY))) return;
    ShotJob *j = (ShotJob *)calloc(1, sizeof *j);
    BYTE *rgb = j ? (BYTE *)malloc((size_t)d.Width * d.Height * 3) : NULL;
    if (!rgb) { free(j); IDirect3DSurface9_UnlockRect(g_shotSys); return; }
    for (UINT y = 0; y < d.Height; y++) {
        const BYTE *s = (const BYTE *)lr.pBits + (size_t)y * lr.Pitch;
        BYTE *o = rgb + (size_t)y * d.Width * 3;
        for (UINT x = 0; x < d.Width; x++, s += 4, o += 3) { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; }
    }
    IDirect3DSurface9_UnlockRect(g_shotSys);

    char dir[MAX_PATH];
    path_in_dir(dir, "Screenshots");
    CreateDirectoryA(dir, NULL);
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(j->path, sizeof j->path, "%s\\ROTK_%04u-%02u-%02u_%02u-%02u-%02u_%03u.png", dir,
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    j->rgb = rgb; j->w = d.Width; j->h = d.Height;
    HANDLE th = CreateThread(NULL, 0, shot_writer, j, 0, NULL);
    if (th) CloseHandle(th); else shot_writer(j);
}
