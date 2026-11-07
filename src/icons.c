/*
 * ROTK Fix - button pictures in the game's on-screen hints
 *
 * The game draws text with bitmap fonts: each font has a 256-entry table of
 * glyph records and one texture (A4R4G4B4, padded to a power of two, so
 * there are unused rows below the letters). When a font has loaded, ROTK Fix
 * paints its own button icons into those unused rows and registers them as
 * the unused character codes 0x10-0x1F. A hint such as "X - Parry" can then
 * show the icon for X instead of the letter. The icons are white with dark
 * letters, so the game tints them with the text colour like any other glyph.
 *
 * Game structures (rotk.exe 1.0):
 *   0x49b740  font loader, returns the font; epilogue hooked at 0x49bcb1
 *   font +0x00c  glyph table [256] -> 0x28-byte glyph records
 *        +0x418  short texture width, +0x41a short height (used part)
 *        +0x438  IDirect3DTexture8 *, +0x43c / +0x440 power-of-two size
 *   glyph record: [0] width, [4] height (screen units), [8] u0, [0xc] u1,
 *        [0x10] v0, [0x14] v1 (fractions of the used part; the draw code at
 *        0x49cc50 scales them by +0x444 / +0x448), [0x18] advance,
 *        [0x1c] x offset, [0x20] y offset, [0x24] -> 16-byte source record
 *        (code u16, w u8, h u8, x u16, y u16, advance s8, xoff s8, yoff s8)
 *
 * A font a texture pack replaces with an exact 2x / 4x picture (textures.c,
 * #72) gets the same icons painted into that picture, at that scale.
 *
 * The artwork is original (scripts/mkicons.py); no game or console-maker art.
 */
#define CINTERFACE
#include "rotkfix.h"
#include <d3d8.h>
#include <string.h>
#include "icons_data.h"

#define ADDR_FONT_EPILOGUE 0x0049BCB1u   /* pop edi; mov eax,esi; pop esi; pop ebx */
#define ADDR_FONT_RETURN   0x0049BCB6u   /* mov esp,ebp; pop ebp; ret */
#define ADDR_FONT_FREE     0x0049BD20u   /* void __cdecl font_free(font): push esi; mov esi,[esp+8] */
#define ICON_CODE0 0x10
#define NUM_ICONS 16
#define MAX_FONTS 16

typedef struct {
    float w, h, u0, u1, v0, v1, adv, xoff, yoff;
    const void *src;
} Glyph;   /* 0x28 bytes, the game's layout */
typedef struct { unsigned short code; unsigned char w, h; unsigned short x, y; signed char adv, xoff, yoff, pad[5]; } SrcGlyph;

typedef struct {
    BYTE *font;
    int x[NUM_ICONS], y[NUM_ICONS], w[NUM_ICONS], h;   /* where each icon sits in the texture */
    Glyph glyph[NUM_ICONS];
    SrcGlyph src[NUM_ICONS];
    int style;                                            /* icon set painted now, -1 = none */
} FontIcons;

/* Slots never move (the game's glyph tables point into them); a freed
 * font's slot has font == NULL and is reused. */
static FontIcons g_fonts[MAX_FONTS];
static int g_nfonts;            /* slots in use so far (high-water mark) */
static int g_enabled = 1;
static volatile int g_ready;      /* at least one font has icons */

void icons_load_settings(const char *ini)
{
    (void)ini;   /* button pictures are always on (coloured face buttons) */
}

/* Our own colours for the four face buttons (A, B, X, Y slots), 0-15 per
 * channel: fill (the button) and ink (letter, symbol and outline).
 * Xbox style: coloured button, dark letter. PlayStation style: light button,
 * coloured symbol. Nintendo buttons are not coloured. */
typedef struct { BYTE fill[3], ink[3]; } IconColour;
static const IconColour k_faceColour[2][4] = {
    { { { 6, 12, 4 }, { 1, 1, 1 } },      /* A green  */
      { { 14, 4, 3 }, { 1, 1, 1 } },      /* B red    */
      { { 3, 8, 14 }, { 1, 1, 1 } },      /* X blue   */
      { { 15, 12, 2 }, { 1, 1, 1 } } },   /* Y yellow */
    { { { 15, 15, 15 }, { 6, 9, 15 } },   /* cross: blue     */
      { { 15, 15, 15 }, { 14, 5, 5 } },   /* circle: red     */
      { { 15, 15, 15 }, { 13, 7, 12 } },  /* square: pink    */
      { { 15, 15, 15 }, { 4, 12, 10 } } } /* triangle: green */
};

typedef HRESULT (WINAPI *Lock_t)(IDirect3DTexture8 *, UINT, D3DLOCKED_RECT *, const RECT *, DWORD);
typedef HRESULT (WINAPI *Unlock_t)(IDirect3DTexture8 *, UINT);

/* One texel of an icon drawn in a cell W x H from the master (sw x ICON_H): its coverage (0-1) and how dark (0-15).
 * Smaller than the master: the master's texels under it averaged; larger (a 2x / 4x font, #72): bilinear between
 * the four nearest, so the edges stay smooth instead of turning into steps. */
static void sample(const IconInfo *ic, int x, int y, int W, int H, float *cov, float *ink)
{
    int sw = ic->w;
    float c = 0, k = 0;
    if (H > ICON_H) {
        float fx = (x + 0.5f) * sw / W - 0.5f, fy = (y + 0.5f) * ICON_H / H - 0.5f;
        int x0 = (int)(fx < 0 ? -1 : fx), y0 = (int)(fy < 0 ? -1 : fy);   /* floor */
        float tx = fx - x0, ty = fy - y0;
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++) {
                int sx = x0 + i, sy = y0 + j;
                sx = sx < 0 ? 0 : sx >= sw ? sw - 1 : sx;
                sy = sy < 0 ? 0 : sy >= ICON_H ? ICON_H - 1 : sy;
                float wgt = (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
                BYTE p = k_iconPixels[ic->off + sy * sw + sx];
                c += wgt * (p >> 4); k += wgt * (p & 15) * (p >> 4);
            }
    } else {
        int sy0 = y * ICON_H / H, sy1 = (y + 1) * ICON_H / H, sx0 = x * sw / W, sx1 = (x + 1) * sw / W, n = 0;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sx1 <= sx0) sx1 = sx0 + 1;
        for (int sy = sy0; sy < sy1 && sy < ICON_H; sy++)
            for (int sx = sx0; sx < sx1 && sx < sw; sx++) {
                BYTE p = k_iconPixels[ic->off + sy * sw + sx];
                c += p >> 4; k += (p & 15) * (p >> 4); n++;
            }
        if (n) { c /= n; k /= n; }
    }
    *cov = c / 15.0f;
    *ink = c > 0 ? k / c : 0;
}

/* The same icon set painted into the 2x / 4x replacement the font is drawn from, if it has one (#72): each cell at
 * k times its place and size, in A8R8G8B8, then the smaller levels of those rows made again from it (the
 * replacement's own mipmaps don't have the pictures). */
static void paint_hd(FontIcons *fi, int style)
{
    const void *tex = *(void **)(fi->font + 0x438);
    void *bits; int pitch, k; unsigned tw, th, levels;
    if (!textures_font_hd_lock(tex, 0, &bits, &pitch, &tw, &th, &k, &levels)) return;
    int H = fi->h * k, top = (int)th, bottom = 0;
    for (int i = 0; i < NUM_ICONS; i++) {
        const IconInfo *ic = &k_iconInfo[style][i];
        const IconColour *col = (style < 2 && i < 4) ? &k_faceColour[style][i] : NULL;
        int W = fi->w[i] * k, x0 = fi->x[i] * k, y0 = fi->y[i] * k;
        if (x0 + W > (int)tw || y0 + H > (int)th) continue;   /* (can't happen: the cells fit the game's texture) */
        if (y0 < top) top = y0;
        if (y0 + H > bottom) bottom = y0 + H;
        for (int y = 0; y < H; y++) {
            DWORD *row = (DWORD *)((BYTE *)bits + (size_t)(y0 + y) * pitch) + x0;
            for (int x = 0; x < W; x++) {
                float cov, ink;
                sample(ic, x, y, W, H, &cov, &ink);
                DWORD a = (DWORD)(cov * 255.0f + 0.5f), c[3];
                for (int ch = 0; ch < 3; ch++) {
                    float v = col ? col->fill[ch] + (col->ink[ch] - col->fill[ch]) * ink / 15.0f
                                  : 15.0f - ink * 14.0f / 15.0f;   /* light fill -> dark ink */
                    c[ch] = (DWORD)(v * 17.0f + 0.5f);
                }
                row[x] = a << 24 | c[0] << 16 | c[1] << 8 | c[2];
            }
        }
    }
    textures_font_hd_unlock(tex, 0);
    /* the smaller levels: each texel of those rows the average of the four under it in the level above */
    for (unsigned lv = 1; lv < levels && bottom > top; lv++) {
        void *sb, *db; int sp, dp, kk; unsigned sw2, sh2, dw, dh, nl;
        if (!textures_font_hd_lock(tex, lv - 1, &sb, &sp, &sw2, &sh2, &kk, &nl)) break;
        if (!textures_font_hd_lock(tex, lv, &db, &dp, &dw, &dh, &kk, &nl)) { textures_font_hd_unlock(tex, lv - 1); break; }
        unsigned y0 = (unsigned)top >> lv, y1 = ((unsigned)bottom + (1u << lv) - 1) >> lv;
        if (y1 > dh) y1 = dh;
        for (unsigned y = y0; y < y1; y++) {
            unsigned ya = 2 * y < sh2 ? 2 * y : sh2 - 1, yb = 2 * y + 1 < sh2 ? 2 * y + 1 : sh2 - 1;
            const BYTE *ra = (const BYTE *)sb + (size_t)ya * sp, *rb = (const BYTE *)sb + (size_t)yb * sp;
            BYTE *d = (BYTE *)db + (size_t)y * dp;
            for (unsigned x = 0; x < dw; x++) {
                unsigned xa = 2 * x < sw2 ? 2 * x : sw2 - 1, xb = 2 * x + 1 < sw2 ? 2 * x + 1 : sw2 - 1;
                for (int b = 0; b < 4; b++)
                    d[x * 4 + b] = (BYTE)((ra[xa * 4 + b] + ra[xb * 4 + b] + rb[xa * 4 + b] + rb[xb * 4 + b] + 2) / 4);
            }
        }
        textures_font_hd_unlock(tex, lv);
        textures_font_hd_unlock(tex, lv - 1);
    }
}

/* Paint one icon set (0 Xbox, 1 PlayStation, 2 Nintendo) into a font's texture. */
static int paint(FontIcons *fi, int style)
{
    IDirect3DTexture8 *tex = *(IDirect3DTexture8 **)(fi->font + 0x438);
    if (!tex || !readable(tex, 4)) return 0;
    void **vt = *(void ***)tex;
    D3DLOCKED_RECT lr;
    if (FAILED(((Lock_t)vt[16])(tex, 0, &lr, NULL, 0)) || !lr.pBits) return 0;
    int H = fi->h;
    for (int i = 0; i < NUM_ICONS; i++) {
        const IconInfo *ic = &k_iconInfo[style][i];
        const IconColour *col = (style < 2 && i < 4) ? &k_faceColour[style][i] : NULL;
        int W = fi->w[i], sw = ic->w;
        /* clear the cell, then area-sample the 48 px master down to H */
        for (int y = 0; y < H; y++) {
            WORD *row = (WORD *)((BYTE *)lr.pBits + (size_t)(fi->y[i] + y) * lr.Pitch) + fi->x[i];
            int sy0 = y * ICON_H / H, sy1 = (y + 1) * ICON_H / H;
            if (sy1 <= sy0) sy1 = sy0 + 1;
            for (int x = 0; x < W; x++) {
                int sx0 = x * sw / W, sx1 = (x + 1) * sw / W;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                int cov = 0, ink = 0, n = 0;
                for (int sy = sy0; sy < sy1 && sy < ICON_H; sy++)
                    for (int sx = sx0; sx < sx1 && sx < sw; sx++) {
                        BYTE p = k_iconPixels[ic->off + sy * sw + sx];
                        int c = p >> 4;
                        cov += c; ink += (p & 15) * c; n++;
                    }
                int a = n ? (cov + n / 2) / n : 0;                       /* 0-15 */
                int k = cov ? ink / cov : 0;                             /* 0-15 */
                if (col) {
                    int c[3];
                    for (int ch = 0; ch < 3; ch++)
                        c[ch] = col->fill[ch] + ((col->ink[ch] - col->fill[ch]) * k + 7) / 15;
                    row[x] = (WORD)(a << 12 | c[0] << 8 | c[1] << 4 | c[2]);
                } else {
                    int v = 15 - (k * 14 + 7) / 15;                      /* light fill -> dark ink */
                    row[x] = (WORD)(a << 12 | v << 8 | v << 4 | v);
                }
            }
        }
    }
    ((Unlock_t)vt[17])(tex, 0);
    paint_hd(fi, style);
    fi->style = style;
    return 1;
}

/* Called with each font the game has just loaded (its texture is ready). */
/* Every font's texture, so other code can tell text from pictures. */
#define MAX_FONT_TEX 32
static BYTE *g_fontObj[MAX_FONT_TEX];
static void *g_fontTex[MAX_FONT_TEX];
static int g_fontWhite[MAX_FONT_TEX];   /* its letters are white: only their alpha shapes them */
int icons_is_font_texture(const void *tex)
{
    if (!tex) return 0;
    for (int i = 0; i < MAX_FONT_TEX; i++) if (g_fontTex[i] == tex) return 1;
    return 0;
}
static void remember_font(BYTE *font)
{
    if (!font || !readable(font, 0x43c)) return;
    void *tex = *(void **)(font + 0x438);
    int slot = -1;
    for (int i = 0; i < MAX_FONT_TEX; i++) {
        if (g_fontObj[i] == font) { slot = i; break; }
        if (slot < 0 && !g_fontObj[i]) slot = i;
    }
    if (slot >= 0) { g_fontObj[slot] = font; g_fontTex[slot] = tex; g_fontWhite[slot] = 0; }
}

/* Are the font's letters white (every well-covered texel of the letters' rows light)? Then drawing it
 * with texture colour x text colour looks the same as with the text colour alone. */
static int letters_white(BYTE *font)
{
    IDirect3DTexture8 *tex = *(IDirect3DTexture8 **)(font + 0x438);
    int tw = *(short *)(font + 0x418), th = *(short *)(font + 0x41a);
    if (!tex || !readable(tex, 4) || tw <= 0 || th <= 0) return 0;
    void **vt = *(void ***)tex;
    D3DLOCKED_RECT lr;
    if (FAILED(((Lock_t)vt[16])(tex, 0, &lr, NULL, D3DLOCK_READONLY)) || !lr.pBits) return 0;
    int seen = 0, dark = 0;
    for (int y = 0; y < th; y++) {
        const WORD *row = (const WORD *)((const BYTE *)lr.pBits + (size_t)y * lr.Pitch);
        for (int x = 0; x < tw; x++) {
            WORD p = row[x];
            if ((p >> 12) < 12) continue;
            seen++;
            if (((p >> 8) & 15) < 13 || ((p >> 4) & 15) < 13 || (p & 15) < 13) dark++;
        }
    }
    ((Unlock_t)vt[17])(tex, 0);
    return seen > 0 && dark * 50 < seen;   /* (a few edge texels may be off-white) */
}
/* v (as drawn) where a font's letters end: the text draw (0x49cc50) multiplies the glyph's v by font +0x448, the used
 * height over the texture's power-of-two height (th / ph), so letters reach th / ph and the button pictures below
 * them (from y = th + 1) lie between that and 1; 0 = not a font (#34, brig-dev's analysis) */
float icons_letters_v_end(const void *tex)
{
    if (!tex) return 0.0f;
    for (int i = 0; i < MAX_FONT_TEX; i++)
        if (g_fontTex[i] == tex && g_fontObj[i] && readable(g_fontObj[i], 0x444)) {
            int th = *(short *)(g_fontObj[i] + 0x41a), ph = *(int *)(g_fontObj[i] + 0x440);
            return th > 0 && ph > th ? (th + 0.5f) / (float)ph : 0.0f;
        }
    return 0.0f;
}
int icons_font_is_white(const void *tex)
{
    if (!tex) return 0;
    for (int i = 0; i < MAX_FONT_TEX; i++) if (g_fontTex[i] == tex) return g_fontWhite[i];
    return 0;
}

static void __cdecl on_font_loaded(BYTE *font)
{
    remember_font(font);
    /* a texture pack's 2x / 4x picture of this font: drawn instead of it from now on (before the button pictures are
     * painted, which go into both) */
    if (font && readable(font, 0x43c)) textures_font_begin(*(void **)(font + 0x438));
    if (!g_enabled || !font || !readable(font, 0x464)) return;
    Glyph **table = (Glyph **)(font + 0x0c);
    int tw = *(short *)(font + 0x418), th = *(short *)(font + 0x41a);
    int pw = *(int *)(font + 0x43c), ph = *(int *)(font + 0x440);
    Glyph *A = table['A'];
    if (tw <= 0 || th <= 0 || pw < tw || ph <= th || !A || !readable(A, sizeof *A) || !A->src) return;
    const SrcGlyph *sa = (const SrcGlyph *)A->src;
    if (!readable(sa, sizeof *sa) || sa->w == 0 || sa->h == 0) return;
    for (int c = ICON_CODE0; c < ICON_CODE0 + NUM_ICONS; c++)
        if (table[c]) { logf_("icons: font %p already uses character %#x; left alone", (void *)font, c); return; }

    FontIcons *fi = NULL;
    for (int i = 0; i < g_nfonts && !fi; i++) if (!g_fonts[i].font || g_fonts[i].font == font) fi = &g_fonts[i];
    if (!fi) {
        if (g_nfonts >= MAX_FONTS) { logf_("icons: too many fonts"); return; }
        fi = &g_fonts[g_nfonts++];
    }
    memset(fi, 0, sizeof *fi);
    fi->font = font; fi->style = -1;
    /* icon height: a little taller than a capital letter, and it must fit the free rows */
    int H = sa->h * 13 / 10;
    if (H < 8) H = 8;
    if (H > ICON_H) H = ICON_H;
    if (H > ph - th - 2) H = ph - th - 2;
    if (H < 8) { logf_("icons: no room in font %p (%dx%d in %dx%d)", (void *)font, tw, th, pw, ph); return; }
    fi->h = H;
    /* lay the icons out in rows below the letters */
    int x = 1, y = th + 1;
    for (int i = 0; i < NUM_ICONS; i++) {
        int W = 0;
        for (int s = 0; s < 3; s++) {                          /* room for the widest style */
            int w = (k_iconInfo[s][i].w * H + ICON_H / 2) / ICON_H;
            if (w > W) W = w;
        }
        if (x + W + 1 > pw) { x = 1; y += H + 2; }
        if (y + H + 1 > ph) { logf_("icons: font %p full", (void *)font); fi->font = NULL; return; }
        fi->x[i] = x; fi->y[i] = y; fi->w[i] = W;
        x += W + 2;
    }
    if (!paint(fi, 0)) { logf_("icons: could not draw into font %p", (void *)font); fi->font = NULL; return; }

    float sx = A->w / sa->w, sy = A->h / sa->h;
    float capMid = (float)sa->yoff + sa->h * 0.5f;
    for (int i = 0; i < NUM_ICONS; i++) {
        Glyph *g = &fi->glyph[i];
        SrcGlyph *s = &fi->src[i];
        int W = fi->w[i];
        s->code = (unsigned short)(ICON_CODE0 + i); s->w = (unsigned char)W; s->h = (unsigned char)H;
        s->x = (unsigned short)fi->x[i]; s->y = (unsigned short)fi->y[i];
        s->adv = (signed char)(W + 2 > 127 ? 127 : W + 2); s->xoff = 1;
        s->yoff = (signed char)(capMid - H * 0.5f);
        g->w = W * sx; g->h = H * sy;
        g->u0 = (float)fi->x[i] / tw; g->u1 = (float)(fi->x[i] + W) / tw;
        g->v0 = (float)fi->y[i] / th; g->v1 = (float)(fi->y[i] + H) / th;
        g->adv = s->adv * sx; g->xoff = s->xoff * sx; g->yoff = s->yoff * sy;
        g->src = s;
    }
    for (int i = 0; i < NUM_ICONS; i++) table[ICON_CODE0 + i] = &fi->glyph[i];
    g_ready = 1;
    for (int i = 0; i < MAX_FONT_TEX; i++)
        if (g_fontObj[i] == font) g_fontWhite[i] = letters_white(font);
    logf_("icons: font %p (%dx%d, capital %dpx): %d px icons, letters %s", (void *)font, tw, th, sa->h, H,
           icons_font_is_white(*(void **)(font + 0x438)) ? "white" : "coloured");
}

/* The text to show for a controller button: one icon character, or NULL. */
const char *icons_label(int phys, int style)
{
    static char s[2];
    if (!g_enabled || !g_ready || phys < 0 || phys >= NUM_ICONS || style < 0 || style > 2) return NULL;
    for (int i = 0; i < g_nfonts; i++)
        if (g_fonts[i].font && g_fonts[i].style != style) paint(&g_fonts[i], style);   /* the controller changed brand */
    s[0] = (char)(ICON_CODE0 + phys); s[1] = 0;
    return s;
}

/* The game is about to free a font: forget it. */
static void __cdecl on_font_free(BYTE *font)
{
    for (int i = 0; i < MAX_FONT_TEX; i++) if (g_fontObj[i] == font) { g_fontObj[i] = NULL; g_fontTex[i] = NULL; }
    for (int i = 0; i < g_nfonts; i++)
        if (g_fonts[i].font == font) { g_fonts[i].font = NULL; dlogf_("icons: font %p freed", (void *)font); }
}

void icons_patch(void)
{
    static const BYTE sig[9] = { 0x5F, 0x8B, 0xC6, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC3 };
    if (!g_enabled) return;
    if (!readable((void *)ADDR_FONT_EPILOGUE, sizeof sig) || memcmp((void *)ADDR_FONT_EPILOGUE, sig, sizeof sig)) {
        logf_("icons: font loader not recognised; hints keep button names");
        return;
    }
    BYTE *t = (BYTE *)code_alloc(32);
    if (!t) return;
    BYTE *p = t;
    memcpy(p, sig, 5); p += 5;                       /* pop edi; mov eax,esi; pop esi; pop ebx */
    *p++ = 0x60;                                     /* pushad */
    *p++ = 0x50;                                     /* push eax (the font) */
    *p++ = 0xE8; *(LONG *)p = (LONG)((DWORD_PTR)&on_font_loaded - ((DWORD_PTR)p + 4)); p += 4;
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;           /* add esp,4 */
    *p++ = 0x61;                                     /* popad */
    *p++ = 0xE9; *(LONG *)p = (LONG)(ADDR_FONT_RETURN - ((DWORD_PTR)p + 4));
    BYTE jmp[5] = { 0xE9 };
    *(LONG *)(jmp + 1) = (LONG)((DWORD_PTR)t - (ADDR_FONT_EPILOGUE + 5));
    /* the free routine must be hooked too, or a repaint could reach a freed texture */
    static const BYTE sigFree[5] = { 0x56, 0x8B, 0x74, 0x24, 0x08 };
    if (!readable((void *)ADDR_FONT_FREE, 5) || memcmp((void *)ADDR_FONT_FREE, sigFree, 5)) {
        logf_("icons: font release code not recognised; hints keep button names");
        return;
    }
    BYTE *f = (BYTE *)code_alloc(32);
    if (!f) return;
    p = f;
    *p++ = 0xFF; *p++ = 0x74; *p++ = 0x24; *p++ = 0x04;   /* push [esp+4] (the font) */
    *p++ = 0xE8; *(LONG *)p = (LONG)((DWORD_PTR)&on_font_free - ((DWORD_PTR)p + 4)); p += 4;
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;                 /* add esp,4 */
    memcpy(p, sigFree, 5); p += 5;                         /* original first instructions */
    *p++ = 0xE9; *(LONG *)p = (LONG)((ADDR_FONT_FREE + 5) - ((DWORD_PTR)p + 4));
    BYTE jf[5] = { 0xE9 };
    *(LONG *)(jf + 1) = (LONG)((DWORD_PTR)f - (ADDR_FONT_FREE + 5));
    if (!write_code((void *)ADDR_FONT_FREE, jf, 5)) return;
    if (write_code((void *)ADDR_FONT_EPILOGUE, jmp, 5)) logf_("icons: button pictures ready for on-screen hints");
}
