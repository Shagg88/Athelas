// Athelas - "It worked" / "It didn't work": telling us about an untested release or region.
#include "app.h"

// The port has closed: back to the launcher, saying why if it wasn't a normal quit
// The CPU an exe is built for, from its PE header: "ARM64", "x64", "x86", or "" when unreadable
std::string exe_machine(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return "";
    unsigned char h[0x40], pe[6];
    std::string r;
    if (fread(h, 1, sizeof h, f) == sizeof h && h[0] == 'M' && h[1] == 'Z') {
        long at = (long)(h[0x3C] | h[0x3D] << 8 | h[0x3E] << 16 | (unsigned)h[0x3F] << 24);
        if (!fseek(f, at, SEEK_SET) && fread(pe, 1, 6, f) == 6 && !memcmp(pe, "PE\0\0", 4)) {
            unsigned m = pe[4] | pe[5] << 8;
            r = m == 0xAA64 ? "ARM64" : m == 0x8664 ? "x64" : m == 0x14C ? "x86" : "";
        }
    }
    fclose(f);
    return r;
}

std::string url_encode(const std::string &s)
{
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') o += (char)c;
        else { char b[4]; snprintf(b, sizeof b, "%%%02X", c); o += b; }
    }
    return o;
}

// 1.7.5 (Shagg, as COGBox does): a release of a game the ports weren't made from (another region or revision) can be
// played. The game's page says it is untested; "This version works" remembers its default.xbe by SHA-1 in
// Athelas.ini [WorksRelease] (no more question at Play: it starts with --any-release) and opens a pre-filled GitHub
// issue, so the developer can add it as known to work. Nothing is sent by Athelas itself.
static std::map<std::string, std::string> g_xbeSha;   // game path -> its default.xbe's SHA-1 (read once)

std::vector<std::string> g_untested;           // port folders whose last start said "another release" (exit 3)

static const std::string &xbe_sha_of(const LibGame &g)
{
    auto it = g_xbeSha.find(g.path);
    if (it == g_xbeSha.end()) it = g_xbeSha.emplace(g.path, xbe_sha1(g)).first;
    return it->second;
}

static std::map<std::string, std::string> g_works;    // [WorksRelease] read once per folder (not every frame)

static std::string works_get(const char *dir)
{
    auto it = g_works.find(dir);
    if (it != g_works.end()) return it->second;
    char v[64] = {};
    GetPrivateProfileStringA("WorksRelease", dir, "", v, sizeof v, ports_ini().c_str());
    return g_works[dir] = v;
}

bool release_confirmed(const char *dir, const LibGame &g)
{
    std::string w = works_get(dir);
    return !w.empty() && w == xbe_sha_of(g);
}

bool release_untested(const char *dir, const LibGame &g)
{
    return !game_region_us(g) || std::find(g_untested.begin(), g_untested.end(), std::string(dir)) != g_untested.end();
}

// "It worked" / "It didn't work" on an untested version's page: a pre-filled GitHub issue with the copy's details
// (Athelas sends nothing itself; the player submits the page). "It worked" also remembers the copy, so Play stops asking.
void report_version(App &a, const char *dir, const char *game, const LibGame &g, bool worked)
{
    const std::string &h = xbe_sha_of(g);
    if (h.empty()) { set_message(a, "The game's default.xbe couldn't be read - nothing was changed.", true); return; }
    if (worked) {
        WritePrivateProfileStringA("WorksRelease", dir, h.c_str(), ports_ini().c_str());
        g_works[dir] = h;
    }
    std::string title = std::string(worked ? "Version works: " : "Version doesn't work: ") + game + " - " +
                        game_region_name(g) + " copy " + h.substr(0, 12);
    std::string body = std::string(worked ? "This version runs for me" : "This version doesn't run for me") + " in Athelas " +
                       LAUNCHER_VERSION + ".\n\nGame: " + game + "\nRegion: " + game_region_name(g) + "\nTitle ID: " +
                       [&] { char b[16]; snprintf(b, sizeof b, "%08X", (unsigned)g.titleId); return std::string(b); }() +
                       "\nTitle: " + g.title + "\nCopy: " + (g.iso ? "disc image" : "extracted files") + "\ndefault.xbe SHA-1: " + h +
                       (worked ? "\n\nWhere it came from (region, disc revision, Platinum Hits...), and anything that didn't work:\n"
                               : "\n\nWhat happened (didn't start, crashed, a level that broke...). Please drag the game's log in too "
                                 "(the Report a problem button on the game's page collects it):\n");
    ShellExecuteA(NULL, "open", ("https://github.com/Shagg88/Athelas/issues/new?labels=version&title=" + url_encode(title) +
                                 "&body=" + url_encode(body)).c_str(), NULL, NULL, SW_SHOWNORMAL);
    set_message(a, worked ? "Thanks - this version won't be questioned again. A GitHub page opened with its details: submit it to let us know."
                          : "Thanks - a GitHub page opened with this version's details: submit it to let us know.", false);
}
