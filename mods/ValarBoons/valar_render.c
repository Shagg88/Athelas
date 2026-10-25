/* Valar Boons - Direct3D 9 & GDI Visual Effects
 *
 * Implements:
 *  - Dynamic Screen-Edge Aura with hit reaction flashes (Attack Surge & Defense Flash)
 *  - Top-Left Active Boons HUD Status Widget with real-time countdown timers
 *  - Direct3D 9 state block management and device reset handling
 */

#include "valar_boons.h"

/* D3D9 Overlay Resources */
static IDirect3DStateBlock9 *g_d3dSb = NULL;
static IDirect3DTexture9    *g_hudTex = NULL;
static UINT g_hudW = 0, g_hudH = 0;
static DWORD g_lastHudUpdate = 0;

typedef struct { float x, y, z, rhw; DWORD color; } AuraVtx;
typedef struct { float x, y, z, rhw; DWORD color; float u, v; } HudVtx;

void valar_reset_hook(void *d3d9dev)
{
    (void)d3d9dev;
    if (g_hudTex) {
        IDirect3DTexture9_Release(g_hudTex);
        g_hudTex = NULL;
    }
    if (g_d3dSb) {
        IDirect3DStateBlock9_Release(g_d3dSb);
        g_d3dSb = NULL;
    }
}

static void update_hud_texture(IDirect3DDevice9 *dev, UINT w, UINT h)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = (LONG)w;
    bi.bmiHeader.biHeight = -(LONG)h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;

    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(dc);
        return;
    }

    HGDIOBJ old = SelectObject(dc, bmp);

    /* Background pill */
    HBRUSH bg = CreateSolidBrush(RGB(14, 12, 10));
    RECT all = { 0, 0, (LONG)w, (LONG)h };
    FillRect(dc, &all, bg);
    DeleteObject(bg);

    HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(120, 100, 65));
    HGDIOBJ op = SelectObject(dc, borderPen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 1, 1, (int)w - 1, (int)h - 1, 8, 8);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(borderPen);

    SetBkMode(dc, TRANSPARENT);
    HFONT fontBold = CreateFontA(-(int)(h * 0.38f), 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HFONT fontSub  = CreateFontA(-(int)(h * 0.25f), 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");

    DWORD now = GetTickCount();
    int cols = g_boonCount > 0 ? g_boonCount : 1;
    int colW = (int)(w / cols);

    for (int i = 0; i < g_boonCount; i++) {
        BoonConfig *b = &g_boons[i];
        int x = i * colW + 10;

        char keyLabel[32];
        const char *firstKey = b->keyStr[0] ? b->keyStr : "?";
        char cleanKey[16] = {0};
        sscanf(firstKey, "%15[^, ]", cleanKey);
        snprintf(keyLabel, sizeof keyLabel, "[%s] %s", cleanKey[0] ? cleanKey : "?", b->name);

        int onCooldown = (now < b->cooldownUntil) || (b->group > 0 && now < g_groupCooldownUntil[b->group]);

        SelectObject(dc, fontBold);
        if (b->active) {
            SetTextColor(dc, RGB(b->colorR, b->colorG, b->colorB));
        } else if (onCooldown) {
            SetTextColor(dc, RGB(220, 140, 50));
        } else {
            SetTextColor(dc, RGB(110, 115, 120));
        }
        TextOutA(dc, x, (int)(h * 0.12f), keyLabel, (int)strlen(keyLabel));

        SelectObject(dc, fontSub);
        char sub[48];
        if (b->active) {
            if (b->activeUntil > 0) {
                float remain = (float)(b->activeUntil - now) / 1000.0f;
                if (remain < 0.0f) remain = 0.0f;
                snprintf(sub, sizeof sub, "Active (%.0fs)", remain);
            } else {
                snprintf(sub, sizeof sub, "%s", b->title);
            }
            SetTextColor(dc, RGB(220, 240, 255));
        } else if (onCooldown) {
            DWORD cdEnd = b->cooldownUntil;
            if (b->group > 0 && g_groupCooldownUntil[b->group] > cdEnd) {
                cdEnd = g_groupCooldownUntil[b->group];
            }
            float waitSec = (float)(cdEnd - now) / 1000.0f;
            if (waitSec < 0.0f) waitSec = 0.0f;
            snprintf(sub, sizeof sub, "CD: %.1fs", waitSec);
            SetTextColor(dc, RGB(220, 140, 50));
        } else {
            snprintf(sub, sizeof sub, "%s", b->title);
            SetTextColor(dc, RGB(80, 85, 90));
        }
        TextOutA(dc, x, (int)(h * 0.58f), sub, (int)strlen(sub));
    }

    GdiFlush();
    SelectObject(dc, old);
    DeleteObject(fontBold);
    DeleteObject(fontSub);

    if (g_hudTex && (g_hudW != w || g_hudH != h)) {
        IDirect3DTexture9_Release(g_hudTex);
        g_hudTex = NULL;
    }
    if (!g_hudTex) {
        if (FAILED(IDirect3DDevice9_CreateTexture(dev, w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_hudTex, NULL))) {
            DeleteObject(bmp);
            DeleteDC(dc);
            return;
        }
    }

    g_hudW = w;
    g_hudH = h;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(IDirect3DTexture9_LockRect(g_hudTex, 0, &lr, NULL, 0))) {
        for (UINT y = 0; y < h; y++) {
            DWORD *src = (DWORD *)bits + y * w;
            DWORD *dst = (DWORD *)((BYTE *)lr.pBits + y * lr.Pitch);
            for (UINT x = 0; x < w; x++) {
                DWORD a = 215;
                dst[x] = (src[x] & 0x00FFFFFF) | (a << 24);
            }
        }
        IDirect3DTexture9_UnlockRect(g_hudTex, 0);
    }

    DeleteObject(bmp);
    DeleteDC(dc);
}

void valar_render_hook(void *d3d9dev)
{
    IDirect3DDevice9 *dev = (IDirect3DDevice9 *)d3d9dev;
    if (!dev) return;

    if (!g_d3dSb && FAILED(IDirect3DDevice9_CreateStateBlock(dev, D3DSBT_ALL, &g_d3dSb))) {
        g_d3dSb = NULL;
        return;
    }
    IDirect3DStateBlock9_Capture(g_d3dSb);

    IDirect3DSurface9 *bb = NULL;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    D3DSURFACE_DESC bd;
    IDirect3DSurface9_GetDesc(bb, &bd);

    IDirect3DSurface9 *oldRT = NULL, *oldZ = NULL;
    IDirect3DDevice9_GetRenderTarget(dev, 0, &oldRT);
    IDirect3DDevice9_GetDepthStencilSurface(dev, &oldZ);
    IDirect3DDevice9_SetRenderTarget(dev, 0, bb);
    IDirect3DDevice9_SetDepthStencilSurface(dev, NULL);

    D3DVIEWPORT9 vp = { 0, 0, bd.Width, bd.Height, 0.0f, 1.0f };
    IDirect3DDevice9_SetViewport(dev, &vp);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHATESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_STENCILENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SCISSORTESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SRGBWRITEENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_COLORWRITEENABLE, 0xF);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetVertexShader(dev, NULL);
    IDirect3DDevice9_SetPixelShader(dev, NULL);

    DWORD now = GetTickCount();

    int activeCount = 0;
    int sumR = 0, sumG = 0, sumB = 0;
    for (int i = 0; i < g_boonCount; i++) {
        if (g_boons[i].active) {
            sumR += g_boons[i].colorR;
            sumG += g_boons[i].colorG;
            sumB += g_boons[i].colorB;
            activeCount++;
        }
    }

    int isAttack = (now < g_attackFlashUntil);
    int isDefend = (now < g_defendFlashUntil);

    /* ------------------------------------------------------------- */
    /* 1. Dynamic Screen-Edge Aura                                   */
    /* ------------------------------------------------------------- */
    if (g_showAura && (activeCount > 0 || isAttack || isDefend)) {
        int r = 0, g = 0, b = 0, a = 0;
        if (isDefend) {
            /* Defensive Shield Flash */
            r = 255; g = 220; b = 30; a = 120;
        } else if (isAttack) {
            /* Attack Surge */
            r = 30; g = 225; b = 255; a = 95;
            if (activeCount > 0) {
                r = sumR / activeCount;
                g = sumG / activeCount;
                b = sumB / activeCount;
            }
        } else {
            /* Ambient Active Aura */
            r = sumR / activeCount;
            g = sumG / activeCount;
            b = sumB / activeCount;
            a = 36;
        }

        DWORD c_outer = ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
        DWORD c_inner = ((DWORD)0 << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;

        float W = (float)bd.Width;
        float H = (float)bd.Height;
        float T = (isAttack || isDefend) ? (H / 22.0f) : (H / 32.0f);
        if (T < 24.0f) T = 24.0f;
        if (T > 60.0f) T = 60.0f;

        IDirect3DDevice9_SetTexture(dev, 0, NULL);
        IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

        AuraVtx top[4] = {
            { 0, 0, 0, 1, c_outer }, { 0, T, 0, 1, c_inner },
            { W, 0, 0, 1, c_outer }, { W, T, 0, 1, c_inner }
        };
        AuraVtx bot[4] = {
            { 0, H - T, 0, 1, c_inner }, { 0, H, 0, 1, c_outer },
            { W, H - T, 0, 1, c_inner }, { W, H, 0, 1, c_outer }
        };
        AuraVtx left[4] = {
            { 0, 0, 0, 1, c_outer }, { T, 0, 0, 1, c_inner },
            { 0, H, 0, 1, c_outer }, { T, H, 0, 1, c_inner }
        };
        AuraVtx right[4] = {
            { W - T, 0, 0, 1, c_inner }, { W, 0, 0, 1, c_outer },
            { W - T, H, 0, 1, c_inner }, { W, H, 0, 1, c_outer }
        };

        IDirect3DDevice9_BeginScene(dev);
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, top, sizeof(AuraVtx));
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, bot, sizeof(AuraVtx));
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, left, sizeof(AuraVtx));
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, right, sizeof(AuraVtx));
        IDirect3DDevice9_EndScene(dev);
    }

    /* ------------------------------------------------------------- */
    /* 2. Top-Left Active Boons HUD Status Widget                     */
    /* ------------------------------------------------------------- */
    if (g_showHud && g_boonCount > 0) {
        UINT hudW = (UINT)(bd.Height * 0.14f * g_boonCount);
        if (hudW < 300) hudW = 300;
        if (hudW > bd.Width * 0.7f) hudW = (UINT)(bd.Width * 0.7f);
        UINT hudH = (UINT)(bd.Height / 24);
        if (hudH < 44) hudH = 44;

        /* Update texture periodically (every 100ms) or when state changes */
        if (!g_hudTex || (now - g_lastHudUpdate > 100)) {
            update_hud_texture(dev, hudW, hudH);
            g_lastHudUpdate = now;
        }

        if (g_hudTex) {
            float x0 = bd.Width * 0.02f;
            float y0 = bd.Height * 0.02f;
            float x1 = x0 + g_hudW;
            float y1 = y0 + g_hudH;

            HudVtx quad[4] = {
                { x0, y0, 0, 1, 0xFFFFFFFF, 0, 0 },
                { x1, y0, 0, 1, 0xFFFFFFFF, 1, 0 },
                { x0, y1, 0, 1, 0xFFFFFFFF, 0, 1 },
                { x1, y1, 0, 1, 0xFFFFFFFF, 1, 1 },
            };

            IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)g_hudTex);
            IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

            IDirect3DDevice9_BeginScene(dev);
            IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(HudVtx));
            IDirect3DDevice9_EndScene(dev);
        }
    }

    /* Restore pipeline state */
    IDirect3DDevice9_SetRenderTarget(dev, 0, oldRT);
    IDirect3DDevice9_SetDepthStencilSurface(dev, oldZ);
    IDirect3DStateBlock9_Apply(g_d3dSb);
    if (oldRT) IDirect3DSurface9_Release(oldRT);
    if (oldZ) IDirect3DSurface9_Release(oldZ);
    IDirect3DSurface9_Release(bb);
}
