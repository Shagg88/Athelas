/*
 * ROTK Fix - texture dump and replacement
 *
 * Every game texture is recognised by a fingerprint of its contents (size,
 * format and pixels of the full-size level), taken as the game loads it (so
 * loading a level saves all of that level's textures, and replacements are
 * made during the loading screen rather than in the middle of play).
 *
 *  - Dump ([Textures] Dump=1): each new texture is saved once as
 *      ROTKFix\textures\dump\<w>x<h>_<fingerprint>.png
 *    (PNG encoding on a background thread, so play does not stall).
 *  - Replace ([Textures] Replace=1, default): a PNG anywhere under
 *      ROTKFix\textures\replace\  (subfolders allowed)
 *    whose name ends in the same 16-digit fingerprint is drawn instead of
 *    that texture. Any size; mipmaps are made from it. Names may carry a
 *    prefix, e.g. "hud_health_256x64_0123456789abcdef.png".
 *    A DDS file of the same name (.dds, next to or instead of the PNG) is
 *    used first: DXT1, DXT3, DXT5 or A8R8G8B8, with its own mipmaps. It is
 *    copied to the graphics card as it is, with no decoding, so it loads
 *    several times faster than a PNG. If the card cannot use its format,
 *    the PNG is used.
 *    Replacements whose game texture is freed (level change, menu) are kept
 *    for a while (up to 96 MB), so going back does not make them again.
 *
 * Skipped: render targets and screen copies, textures the game rewrites
 * (movie frames: changed more than 3 times), and formats without a
 * straightforward picture (palettes, bump maps).
 *
 * Fonts (#72): a font texture's replacement is used only when it is exactly
 * 2 or 4 times the game's texture (the whole power-of-two texture, as the
 * dump saves it) and A8R8G8B8 (a PNG, or an uncompressed DDS): the glyph
 * records hold fractions of the texture, so they fit any exact multiple, and
 * icons.c paints the button pictures into the replacement at that scale
 * (textures_font_hd_lock). Any other replacement of a font is left out, as
 * all of them were before.
 * ROTK Fix itself ships no game art; packs are made by players from their
 * own copy.
 *
 * PNG: stb_image / stb_image_write by Sean Barrett (public domain / MIT).
 */
#define CINTERFACE
#include "rotkfix.h"
#include <d3d8.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#include "third_party/stb_image.h"
#pragma GCC diagnostic pop
int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);   /* shot.c */

typedef HRESULT (WINAPI *CreateTex_t)(IDirect3DDevice8 *, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture8 **);
typedef HRESULT (WINAPI *TexLock_t)(IDirect3DTexture8 *, UINT, D3DLOCKED_RECT *, const RECT *, DWORD);
typedef HRESULT (WINAPI *TexUnlock_t)(IDirect3DTexture8 *, UINT);
typedef HRESULT (WINAPI *GetLevelDesc_t)(IDirect3DTexture8 *, UINT, D3DSURFACE_DESC *);
typedef D3DRESOURCETYPE (WINAPI *GetType_t)(IDirect3DBaseTexture8 *);
typedef ULONG (WINAPI *Rel_t)(IUnknown *);

static int g_dump, g_replace = 1;
static CreateTex_t g_createTex;          /* the device's real CreateTexture (set by graphics.c) */
static TexLock_t   g_realLock;
static TexUnlock_t g_realUnlock;
static char g_dumpDir[MAX_PATH], g_replDir[MAX_PATH];

void textures_load_settings(const char *ini)
{
    g_dump = GetPrivateProfileIntA("Textures", "Dump", 0, ini) != 0;
    g_replace = GetPrivateProfileIntA("Textures", "Replace", 1, ini) != 0;
}

/* ---- replacement files: fingerprint -> path ---- */
/* path is the file name without ".png"/".dds"; png/dds say which exist */
typedef struct { unsigned long long hash; char *path; BYTE png, dds; } Repl;
static Repl *g_repl;
static int g_nrepl, g_caprepl;

static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static void scan_dir(const char *dir, int depth)
{
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        char full[MAX_PATH];
        snprintf(full, sizeof full, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (depth < 6) scan_dir(full, depth + 1); continue; }
        size_t n = strlen(fd.cFileName);
        int isdds = n >= 20 && !_stricmp(fd.cFileName + n - 4, ".dds");
        if (n < 20 || (!isdds && _stricmp(fd.cFileName + n - 4, ".png"))) continue;
        const char *hx = fd.cFileName + n - 4 - 16;
        unsigned long long v = 0; int ok = 1;
        for (int i = 0; i < 16 && ok; i++) { int d = hexval(hx[i]); if (d < 0) ok = 0; else v = v << 4 | (unsigned)d; }
        if (!ok) continue;
        if (g_nrepl == g_caprepl) {
            int cap = g_caprepl ? g_caprepl * 2 : 256;
            Repl *r = (Repl *)realloc(g_repl, cap * sizeof *r);
            if (!r) break;
            g_repl = r; g_caprepl = cap;
        }
        g_repl[g_nrepl].hash = v;
        g_repl[g_nrepl].png = (BYTE)!isdds; g_repl[g_nrepl].dds = (BYTE)isdds;
        full[strlen(full) - 4] = 0;
        g_repl[g_nrepl].path = _strdup(full);
        if (g_repl[g_nrepl].path) g_nrepl++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
static int cmp_repl(const void *a, const void *b)
{
    unsigned long long x = ((const Repl *)a)->hash, y = ((const Repl *)b)->hash;
    if (x != y) return (x > y) - (x < y);
    return _stricmp(((const Repl *)a)->path, ((const Repl *)b)->path);
}
/* After sorting: a PNG and a DDS of the same name become one entry. Two
 * different names with the same fingerprint: the first (by name) is used. */
static void merge_repl(void)
{
    int o = 0;
    for (int i = 0; i < g_nrepl; i++) {
        if (o && g_repl[o - 1].hash == g_repl[i].hash) {
            if (!_stricmp(g_repl[o - 1].path, g_repl[i].path)) { g_repl[o - 1].png |= g_repl[i].png; g_repl[o - 1].dds |= g_repl[i].dds; }
            free(g_repl[i].path);
            continue;
        }
        g_repl[o++] = g_repl[i];
    }
    g_nrepl = o;
}
static const Repl *find_repl(unsigned long long h)
{
    int lo = 0, hi = g_nrepl - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_repl[mid].hash == h) return &g_repl[mid];
        if (g_repl[mid].hash < h) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* ---- per-texture records (open addressing, keyed by texture pointer) ---- */
enum { ST_NEW, ST_HASHED, ST_DONE, ST_SKIP };   /* HASHED: fingerprinted at load, replacement not looked up yet */
/* fontk: a font texture drawn from its replacement, at that scale (2 or 4); 0 = not one */
typedef struct { void *tex; IDirect3DTexture8 *repl; unsigned long long hash; WORD w, h; BYTE state, unlocks, managed, fontk; } Rec;
#define TAB_BITS 15
#define TAB_SIZE (1u << TAB_BITS)
static Rec g_tab[TAB_SIZE];
static int g_used;

static unsigned slot_of(const void *p) { return (unsigned)(((DWORD_PTR)p >> 4) * 2654435761u) >> (32 - TAB_BITS); }
static Rec *rec_find(const void *t)
{
    for (unsigned i = slot_of(t), n = 0; n < TAB_SIZE; i = (i + 1) & (TAB_SIZE - 1), n++) {
        if (g_tab[i].tex == t) return &g_tab[i];
        if (!g_tab[i].tex) return NULL;
    }
    return NULL;
}
static Rec *rec_add(void *t)
{
    if (g_used > (int)(TAB_SIZE * 3 / 4)) return NULL;   /* full: leave further textures alone */
    unsigned i = slot_of(t);
    while (g_tab[i].tex) i = (i + 1) & (TAB_SIZE - 1);
    memset(&g_tab[i], 0, sizeof g_tab[i]);
    g_tab[i].tex = t;
    g_used++;
    return &g_tab[i];
}
static void rec_remove(Rec *r)
{
    /* backward-shift deletion keeps the probe chains intact */
    unsigned i = (unsigned)(r - g_tab);
    g_tab[i].tex = NULL; g_used--;
    for (unsigned j = (i + 1) & (TAB_SIZE - 1); g_tab[j].tex; j = (j + 1) & (TAB_SIZE - 1)) {
        unsigned k = slot_of(g_tab[j].tex);
        int move = (j > i) ? (k <= i || k > j) : (k <= i && k > j);
        if (move) { g_tab[i] = g_tab[j]; g_tab[j].tex = NULL; i = j; }
    }
}

/* ---- formats ---- */
#define FOURCC(a, b, c, d) ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))
static int fmt_block(D3DFORMAT f, int *bpp, int *blk)   /* bytes per pixel, or bytes per 4x4 block */
{
    *bpp = 0; *blk = 0;
    switch ((DWORD)f) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: *bpp = 4; return 1;
    case D3DFMT_R8G8B8: *bpp = 3; return 1;
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
    case D3DFMT_A8L8: *bpp = 2; return 1;
    case D3DFMT_A8: case D3DFMT_L8: *bpp = 1; return 1;
    }
    if ((DWORD)f == FOURCC('D','X','T','1')) { *blk = 8; return 1; }
    if ((DWORD)f == FOURCC('D','X','T','2') || (DWORD)f == FOURCC('D','X','T','3') ||
        (DWORD)f == FOURCC('D','X','T','4') || (DWORD)f == FOURCC('D','X','T','5')) { *blk = 16; return 1; }
    return 0;
}

static void c565(WORD c, BYTE *o) { o[0] = (BYTE)((c >> 11 & 31) * 255 / 31); o[1] = (BYTE)((c >> 5 & 63) * 255 / 63); o[2] = (BYTE)((c & 31) * 255 / 31); }

static void dxt_color(const BYTE *b, BYTE out[16][4], int dxt1)
{
    WORD c0 = (WORD)(b[0] | b[1] << 8), c1 = (WORD)(b[2] | b[3] << 8);
    BYTE p[4][4];
    c565(c0, p[0]); c565(c1, p[1]); p[0][3] = p[1][3] = 255;
    if (c0 > c1 || !dxt1) {
        for (int k = 0; k < 3; k++) { p[2][k] = (BYTE)((2 * p[0][k] + p[1][k]) / 3); p[3][k] = (BYTE)((p[0][k] + 2 * p[1][k]) / 3); }
        p[2][3] = p[3][3] = 255;
    } else {
        for (int k = 0; k < 3; k++) { p[2][k] = (BYTE)((p[0][k] + p[1][k]) / 2); p[3][k] = 0; }
        p[2][3] = 255; p[3][3] = 0;
    }
    DWORD idx = (DWORD)b[4] | (DWORD)b[5] << 8 | (DWORD)b[6] << 16 | (DWORD)b[7] << 24;
    for (int i = 0; i < 16; i++) memcpy(out[i], p[idx >> (2 * i) & 3], 4);
}

/* Full-size level as RGBA (R,G,B,A bytes). NULL if the format is not handled. */
static BYTE *to_rgba(const BYTE *src, int pitch, UINT w, UINT h, D3DFORMAT f)
{
    int bpp, blk;
    if (!fmt_block(f, &bpp, &blk)) return NULL;
    BYTE *o = (BYTE *)malloc((size_t)w * h * 4);
    if (!o) return NULL;
    if (blk) {
        DWORD fc = (DWORD)f;
        for (UINT by = 0; by < (h + 3) / 4; by++)
            for (UINT bx = 0; bx < (w + 3) / 4; bx++) {
                const BYTE *b = src + by * pitch + bx * blk;
                BYTE px[16][4];
                if (blk == 8) dxt_color(b, px, 1);
                else {
                    dxt_color(b + 8, px, 0);
                    if (fc == FOURCC('D','X','T','2') || fc == FOURCC('D','X','T','3')) {
                        for (int i = 0; i < 16; i++) { int a = b[i / 2] >> (4 * (i & 1)) & 15; px[i][3] = (BYTE)(a * 17); }
                    } else {
                        BYTE a[8]; a[0] = b[0]; a[1] = b[1];
                        if (a[0] > a[1]) for (int i = 1; i < 7; i++) a[i + 1] = (BYTE)(((7 - i) * a[0] + i * a[1]) / 7);
                        else { for (int i = 1; i < 5; i++) a[i + 1] = (BYTE)(((5 - i) * a[0] + i * a[1]) / 5); a[6] = 0; a[7] = 255; }
                        unsigned long long bits = 0;
                        for (int i = 0; i < 6; i++) bits |= (unsigned long long)b[2 + i] << (8 * i);
                        for (int i = 0; i < 16; i++) px[i][3] = a[bits >> (3 * i) & 7];
                    }
                }
                for (int i = 0; i < 16; i++) {
                    UINT x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                    if (x < w && y < h) memcpy(o + ((size_t)y * w + x) * 4, px[i], 4);
                }
            }
        return o;
    }
    for (UINT y = 0; y < h; y++) {
        const BYTE *s = src + (size_t)y * pitch;
        BYTE *d = o + (size_t)y * w * 4;
        for (UINT x = 0; x < w; x++, d += 4) {
            WORD v = bpp == 2 ? (WORD)(s[x * 2] | s[x * 2 + 1] << 8) : 0;
            switch ((DWORD)f) {
            case D3DFMT_A8R8G8B8: d[0] = s[x*4+2]; d[1] = s[x*4+1]; d[2] = s[x*4]; d[3] = s[x*4+3]; break;
            case D3DFMT_X8R8G8B8: d[0] = s[x*4+2]; d[1] = s[x*4+1]; d[2] = s[x*4]; d[3] = 255; break;
            case D3DFMT_R8G8B8:   d[0] = s[x*3+2]; d[1] = s[x*3+1]; d[2] = s[x*3]; d[3] = 255; break;
            case D3DFMT_R5G6B5:   c565(v, d); d[3] = 255; break;
            case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
                d[0] = (BYTE)((v >> 10 & 31) * 255 / 31); d[1] = (BYTE)((v >> 5 & 31) * 255 / 31); d[2] = (BYTE)((v & 31) * 255 / 31);
                d[3] = (DWORD)f == D3DFMT_A1R5G5B5 ? (v & 0x8000 ? 255 : 0) : 255; break;
            case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
                d[0] = (BYTE)((v >> 8 & 15) * 17); d[1] = (BYTE)((v >> 4 & 15) * 17); d[2] = (BYTE)((v & 15) * 17);
                d[3] = (DWORD)f == D3DFMT_A4R4G4B4 ? (BYTE)((v >> 12) * 17) : 255; break;
            case D3DFMT_A8L8:     d[0] = d[1] = d[2] = (BYTE)v; d[3] = (BYTE)(v >> 8); break;
            case D3DFMT_A8:       d[0] = d[1] = d[2] = 255; d[3] = s[x]; break;
            case D3DFMT_L8:       d[0] = d[1] = d[2] = s[x]; d[3] = 255; break;
            }
        }
    }
    return o;
}

/* ---- dump writer thread ---- */
typedef struct Job { struct Job *next; BYTE *rgba; UINT w, h; char path[MAX_PATH]; } Job;
static Job *g_jobs, *g_jobsTail;
static CRITICAL_SECTION g_jobLock;
static HANDLE g_jobSem;
static volatile LONG g_jobBytes;
static LONG g_dumped;

static DWORD WINAPI dump_writer(LPVOID unused)
{
    (void)unused;
    for (;;) {
        WaitForSingleObject(g_jobSem, INFINITE);
        EnterCriticalSection(&g_jobLock);
        Job *j = g_jobs;
        if (j) { g_jobs = j->next; if (!g_jobs) g_jobsTail = NULL; }
        LeaveCriticalSection(&g_jobLock);
        if (!j) continue;
        char tmp[MAX_PATH];
        snprintf(tmp, sizeof tmp, "%s.tmp", j->path);
        if (stbi_write_png(tmp, (int)j->w, (int)j->h, 4, j->rgba, (int)j->w * 4) && MoveFileExA(tmp, j->path, MOVEFILE_REPLACE_EXISTING))
        {
            LONG n = InterlockedIncrement(&g_dumped);
            if (n % 250 == 0) logf_("textures: %ld textures dumped", (long)n);
        }
        else DeleteFileA(tmp);
        InterlockedExchangeAdd(&g_jobBytes, -(LONG)(j->w * j->h * 4));
        free(j->rgba);
        free(j);
    }
    return 0;
}

static void queue_dump(BYTE *rgba, UINT w, UINT h, unsigned long long hash)
{
    Job *j = (Job *)calloc(1, sizeof *j);
    if (!j) { free(rgba); return; }
    snprintf(j->path, sizeof j->path, "%s\\%ux%u_%016llx.png", g_dumpDir, w, h, hash);
    if (GetFileAttributesA(j->path) != INVALID_FILE_ATTRIBUTES) { free(rgba); free(j); return; }   /* dumped in an earlier session */
    j->rgba = rgba; j->w = w; j->h = h;
    /* never hold more than 256 MB of pictures waiting to be written */
    while (g_jobBytes > (256 << 20)) Sleep(5);
    InterlockedExchangeAdd(&g_jobBytes, (LONG)(w * h * 4));
    EnterCriticalSection(&g_jobLock);
    if (g_jobsTail) g_jobsTail->next = j; else g_jobs = j;
    g_jobsTail = j;
    LeaveCriticalSection(&g_jobLock);
    ReleaseSemaphore(g_jobSem, 1, NULL);
}

/* ---- replacement textures ---- */
static IDirect3DDevice8 *g_dev;          /* the device replacements (and the cache) belong to */

static IDirect3DTexture8 *make_png(IDirect3DDevice8 *dev, const char *path)
{
    int w, h, n;
    BYTE *img = stbi_load(path, &w, &h, &n, 4);
    if (!img) { logf_("textures: could not read %s", path); return NULL; }
    UINT levels = 1;
    for (int m = w > h ? w : h; m > 1; m >>= 1) levels++;
    IDirect3DTexture8 *t = NULL;
    if (FAILED(g_createTex(dev, (UINT)w, (UINT)h, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t)) || !t) {
        stbi_image_free(img);
        logf_("textures: could not create a %dx%d texture for %s", w, h, path);
        return NULL;
    }
    BYTE *cur = img; int cw = w, ch = h;
    for (UINT lv = 0; lv < levels; lv++) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(g_realLock(t, lv, &lr, NULL, 0)) && lr.pBits) {
            for (int y = 0; y < ch; y++) {
                const BYTE *s = cur + (size_t)y * cw * 4;
                BYTE *d = (BYTE *)lr.pBits + (size_t)y * lr.Pitch;
                for (int x = 0; x < cw; x++) { d[x*4] = s[x*4+2]; d[x*4+1] = s[x*4+1]; d[x*4+2] = s[x*4]; d[x*4+3] = s[x*4+3]; }
            }
            g_realUnlock(t, lv);
        }
        if (lv + 1 == levels) break;
        int nw = cw > 1 ? cw / 2 : 1, nh = ch > 1 ? ch / 2 : 1;
        BYTE *nx = (BYTE *)malloc((size_t)nw * nh * 4);
        if (!nx) break;
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                for (int k = 0; k < 4; k++) {
                    int x0 = x * 2 < cw ? x * 2 : cw - 1, x1 = x * 2 + 1 < cw ? x * 2 + 1 : cw - 1;
                    int y0 = y * 2 < ch ? y * 2 : ch - 1, y1 = y * 2 + 1 < ch ? y * 2 + 1 : ch - 1;
                    nx[((size_t)y * nw + x) * 4 + k] = (BYTE)((cur[((size_t)y0 * cw + x0) * 4 + k] + cur[((size_t)y0 * cw + x1) * 4 + k] +
                                                              cur[((size_t)y1 * cw + x0) * 4 + k] + cur[((size_t)y1 * cw + x1) * 4 + k] + 2) / 4);
                }
        if (cur != img) free(cur);
        cur = nx; cw = nw; ch = nh;
    }
    if (cur != img) free(cur);
    stbi_image_free(img);
    return t;
}

/* Can the card use format f for an ordinary texture? Asked once per format
 * (per device). Real D3D8 cards and DXVK all take DXT1/3/5 and A8R8G8B8,
 * but a pack should not turn into black squares on one that does not. */
static int format_ok(IDirect3DDevice8 *dev, D3DFORMAT f)
{
    static IDirect3DDevice8 *askedDev;
    static DWORD asked[8]; static BYTE answer[8]; static int nasked;
    if (askedDev != dev) { askedDev = dev; nasked = 0; }
    for (int i = 0; i < nasked; i++) if (asked[i] == (DWORD)f) return answer[i];
    int ok = 0;
    IDirect3D8 *d3d = NULL;
    D3DDEVICE_CREATION_PARAMETERS cp;
    D3DDISPLAYMODE dm;
    if (SUCCEEDED(IDirect3DDevice8_GetDirect3D(dev, &d3d)) && d3d) {
        if (SUCCEEDED(IDirect3DDevice8_GetCreationParameters(dev, &cp)) &&
            SUCCEEDED(IDirect3D8_GetAdapterDisplayMode(d3d, cp.AdapterOrdinal, &dm)))
            ok = SUCCEEDED(IDirect3D8_CheckDeviceFormat(d3d, cp.AdapterOrdinal, cp.DeviceType, dm.Format, 0, D3DRTYPE_TEXTURE, f));
        IDirect3D8_Release(d3d);
    }
    if (nasked < 8) { asked[nasked] = (DWORD)f; answer[nasked] = (BYTE)ok; nasked++; }
    if (!ok) logf_("textures: the graphics card cannot use DDS format %#lx; the PNG is used instead", (unsigned long)f);
    return ok;
}

/* A DDS file already holds the picture the way the card stores it, mipmaps
 * included, so each level is copied straight in. NULL (and the PNG is tried)
 * if the file is not one of the formats below or the card cannot use it. */
static DWORD rd32(const BYTE *p) { return (DWORD)p[0] | (DWORD)p[1] << 8 | (DWORD)p[2] << 16 | (DWORD)p[3] << 24; }
static IDirect3DTexture8 *make_dds(IDirect3DDevice8 *dev, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { logf_("textures: could not read %s", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    BYTE *buf = size > 128 && size < (64L << 20) ? (BYTE *)malloc((size_t)size) : NULL;
    int got = buf && fread(buf, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!got) { free(buf); logf_("textures: could not read %s", path); return NULL; }

    /* header: "DDS ", then 124 bytes; the pixel format block starts at 76 */
    const char *why = NULL;
    UINT w = rd32(buf + 16), h = rd32(buf + 12), mips = rd32(buf + 28);
    DWORD pfFlags = rd32(buf + 80), fourcc = rd32(buf + 84);
    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    int blk = 0;
    if (memcmp(buf, "DDS ", 4) || rd32(buf + 4) != 124) why = "not a DDS file";
    else if (rd32(buf + 112) & 0x200) why = "cube maps are not used";              /* DDSCAPS2_CUBEMAP */
    else if (pfFlags & 4) {                                                         /* DDPF_FOURCC */
        if (fourcc == FOURCC('D','X','T','1')) blk = 8;
        else if (fourcc == FOURCC('D','X','T','3') || fourcc == FOURCC('D','X','T','5')) blk = 16;
        else why = "unsupported format (use DXT1, DXT3, DXT5 or A8R8G8B8)";
        fmt = (D3DFORMAT)fourcc;
    } else if ((pfFlags & 0x40) && (pfFlags & 1) && rd32(buf + 88) == 32 &&         /* DDPF_RGB | DDPF_ALPHAPIXELS */
               rd32(buf + 92) == 0xff0000 && rd32(buf + 96) == 0xff00 && rd32(buf + 100) == 0xff && rd32(buf + 104) == 0xff000000u)
        fmt = D3DFMT_A8R8G8B8;
    else why = "unsupported format (use DXT1, DXT3, DXT5 or A8R8G8B8)";
    if (!why && (w < 1 || h < 1 || w > 4096 || h > 4096)) why = "bad size";
    /* DXT textures are made of 4x4 blocks; cards want the full size to be whole blocks */
    if (!why && blk && ((w & 3) || (h & 3))) why = "DXT size must be a multiple of 4";
    if (why) { free(buf); logf_("textures: %s: %s; trying the PNG", path, why); return NULL; }
    if (!format_ok(dev, fmt)) { free(buf); return NULL; }

    /* levels: as many as the file says (DDSD_MIPMAPCOUNT), no more than a full chain */
    UINT full = 1;
    for (UINT m = w > h ? w : h; m > 1; m >>= 1) full++;
    UINT levels = (rd32(buf + 8) & 0x20000) && mips ? mips : 1;
    if (levels > full) levels = full;
    /* a short file: keep the levels it really holds */
    size_t off = 128, need = off;
    UINT have = 0;
    for (UINT lv = 0, lw = w, lh = h; lv < levels; lv++) {
        size_t bytes = blk ? (size_t)((lw + 3) / 4) * ((lh + 3) / 4) * blk : (size_t)lw * lh * 4;
        if (need + bytes > (size_t)size) break;
        need += bytes; have++;
        lw = lw > 1 ? lw / 2 : 1; lh = lh > 1 ? lh / 2 : 1;
    }
    if (!have) { free(buf); logf_("textures: %s is cut short; trying the PNG", path); return NULL; }
    levels = have;

    IDirect3DTexture8 *t = NULL;
    if (FAILED(g_createTex(dev, w, h, levels, 0, fmt, D3DPOOL_MANAGED, &t)) || !t) {
        free(buf);
        logf_("textures: could not create a %ux%u texture for %s; trying the PNG", w, h, path);
        return NULL;
    }
    for (UINT lv = 0, lw = w, lh = h; lv < levels; lv++) {
        UINT rows = blk ? (lh + 3) / 4 : lh;
        size_t rowBytes = blk ? (size_t)((lw + 3) / 4) * blk : (size_t)lw * 4;
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(g_realLock(t, lv, &lr, NULL, 0)) && lr.pBits) {
            for (UINT y = 0; y < rows; y++) memcpy((BYTE *)lr.pBits + (size_t)y * lr.Pitch, buf + off + y * rowBytes, rowBytes);
            g_realUnlock(t, lv);
        }
        off += rowBytes * rows;
        lw = lw > 1 ? lw / 2 : 1; lh = lh > 1 ? lh / 2 : 1;
    }
    free(buf);
    return t;
}

static void release_tex(IDirect3DTexture8 *t) { ((Rel_t)(*(void ***)t)[2])((IUnknown *)t); }

/* ---- replacements kept after their game texture is freed ----
 * The game frees and reloads its textures on every level change and menu
 * visit. Keeping the most recently freed replacements (by fingerprint) means
 * coming back to a place does not read and build them again. The game runs
 * in 32 bits, and a managed texture also keeps a copy in the game's own
 * memory, so the total is capped well below what would hurt. */
#define CACHE_MAX   256
#define CACHE_BYTES (96u << 20)
typedef struct { unsigned long long hash; IDirect3DTexture8 *tex; DWORD bytes, used; } Cached;
static Cached g_cache[CACHE_MAX];
static int g_ncache;
static DWORD g_cacheBytes, g_cacheTick;

static DWORD tex_bytes(IDirect3DTexture8 *t)
{
    D3DSURFACE_DESC d;
    if (FAILED(((GetLevelDesc_t)(*(void ***)t)[14])(t, 0, &d))) return 1u << 20;
    return d.Size + d.Size / 3;   /* the mipmaps add about a third */
}
static void cache_drop(int i)
{
    release_tex(g_cache[i].tex);
    g_cacheBytes -= g_cache[i].bytes;
    g_cache[i] = g_cache[--g_ncache];
}
static void cache_flush(void) { while (g_ncache) cache_drop(g_ncache - 1); }
static void cache_put(unsigned long long hash, IDirect3DTexture8 *t)
{
    DWORD bytes = tex_bytes(t);
    if (bytes > CACHE_BYTES / 4) { release_tex(t); return; }   /* one huge picture would push out everything else */
    while (g_ncache && (g_ncache == CACHE_MAX || g_cacheBytes + bytes > CACHE_BYTES)) {
        int old = 0;   /* the one unused the longest goes first */
        for (int i = 1; i < g_ncache; i++) if (g_cache[i].used - g_cacheTick < g_cache[old].used - g_cacheTick) old = i;
        cache_drop(old);
    }
    g_cache[g_ncache].hash = hash; g_cache[g_ncache].tex = t;
    g_cache[g_ncache].bytes = bytes; g_cache[g_ncache].used = ++g_cacheTick;
    g_ncache++;
    g_cacheBytes += bytes;
}
static IDirect3DTexture8 *cache_take(unsigned long long hash)
{
    for (int i = 0; i < g_ncache; i++)
        if (g_cache[i].hash == hash) {
            IDirect3DTexture8 *t = g_cache[i].tex;
            g_cacheBytes -= g_cache[i].bytes;
            g_cache[i] = g_cache[--g_ncache];
            return t;
        }
    return NULL;
}

/* The replacement for fingerprint r->hash: kept from before, the DDS file,
 * or the PNG file, in that order. */
static void make_replacement(Rec *r, IDirect3DDevice8 *dev)
{
    const Repl *e = g_nrepl ? find_repl(r->hash) : NULL;
    if (!e) return;
    r->repl = cache_take(r->hash);
    int fromDds = 0;
    if (!r->repl) {
        char file[MAX_PATH];
        if (e->dds) { snprintf(file, sizeof file, "%s.dds", e->path); r->repl = make_dds(dev, file); fromDds = r->repl != NULL; }
        if (!r->repl && e->png) { snprintf(file, sizeof file, "%s.png", e->path); r->repl = make_png(dev, file); }
    }
    static int logged;
    if (r->repl && logged++ < 20) logf_("textures: replaced %ux%u_%016llx%s", r->w, r->h, r->hash, fromDds ? " (DDS)" : "");
}

/* the game texture's contents changed or it was freed: keep its replacement for later */
static void drop_replacement(Rec *r)
{
    if (r->repl) { IDirect3DTexture8 *t = r->repl; r->repl = NULL; cache_put(r->hash, t); }
}

/* ---- hooks ---- */
static int examine(Rec *r, IDirect3DTexture8 *tx);
static HRESULT WINAPI hook_TexUnlock(IDirect3DTexture8 *t, UINT level)
{
    HRESULT hr = g_realUnlock(t, level);
    Rec *r = level == 0 ? rec_find(t) : NULL;
    if (!r && level == 0) {
        /* the first time the game fills a texture: fingerprint it now, as
         * the level loads. Dumping saves it; with a pack, its replacement is
         * made now too, during the loading screen, instead of stalling play
         * the first time it is drawn. Only for managed textures, the ones
         * the game draws with (its other textures are just staging copies). */
        r = rec_add(t);
        if (r && examine(r, t) && g_nrepl && r->managed && g_dev && g_createTex) {
            make_replacement(r, g_dev);
            r->state = ST_DONE;
        }
        return hr;
    }
    /* a font drawn from its replacement: its later changes are ROTK Fix's own button pictures (icons.c), painted
     * into the replacement too */
    if (r && r->fontk) return hr;
    if (r && r->state != ST_SKIP) {
        if (r->state == ST_DONE) drop_replacement(r);
        /* changed after use: look again next time; changed over and over
         * (movie frames): leave it alone */
        r->state = ++r->unlocks > 3 ? ST_SKIP : ST_NEW;
    }
    return hr;
}

static void patch_slot(void **vt, int idx, void *hook, void **orig)
{
    if (vt[idx] == hook) return;
    if (*orig && *orig != vt[idx]) return;
    DWORD old;
    if (!VirtualProtect(&vt[idx], sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) return;
    *orig = vt[idx];
    vt[idx] = hook;
    VirtualProtect(&vt[idx], sizeof(void *), old, &old);
}

static int g_started;
static void start(void)
{
    g_started = 1;
    if (g_replace) {
        snprintf(g_replDir, sizeof g_replDir, "%s\\ROTKFix\\textures\\replace", g_dir);
        scan_dir(g_replDir, 0);
        if (g_nrepl) {
            qsort(g_repl, g_nrepl, sizeof *g_repl, cmp_repl);
            merge_repl();
            int ndds = 0;
            for (int i = 0; i < g_nrepl; i++) ndds += g_repl[i].dds;
            logf_("textures: %d replacement picture(s) in ROTKFix\\textures\\replace (%d as DDS)", g_nrepl, ndds);
        }
    }
    if (g_dump) {
        char d[MAX_PATH];
        snprintf(d, sizeof d, "%s\\ROTKFix\\textures", g_dir); CreateDirectoryA(d, NULL);
        snprintf(g_dumpDir, sizeof g_dumpDir, "%s\\ROTKFix\\textures\\dump", g_dir); CreateDirectoryA(g_dumpDir, NULL);
        InitializeCriticalSection(&g_jobLock);
        g_jobSem = CreateSemaphoreA(NULL, 0, 0x7fffffff, NULL);
        HANDLE th = g_jobSem ? CreateThread(NULL, 0, dump_writer, NULL, 0, NULL) : NULL;
        if (th) { SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(th); logf_("textures: dumping new textures to ROTKFix\\textures\\dump"); }
        else g_dump = 0;
    }
}

/* graphics.c: a texture was created (hook the shared texture vtable once) */
void textures_on_create(void *dev, void *create, void *tex)
{
    if (!g_started) start();
    if (!g_dump && !g_nrepl) return;
    /* kept replacements belong to the device that made them */
    if (g_dev != (IDirect3DDevice8 *)dev) { cache_flush(); g_dev = (IDirect3DDevice8 *)dev; }
    g_createTex = (CreateTex_t)create;
    void **vt = *(void ***)tex;
    g_realLock = (TexLock_t)vt[16];
    patch_slot(vt, 17, (void *)hook_TexUnlock, (void **)&g_realUnlock);
}

/* Fingerprint (and, when dumping, save) a texture's full-size level.
 * Returns 0 and marks it skipped if it is not a plain picture. */
static int examine(Rec *r, IDirect3DTexture8 *tx)
{
    D3DSURFACE_DESC d;
    int bpp, blk;
    if (((GetType_t)(*(void ***)tx)[10])((IDirect3DBaseTexture8 *)tx) != D3DRTYPE_TEXTURE ||
        FAILED(((GetLevelDesc_t)(*(void ***)tx)[14])(tx, 0, &d)) || d.Pool == D3DPOOL_DEFAULT ||
        (d.Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC)) || !fmt_block(d.Format, &bpp, &blk) ||
        d.Width < 4 || d.Height < 4) {
        r->state = ST_SKIP;
        return 0;
    }
    D3DLOCKED_RECT lr;
    if (FAILED(g_realLock(tx, 0, &lr, NULL, D3DLOCK_READONLY)) || !lr.pBits) { r->state = ST_SKIP; return 0; }
    /* fingerprint: size, format and pixel data (row by row, without padding) */
    UINT rows = blk ? (d.Height + 3) / 4 : d.Height;
    UINT rowBytes = blk ? (d.Width + 3) / 4 * blk : d.Width * bpp;
    unsigned long long h = 1469598103934665603ull;
    h = (h ^ d.Width) * 1099511628211ull; h = (h ^ d.Height) * 1099511628211ull; h = (h ^ (DWORD)d.Format) * 1099511628211ull;
    for (UINT y = 0; y < rows; y++) {
        const BYTE *p = (const BYTE *)lr.pBits + (size_t)y * lr.Pitch;
        UINT i = 0;
        for (; i + 8 <= rowBytes; i += 8) { unsigned long long v; memcpy(&v, p + i, 8); h = (h ^ v) * 1099511628211ull; h ^= h >> 29; }
        for (; i < rowBytes; i++) h = (h ^ p[i]) * 1099511628211ull;
    }
    r->hash = h;
    r->managed = d.Pool == D3DPOOL_MANAGED;
    r->w = (WORD)d.Width; r->h = (WORD)d.Height;
    BYTE *rgba = g_dump ? to_rgba((const BYTE *)lr.pBits, lr.Pitch, d.Width, d.Height, d.Format) : NULL;
    g_realUnlock(tx, 0);
    if (rgba) queue_dump(rgba, d.Width, d.Height, h);
    r->state = ST_HASHED;
    return 1;
}

/* graphics.c: the game is binding texture t; returns what to bind instead.
 * skip = render target / screen copy / font texture. */
void *textures_bind(void *dev, void *t, int skip)
{
    if (!t || (!g_dump && !g_nrepl) || !g_realUnlock || !g_createTex) return t;
    Rec *r = rec_find(t);
    /* a font accepted by textures_font_begin: drawn from its replacement */
    if (r && r->fontk && r->repl && r->state == ST_DONE) return r->repl;
    /* a replacement made at load time for what turns out to be text (not an
     * exact 2x / 4x A8R8G8B8 one) or a screen copy: not used */
    if (r && skip && r->repl) drop_replacement(r);
    if (r && r->state == ST_DONE) return r->repl ? (void *)r->repl : t;
    if (r && r->state == ST_SKIP) return t;
    if (!r) {
        r = rec_add(t);
        if (!r) return t;
        if (skip) { r->state = ST_SKIP; return t; }
    }
    /* text or a screen copy changed since it was fingerprinted (a font with the button pictures painted in): not
     * fingerprinted again, so the dump keeps each font only as the game made it, the copy a pack replaces */
    if (r->state == ST_NEW && skip) { r->state = ST_DONE; return t; }
    if (r->state == ST_NEW && !examine(r, (IDirect3DTexture8 *)t)) return t;
    r->state = ST_DONE;
    if (!skip) make_replacement(r, (IDirect3DDevice8 *)dev);
    return r->repl ? (void *)r->repl : t;
}

/* icons.c: font texture t has just been loaded (its letters are in it, the button pictures not yet). Its replacement
 * is drawn from now on if it is exactly 2 or 4 times t, in A8R8G8B8 (the button pictures are then painted into it
 * too); returns that scale, or 0 (no replacement, or one that can't be used: it is left out, as before). */
int textures_font_begin(void *t)
{
    if (!t || !g_nrepl || !g_realUnlock || !g_createTex || !g_dev) return 0;
    Rec *r = rec_find(t);
    if (!r || r->state == ST_SKIP) return 0;
    if (r->state == ST_NEW && !examine(r, (IDirect3DTexture8 *)t)) return 0;
    if (!r->repl && r->hash) make_replacement(r, g_dev);   /* (made at load already, as a rule; or kept from before) */
    r->state = ST_DONE;
    if (!r->repl) return 0;
    D3DSURFACE_DESC d;
    UINT dw = 0, dh = 0;
    int k = 0;
    const char *why = NULL;
    if (FAILED(((GetLevelDesc_t)(*(void ***)r->repl)[14])(r->repl, 0, &d))) why = "can't be read";
    else {
        dw = d.Width; dh = d.Height;
        if (d.Format != D3DFMT_A8R8G8B8) why = "is not A8R8G8B8 (use a PNG, or an uncompressed DDS)";
        else if (dw == 2u * r->w && dh == 2u * r->h) k = 2;
        else if (dw == 4u * r->w && dh == 4u * r->h) k = 4;
        else why = "is not exactly 2 or 4 times the game's size";
    }
    /* told once per font: the game loads its fonts again with every level */
    static unsigned long long told[32];
    static int ntold;
    int seen = 0;
    for (int i = 0; i < ntold; i++) if (told[i] == r->hash) seen = 1;
    if (!seen && ntold < 32) told[ntold++] = r->hash;
    if (!k) {
        if (!seen) logf_("textures: font %ux%u_%016llx: its replacement (%ux%u) %s; the game's font is used", r->w, r->h,
                         r->hash, dw, dh, why);
        drop_replacement(r);
        return 0;
    }
    r->fontk = (BYTE)k;
    if (!seen) logf_("textures: font %ux%u_%016llx drawn from its replacement at %dx (%ux%u)", r->w, r->h, r->hash, k,
                     dw, dh);
    return k;
}

/* icons.c: lock one level of the replacement font texture t is drawn from (the real Lock: the replacement isn't one of
 * the game's textures); 0 if t has none. Its size, scale and number of levels come back too. */
int textures_font_hd_lock(const void *t, unsigned level, void **bits, int *pitch, unsigned *w, unsigned *h, int *k,
                          unsigned *levels)
{
    Rec *r = t ? rec_find(t) : NULL;
    if (!r || !r->fontk || !r->repl || !g_realLock) return 0;
    IDirect3DTexture8 *x = r->repl;
    D3DSURFACE_DESC d;
    if (FAILED(((GetLevelDesc_t)(*(void ***)x)[14])(x, level, &d))) return 0;
    D3DLOCKED_RECT lr;
    if (FAILED(g_realLock(x, level, &lr, NULL, 0)) || !lr.pBits) return 0;
    *bits = lr.pBits; *pitch = lr.Pitch; *w = d.Width; *h = d.Height; *k = r->fontk;
    *levels = IDirect3DTexture8_GetLevelCount(x);
    return 1;
}
void textures_font_hd_unlock(const void *t, unsigned level)
{
    Rec *r = t ? rec_find(t) : NULL;
    if (r && r->fontk && r->repl && g_realUnlock) g_realUnlock(r->repl, level);
}

/* graphics.c: texture t is drawn as a flat 2D picture (HUD, menus). With
 * Dump on, each one is listed once in the log by its file name, so the
 * pictures that make up the HUD are easy to find among the dumped files. */
void textures_note_2d(const void *t)
{
    if (!g_dump || !t) return;
    Rec *r = rec_find(t);
    if (!r || r->state != ST_DONE || !r->hash) return;
    static unsigned long long seen[256];
    static int n;
    for (int i = 0; i < n; i++) if (seen[i] == r->hash) return;
    if (n >= 256) return;
    seen[n++] = r->hash;
    logf_("textures: 2D picture %ux%u_%016llx.png", r->w, r->h, r->hash);
}

/* graphics.c: texture t was freed */
void textures_on_release(void *t)
{
    if (!g_used) return;
    Rec *r = rec_find(t);
    if (!r) return;
    IDirect3DTexture8 *repl = r->repl;
    unsigned long long hash = r->hash;
    rec_remove(r);
    if (repl) cache_put(hash, repl);
}
