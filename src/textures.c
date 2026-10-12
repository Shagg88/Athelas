/*
 * ROTK Fix - texture dump and replacement
 *
 * Every game texture is recognised by a fingerprint of its contents (size,
 * format and pixels of the full-size level), the first time the game binds
 * it for drawing.
 *
 *  - Dump ([Textures] Dump=1): each new texture is saved once as
 *      ROTKFix\textures\dump\<w>x<h>_<fingerprint>.png
 *    (PNG encoding on a background thread, so play does not stall).
 *  - Replace ([Textures] Replace=1, default): a PNG anywhere under
 *      ROTKFix\textures\replace\  (subfolders allowed)
 *    whose name ends in the same 16-digit fingerprint is drawn instead of
 *    that texture. Any size; mipmaps are made from it. Names may carry a
 *    prefix, e.g. "hud_health_256x64_0123456789abcdef.png".
 *
 * Skipped: render targets and screen copies, textures the game rewrites
 * (movie frames: changed more than 3 times), text (font textures), and
 * formats without a straightforward picture (palettes, bump maps).
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
typedef struct { unsigned long long hash; char *path; } Repl;
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
        if (n < 20 || _stricmp(fd.cFileName + n - 4, ".png")) continue;
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
        g_repl[g_nrepl].path = _strdup(full);
        if (g_repl[g_nrepl].path) g_nrepl++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
static int cmp_repl(const void *a, const void *b)
{
    unsigned long long x = ((const Repl *)a)->hash, y = ((const Repl *)b)->hash;
    return (x > y) - (x < y);
}
static const char *find_repl(unsigned long long h)
{
    int lo = 0, hi = g_nrepl - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_repl[mid].hash == h) return g_repl[mid].path;
        if (g_repl[mid].hash < h) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* ---- per-texture records (open addressing, keyed by texture pointer) ---- */
enum { ST_NEW, ST_DONE, ST_SKIP };
typedef struct { void *tex; IDirect3DTexture8 *repl; unsigned long long hash; BYTE state, unlocks; } Rec;
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
static IDirect3DTexture8 *make_replacement(IDirect3DDevice8 *dev, const char *path)
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

static void drop_replacement(Rec *r)
{
    if (r->repl) { IDirect3DTexture8 *t = r->repl; r->repl = NULL; ((Rel_t)(*(void ***)t)[2])((IUnknown *)t); }
}

/* ---- hooks ---- */
static HRESULT WINAPI hook_TexUnlock(IDirect3DTexture8 *t, UINT level)
{
    HRESULT hr = g_realUnlock(t, level);
    Rec *r = level == 0 ? rec_find(t) : NULL;
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
            logf_("textures: %d replacement picture(s) in ROTKFix\\textures\\replace", g_nrepl);
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
    (void)dev;
    if (!g_started) start();
    if (!g_dump && !g_nrepl) return;
    g_createTex = (CreateTex_t)create;
    void **vt = *(void ***)tex;
    g_realLock = (TexLock_t)vt[16];
    patch_slot(vt, 17, (void *)hook_TexUnlock, (void **)&g_realUnlock);
}

/* graphics.c: the game is binding texture t; returns what to bind instead.
 * skip = render target / screen copy / font texture. */
void *textures_bind(void *dev, void *t, int skip)
{
    if (!t || (!g_dump && !g_nrepl) || !g_realUnlock || !g_createTex) return t;
    Rec *r = rec_find(t);
    if (r && r->state == ST_DONE) return r->repl ? (void *)r->repl : t;
    if (r && r->state == ST_SKIP) return t;
    if (!r) {
        r = rec_add(t);
        if (!r) return t;
        if (skip || ((GetType_t)(*(void ***)t)[10])((IDirect3DBaseTexture8 *)t) != D3DRTYPE_TEXTURE) { r->state = ST_SKIP; return t; }
    }
    IDirect3DTexture8 *tx = (IDirect3DTexture8 *)t;
    D3DSURFACE_DESC d;
    int bpp, blk;
    if (FAILED(((GetLevelDesc_t)(*(void ***)t)[14])(tx, 0, &d)) || d.Pool == D3DPOOL_DEFAULT ||
        (d.Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC)) || !fmt_block(d.Format, &bpp, &blk) ||
        d.Width < 4 || d.Height < 4) {
        r->state = ST_SKIP;
        return t;
    }
    D3DLOCKED_RECT lr;
    if (FAILED(g_realLock(tx, 0, &lr, NULL, D3DLOCK_READONLY)) || !lr.pBits) { r->state = ST_SKIP; return t; }
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
    BYTE *rgba = g_dump ? to_rgba((const BYTE *)lr.pBits, lr.Pitch, d.Width, d.Height, d.Format) : NULL;
    g_realUnlock(tx, 0);
    if (rgba) queue_dump(rgba, d.Width, d.Height, h);
    r->state = ST_DONE;
    const char *path = g_nrepl ? find_repl(h) : NULL;
    if (path) {
        r->repl = make_replacement((IDirect3DDevice8 *)dev, path);
        static int logged;
        if (r->repl && logged++ < 20) logf_("textures: replaced %ux%u_%016llx", d.Width, d.Height, h);
    }
    return r->repl ? (void *)r->repl : t;
}

/* graphics.c: texture t was freed */
void textures_on_release(void *t)
{
    if (!g_used) return;
    Rec *r = rec_find(t);
    if (!r) return;
    IDirect3DTexture8 *repl = r->repl;
    rec_remove(r);
    if (repl) ((Rel_t)(*(void ***)repl)[2])((IUnknown *)repl);
}
