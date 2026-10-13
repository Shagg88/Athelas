// ROTK Fix Launcher - the player's games (see library.h)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include "library.h"
#include "config.h"

// Title IDs from the games' default.xbe (the publisher code 'EA' + a number)
static const uint32_t TID_TT   = 0x45410010;   // The Two Towers
static const uint32_t TID_ROTK = 0x4541003E;   // The Return of the King

const char *game_kind_name(GameKind k)
{
    switch (k) {
    case GK_ROTK_PC:   return "The Return of the King  -  PC";
    case GK_ROTK_XBOX: return "The Return of the King  -  Xbox";
    case GK_TT_XBOX:   return "The Two Towers  -  Xbox";
    default:           return "Unknown game";
    }
}
const char *game_kind_short(GameKind k) { return k == GK_ROTK_PC ? "PC" : k == GK_NONE ? "" : "Xbox"; }
bool game_kind_playable(GameKind k) { return k == GK_ROTK_PC; }   // the Xbox ports are not ready yet

static const char *kind_key(GameKind k)
{
    return k == GK_ROTK_PC ? "rotk-pc" : k == GK_ROTK_XBOX ? "rotk-xbox" : k == GK_TT_XBOX ? "tt-xbox" : "none";
}
static GameKind kind_from_key(const char *s)
{
    for (int k = GK_ROTK_PC; k < GK_COUNT; k++) if (!_stricmp(s, kind_key((GameKind)k))) return (GameKind)k;
    return GK_NONE;
}

static bool exists(const std::string &p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static bool is_dir(const std::string &p)
{
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool any_file(const std::string &pattern)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    FindClose(h);
    return true;
}

// ---- reading a file or a piece of a disc image -------------------------------

struct Reader {
    FILE *f = nullptr;
    unsigned long long base = 0;     // where the game partition starts in the image
    bool read(unsigned long long off, void *buf, size_t n)
    {
        return f && !_fseeki64(f, (long long)(base + off), SEEK_SET) && fread(buf, 1, n, f) == n;
    }
};

// Title ID and name from an XBE whose first bytes are at 'off' in the reader.
static bool xbe_title(Reader &r, unsigned long long off, uint32_t &tid, std::string &title)
{
    unsigned char h[0x180];
    if (!r.read(off, h, sizeof h) || memcmp(h, "XBEH", 4)) return false;
    uint32_t base, cert;
    memcpy(&base, h + 0x104, 4);
    memcpy(&cert, h + 0x118, 4);
    if (cert < base || cert - base > 0x10000) return false;
    unsigned char c[0x60];
    if (!r.read(off + (cert - base), c, sizeof c)) return false;
    memcpy(&tid, c + 8, 4);
    wchar_t w[41] = {};
    memcpy(w, c + 0x0C, 80);
    char b[128];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, b, sizeof b, NULL, NULL);
    title = b;
    return true;
}

static GameKind kind_from_tid(uint32_t tid)
{
    return tid == TID_TT ? GK_TT_XBOX : tid == TID_ROTK ? GK_ROTK_XBOX : GK_NONE;
}

// ---- Xbox disc images (XDVDFS) -----------------------------------------------
// The file system starts at sector 32 of the game partition: "MICROSOFT*XBOX*MEDIA",
// then the root directory's sector and size. A directory is a binary tree of
// entries: left and right offsets (in 4-byte units), sector, size, attributes,
// name length, name. Full disc dumps put the game partition further in.

static bool iso_find(Reader &r, const char *name, uint32_t &sector, uint32_t &size, bool *sawDirs, const char *const *dirs, int ndirs)
{
    unsigned char vd[28];
    if (!r.read(32 * 2048ull, vd, sizeof vd) || memcmp(vd, "MICROSOFT*XBOX*MEDIA", 20)) return false;
    uint32_t rootSec, rootSize;
    memcpy(&rootSec, vd + 20, 4);
    memcpy(&rootSize, vd + 24, 4);
    if (!rootSize || rootSize > 4 * 1024 * 1024) return false;
    std::vector<unsigned char> t(rootSize);
    if (!r.read(rootSec * 2048ull, t.data(), rootSize)) return false;
    bool found = false;
    std::vector<uint32_t> todo{ 0 };
    int guard = 0;
    while (!todo.empty() && guard++ < 100000) {
        uint32_t o = todo.back(); todo.pop_back();
        if (o + 14 > rootSize) continue;
        uint16_t l, rt; uint32_t sec, sz;
        memcpy(&l, &t[o], 2); memcpy(&rt, &t[o + 2], 2);
        if (l == 0xFFFF) continue;                       // padding
        memcpy(&sec, &t[o + 4], 4); memcpy(&sz, &t[o + 8], 4);
        unsigned n = t[o + 13];
        if (o + 14 + n > rootSize) continue;
        std::string nm((const char *)&t[o + 14], n);
        if (!_stricmp(nm.c_str(), name)) { sector = sec; size = sz; found = true; }
        for (int i = 0; i < ndirs; i++) if (!_stricmp(nm.c_str(), dirs[i])) sawDirs[i] = true;
        if (l) todo.push_back(l * 4u);
        if (rt) todo.push_back(rt * 4u);
    }
    return found;
}

static GameKind detect_iso(const std::string &path, LibGame &out)
{
    Reader r;
    r.f = fopen(path.c_str(), "rb");
    if (!r.f) return GK_NONE;
    // game partition at the start (extracted game partition), or after the video
    // partition in full dumps (XGD1, XGD2, XGD3)
    static const unsigned long long starts[] = { 0, 0x18300000ull, 0xFD90000ull, 0x89D80000ull };
    GameKind k = GK_NONE;
    for (unsigned long long s : starts) {
        r.base = s;
        static const char *const dirs[] = { "Worlds", "Data" };
        bool saw[2] = { false, false };
        uint32_t sec = 0, sz = 0;
        if (!iso_find(r, "default.xbe", sec, sz, saw, dirs, 2)) continue;
        uint32_t tid = 0; std::string title;
        if (xbe_title(r, sec * 2048ull, tid, title)) {
            k = kind_from_tid(tid);
            out.titleId = tid; out.title = title;
        }
        if (k == GK_NONE) k = saw[0] ? GK_TT_XBOX : GK_NONE;   // other regions may use other title IDs
        break;
    }
    fclose(r.f);
    return k;
}

// ---- folders -------------------------------------------------------------------

static GameKind detect_folder(const std::string &dir, LibGame &out)
{
    if (exists(dir + "\\rotk.exe")) return GK_ROTK_PC;
    std::string xbe = dir + "\\default.xbe";
    if (!exists(xbe)) return GK_NONE;
    GameKind k = GK_NONE;
    Reader r;
    r.f = fopen(xbe.c_str(), "rb");
    uint32_t tid = 0; std::string title;
    if (r.f && xbe_title(r, 0, tid, title)) { k = kind_from_tid(tid); out.titleId = tid; out.title = title; }
    if (r.f) fclose(r.f);
    // other regions may use other title IDs: go by the files
    if (k == GK_NONE && any_file(dir + "\\Worlds\\*.bfa")) k = GK_TT_XBOX;
    if (k == GK_NONE && exists(dir + "\\Data\\Game\\GlobScen.scx")) k = GK_ROTK_XBOX;
    return k;
}

GameKind detect_game_at(const std::string &path, LibGame &out)
{
    out = LibGame();
    std::string p = path;
    while (p.size() > 3 && (p.back() == '\\' || p.back() == '/')) p.pop_back();
    out.path = p;
    if (is_dir(p)) out.kind = detect_folder(p, out);
    else if (exists(p)) { out.iso = true; out.kind = detect_iso(p, out); }
    return out.kind;
}

// ---- the remembered list ---------------------------------------------------------

static std::string lib_file(const std::string &launcherDir) { return launcherDir + "\\ROTKFix\\games.ini"; }

std::vector<LibGame> library_load(const std::string &launcherDir)
{
    std::vector<LibGame> out;
    FILE *f = fopen(lib_file(launcherDir).c_str(), "rb");
    if (!f) return out;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        // Game = kind|path
        char *eq = strchr(line, '=');
        if (!eq || _strnicmp(line, "Game", 4)) continue;
        std::string v = eq + 1;
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        size_t bar = v.find('|');
        if (bar == std::string::npos) continue;
        GameKind saved = kind_from_key(v.substr(0, bar).c_str());
        LibGame g;
        // check again: the folder may have moved, or a port may now read it differently
        if (detect_game_at(v.substr(bar + 1), g) == GK_NONE || g.kind != saved) continue;
        library_add(out, g);
    }
    fclose(f);
    return out;
}

bool library_save(const std::string &launcherDir, const std::vector<LibGame> &games)
{
    CreateDirectoryA((launcherDir + "\\ROTKFix").c_str(), NULL);
    std::string s = "; Games the ROTK Fix launcher knows about (Game = kind|location). Edited by the launcher.\r\n[Games]\r\n";
    for (auto &g : games) s += std::string("Game = ") + kind_key(g.kind) + "|" + g.path + "\r\n";
    FILE *f = fopen(lib_file(launcherDir).c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
    return fclose(f) == 0 && ok;
}

int library_add(std::vector<LibGame> &games, const LibGame &g)
{
    for (size_t i = 0; i < games.size(); i++)
        if (!_stricmp(games[i].path.c_str(), g.path.c_str())) { games[i] = g; return (int)i; }
    games.push_back(g);
    return (int)games.size() - 1;
}

bool pick_disc_image(std::string &out)
{
    bool ok = false;
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog *dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, (void **)&dlg))) {
        COMDLG_FILTERSPEC types[] = { { L"Xbox disc image (*.iso)", L"*.iso" }, { L"All files", L"*.*" } };
        dlg->SetFileTypes(2, types);
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        dlg->SetTitle(L"Choose your Xbox disc image");
        if (SUCCEEDED(dlg->Show(GetActiveWindow()))) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR w = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &w))) {
                    char b[MAX_PATH * 2];
                    if (WideCharToMultiByte(CP_ACP, 0, w, -1, b, sizeof b, NULL, NULL) > 0) { out = b; ok = true; }
                    CoTaskMemFree(w);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return ok;
}
