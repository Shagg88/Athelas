// ---------------------------------------------------------------------------
// The front screen (included by main.cpp): the player's games as boxes on a carousel. The box in front is large;
// the others stand smaller, darker and turned away at the sides until they come round. Behind it, the art of the game
// in front (read from the player's own copy, as on the settings pages), crossfading as the carousel turns.
//
// A box's front is, in this order: the player's own cover scan (covers\<key>.png or .jpg beside Athelas), the box
// art downloaded on request from RetroArch's collection (libretro thumbnails, covers\<key>-libretro.png), or a cover
// made from the game's own art with its title. The Return of the King is one box for both versions; with both, Play
// and Settings ask which (and can remember it). Nothing is shipped: art comes from the player's files or, when they
// ask, from the public collection.

enum { FR_FOTR, FR_TT, FR_ROTK, FR_COUNT };
static const char *const k_frKey[FR_COUNT] = { "fotr", "tt", "rotk" };
static const char *const k_frTitle[FR_COUNT] = { "The Fellowship of the Ring", "The Two Towers", "The Return of the King" };
// libretro thumbnails' names (No-Intro's) for the US Xbox releases
static const char *const k_frLibretro[FR_COUNT] = {
    "Lord of the Rings, The - The Fellowship of the Ring (USA)",
    "Lord of the Rings, The - The Two Towers (USA)",
    "Lord of the Rings, The - The Return of the King (USA)" };

struct FrontTex { SDL_Texture *tex = nullptr; int w = 0, h = 0; };
struct Front {
    int sel = 0;                       // the box in front
    float pos = 0;                     // where the carousel is (eases towards sel)
    bool started = false;
    FrontTex cover[FR_COUNT];          // a cover scan or downloaded box art
    int coverFrom[FR_COUNT] = {};      // 0 none, 1 the player's own, 2 libretro
    bool coversLooked = false;
    FrontTex art[FR_COUNT];            // the game's own art, for a made cover
    BgJob *artJob[FR_COUNT] = {};
    bool artAsked[FR_COUNT] = {};
    std::string artFor[FR_COUNT];      // the game it was read from
    int ask = -1;                      // the version question is open for: 0 Play, 1 Settings
    double turnedAt = 0;
    ImRect hit[FR_COUNT];              // where each box was drawn (last frame), for the mouse
};
static Front g_front;

static SDL_AtomicInt g_artDl;          // box art download: 0 idle, 1 running, 2 done
static std::string g_artDlMsg;
static bool g_artDlBad = false;

static std::string covers_dir() { return exe_dir() + "\\covers"; }

static SDL_Texture *make_texture(SDL_Renderer *ren, const unsigned char *px, int w, int h)
{
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, (void *)px, w * 4);
    SDL_Texture *t = s ? SDL_CreateTextureFromSurface(ren, s) : nullptr;
    if (s) SDL_DestroySurface(s);
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}
static void free_tex(FrontTex &t) { if (t.tex) SDL_DestroyTexture(t.tex); t = FrontTex(); }

// the library entries of a box: Fellowship's and The Two Towers' copies; The Return of the King's PC copies (the one
// whose settings are shown first) then its Xbox ones
static std::vector<int> fr_lib(const App &a, int e)
{
    std::vector<int> out;
    auto add = [&](GameKind k, bool ownFirst) {
        if (ownFirst)
            for (size_t i = 0; i < a.lib.size(); i++)
                if (a.lib[i].kind == k && !_stricmp(a.lib[i].path.c_str(), a.dir.c_str())) out.push_back((int)i);
        for (size_t i = 0; i < a.lib.size(); i++)
            if (a.lib[i].kind == k && std::find(out.begin(), out.end(), (int)i) == out.end()) out.push_back((int)i);
    };
    if (e == FR_FOTR) add(GK_FOTR_XBOX, false);
    else if (e == FR_TT) add(GK_TT_XBOX, false);
    else { add(GK_ROTK_PC, true); add(GK_ROTK_XBOX, false); }
    return out;
}
// the copy of that kind a box plays: a supported (US) release first, when the player has several (a European disc
// and the US one, say), else the first
static int fr_first(const App &a, int e, GameKind k)
{
    for (int i : fr_lib(a, e)) if (a.lib[i].kind == k && game_region_supported(a.lib[i])) return i;
    for (int i : fr_lib(a, e)) if (a.lib[i].kind == k) return i;
    return -1;
}
// every copy of that kind, as music_play takes them (one per line, the one fr_first picks first): when one copy's
// music can't be read (another release lays its sound out differently), the next is tried
static std::string fr_music_copies(const App &a, int e, GameKind k)
{
    int first = fr_first(a, e, k);
    if (first < 0) return std::string();
    std::string s = a.lib[first].path;
    for (int i : fr_lib(a, e)) if (i != first && a.lib[i].kind == k) s += "\n" + a.lib[i].path;
    return s;
}
static bool fr_owned(const App &a, int e) { return !fr_lib(a, e).empty(); }
static int fr_entry_of(GameKind k) { return k == GK_FOTR_XBOX ? FR_FOTR : k == GK_TT_XBOX ? FR_TT : FR_ROTK; }

// [Front] in Athelas.ini: the version The Return of the King plays (pc / xbox, empty: ask) and the box last chosen
// read once and kept: the front screen asks every frame, and Athelas.ini may sit in a folder an antivirus scans or
// OneDrive syncs. Only Athelas writes these keys, through ini_put, which keeps the copy current.
static std::map<std::string, std::string> g_iniCache;
static std::string ini_get(const char *sec, const char *key, const char *def)
{
    std::string k = std::string(sec) + "\x01" + key;
    auto it = g_iniCache.find(k);
    if (it != g_iniCache.end()) return it->second;
    char v[64] = "";
    GetPrivateProfileStringA(sec, key, def, v, sizeof v, ports_ini().c_str());
    return g_iniCache[k] = v;
}
static void ini_put(const char *sec, const char *key, const std::string &v)
{
    WritePrivateProfileStringA(sec, key, v.c_str(), ports_ini().c_str());
    g_iniCache[std::string(sec) + "\x01" + key] = v;
}
static std::string front_get(const char *key) { return ini_get("Front", key, ""); }
static void front_put(const char *key, const std::string &v) { ini_put("Front", key, v); }

// play time: [PlayTime] (seconds) and [LastPlayed] (Unix time) in Athelas.ini, per version (fotr, tt, rotk-xbox,
// rotk-pc). The launcher times each game: it waits, hidden, while it plays (the PC game too, since 1.7).
static void play_time_add(const std::string &key, unsigned long long secs)
{
    unsigned long long had = strtoull(ini_get("PlayTime", key.c_str(), "0").c_str(), nullptr, 10);
    if (secs < 20) return;   // (a start that ended at once is no play, and no "last played")
    ini_put("PlayTime", key.c_str(), std::to_string(had + secs));
    ini_put("LastPlayed", key.c_str(), std::to_string((unsigned long long)time(nullptr)));
}
static void played_now(const std::string &key)
{
    ini_put("LastPlayed", key.c_str(), std::to_string((unsigned long long)time(nullptr)));
}
// "12 h 5 min played  ·  last played 24 Oct 2026" for a box (both Return of the King versions together); empty when
// it hasn't been played with Athelas yet
static std::string play_line(int e)
{
    const char *keys[2] = { k_frKey[e], nullptr };
    if (e == FR_ROTK) { keys[0] = "rotk-xbox"; keys[1] = "rotk-pc"; }
    unsigned long long secs = 0, last = 0;
    for (const char *k : keys) {
        if (!k) continue;
        secs += strtoull(ini_get("PlayTime", k, "0").c_str(), nullptr, 10);
        last = std::max(last, (unsigned long long)strtoull(ini_get("LastPlayed", k, "0").c_str(), nullptr, 10));
    }
    std::string out;
    if (secs >= 60) {
        unsigned long long h = secs / 3600, m = secs / 60 % 60;
        out = h ? std::to_string(h) + " h " + std::to_string(m) + " min played" : std::to_string(m) + " min played";
    }
    if (last) {
        time_t t = (time_t)last;
        struct tm lt;
        localtime_s(&lt, &t);
        time_t now = time(nullptr);
        struct tm nt;
        localtime_s(&nt, &now);
        char d[48];
        if (lt.tm_year == nt.tm_year && lt.tm_yday == nt.tm_yday) snprintf(d, sizeof d, "today");
        else if (lt.tm_year == nt.tm_year && lt.tm_yday == nt.tm_yday - 1) snprintf(d, sizeof d, "yesterday");
        else strftime(d, sizeof d, "%d %b %Y", &lt);
        if (d[0] == '0') memmove(d, d + 1, strlen(d));
        out += (out.empty() ? "Last played " : "  \xc2\xb7  last played ") + std::string(d);
    }
    return out;
}

static void front_load_covers(SDL_Renderer *ren)
{
    g_front.coversLooked = true;
    for (int e = 0; e < FR_COUNT; e++) {
        free_tex(g_front.cover[e]);
        g_front.coverFrom[e] = 0;
        std::string base = covers_dir() + "\\" + k_frKey[e];
        const std::string tries[] = { base + ".png", base + ".jpg", base + ".jpeg", base + "-libretro.png" };
        for (int i = 0; i < 4 && !g_front.cover[e].tex; i++) {
            int w, h, c;
            unsigned char *px = stbi_load(tries[i].c_str(), &w, &h, &c, 4);
            if (!px) continue;
            g_front.cover[e].tex = make_texture(ren, px, w, h);
            g_front.cover[e].w = w; g_front.cover[e].h = h;
            g_front.coverFrom[e] = i < 3 ? 1 : 2;
            stbi_image_free(px);
        }
    }
}

// the game's own art for a made cover: one picture, read on a worker thread as the backgrounds are
static void front_art_poll(App &a, SDL_Renderer *ren)
{
    for (int e = 0; e < FR_COUNT; e++) {
        Front &f = g_front;
        if (BgJob *j = f.artJob[e]) {
            if (!SDL_GetAtomicInt(&j->done)) continue;
            f.artJob[e] = nullptr;
            if (j->px) {
                free_tex(f.art[e]);
                f.art[e].tex = make_texture(ren, j->px, j->w, j->h);
                f.art[e].w = j->w; f.art[e].h = j->showH;
                stbi_image_free(j->px);
            }
            delete j;
            continue;
        }
        if (f.cover[e].tex) continue;              // not needed
        std::vector<int> li = fr_lib(a, e);
        if (li.empty()) continue;
        std::vector<std::string> files;
        std::string from;
        if (e == FR_ROTK) {
            if (!art_ready() || g_art.empty()) continue;
            files.push_back("\001art:0");
            from = "rotk";
        } else {
            from = a.lib[li[0]].path;
            if (e == FR_TT) tt_art_list(from, files); else fotr_art_list(from, files);
        }
        if (f.artAsked[e] && f.artFor[e] == from) continue;
        f.artAsked[e] = true;
        f.artFor[e] = from;
        if (files.empty()) continue;
        BgJob *j = new BgJob();
        j->files = files;
        j->start = 0;
        SDL_SetAtomicInt(&j->done, 0);
        SDL_Thread *t = SDL_CreateThread(bg_worker, "cover", j);
        if (!t) { delete j; continue; }
        SDL_DetachThread(t);
        f.artJob[e] = j;
    }
}

// "Get box art": the three US Xbox boxes from libretro thumbnails (the collection RetroArch uses), on GitHub
static int SDLCALL art_download_worker(void *)
{
    std::string dir = covers_dir();
    SHCreateDirectoryExA(NULL, dir.c_str(), NULL);
    int got = 0, failed = 0;
    for (int e = 0; e < FR_COUNT; e++) {
        std::string path = "/libretro-thumbnails/Microsoft_-_Xbox/master/Named_Boxarts/" + url_encode(k_frLibretro[e]) + ".png";
        std::string data;
        if (!http_download(L"raw.githubusercontent.com", std::wstring(path.begin(), path.end()), data) || data.size() < 1000 ||
            data.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) != 0) { failed++; continue; }
        std::string out = dir + "\\" + k_frKey[e] + "-libretro.png", tmp = out + ".new";
        FILE *f = fopen(tmp.c_str(), "wb");
        bool ok = f && fwrite(data.data(), 1, data.size(), f) == data.size();
        if (f) ok = fclose(f) == 0 && ok;
        if (ok && MoveFileExA(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING)) got++;
        else { DeleteFileA(tmp.c_str()); failed++; }
    }
    g_artDlBad = got == 0;
    g_artDlMsg = got == 0 ? "The box art couldn't be downloaded - check the internet connection and try again."
               : failed ? "Box art downloaded for " + std::to_string(got) + " of the games (from RetroArch's libretro thumbnails)."
                        : "Box art downloaded (from RetroArch's libretro thumbnails).";
    SDL_SetAtomicInt(&g_artDl, 2);
    return 0;
}
static void art_download_start()
{
    if (SDL_GetAtomicInt(&g_artDl) == 1) return;
    SDL_SetAtomicInt(&g_artDl, 1);
    SDL_Thread *t = SDL_CreateThread(art_download_worker, "boxart", nullptr);
    if (t) SDL_DetachThread(t); else SDL_SetAtomicInt(&g_artDl, 0);
}

// ---- drawing a box: a case turned by yaw around its upright middle, seen in perspective
struct BoxProj {
    float cx, cy, c, s, f;
    // lx right, ly down, lz into the box (behind its front)
    ImVec2 at(float lx, float ly, float lz = 0) const
    {
        float X = lx * c - lz * s, Z = lx * s + lz * c, p = f / (f + Z);
        return ImVec2(cx + X * p, cy + ly * p);
    }
};
// part of a texture over a rectangle of the box's front, in upright strips so it follows the perspective
static void box_image(ImDrawList *dl, SDL_Texture *tex, const BoxProj &P, float x0, float y0, float x1, float y1, ImVec2 uv0,
                      ImVec2 uv1, ImU32 top, ImU32 bottom, float lz = 0)
{
    const int N = 14;
    dl->PushTexture((ImTextureID)(intptr_t)tex);
    dl->PrimReserve(N * 6, (N + 1) * 2);
    ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    for (int i = 0; i <= N; i++) {
        float t = (float)i / N, lx = x0 + (x1 - x0) * t, u = uv0.x + (uv1.x - uv0.x) * t;
        dl->PrimWriteVtx(P.at(lx, y0, lz), ImVec2(u, uv0.y), top);
        dl->PrimWriteVtx(P.at(lx, y1, lz), ImVec2(u, uv1.y), bottom);
    }
    for (int i = 0; i < N; i++) {
        ImDrawIdx k = (ImDrawIdx)(base + i * 2);
        dl->PrimWriteIdx(k); dl->PrimWriteIdx((ImDrawIdx)(k + 1)); dl->PrimWriteIdx((ImDrawIdx)(k + 3));
        dl->PrimWriteIdx(k); dl->PrimWriteIdx((ImDrawIdx)(k + 3)); dl->PrimWriteIdx((ImDrawIdx)(k + 2));
    }
    dl->PopTexture();
}
static void box_quad(ImDrawList *dl, const BoxProj &P, float x0, float y0, float x1, float y1, ImU32 col, float lz = 0)
{
    dl->AddQuadFilled(P.at(x0, y0, lz), P.at(x1, y0, lz), P.at(x1, y1, lz), P.at(x0, y1, lz), col);
}
static ImU32 shade(int r, int g, int b, float light, float alpha)
{
    return IM_COL32((int)(r * light), (int)(g * light), (int)(b * light), (int)(255 * alpha));
}
// a texture cropped to fill w x h (centred), as uv corners
static void crop_uv(int tw, int th, float w, float h, ImVec2 &uv0, ImVec2 &uv1)
{
    float ta = (float)tw / std::max(1, th), a = w / std::max(1.0f, h);
    uv0 = ImVec2(0, 0); uv1 = ImVec2(1, 1);
    if (ta > a) { float k = a / ta; uv0.x = (1 - k) * 0.5f; uv1.x = 1 - uv0.x; }
    else        { float k = ta / a; uv0.y = (1 - k) * 0.5f; uv1.y = 1 - uv0.y; }
}
// text centred in lines of at most width w, from the top at y
static float centred_text(ImDrawList *dl, ImFont *font, float size, float cx, float y, float w, ImU32 col, const char *text)
{
    text = tr(text);   // (drawn straight onto the draw list: its translation here)
    std::vector<std::string> lines;
    std::string cur, word;
    auto width = [&](const std::string &s) { return font->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x; };
    std::string t = text;
    size_t i = 0;
    while (i <= t.size()) {
        size_t j = t.find(' ', i);
        if (j == std::string::npos) j = t.size();
        word = t.substr(i, j - i);
        std::string trial = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && width(trial) > w) { lines.push_back(cur); cur = word; } else cur = trial;
        i = j + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    for (auto &l : lines) {
        float lw = width(l);
        dl->AddText(font, size, ImVec2(cx - lw * 0.5f + 1, y + 1.5f), IM_COL32(0, 0, 0, (int)(((col >> 24) & 255) * 0.8f)), l.c_str());
        dl->AddText(font, size, ImVec2(cx - lw * 0.5f, y), col, l.c_str());
        y += size * 1.12f;
    }
    return y;
}

// One box: d is its place from the front (0 in front, -1 / +1 beside it, wrapping round)
static ImRect draw_box(ImDrawList *dl, const App &a, int e, float d, ImVec2 c, float H, bool hover)
{
    float ad = std::min(fabsf(d), 1.6f);
    float scale = 1.0f - 0.30f * std::min(ad, 1.0f) - 0.12f * std::max(0.0f, ad - 1.0f);
    float yaw = std::max(-1.0f, std::min(1.0f, d)) * 0.62f;             // up to about 35 degrees
    float light = 1.0f - 0.45f * std::min(ad, 1.0f), alpha = 1.0f - 0.45f * std::min(ad, 1.0f) - 0.4f * std::max(0.0f, ad - 1.0f);
    alpha = std::max(0.0f, alpha);
    bool owned = fr_owned(a, e);
    if (!owned) { light *= 0.55f; alpha *= 0.7f; }
    float h = H * scale, w = h * 0.70f, hw = w * 0.5f, hh = h * 0.5f, depth = h * 0.05f;
    BoxProj P{ c.x, c.y + (1 - scale) * H * 0.18f, cosf(yaw), sinf(yaw), H * 3.2f };
    // the PC box (the PC version's own scan) is a plain black case; the Xbox ones have their green band
    bool pcCase = e == FR_ROTK && g_front.coverFrom[e] == 1 && fr_first(a, e, GK_ROTK_XBOX) < 0;
    // a scanned cover has its own top band; a made one gets the case's green band
    float band = pcCase || g_front.cover[e].tex ? 0.0f : h * 0.085f, edge = h * 0.012f;

    // the floor's reflection, fading downwards
    const FrontTex &ct = g_front.cover[e].tex ? g_front.cover[e] : g_front.art[e];
    float coverX0 = -hw + edge, coverX1 = hw - edge, coverY0 = -hh + band + edge, coverY1 = hh - edge;
    ImVec2 uv0, uv1;
    if (ct.tex) crop_uv(ct.w, ct.h, coverX1 - coverX0, coverY1 - coverY0, uv0, uv1);
    if (ct.tex && alpha > 0.05f) {
        float rh = h * 0.06f, gap = h * 0.012f, vspan = (uv1.y - uv0.y) * rh / (coverY1 - coverY0);
        box_image(dl, ct.tex, P, coverX0, hh + gap, coverX1, hh + gap + rh, ImVec2(uv0.x, uv1.y), ImVec2(uv1.x, uv1.y - vspan),
                  shade(255, 255, 255, light * 0.8f, alpha * 0.22f), shade(255, 255, 255, light, 0.0f));
    }
    // the spine on the side that faces the middle
    if (fabsf(yaw) > 0.02f) {
        float sx = yaw > 0 ? -hw : hw;
        ImU32 sc = shade(pcCase ? 22 : 30, pcCase ? 22 : 34, pcCase ? 24 : 28, light * 0.8f, alpha);
        dl->AddQuadFilled(P.at(sx, -hh, 0), P.at(sx, -hh, depth), P.at(sx, hh, depth), P.at(sx, hh, 0), sc);
        if (band > 0)
            dl->AddQuadFilled(P.at(sx, -hh, 0), P.at(sx, -hh, depth), P.at(sx, -hh + band, depth), P.at(sx, -hh + band, 0),
                              shade(70, 120, 30, light * 0.7f, alpha));
    }
    // the case
    box_quad(dl, P, -hw, -hh, hw, hh, shade(pcCase ? 14 : 18, pcCase ? 14 : 20, pcCase ? 16 : 17, light, alpha));
    if (band > 0) {
        box_quad(dl, P, -hw, -hh, hw, -hh + band, shade(96, 156, 38, light, alpha));
        box_quad(dl, P, -hw, -hh + band * 0.82f, hw, -hh + band, shade(60, 104, 22, light, alpha));
    }
    // the cover
    ImU32 cw = shade(255, 255, 255, light, alpha);
    if (g_front.cover[e].tex) {
        box_image(dl, ct.tex, P, coverX0, coverY0, coverX1, coverY1, uv0, uv1, cw, cw);
    } else {
        // made from the game's art: the picture, darker towards the title, and the names on it (in front only)
        box_quad(dl, P, coverX0, coverY0, coverX1, coverY1, shade(26, 22, 18, light, alpha));
        if (ct.tex) box_image(dl, ct.tex, P, coverX0, coverY0 + (coverY1 - coverY0) * 0.28f, coverX1, coverY1,
                              ImVec2(uv0.x, uv0.y + (uv1.y - uv0.y) * 0.28f), uv1, shade(255, 255, 255, light * 0.75f, alpha), cw);
        ImU32 top = shade(12, 10, 8, 1, alpha * 0.95f), clear = shade(12, 10, 8, 1, 0.0f);
        ImVec2 q[4] = { P.at(coverX0, coverY0), P.at(coverX1, coverY0), P.at(coverX1, coverY0 + (coverY1 - coverY0) * 0.55f),
                        P.at(coverX0, coverY0 + (coverY1 - coverY0) * 0.55f) };
        dl->AddQuadFilled(q[0], q[1], P.at(coverX1, coverY0 + (coverY1 - coverY0) * 0.28f), P.at(coverX0, coverY0 + (coverY1 - coverY0) * 0.28f), top);
        dl->PrimReserve(6, 4);
        ImDrawIdx i0 = (ImDrawIdx)dl->_VtxCurrentIdx;
        ImVec2 uvw = dl->_Data->TexUvWhitePixel;
        dl->PrimWriteVtx(P.at(coverX0, coverY0 + (coverY1 - coverY0) * 0.28f), uvw, top);
        dl->PrimWriteVtx(P.at(coverX1, coverY0 + (coverY1 - coverY0) * 0.28f), uvw, top);
        dl->PrimWriteVtx(q[2], uvw, clear);
        dl->PrimWriteVtx(q[3], uvw, clear);
        dl->PrimWriteIdx(i0); dl->PrimWriteIdx((ImDrawIdx)(i0 + 1)); dl->PrimWriteIdx((ImDrawIdx)(i0 + 2));
        dl->PrimWriteIdx(i0); dl->PrimWriteIdx((ImDrawIdx)(i0 + 2)); dl->PrimWriteIdx((ImDrawIdx)(i0 + 3));
        float ta = std::max(0.0f, 1.0f - fabsf(d) * 2.5f) * alpha;   // flat text: only on the box in front
        if (ta > 0.02f) {
            ImVec2 tl = P.at(coverX0, coverY0), tr = P.at(coverX1, coverY0);
            float tw = (tr.x - tl.x) * 0.86f, tcx = (tl.x + tr.x) * 0.5f, ty = tl.y + h * 0.05f;
            ty = centred_text(dl, F_HEAD, h * 0.045f, tcx, ty, tw, IM_COL32(200, 170, 110, (int)(255 * ta)), "THE LORD OF THE RINGS");
            centred_text(dl, F_TITLE, h * 0.085f, tcx, ty + h * 0.012f, tw, IM_COL32(236, 206, 140, (int)(255 * ta)), k_frTitle[e]);
        }
        if (!owned) {
            float tb = std::max(0.0f, 1.0f - fabsf(d) * 2.5f);
            ImVec2 m = P.at(0, coverY1 - h * 0.12f);
            centred_text(dl, F_BODY, h * 0.06f, m.x, m.y - h * 0.05f, w, IM_COL32(220, 200, 160, (int)(255 * tb)), "Add your copy");
            centred_text(dl, F_SMALL, h * 0.045f, m.x, m.y + h * 0.03f, w * 0.9f, IM_COL32(168, 152, 124, (int)(230 * tb)), "or drop it here");
        }
    }
    // no outline: the box in front brightens a little when the mouse is over it
    if (hover) box_quad(dl, P, coverX0, coverY0, coverX1, coverY1, IM_COL32(255, 244, 220, (int)(18 * alpha)));
    ImVec2 p0 = P.at(-hw, -hh), p1 = P.at(hw, -hh), p2 = P.at(hw, hh), p3 = P.at(-hw, hh);
    return ImRect(ImVec2(std::min(p0.x, p3.x), std::min(p0.y, p1.y)), ImVec2(std::max(p1.x, p2.x), std::max(p2.y, p3.y)));
}

// ---- going between the front screen and a page, in two halves: the screen darkens while the box grows out of the
// carousel, the page takes its place in the dark, then it lightens as the box fades away (and the other way round
// for the Library button: the page darkens, the box shrinks back into its place as the carousel lightens)
struct FrontTrans { double at = -1; bool toPage = true; ImRect box; int e = 0; };
static FrontTrans g_trans;
static const float kTransTime = getenv("ATHELAS_SLOW_TRANSITIONS") ? 4.0f : 0.5f;   // (slow: testing)
static void trans_start(bool toPage)
{
    g_trans.at = ImGui::GetTime();
    g_trans.toPage = toPage;
    g_trans.e = g_front.sel;
    g_trans.box = g_front.hit[g_front.sel];
}
static bool trans_running() { return g_trans.at >= 0; }
// drawn last, over everything (ImGui's foreground list); switches the screen half way
static void draw_transition(App &a, ImVec2 size)
{
    if (g_trans.at < 0) return;
    float t = (float)(ImGui::GetTime() - g_trans.at) / kTransTime;
    if (t >= 1) { g_trans.at = -1; return; }
    if (t >= 0.5f && a.front == g_trans.toPage) a.front = !g_trans.toPage;
    auto smooth = [](float x) { x = std::max(0.0f, std::min(1.0f, x)); return x * x * (3 - 2 * x); };
    float dark = t < 0.5f ? smooth(t * 2) : 1 - smooth(t * 2 - 1);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImVec2 wp = ImGui::GetMainViewport()->Pos;
    dl->AddRectFilled(wp, wp + size, IM_COL32(6, 8, 6, (int)(245 * dark)));
    const FrontTex &ct = g_front.cover[g_trans.e].tex ? g_front.cover[g_trans.e] : g_front.art[g_trans.e];
    if (!ct.tex || g_trans.box.GetWidth() < 4) return;
    float k = smooth(t);
    float grow = g_trans.toPage ? 1 + 0.8f * k : 1.8f - 0.8f * k;
    float alpha = g_trans.toPage ? (t < 0.5f ? 1.0f : 1 - smooth(t * 2 - 1)) : (t < 0.5f ? smooth(t * 2) : 1.0f);
    ImVec2 c = g_trans.box.GetCenter(), half = g_trans.box.GetSize() * 0.5f * grow;
    ImVec2 uv0, uv1;
    crop_uv(ct.w, ct.h, half.x * 2, half.y * 2, uv0, uv1);
    dl->AddImage((ImTextureID)(intptr_t)ct.tex, c - half, c + half, uv0, uv1, IM_COL32(255, 255, 255, (int)(255 * alpha)));
}

// ---- what the box's buttons do
static int g_frBtn;   // with a controller: 0 Play is lit, 1 Settings (LB / RB)
static void open_settings(App &a, int libIndex)
{
    if (libIndex >= 0) a.cur = libIndex;
    trans_start(true);   // the page shows half way through
}
// the page's Library button
static void back_to_front(App &)
{
    g_front.turnedAt = ImGui::GetTime() + kTransTime * 0.5f;
    trans_start(false);  // the carousel shows half way through
}

// Play The Return of the King on PC (the same as the PC page's Play): its settings saved, ROTK Fix brought up to date
static void pc_play(App &a)
{
    if (!save(a)) return;
    std::string err;
    if (fix_outdated(a.dir)) {
        bool denied = false;
        std::string why;
        if (!install_to(pc_fix_source(), a.dir, why, denied)) set_message(a, "ROTK Fix couldn't be updated in the game folder (" + why + ") - the game starts with the previous version.", true);
    }
    // The launcher waits, hidden, while the game plays (as for the ports), for its play time and its folder in Logs;
    // it closes when the game does
    HANDLE proc = NULL;
    if (!launch_game(a.dir, err, (void **)&proc)) { set_message(a, err, true); return; }
    played_now("rotk-pc");   // (only once it has started)
    if (!proc) { a.quit = true; return; }
    a.running = proc;
    a.runningPc = true;
    a.runningSince = GetTickCount64();
    a.runningKey = "rotk-pc";
    a.runningLog = a.dir + "\\ROTKFix.log";
    a.runningGame = "The Return of the King";
    a.session = Session();
    a.session.game = "The Return of the King (PC)"; a.session.exe = a.dir + "\\rotk.exe"; a.session.log = a.runningLog;
    a.session.startedMs = a.runningSince; a.session.startedUnix = (long long)time(nullptr);
    SDL_HideWindow(a.win);
}

// Play one copy from the front screen; when it can't start from here (a port missing, ROTK Fix not set up in that
// PC copy, an unsupported release), its page opens instead and says why
static void front_play(App &a, int li)
{
    if (li < 0) return;
    const LibGame g = a.lib[li];
    front_put("Last", k_frKey[fr_entry_of(g.kind)]);
    if (tp_kind(g.kind)) {
        a.cur = li;
        tp_use(a, tp_of(g.kind));
        if (!a.ttLooked) tt_find(a);
        if (!game_region_supported(g) || !tt_ready(a)) { open_settings(a, li); return; }
        if (!a.ttLoaded) tt_load_settings(a);
        tt_play(a, a.lib[li]);
        return;
    }
    // The Return of the King on PC
    a.cur = li;
    if (_stricmp(g.path.c_str(), a.dir.c_str())) {
        if (g.iso || !fix_installed(inspect_game(g.path))) { open_settings(a, li); return; }
        use_pc_game(a, g.path);
        if (a.quit) return;      // started again with the permission prompt
    }
    if (!a.game.exeFound || g_fixRemoved) { open_settings(a, li); return; }
    pc_play(a);
}

// A game's desktop shortcut (--play <key>): that game started as from its box; not found: the front screen says so
static void shortcut_play(App &a, const std::string &key)
{
    GameKind k = key == "fotr" ? GK_FOTR_XBOX : key == "tt" ? GK_TT_XBOX : key == "rotk-xbox" ? GK_ROTK_XBOX : GK_ROTK_PC;
    for (size_t i = 0; i < a.lib.size(); i++)
        if (a.lib[i].kind == k) { front_play(a, (int)i); return; }
    set_message(a, std::string(game_kind_name(k)) + " isn't added to Athelas any more - add it again, then make a new shortcut.", true);
}

// Play (what = 0) or Settings (1) for a box: The Return of the King with both versions asks which, unless remembered
static void front_act(App &a, int e, int what)
{
    std::vector<int> li = fr_lib(a, e);
    if (li.empty()) return;
    int pc = fr_first(a, e, GK_ROTK_PC), xb = fr_first(a, e, GK_ROTK_XBOX);
    if (e == FR_ROTK && pc >= 0 && xb >= 0) {
        std::string v = front_get("Rotk");
        if (v == "pc" || v == "xbox") { int i = v == "pc" ? pc : xb; if (what == 0) front_play(a, i); else open_settings(a, i); return; }
        g_front.ask = what;
        return;
    }
    if (what == 0) front_play(a, li[0]); else open_settings(a, li[0]);
}

static void add_game_buttons(App &a, int e, float x, float y)
{
    // The Return of the King on PC is set up from its own page (ROTK Fix goes into the game's folder)
    ImGui::SetCursorPos(ImVec2(x, y));
    if (orn_plate_button("Add a game folder...", ImVec2(S(190), S(40)), true)) {
        std::string d;
        if (pick_folder(d, L"Choose the game's folder (an extracted Xbox game, or the PC game with rotk.exe)")) add_game_from(a, d);
    }
    ImGui::SetCursorPos(ImVec2(x + S(202), y));
    if (orn_plate_button("Add an Xbox disc image...", ImVec2(S(210), S(40)), true)) {
        std::string f;
        if (pick_disc_image(f)) add_game_from(a, f);
    }
    if (e == FR_ROTK) {
        ImGui::SetCursorPos(ImVec2(x + S(424), y));
        if (orn_plate_button("Set up the PC game", ImVec2(S(170), S(40)), true)) { a.cur = -1; a.front = false; }
    }
}

// a folder or disc image dropped on the window: added to the games, and the carousel turns to it
static void front_drop(App &a, const char *utf8)
{
    if (!utf8 || !*utf8) return;
    wchar_t w[MAX_PATH * 2];
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, MAX_PATH * 2)) return;
    std::string path = ansi_path(w);    // as the file pickers do: a short name where the name has other letters
    if (path.empty()) return;
    size_t had = a.lib.size();
    int was = a.cur;
    add_game_from(a, path);
    if (a.cur >= 0 && (a.lib.size() != had || a.cur != was)) {
        g_front.sel = fr_entry_of(a.lib[a.cur].kind);
        g_front.turnedAt = ImGui::GetTime();
        g_front.artAsked[g_front.sel] = false;     // its art, for a made cover
        if (!a.front) back_to_front(a);
    }
}

// the version question for The Return of the King
static void rotk_version_popup(App &a)
{
    if (g_front.ask >= 0 && !ImGui::IsPopupOpen("##rotkver")) ImGui::OpenPopup("##rotkver");
    ImGui::SetNextWindowSize(ImVec2(S(470), 0));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("##rotkver", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) return;
    static bool remember = false;
    ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
    ImGui::TextColored(C_GOLD_H, "THE RETURN OF THE KING");
    ImGui::PopFont();
    ImGui::TextColored(C_DIM, g_front.ask == 0 ? "Which version would you like to play?" : "Settings for which version?");
    ImGui::Spacing();
    int pc = fr_first(a, FR_ROTK, GK_ROTK_PC), xb = fr_first(a, FR_ROTK, GK_ROTK_XBOX);
    int chosen = -1;
    float bw = (ImGui::GetContentRegionAvail().x - S(12)) * 0.5f;
    ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
    if (orn_plate_button("PC##rv", ImVec2(bw, S(46)), true)) chosen = 0;
    ImGui::SameLine(0, S(12));
    if (orn_plate_button("XBOX##rv", ImVec2(bw, S(46)), true)) chosen = 1;
    ImGui::PopFont();
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bw);
    ImGui::TextColored(C_DIM, "The original PC game, with ROTK Fix.");
    ImGui::PopTextWrapPos();
    ImGui::SameLine(bw + S(12) + ImGui::GetStyle().WindowPadding.x);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bw);
    ImGui::TextColored(C_DIM, "The Xbox version: co-op, \xc3\x89owyn, FSR and DLSS.");
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::Checkbox("Remember my choice", &remember);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x - S(90));
    if (ImGui::Button("Cancel", ImVec2(S(90), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
        g_front.ask = -1;
        ImGui::CloseCurrentPopup();
    }
    if (chosen >= 0) {
        if (remember) front_put("Rotk", chosen ? "xbox" : "pc");
        int what = g_front.ask, li = chosen ? xb : pc;
        g_front.ask = -1;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        if (what == 0) front_play(a, li); else open_settings(a, li);
        return;
    }
    ImGui::EndPopup();
}

// the background behind the front screen: the art of the game in front (group as backgrounds_rebuild takes it: 0 The
// Return of the King, 1 The Two Towers, 2 Fellowship, 3 none of the games' art)
static void front_bg_group(const App &a, int &group, std::string &dir)
{
    int e = g_front.sel;
    std::vector<int> li = fr_lib(a, e);
    dir.clear();
    if (li.empty()) { group = 3; return; }
    group = e == FR_ROTK ? 0 : e == FR_TT ? 1 : 2;
    if (e != FR_ROTK) dir = a.lib[li[0]].path;
}

static void draw_front(App &a, ImVec2 size, SDL_Renderer *ren)
{
    Front &f = g_front;
    if (!f.started) {
        f.started = true;
        std::string last = front_get("Last");
        int pick = -1;
        for (int e = 0; e < FR_COUNT; e++) if (last == k_frKey[e] && fr_owned(a, e)) pick = e;
        for (int e = 0; e < FR_COUNT && pick < 0; e++) if (fr_owned(a, e)) pick = e;
        if (const char *t = getenv("ATHELAS_FRONT_SEL")) pick = atoi(t) % FR_COUNT;   // testing
        f.sel = pick < 0 ? FR_ROTK : pick;
        f.pos = (float)f.sel;
    }
    if (!f.coversLooked) front_load_covers(ren);
    if (SDL_GetAtomicInt(&g_artDl) == 2) {
        SDL_SetAtomicInt(&g_artDl, 0);
        front_load_covers(ren);
        set_message(a, g_artDlMsg, g_artDlBad);
    }
    front_art_poll(a, ren);
    // the music of the game in front ([Front] Music = 0: off)
    {
        static int musicOn = -1;
        if (musicOn < 0) { musicOn = front_get("Music") != "0"; music_enable(musicOn != 0); }
        std::vector<int> li = fr_lib(a, f.sel);
        int tt = fr_first(a, f.sel, GK_TT_XBOX), fx = fr_first(a, f.sel, GK_FOTR_XBOX);
        if (f.sel == FR_TT && tt >= 0) music_play(MUSIC_TT, fr_music_copies(a, f.sel, GK_TT_XBOX));
        else if (f.sel == FR_FOTR && fx >= 0) music_play(MUSIC_FOTR, fr_music_copies(a, f.sel, GK_FOTR_XBOX));
        else music_play(MUSIC_NONE, std::string());
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImGuiIO &io = ImGui::GetIO();
    // the keys and the controller work the carousel directly (no cursor moving between the buttons)
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    bool modal = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || trans_running();
    bool padOn = (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;

    // the title
    // (small, in the top left corner: the boxes take the height)
    const float ts = F_TITLE->LegacySize * 0.5f * ImGui::GetStyle().FontScaleMain;   // (the window's size)
    ImVec2 tp = ImGui::GetWindowPos() + ImVec2(S(28), S(16));
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            if (dx || dy) dl->AddText(F_TITLE, ts, ImVec2(tp.x + dx * S(1.5f), tp.y + dy * S(1.5f)), IM_COL32(0, 0, 0, 110), "Athelas");
    dl->AddText(F_TITLE, ts, tp, ImGui::GetColorU32(C_GOLD_H), "Athelas");

    // turning: arrow keys, the D-pad or left stick, the mouse wheel, or a click on a box at the side
    int step = 0;
    if (!modal) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || (padOn && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft)))) step = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || (padOn && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight)))) step = 1;
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && io.MouseWheel != 0) step = io.MouseWheel > 0 ? -1 : 1;
    }
    if (step) { f.sel = (f.sel + step + FR_COUNT) % FR_COUNT; f.turnedAt = ImGui::GetTime(); g_frBtn = 0; }
    // ease towards it the short way round
    float target = (float)f.sel, diff = target - f.pos;
    while (diff > FR_COUNT * 0.5f) diff -= FR_COUNT;
    while (diff < -FR_COUNT * 0.5f) diff += FR_COUNT;
    f.pos += diff * std::min(1.0f, io.DeltaTime * 9.0f);
    if (fabsf(diff) < 0.001f) f.pos = target;
    while (f.pos < 0) f.pos += FR_COUNT;
    while (f.pos >= FR_COUNT) f.pos -= FR_COUNT;

    // the boxes, the farthest first
    // (the box, its name, version and buttons between the title and the bottom line)
    float H = std::min(size.y - S(60) - S(182), size.x * 0.58f);   // the same room above (under the arch) as below
    ImVec2 c = ImGui::GetWindowPos() + ImVec2(size.x * 0.5f, S(60) + H * 0.5f);
    float spread = std::min(size.x * 0.31f, H * 0.92f);
    struct B { int e; float d; };
    B order[FR_COUNT];
    for (int e = 0; e < FR_COUNT; e++) {
        float d = e - f.pos;
        while (d > FR_COUNT * 0.5f) d -= FR_COUNT;
        while (d < -FR_COUNT * 0.5f) d += FR_COUNT;
        order[e] = { e, d };
    }
    std::sort(order, order + FR_COUNT, [](const B &x, const B &y) { return fabsf(x.d) > fabsf(y.d); });
    ImRect *hit = f.hit;
    ImVec2 mouse = io.MousePos;
    for (const B &b : order) {
        float ad = fabsf(b.d);
        float x = c.x + (b.d > 0 ? 1 : -1) * spread * (std::min(ad, 1.0f) + 0.55f * std::max(0.0f, ad - 1.0f));
        bool front = ad < 0.5f;
        bool hov = front && !modal && ImGui::IsWindowHovered() && hit[b.e].Contains(mouse);
        hit[b.e] = draw_box(dl, a, b.e, b.d, ImVec2(x, c.y), H, hov);
    }
    // clicks: the box in front plays (or offers to add the game), one at the side turns the carousel to it
    bool clickPlay = false;
    if (!modal && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
        for (int k = FR_COUNT - 1; k >= 0; k--) {
            const B &b = order[k];          // nearest first
            if (!hit[b.e].Contains(mouse)) continue;
            if (b.e == f.sel) clickPlay = true; else { f.sel = b.e; f.turnedAt = ImGui::GetTime(); }
            break;
        }
    }

    // under the boxes: the name, which version, and what can be done
    const int e = f.sel;
    const bool owned = fr_owned(a, e);
    float y = c.y + H * 0.5f + S(18);
    float settle = std::min(1.0f, (float)(ImGui::GetTime() - f.turnedAt) / 0.25f);   // the words fade in as it turns
    ImU32 gold = IM_COL32(232, 196, 112, (int)(255 * settle)), dim = IM_COL32(168, 152, 124, (int)(255 * settle));
    (void)gold;   // the name is on the box itself
    std::string line;
    if (!owned) line = "Not added yet";
    else if (e == FR_ROTK) {
        bool pc = fr_first(a, e, GK_ROTK_PC) >= 0, xb = fr_first(a, e, GK_ROTK_XBOX) >= 0;
        std::string rv = front_get("Rotk");
        line = pc && xb ? (rv == "pc" ? "PC and Xbox  \xc2\xb7  plays the PC version" : rv == "xbox" ? "PC and Xbox  \xc2\xb7  plays the Xbox version" : "PC and Xbox")
             : pc ? "PC" : "Xbox";
    } else line = "Xbox";
    if (owned) {
        std::string pl = play_line(e);
        if (!pl.empty()) line += "  \xc2\xb7  " + pl;
    }
    centred_text(dl, F_SMALL, S(18), ImGui::GetWindowPos().x + size.x * 0.5f, y, size.x, dim, line.c_str());
    y += S(30);
    // "ask again" for a remembered version
    if (owned && e == FR_ROTK && !front_get("Rotk").empty() && fr_first(a, e, GK_ROTK_PC) >= 0 && fr_first(a, e, GK_ROTK_XBOX) >= 0) {
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        const char *lbl = "Ask which version";
        ImGui::SetCursorPos(ImVec2((size.x - ImGui::CalcTextSize(lbl).x) * 0.5f, y - S(10)));
        if (ImGui::TextLink(lbl)) front_put("Rotk", "");
        ImGui::PopFont();
        y += S(14);
    }

    float playW = S(170), playH = S(54), btnH = S(40), setW = S(120);
    float rowW = playW + S(14) + setW;
    float bx = (size.x - rowW) * 0.5f, by = y + S(2);
    // With a controller, as in COGBox: LB / RB light Play or Settings, A (or Start) presses the lit one; X still
    // opens Settings straight away. Turning the carousel lights Play again.
    if (padOn && !modal && ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) g_frBtn = 0;
    if (padOn && !modal && ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) g_frBtn = 1;
    bool padPress = padOn && !modal && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false));
    bool padPlay = padPress && g_frBtn == 0;
    bool keyPlay = !modal && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false));
    bool padSet = padOn && !modal && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) || (padPress && g_frBtn == 1));
    if (owned) {
        ImGui::SetCursorPos(ImVec2(bx, by));
        if (orn_arch_button("PLAY", ImVec2(playW, playH), F_PLAY, true) || padPlay || keyPlay || clickPlay) front_act(a, e, 0);
        ImGui::SetCursorPos(ImVec2(bx + playW + S(14), by + (playH - btnH) * 0.5f));
        if (orn_plate_button("Settings", ImVec2(setW, btnH), true) || padSet) front_act(a, e, 1);
        if (padOn && a.controllers > 0) {   // the lit one: a gold frame around it
            ImVec2 o = ImGui::GetWindowPos();
            ImVec2 p0 = g_frBtn == 0 ? o + ImVec2(bx, by) : o + ImVec2(bx + playW + S(14), by + (playH - btnH) * 0.5f);
            ImVec2 p1 = p0 + (g_frBtn == 0 ? ImVec2(playW, playH) : ImVec2(setW, btnH));
            float pulse = 0.65f + 0.35f * sinf((float)ImGui::GetTime() * 4.0f);
            dl->AddRect(p0 - ImVec2(S(5), S(5)), p1 + ImVec2(S(5), S(5)), IM_COL32(255, 222, 140, (int)(230 * pulse)), S(6), S(2.5f));
        }
    } else {
        float w = e == FR_ROTK ? S(594) : S(412);
        add_game_buttons(a, e, (size.x - w) * 0.5f, by + (playH - btnH) * 0.5f);
    }

    // the bottom line: box art, and the controls or a message
    float ly = size.y - S(52);
    ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
    {
        int st = SDL_GetAtomicInt(&g_artDl);
        const char *lbl = st == 1 ? "Downloading box art...##art" : "Get box art##art";
        ImGui::SetCursorPos(ImVec2(S(40), ly - S(6)));
        if (orn_plate_button(lbl, ImVec2(S(170), S(32)), st != 1) || (padOn && !modal && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)))
            art_download_start();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Downloads the original Xbox box art of the three games from RetroArch's collection\n"
                              "(libretro thumbnails, on GitHub) into the covers folder beside Athelas.\n"
                              "Your own scans there (fotr.png, tt.png, rotk.png) are used first.");
    }
    {   // Music on / off, remembered
        bool on = front_get("Music") != "0";
        const char *lbl = on ? "Music: on##mus" : "Music: off##mus";
        ImGui::SetCursorPos(ImVec2(S(222), ly - S(6)));
        if (orn_plate_button(lbl, ImVec2(S(120), S(32)), true) || (!modal && ImGui::IsKeyPressed(ImGuiKey_M, false))) {
            on = !on;
            front_put("Music", on ? "1" : "0");
            music_enable(on);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The game's own menu music, from your copy of the game (The Two Towers and Fellowship). M switches it.");
    }
    bool msg = !a.message.empty() && ImGui::GetTime() - a.messageAt < (a.messageBad ? 30.0 : 6.0);
    std::string hint, hint2;
    if (msg) hint = a.message;
    else if (a.controllers > 0 && padOn) {
        int ls = label_style(a);
        hint = ls == NAMES_PS ? "\xe2\x97\x80 \xe2\x96\xb6  choose  \xc2\xb7  L1 / R1: Play or Settings  \xc2\xb7  Cross: press  \xc2\xb7  Triangle: box art"
             : ls == NAMES_NINTENDO ? "\xe2\x97\x80 \xe2\x96\xb6  choose  \xc2\xb7  L / R: Play or Settings  \xc2\xb7  B: press  \xc2\xb7  X: box art"
             : "\xe2\x97\x80 \xe2\x96\xb6  choose  \xc2\xb7  LB / RB: Play or Settings  \xc2\xb7  A: press  \xc2\xb7  Y: box art";
        hint2 = fullscreen_hint(ls);
    } else { hint = "\xe2\x86\x90 \xe2\x86\x92  choose  \xc2\xb7  Enter: play"; hint2 = "F11: fullscreen"; }
    ImVec2 hs = ImGui::CalcTextSize(hint.c_str());
    if (!msg && !hint2.empty()) {   // the fullscreen shortcut, on the line above
        ImVec2 h2 = ImGui::CalcTextSize(hint2.c_str());
        ImGui::SetCursorPos(ImVec2(std::max(S(356), size.x - S(40) - h2.x), ly - S(22)));
        ImGui::TextColored(C_DIM, "%s", hint2.c_str());
    }
    ImGui::SetCursorPos(ImVec2(std::max(S(356), size.x - S(40) - hs.x), ly));
    ImGui::TextColored(msg ? (a.messageBad ? C_BAD : C_GOOD) : C_DIM, "%s", hint.c_str());
    ImGui::PopFont();
    ImGui::PopItemFlag();

    rotk_version_popup(a);
    draw_transition(a, size);
}
