/* ROTK Fix - achievements and the Discord status, the same ones as the Xbox port's (its rotk_achievements.cpp).
 *
 * The game's progress is the same block as on Xbox: three 0x988-byte profile slots from 0x5BCB08 (the stats getter
 * 0x47DFF0 pushes it; Xbox 0x1C4FE0), slot 2 the profile in play. In the slot, +0x244 + hero * 0x3C holds a record a
 * hero (0 Aragorn ... 8 Faramir): +0x00 levels completed alone and +0x04 in co-op, a bit a level (0 Helm's Deep ...
 * 12 Crack of Doom), +0x18 the hero's level (float). Read every second from a thread of our own.
 *
 * Enemies defeated: gameplay.c's hit hook, a player's hit that takes an enemy's health from above 0 to 0 or below.
 * The level and hero for the Discord status: the files the game opens (rotkfix.c's CreateFileA hook).
 *
 * ROTKFix\achievements.txt: [unlocked] id = date, [counters] name = n, [list] id = title | description (Athelas shows
 * the list from it). A note shows in the corner when one is earned, drawn over the finished frame on the Vulkan
 * renderer (post.c calls achieve_draw); [Achievements] Notify=0 turns it off.
 *
 * [Discord] Enabled=1 (off by default): the status through the Discord app's local pipe (discord-ipc-N), as the Xbox
 * ports do; AppId is the Discord application whose name Discord shows. */
#include "rotkfix.h"
#include <d3d9.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef ROTK_DISCORD_APP_ID
#define ROTK_DISCORD_APP_ID ""   /* the Discord application whose name Discord shows ("The Return of the King") */
#endif

typedef struct { int bit; const char *code, *name, *id; } Level;
static const Level k_levels[] = {
    { 0, "hel01", "Helm's Deep", "level_helms_deep" },
    { 6, "ise01", "Road to Isengard", "level_road_to_isengard" },
    { 9, "osg01", "Escape from Osgiliath", "level_osgiliath" },
    { 1, "pat01", "Paths of the Dead", "level_paths_of_the_dead" },
    { 2, "pat03", "King of the Dead", "level_king_of_the_dead" },
    { 3, "min02", "The Southern Gate", "level_southern_gate" },
    { 7, "min00", "Top of the Wall", "level_top_of_the_wall" },
    { 8, "min04", "The Courtyard", "level_courtyard" },
    { 4, "pel02", "Pelennor Fields", "level_pelennor_fields" },
    { 10, "she01", "Shelob's Lair", "level_shelobs_lair" },
    { 11, "cir00", "Cirith Ungol", "level_cirith_ungol" },
    { 5, "bla01", "The Black Gate", "level_black_gate" },
    { 12, "cra01", "The Crack of Doom", "level_crack_of_doom" },
};
#define LEVEL_COUNT ((int)(sizeof k_levels / sizeof k_levels[0]))
static const char *const k_heroes[9] = { "Aragorn", "Legolas", "Gimli", "Gandalf", "Sam", "Frodo", "Pippin", "Merry", "Faramir" };
static const char *const k_heroCodes[9] = { "ara", "leg", "gim", "gan", "sam", "fro", "pip", "mer", "far" };
static const char *const k_heroIds[9] = { "hero_aragorn", "hero_legolas", "hero_gimli", "hero_gandalf", "hero_sam",
                                          "hero_frodo", "hero_pippin", "hero_merry", "hero_faramir" };

typedef struct { const char *id; char title[48]; char desc[96]; } Def;
#define DEF_COUNT (LEVEL_COUNT + 9 + 7)
static Def g_defs[DEF_COUNT];
static char g_when[DEF_COUNT][24];    /* "" not earned */
static unsigned long long g_kills;
static CRITICAL_SECTION g_cs;
static int g_ready, g_notify = 1, g_discord;
static char g_appId[40];
static char g_file[MAX_PATH];
static char g_level[48], g_hero[16];   /* the Discord status */

#define SLOT 0x5BCB08u + 2u * 0x988u
static const BYTE SIG_STATS[5] = { 0x68, 0x08, 0xCB, 0x5B, 0x00 };   /* 0x47E02D push 0x5BCB08 */

static void defs_init(void)
{
    int n = 0;
    for (int i = 0; i < LEVEL_COUNT; i++, n++) {
        g_defs[n].id = k_levels[i].id;
        lstrcpynA(g_defs[n].title, k_levels[i].name, sizeof g_defs[n].title);
        if (!strncmp(k_levels[i].name, "The ", 4)) snprintf(g_defs[n].desc, sizeof g_defs[n].desc, "Complete the %s.", k_levels[i].name + 4);
        else snprintf(g_defs[n].desc, sizeof g_defs[n].desc, "Complete %s.", k_levels[i].name);
    }
    for (int h = 0; h < 9; h++, n++) {
        g_defs[n].id = k_heroIds[h];
        lstrcpynA(g_defs[n].title, k_heroes[h], sizeof g_defs[n].title);
        snprintf(g_defs[n].desc, sizeof g_defs[n].desc, "Complete a level as %s%s", k_heroes[h], h >= 6 ? " (a hero you unlock)." : ".");
    }
    static const struct { const char *id, *title, *desc; } more[7] = {
        { "coop", "Side by Side", "Complete a level in co-op." },
        { "hero_level_10", "Seasoned Hero", "Raise a hero to level 10." },
        { "every_level", "The Return of the King", "Complete every level." },
        { "every_level_every_hero", "Every Path Walked", "Complete every level with every hero." },
        { "kills_100", "A Hundred Foes", "Defeat 100 enemies." },
        { "kills_1000", "A Thousand Foes", "Defeat 1,000 enemies." },
        { "kills_10000", "The Host of Mordor", "Defeat 10,000 enemies." },
    };
    for (int i = 0; i < 7; i++, n++) {
        g_defs[n].id = more[i].id;
        lstrcpynA(g_defs[n].title, more[i].title, sizeof g_defs[n].title);
        lstrcpynA(g_defs[n].desc, more[i].desc, sizeof g_defs[n].desc);
    }
}

static int def_index(const char *id)
{
    for (int i = 0; i < DEF_COUNT; i++) if (!strcmp(g_defs[i].id, id)) return i;
    return -1;
}

/* lock held */
static void save(void)
{
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof tmp, "%s.new", g_file);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    fprintf(f, "; Achievements earned in this game (kept by ROTK Fix; Athelas shows them).\r\n[unlocked]\r\n");
    for (int i = 0; i < DEF_COUNT; i++) if (g_when[i][0]) fprintf(f, "%s = %s\r\n", g_defs[i].id, g_when[i]);
    fprintf(f, "\r\n[counters]\r\nenemies_defeated = %llu\r\n\r\n[list]\r\n", g_kills);
    for (int i = 0; i < DEF_COUNT; i++) fprintf(f, "%s = %s | %s\r\n", g_defs[i].id, g_defs[i].title, g_defs[i].desc);
    fclose(f);
    MoveFileExA(tmp, g_file, MOVEFILE_REPLACE_EXISTING);
}

static void load(void)
{
    FILE *f = fopen(g_file, "rb");
    if (!f) return;
    char line[256], sec[32] = "";
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        size_t n = strlen(s);
        while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ')) s[--n] = 0;
        if (!*s || *s == ';') continue;
        if (*s == '[') { lstrcpynA(sec, s, sizeof sec); continue; }
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = s, *v = eq + 1;
        size_t kn = strlen(k);
        while (kn && k[kn - 1] == ' ') k[--kn] = 0;
        while (*v == ' ') v++;
        if (!strcmp(sec, "[unlocked]")) { int i = def_index(k); if (i >= 0) lstrcpynA(g_when[i], v, sizeof g_when[i]); }
        else if (!strcmp(sec, "[counters]") && !strcmp(k, "enemies_defeated")) g_kills = _strtoui64(v, NULL, 10);
    }
    fclose(f);
}

/* ---- the note in the corner ---- */
typedef struct { char title[48], desc[96]; } Note;
static Note g_notes[8];
static int g_noteHead, g_noteCount;
static DWORD g_noteShown;   /* tick the front note went up, 0 not yet */

static void unlock(const char *id)
{
    if (!g_ready) return;
    EnterCriticalSection(&g_cs);
    int i = def_index(id);
    if (i >= 0 && !g_when[i][0]) {
        time_t t = time(NULL);
        strftime(g_when[i], sizeof g_when[i], "%Y-%m-%d %H:%M", localtime(&t));
        save();
        logf_("achievements: earned \"%s\" (%s)", g_defs[i].title, id);
        if (g_notify && g_noteCount < 8) {
            Note *nt = &g_notes[(g_noteHead + g_noteCount++) % 8];
            lstrcpynA(nt->title, g_defs[i].title, sizeof nt->title);
            lstrcpynA(nt->desc, g_defs[i].desc, sizeof nt->desc);
        }
    }
    LeaveCriticalSection(&g_cs);
}

void achieve_enemy_defeated(void)
{
    if (!g_ready) return;
    EnterCriticalSection(&g_cs);
    unsigned long long n = ++g_kills;
    static DWORD lastSave;
    if (GetTickCount() - lastSave > 10000) { lastSave = GetTickCount(); save(); }
    LeaveCriticalSection(&g_cs);
    if (n >= 100) unlock("kills_100");
    if (n >= 1000) unlock("kills_1000");
    if (n >= 10000) unlock("kills_10000");
}

static DWORD WINAPI progress_thread(LPVOID p)
{
    (void)p;
    for (;;) {
        Sleep(1000);
        const BYTE *slot = (const BYTE *)(DWORD_PTR)(SLOT);
        if (!readable(slot, 0x988)) continue;
        unsigned any = 0, all = 0x1FFF;
        int coop = 0;
        for (int h = 0; h < 9; h++) {
            const BYTE *r = slot + 0x244 + h * 0x3C;
            unsigned solo = *(const unsigned *)r, co = *(const unsigned *)(r + 4);
            unsigned m = (solo | co) & 0x1FFF;
            float lv = *(const float *)(r + 0x18);
            if (m) unlock(k_heroIds[h]);
            if (co & 0x1FFF) coop = 1;
            any |= m;
            all &= m;
            if (lv >= 10.0f && lv < 100.0f) unlock("hero_level_10");
        }
        for (int i = 0; i < LEVEL_COUNT; i++) if (any & (1u << k_levels[i].bit)) unlock(k_levels[i].id);
        if (coop) unlock("coop");
        if ((any & 0x1FFF) == 0x1FFF) unlock("every_level");
        if (all == 0x1FFF) unlock("every_level_every_hero");
    }
    return 0;
}

/* rotkfix.c: each file the game opens, for the Discord status */
void achieve_file(const char *n)
{
    if (!g_ready) return;
    const char *b = n;
    for (const char *q = n; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
    size_t len = strlen(b);
    if (len < 9 || len > 20) return;
    char low[24];
    for (size_t i = 0; i <= len; i++) low[i] = (char)tolower((unsigned char)b[i]);
    if (!strncmp(low, "char_", 5)) {
        for (int h = 0; h < 9; h++)
            if (!strncmp(low + 5, k_heroCodes[h], 3)) { EnterCriticalSection(&g_cs); lstrcpynA(g_hero, k_heroes[h], sizeof g_hero); LeaveCriticalSection(&g_cs); }
        return;
    }
    if (strcmp(low + len - 4, ".scw") && strcmp(low + len - 4, ".scx")) return;
    if (!strncmp(low, "globscen", 8) || !strncmp(low, "loadonce", 8)) return;
    char code[8];
    lstrcpynA(code, low, 6);
    const char *name = NULL;
    if (!strcmp(code, "fro03")) name = "";
    else if (!strcmp(code, "pat02")) name = "Paths of the Dead";
    else if (!strcmp(code, "cir01")) name = "Cirith Ungol";
    else if (!strncmp(code, "pal", 3)) name = "A secret level";
    else for (int i = 0; i < LEVEL_COUNT; i++) if (!strcmp(code, k_levels[i].code)) name = k_levels[i].name;
    if (!name) return;
    EnterCriticalSection(&g_cs);
    lstrcpynA(g_level, name, sizeof g_level);
    if (!*name) g_hero[0] = 0;
    LeaveCriticalSection(&g_cs);
}

/* ---- Discord ---- */
static void json_str(char *out, size_t n, const char *s)
{
    size_t o = 0;
    if (o < n - 1) out[o++] = '"';
    for (; *s && o < n - 3; s++) {
        if (*s == '"' || *s == '\\') out[o++] = '\\';
        out[o++] = *s;
    }
    out[o++] = '"';
    out[o] = 0;
}

static int pipe_send(HANDLE h, DWORD op, const char *json)
{
    DWORD len = (DWORD)strlen(json), w = 0;
    char buf[1024];
    if (len + 8 > sizeof buf) return 0;
    memcpy(buf, &op, 4);
    memcpy(buf + 4, &len, 4);
    memcpy(buf + 8, json, len);
    return WriteFile(h, buf, len + 8, &w, NULL) && w == len + 8;
}

static int pipe_drain(HANDLE h)
{
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) return 0;
        if (!avail) return 1;
        char buf[512];
        DWORD r = 0;
        if (!ReadFile(h, buf, avail < sizeof buf ? avail : sizeof buf, &r, NULL)) return 0;
    }
}

static DWORD WINAPI discord_thread(LPVOID p)
{
    (void)p;
    HANDLE h = INVALID_HANDLE_VALUE;
    char shown[128] = "\x01";
    long long started = (long long)time(NULL);
    int nonce = 0, told = 0;
    DWORD nextTry = 0;
    for (;;) {
        Sleep(2000);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetTickCount() < nextTry) continue;
            nextTry = GetTickCount() + 20000;
            for (int i = 0; i < 10 && h == INVALID_HANDLE_VALUE; i++) {
                char name[40];
                snprintf(name, sizeof name, "\\\\.\\pipe\\discord-ipc-%d", i);
                h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            }
            if (h == INVALID_HANDLE_VALUE) {
                if (!told++) logf_("discord: Discord isn't running, trying again now and then");
                continue;
            }
            char hs[128], id[48];
            json_str(id, sizeof id, g_appId);
            snprintf(hs, sizeof hs, "{\"v\":1,\"client_id\":%s}", id);
            if (!pipe_send(h, 0, hs)) { CloseHandle(h); h = INVALID_HANDLE_VALUE; continue; }
            Sleep(300);
            if (!pipe_drain(h)) { CloseHandle(h); h = INVALID_HANDLE_VALUE; continue; }
            logf_("discord: connected");
            shown[0] = 1; shown[1] = 0;
        }
        char level[48], hero[16];
        EnterCriticalSection(&g_cs);
        lstrcpynA(level, g_level, sizeof level);
        lstrcpynA(hero, g_hero, sizeof hero);
        LeaveCriticalSection(&g_cs);
        char key[128];
        snprintf(key, sizeof key, "%s|%s", level, hero);
        if (strcmp(key, shown)) {
            char d[96], st[48], msg[600];
            json_str(d, sizeof d, *level ? level : "In the menus");
            char as[32] = "";
            if (*level && *hero) snprintf(as, sizeof as, "As %s", hero);
            json_str(st, sizeof st, as);
            snprintf(msg, sizeof msg,
                     "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%lu,\"activity\":{\"details\":%s%s%s,\"timestamps\":{\"start\":%lld}}},\"nonce\":\"%d\"}",
                     GetCurrentProcessId(), d, *as ? ",\"state\":" : "", *as ? st : "", started, ++nonce);
            if (!pipe_send(h, 1, msg)) { logf_("discord: connection closed"); CloseHandle(h); h = INVALID_HANDLE_VALUE; continue; }
            lstrcpynA(shown, key, sizeof shown);
            logf_("discord: status \"%s\"%s%s", *level ? level : "In the menus", *as ? " / " : "", as);
        }
        if (!pipe_drain(h)) { logf_("discord: connection closed"); CloseHandle(h); h = INVALID_HANDLE_VALUE; }
    }
    return 0;
}

void achieve_load_settings(const char *ini)
{
    g_notify = GetPrivateProfileIntA("Achievements", "Notify", 1, ini) != 0;
    g_discord = GetPrivateProfileIntA("Discord", "Enabled", 0, ini) != 0;
    GetPrivateProfileStringA("Discord", "AppId", ROTK_DISCORD_APP_ID, g_appId, sizeof g_appId, ini);
}

/* DllMain, after the settings: the list, the saved state, the threads */
void achieve_start(void)
{
    InitializeCriticalSection(&g_cs);
    defs_init();
    path_in_dir(g_file, "ROTKFix\\achievements.txt");
    load();
    g_ready = 1;
    EnterCriticalSection(&g_cs);
    save();   /* the list for Athelas, from the first run */
    LeaveCriticalSection(&g_cs);
    int n = 0;
    for (int i = 0; i < DEF_COUNT; i++) n += g_when[i][0] != 0;
    const BYTE *sig = (const BYTE *)0x47E02D;
    if (readable(sig, 5) && !memcmp(sig, SIG_STATS, 5)) {
        CloseHandle(CreateThread(NULL, 0, progress_thread, NULL, 0, NULL));
        logf_("achievements: %d of %d earned", n, DEF_COUNT);
    } else {
        logf_("achievements: the game's progress wasn't recognised; only enemies defeated count");
    }
    if (g_discord) {
        if (g_appId[0]) CloseHandle(CreateThread(NULL, 0, discord_thread, NULL, 0, NULL));
        else logf_("discord: no application id, status off");
    }
}

/* ---- drawing the note: post.c, on the finished frame, Direct3D 9 (the Vulkan renderer) ---- */
static IDirect3DTexture9 *g_noteTex;
static IDirect3DStateBlock9 *g_noteSb;
static int g_noteTexFor = -1;    /* which note the texture holds */
static UINT g_noteW, g_noteH;

void achieve_release(void)
{
    if (g_noteTex) { IDirect3DTexture9_Release(g_noteTex); g_noteTex = NULL; }
    if (g_noteSb) { IDirect3DStateBlock9_Release(g_noteSb); g_noteSb = NULL; }
    g_noteTexFor = -1;
}

/* the note as a picture: GDI draws it, alpha from the box's shape */
static int make_note_tex(IDirect3DDevice9 *dev, const Note *nt, UINT screenH)
{
    UINT h = screenH / 9; if (h < 72) h = 72; if (h > 220) h = 220;
    UINT w = h * 4;
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = (LONG)w; bi.bmiHeader.biHeight = -(LONG)h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); DeleteDC(dc); return 0; }
    HGDIOBJ old = SelectObject(dc, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(18, 15, 12));
    RECT all = { 0, 0, (LONG)w, (LONG)h };
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, h / 40 + 1, RGB(140, 112, 60));
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 1, 1, (int)w - 1, (int)h - 1, (int)h / 5, (int)h / 5);
    SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);
    int pad = (int)h / 7;
    HFONT f1 = CreateFontA(-(int)(h * 0.2f), 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Georgia");
    HFONT f2 = CreateFontA(-(int)(h * 0.3f), 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Georgia");
    HFONT f3 = CreateFontA(-(int)(h * 0.17f), 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Georgia");
    SelectObject(dc, f1); SetTextColor(dc, RGB(235, 200, 110));
    TextOutA(dc, pad, pad / 2 + 2, "Achievement earned", 18);
    SelectObject(dc, f2); SetTextColor(dc, RGB(245, 238, 225));
    TextOutA(dc, pad, (int)(h * 0.3f), nt->title, (int)strlen(nt->title));
    SelectObject(dc, f3); SetTextColor(dc, RGB(170, 160, 145));
    TextOutA(dc, pad, (int)(h * 0.66f), nt->desc, (int)strlen(nt->desc));
    GdiFlush();
    SelectObject(dc, old);
    DeleteObject(f1); DeleteObject(f2); DeleteObject(f3);
    if (g_noteTex && (g_noteW != w || g_noteH != h)) { IDirect3DTexture9_Release(g_noteTex); g_noteTex = NULL; }
    if (!g_noteTex && FAILED(IDirect3DDevice9_CreateTexture(dev, w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_noteTex, NULL))) {
        DeleteObject(bmp); DeleteDC(dc); return 0;
    }
    g_noteW = w; g_noteH = h;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(IDirect3DTexture9_LockRect(g_noteTex, 0, &lr, NULL, 0))) {
        float r = h / 5.0f / 2.0f;
        for (UINT y = 0; y < h; y++) {
            DWORD *src = (DWORD *)bits + y * w, *dst = (DWORD *)((BYTE *)lr.pBits + y * lr.Pitch);
            for (UINT x = 0; x < w; x++) {
                /* rounded corners: outside them clear */
                float cx = x < r ? r - x : x > w - 1 - r ? x - (w - 1 - r) : 0, cy = y < r ? r - y : y > h - 1 - r ? y - (h - 1 - r) : 0;
                float d = cx * cx + cy * cy;
                DWORD a = d > r * r ? 0 : 230;
                dst[x] = (src[x] & 0x00FFFFFF) | (a << 24);
            }
        }
        IDirect3DTexture9_UnlockRect(g_noteTex, 0);
    }
    DeleteObject(bmp);
    DeleteDC(dc);
    return 1;
}

typedef struct { float x, y, z, rhw; DWORD c; float u, v; } NoteVtx;

void achieve_draw(IDirect3DDevice9 *dev)
{
    if (!g_ready || !g_noteCount) return;
    EnterCriticalSection(&g_cs);
    Note nt = g_notes[g_noteHead];
    DWORD now = GetTickCount();
    if (!g_noteShown) g_noteShown = now ? now : 1;
    DWORD age = now - g_noteShown;
    int idx = g_noteHead;
    if (age > 6000) {
        g_noteHead = (g_noteHead + 1) % 8; g_noteCount--; g_noteShown = 0;
        LeaveCriticalSection(&g_cs);
        return;
    }
    LeaveCriticalSection(&g_cs);
    IDirect3DSurface9 *bb = NULL;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    D3DSURFACE_DESC bd;
    IDirect3DSurface9_GetDesc(bb, &bd);
    if (g_noteTexFor != idx || !g_noteTex) {
        if (!make_note_tex(dev, &nt, bd.Height)) { IDirect3DSurface9_Release(bb); return; }
        g_noteTexFor = idx;
    }
    if (!g_noteSb && FAILED(IDirect3DDevice9_CreateStateBlock(dev, D3DSBT_ALL, &g_noteSb))) { g_noteSb = NULL; IDirect3DSurface9_Release(bb); return; }
    IDirect3DStateBlock9_Capture(g_noteSb);
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
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    IDirect3DDevice9_SetVertexShader(dev, NULL);
    IDirect3DDevice9_SetPixelShader(dev, NULL);
    IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)g_noteTex);
    for (DWORD s = 1; s < 8; s++) IDirect3DDevice9_SetTexture(dev, s, NULL);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_SRGBTEXTURE, FALSE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_TEXCOORDINDEX, 0);
    IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    float fade = age < 300 ? age / 300.0f : age > 5400 ? (6000 - age) / 600.0f : 1.0f;
    DWORD c = ((DWORD)(fade * 255.0f) << 24) | 0xFFFFFF;
    float m = bd.Height / 40.0f;
    float x1 = bd.Width - m, y1 = bd.Height - m, x0 = x1 - g_noteW, y0 = y1 - g_noteH;
    NoteVtx q[4] = {
        { x0 - 0.5f, y0 - 0.5f, 0, 1, c, 0, 0 }, { x1 - 0.5f, y0 - 0.5f, 0, 1, c, 1, 0 },
        { x0 - 0.5f, y1 - 0.5f, 0, 1, c, 0, 1 }, { x1 - 0.5f, y1 - 0.5f, 0, 1, c, 1, 1 },
    };
    IDirect3DDevice9_BeginScene(dev);
    IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof q[0]);
    IDirect3DDevice9_EndScene(dev);
    IDirect3DDevice9_SetRenderTarget(dev, 0, oldRT);
    IDirect3DDevice9_SetDepthStencilSurface(dev, oldZ);
    IDirect3DStateBlock9_Apply(g_noteSb);
    if (oldRT) IDirect3DSurface9_Release(oldRT);
    if (oldZ) IDirect3DSurface9_Release(oldZ);
    IDirect3DSurface9_Release(bb);
}
