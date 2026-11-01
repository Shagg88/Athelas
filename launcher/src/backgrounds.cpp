// Athelas - the pictures behind the launcher: game art, the player's own pictures, the drawn landscape.
#include "app.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#include "../third_party/stb_image.h"
#pragma GCC diagnostic pop

// ---------------------------------------------------------------------------
// Backgrounds: pictures in ROTKFix\backgrounds (the player's own screenshots
// can be added back later), shown one at a time with a slow cross-fade. Without any
// pictures, an original drawn landscape of misty mountains is used.

static void free_picture(Picture &p)
{
    if (p.tex) SDL_DestroyTexture(p.tex);
    if (p.blur) SDL_DestroyTexture(p.blur);
    p = Picture();
}

// Game art: the menu pictures of the player's own Return of the King (PC, else Xbox), read
// once on a worker thread (rotk_menu_art_load) and kept in memory as DXT1. In the
// list of backgrounds they are entries "\001art:N". Both Return of the King pages
// use them. The Two Towers and The Fellowship of the Ring pages have their own, read from their files when shown
// (entries "\002tt:..." and "\002fx:...", see tt_art_list and fotr_art_list).
std::vector<ArtPic> g_art;

SDL_AtomicInt g_artState;       // 0 not started, 1 reading, 2 done

static bool g_artFromPc = false;       // g_art came from the PC version (its 16 large pictures), not an Xbox copy's 4 small ones

static std::vector<std::string> g_artDirs;   // ROTK copies to try, PC first

static int SDLCALL art_worker(void *)
{
    std::vector<ArtPic> pics;
    for (const std::string &d : g_artDirs) {
        DWORD at = GetFileAttributesA(d.c_str());
        bool folder = at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY);
        if (rotk_menu_art_load(d, pics)) {
            g_artFromPc = folder && GetFileAttributesA((d + "\\Data\\Game\\fro\\fro03.scw").c_str()) != INVALID_FILE_ATTRIBUTES;
            break;
        }
    }
    // a different order each time the launcher opens
    for (size_t i = pics.size(); i > 1; i--) std::swap(pics[i - 1], pics[(size_t)SDL_rand((Sint32)i)]);
    g_art = std::move(pics);
    SDL_SetAtomicInt(&g_artState, 2);
    return 0;
}

void art_start(const std::vector<std::string> &dirs)
{
    if (SDL_GetAtomicInt(&g_artState) != 0 || dirs.empty()) return;
    g_artDirs = dirs;
    SDL_SetAtomicInt(&g_artState, 1);
    SDL_Thread *t = SDL_CreateThread(art_worker, "art", nullptr);
    if (t) SDL_DetachThread(t); else SDL_SetAtomicInt(&g_artState, 2);
}

bool art_ready() { return SDL_GetAtomicInt(&g_artState) == 2; }

int g_bgShow = BG_ART_AND_MINE;

const char *k_bgShowName[BG_SHOW_COUNT] = { "Game art and my pictures", "Game art", "My pictures", "None" };

static const char *k_bgShowKey[BG_SHOW_COUNT] = { "both", "art", "mine", "none" };

void list_pictures(const std::string &dir, std::vector<std::string> &out, size_t maxCount)
{
    std::vector<std::pair<FILETIME, std::string>> found;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string n = fd.cFileName, ext = n.size() > 4 ? n.substr(n.size() - 4) : "";
        for (auto &ch : ext) ch = (char)tolower((unsigned char)ch);
        if (ext == ".jpg" || ext == "jpeg" || ext == ".png") found.push_back({ fd.ftLastWriteTime, dir + "\\" + n });
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return CompareFileTime(&a.first, &b.first) > 0; });
    for (size_t i = 0; i < found.size() && i < maxCount; i++) out.push_back(found[i].second);
}

// Title, legal and loading screens are grey text on black and make poor
// backgrounds; skip screenshots that have almost no colour or light.
static bool looks_like_text_screen(const unsigned char *px, int w, int h)
{
    double chroma = 0, light = 0; int n = 0;
    for (int y = 0; y < h; y += 8)
        for (int x = 0; x < w; x += 8) {
            const unsigned char *c = px + ((size_t)y * w + x) * 4;
            int mx = std::max(c[0], std::max(c[1], c[2])), mn = std::min(c[0], std::min(c[1], c[2]));
            chroma += mx - mn; light += mx; n++;
        }
    // (dim, muted scenes such as the misty forests still count as pictures)
    return n == 0 || chroma / n < 2.5 || light / n < 14.0;
}

// Halve (2x2 average) until no wider than the launcher could ever show.
static void shrink_picture(unsigned char *&px, int &w, int &h)
{
    while (w > 1600 && h >= 2) {
        int nw = w / 2, nh = h / 2;
        unsigned char *o = (unsigned char *)malloc((size_t)nw * nh * 4);
        if (!o) return;
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                const unsigned char *a = px + ((size_t)(2 * y) * w + 2 * x) * 4, *b = a + (size_t)w * 4;
                unsigned char *d = o + ((size_t)y * nw + x) * 4;
                for (int c = 0; c < 4; c++) d[c] = (unsigned char)((a[c] + a[c + 4] + b[c] + b[c + 4] + 2) / 4);
            }
        stbi_image_free(px);
        px = o; w = nw; h = nh;   // freed with free(), same as stbi_image_free
    }
}

int SDLCALL bg_worker(void *p)
{
    BgJob *j = (BgJob *)p;
    size_t n = j->files.size();
    for (size_t tries = 0; tries < n && !j->px; tries++) {
        int i = (int)((j->start + tries) % n);
        int w, h, c;
        if (j->files[i].compare(0, 5, "\001art:") == 0) {
            size_t k = (size_t)atoi(j->files[i].c_str() + 5);
            if (k >= g_art.size()) continue;
            const ArtPic &ap = g_art[k];
            unsigned char *px = dxt1_to_rgba(ap.dxt.data(), ap.dxt.size(), ap.w, ap.h);
            if (!px) continue;
            j->px = px; j->w = ap.w; j->h = ap.h; j->showH = ap.w * 3 / 4; j->found = i;
            break;
        }
        if (j->files[i].compare(0, 4, "\002fx:") == 0) {     // The Fellowship of the Ring: 640x480
            unsigned char *px = fotr_picture_load(j->files[i], w, h);
            if (!px) continue;
            j->px = px; j->w = w; j->h = h; j->showH = h; j->found = i;
            break;
        }
        if (j->files[i].compare(0, 4, "\002tt:") == 0) {     // The Two Towers: 640x448 or 512x256, shown at 4:3
            unsigned char *px = tt_picture_load(j->files[i], w, h);
            if (!px) continue;
            j->px = px; j->w = w; j->h = h; j->showH = w * 3 / 4; j->found = i;
            break;
        }
        unsigned char *px = stbi_load(j->files[i].c_str(), &w, &h, &c, 4);
        if (!px) continue;
        if (j->files[i].find("\\Screenshots\\") != std::string::npos && looks_like_text_screen(px, w, h)) { stbi_image_free(px); continue; }
        shrink_picture(px, w, h);
        j->px = px; j->w = w; j->h = h; j->showH = h; j->found = i;
    }
    if (j->px) {                       // how bright it is, so pale pictures (sketches, mist) can be darkened more
        double sum = 0; int cnt = 0;
        for (int y = 0; y < j->h; y += 8)
            for (int x = 0; x < j->w; x += 8) {
                const unsigned char *c = j->px + ((size_t)y * j->w + x) * 4;
                sum += 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]; cnt++;
            }
        j->lum = cnt ? (float)(sum / cnt) : 0;
    }
    SDL_SetAtomicInt(&j->done, 1);
    return 0;
}

// Ask for the next picture (does nothing while one is still being read).
void backgrounds_next(Backgrounds &b)
{
    if (b.files.empty() || b.job) return;
    BgJob *j = new BgJob();
    j->files = b.files;
    j->start = (b.index + 1) % (int)b.files.size();
    j->gen = b.gen;
    SDL_SetAtomicInt(&j->done, 0);
    SDL_Thread *t = SDL_CreateThread(bg_worker, "bg", j);
    if (!t) { delete j; return; }
    SDL_DetachThread(t);
    b.job = j;
}

// Called every frame: show a picture once it has been read.
void backgrounds_poll(Backgrounds &b, SDL_Renderer *ren)
{
    BgJob *j = b.job;
    if (!j || !SDL_GetAtomicInt(&j->done)) return;
    b.job = nullptr;
    if (j->gen != b.gen) {         // the list changed while it was read
        if (j->px) free(j->px);
        delete j;
        return;
    }
    if (j->px) {
        SDL_Surface *s = SDL_CreateSurfaceFrom(j->w, j->h, SDL_PIXELFORMAT_RGBA32, j->px, j->w * 4);
        SDL_Texture *t = s ? SDL_CreateTextureFromSurface(ren, s) : nullptr;
        if (s) SDL_DestroySurface(s);
        if (t) {
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
            free_picture(b.prev);
            b.prev = b.cur;
            b.cur = Picture();
            b.cur.tex = t; b.cur.w = j->w; b.cur.h = j->showH; b.cur.lum = j->lum;
            // the blurred copy: a 32-pixel-wide average, smoothed again when stretched
            const int bw = 32, bh = std::max(1, 32 * j->showH / std::max(1, j->w));
            std::vector<unsigned char> sm((size_t)bw * bh * 4);
            for (int oy = 0; oy < bh; oy++)
                for (int ox = 0; ox < bw; ox++) {
                    int x0 = ox * j->w / bw, x1 = std::max(x0 + 1, (ox + 1) * j->w / bw);
                    int y0 = oy * j->h / bh, y1 = std::max(y0 + 1, (oy + 1) * j->h / bh);
                    unsigned sum[3] = { 0, 0, 0 }, n = 0;
                    for (int y = y0; y < y1; y += 2)
                        for (int x = x0; x < x1; x += 2) {
                            const unsigned char *c = j->px + ((size_t)y * j->w + x) * 4;
                            sum[0] += c[0]; sum[1] += c[1]; sum[2] += c[2]; n++;
                        }
                    unsigned char *d = &sm[((size_t)oy * bw + ox) * 4];
                    for (int c = 0; c < 3; c++) d[c] = (unsigned char)(n ? sum[c] / n : 0);
                    d[3] = 255;
                }
            if (SDL_Surface *bs = SDL_CreateSurfaceFrom(bw, bh, SDL_PIXELFORMAT_RGBA32, sm.data(), bw * 4)) {
                b.cur.blur = SDL_CreateTextureFromSurface(ren, bs);
                if (b.cur.blur) SDL_SetTextureScaleMode(b.cur.blur, SDL_SCALEMODE_LINEAR);
                SDL_DestroySurface(bs);
            }
            b.changedAt = ImGui::GetTime();
        }
        b.index = j->found;
        stbi_image_free(j->px);
    } else {
        b.files.clear();          // nothing usable: keep the drawn landscape
    }
    delete j;
}

// Make the list for the page shown: game art (The Return of the King's for both of
// its pages, The Two Towers' own for its page) and/or the player's own pictures in
// ROTKFix\backgrounds.
void backgrounds_rebuild(Backgrounds &b, int group, const std::string &launcherDir, const std::string &ttDir)
{
    b.files.clear();
    b.gen++;
    b.group = group;
    b.fresh = true;
    b.artIn = art_ready();
    b.index = -1;
    if ((g_bgShow == BG_ART_AND_MINE || g_bgShow == BG_ART) && group == 0 && b.artIn && g_artFromPc)
        for (size_t i = 0; i < g_art.size(); i++) b.files.push_back("\001art:" + std::to_string(i));
    if ((g_bgShow == BG_ART_AND_MINE || g_bgShow == BG_ART) && group == 0 && b.artIn && !g_artFromPc) {
        // only an Xbox copy: its 4 menu pictures are small (512 x 512), so The Two Towers' gallery and Fellowship's
        // sketches stand in, from whichever the player has (ttDir: "<TT>\n<FOTR>", see ta_mix_dirs); with neither,
        // the 4 pictures after all
        size_t first = b.files.size(), nl = ttDir.find('\n');
        std::string tt = ttDir.substr(0, nl), fx = nl == std::string::npos ? std::string() : ttDir.substr(nl + 1);
        if (!tt.empty()) tt_art_list(tt, b.files);
        if (!fx.empty()) fotr_art_list(fx, b.files);
        if (b.files.size() == first) for (size_t i = 0; i < g_art.size(); i++) b.files.push_back("\001art:" + std::to_string(i));
        for (size_t i = b.files.size(); i > first + 1; i--)
            std::swap(b.files[i - 1], b.files[first + (size_t)SDL_rand((Sint32)(i - first))]);
    }
    if ((g_bgShow == BG_ART_AND_MINE || g_bgShow == BG_ART) && (group == 1 || group == 2) && !ttDir.empty()) {
        size_t first = b.files.size();
        if (group == 1) tt_art_list(ttDir, b.files); else fotr_art_list(ttDir, b.files);
        for (size_t i = b.files.size(); i > first + 1; i--)     // a different order each time
            std::swap(b.files[i - 1], b.files[first + (size_t)SDL_rand((Sint32)(i - first))]);
    }
    if ((g_bgShow == BG_ART_AND_MINE || g_bgShow == BG_ART) && group == 5) {
        // every game for now: The Two Towers' gallery and Fellowship's sketches, shuffled together ("<TT>\n<FOTR>")
        size_t first = b.files.size(), nl = ttDir.find('\n');
        std::string tt = ttDir.substr(0, nl), fx = nl == std::string::npos ? std::string() : ttDir.substr(nl + 1);
        if (!tt.empty()) tt_art_list(tt, b.files);
        if (!fx.empty()) fotr_art_list(fx, b.files);
        for (size_t i = b.files.size(); i > first + 1; i--)
            std::swap(b.files[i - 1], b.files[first + (size_t)SDL_rand((Sint32)(i - first))]);
    }
    if ((g_bgShow == BG_ART_AND_MINE || g_bgShow == BG_ART) && group == 4) {
        // The Third Age: stills of the films the player has watched, saved by the game (art\*.png in its folder),
        // mixed with the other games' art from whichever the player has (ttDir: "<TT>\n<FOTR>", see ta_mix_dirs)
        size_t first = b.files.size();
        list_pictures(exe_dir() + "\\" DIR_TA_XBOX "\\art", b.files, 300);
        size_t nl = ttDir.find('\n');
        std::string tt = ttDir.substr(0, nl), fx = nl == std::string::npos ? std::string() : ttDir.substr(nl + 1);
        if (!tt.empty()) tt_art_list(tt, b.files);
        if (!fx.empty()) fotr_art_list(fx, b.files);
        if (b.artIn && g_artFromPc) for (size_t i = 0; i < g_art.size(); i++) b.files.push_back("\001art:" + std::to_string(i));
        for (size_t i = b.files.size(); i > first + 1; i--)     // a different order each time
            std::swap(b.files[i - 1], b.files[first + (size_t)SDL_rand((Sint32)(i - first))]);
    }
    if (g_bgShow == BG_ART_AND_MINE || g_bgShow == BG_MINE)
        list_pictures(launcherDir + "\\ROTKFix\\backgrounds", b.files, 100);
    if (b.files.empty()) {         // nothing to show here: the drawn landscape
        free_picture(b.cur);
        free_picture(b.prev);
    }
}

void bg_show_load()
{
    char v[16] = {};
    GetPrivateProfileStringA("Background", "Show", "both", v, sizeof v, (exe_dir() + "\\ROTKFix\\launcher.ini").c_str());
    for (int i = 0; i < BG_SHOW_COUNT; i++) if (!_stricmp(v, k_bgShowKey[i])) g_bgShow = i;
}

void bg_show_save()
{
    std::string d = exe_dir() + "\\ROTKFix";
    SHCreateDirectoryExA(NULL, d.c_str(), NULL);
    WritePrivateProfileStringA("Background", "Show", k_bgShowKey[g_bgShow], (d + "\\launcher.ini").c_str());
}

// Draw a texture to cover the rect (cropping, never stretching).
static void draw_cover_tex(ImDrawList *dl, SDL_Texture *tex, float ia, ImVec2 size, ImU32 col)
{
    float sa = size.x / size.y;
    ImVec2 uv0(0, 0), uv1(1, 1);
    if (ia > sa) { float f = sa / ia; uv0.x = (1 - f) * 0.5f; uv1.x = 1 - uv0.x; }
    else         { float f = ia / sa; uv0.y = (1 - f) * 0.35f; uv1.y = uv0.y + f; }
    dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(0, 0), size, uv0, uv1, col);
}

// Part of a texture with its own alpha at each corner (for soft edges).
static void image_fade(ImDrawList *dl, SDL_Texture *tex, ImVec2 a, ImVec2 b, ImVec2 uva, ImVec2 uvb, int aTL, int aTR, int aBR, int aBL)
{
    dl->PushTexture((ImTextureID)(intptr_t)tex);
    dl->PrimReserve(6, 4);
    ImDrawIdx i = (ImDrawIdx)dl->_VtxCurrentIdx;
    dl->PrimWriteIdx(i); dl->PrimWriteIdx((ImDrawIdx)(i + 1)); dl->PrimWriteIdx((ImDrawIdx)(i + 2));
    dl->PrimWriteIdx(i); dl->PrimWriteIdx((ImDrawIdx)(i + 2)); dl->PrimWriteIdx((ImDrawIdx)(i + 3));
    dl->PrimWriteVtx(a, uva, IM_COL32(255, 255, 255, aTL));
    dl->PrimWriteVtx(ImVec2(b.x, a.y), ImVec2(uvb.x, uva.y), IM_COL32(255, 255, 255, aTR));
    dl->PrimWriteVtx(b, uvb, IM_COL32(255, 255, 255, aBR));
    dl->PrimWriteVtx(ImVec2(a.x, b.y), ImVec2(uva.x, uvb.y), IM_COL32(255, 255, 255, aBL));
    dl->PopTexture();
}

// Draw a picture over the window. Big enough pictures fill it (cropped). Smaller ones
// (the Two Towers gallery, 640 wide; the Xbox menu art, 512) would be blown up and
// blurry, so they are shown nearer their own size, over a blurred copy of themselves,
// with soft edges.
static void draw_cover(ImDrawList *dl, const Picture &p, ImVec2 size, float alpha)
{
    if (!p.tex || alpha <= 0.0f) return;
    int A = (int)(alpha * 255);
    float coverScale = std::max(size.x / p.w, size.y / p.h);
    if (coverScale <= 1.3f || !p.blur) { draw_cover_tex(dl, p.tex, (float)p.w / p.h, size, IM_COL32(255, 255, 255, A)); return; }
    draw_cover_tex(dl, p.blur, (float)p.w / p.h, size, IM_COL32(150, 150, 150, A));   // dimmed, so the sharp picture stands out
    float sc = std::min(size.x / p.w, size.y / p.h);
    float w = p.w * sc, h = p.h * sc, x = (size.x - w) * 0.5f, y = (size.y - h) * 0.4f;
    if (w >= size.x - 1) {             // fills the width: soft top and bottom edges
        float f = h * 0.14f;
        image_fade(dl, p.tex, ImVec2(x, y), ImVec2(x + w, y + f), ImVec2(0, 0), ImVec2(1, 0.14f), 0, 0, A, A);
        image_fade(dl, p.tex, ImVec2(x, y + f), ImVec2(x + w, y + h - f), ImVec2(0, 0.14f), ImVec2(1, 0.86f), A, A, A, A);
        image_fade(dl, p.tex, ImVec2(x, y + h - f), ImVec2(x + w, y + h), ImVec2(0, 0.86f), ImVec2(1, 1), A, A, 0, 0);
    } else {                           // fills the height: soft left and right edges
        float f = w * 0.14f;
        image_fade(dl, p.tex, ImVec2(x, y), ImVec2(x + f, y + h), ImVec2(0, 0), ImVec2(0.14f, 1), 0, A, A, 0);
        image_fade(dl, p.tex, ImVec2(x + f, y), ImVec2(x + w - f, y + h), ImVec2(0.14f, 0), ImVec2(0.86f, 1), A, A, A, A);
        image_fade(dl, p.tex, ImVec2(x + w - f, y), ImVec2(x + w, y + h), ImVec2(0.86f, 0), ImVec2(1, 1), A, 0, 0, A);
    }
}

// Original drawn backdrop: layered mountain ridges in mist under a pale moon.
static float ridge(float x, float seed)
{
    return sinf(x * 0.0071f + seed) * 0.5f + sinf(x * 0.0173f + seed * 2.1f) * 0.28f + sinf(x * 0.041f + seed * 3.7f) * 0.12f
         + fabsf(sinf(x * 0.0023f + seed * 0.7f)) * 0.6f;
}

static void draw_landscape(ImDrawList *dl, ImVec2 size)
{
    ImU32 top = IM_COL32(12, 28, 19, 255), mid = IM_COL32(40, 72, 50, 255);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, size.y * 0.62f), top, top, mid, mid);
    dl->AddRectFilled(ImVec2(0, size.y * 0.62f), size, IM_COL32(6, 13, 9, 255));
    ImVec2 moon(size.x * 0.74f, size.y * 0.2f);
    for (int i = 10; i > 0; i--) dl->AddCircleFilled(moon, S(26) + i * S(14), IM_COL32(200, 220, 190, 5));
    dl->AddCircleFilled(moon, S(26), IM_COL32(214, 226, 204, 190));
    struct L { float base, amp, seed; ImU32 col; } layers[] = {
        { 0.40f, 0.16f, 1.3f, IM_COL32(30, 50, 37, 255) },
        { 0.50f, 0.13f, 4.1f, IM_COL32(21, 38, 27, 255) },
        { 0.62f, 0.10f, 7.7f, IM_COL32(13, 27, 18, 255) },
        { 0.74f, 0.07f, 2.9f, IM_COL32(6, 15, 10, 255) } };
    float step = S(6);
    for (auto &l : layers) {
        for (float x = 0; x < size.x; x += step) {
            float y0 = size.y * (l.base - l.amp * ridge(x / g_scale, l.seed));
            float y1 = size.y * (l.base - l.amp * ridge((x + step) / g_scale, l.seed));
            dl->AddQuadFilled(ImVec2(x, y0), ImVec2(x + step + 1, y1), ImVec2(x + step + 1, size.y), ImVec2(x, size.y), l.col);
        }
        // pines along the nearer ridges
        if (l.base > 0.55f) {
            for (float x = 0; x < size.x; x += S(7)) {
                float r = fabsf(sinf(x / g_scale * 12.9898f + l.seed * 78.233f));
                float y = size.y * (l.base - l.amp * ridge(x / g_scale, l.seed)) + S(2);
                float h = S(10) + S(18) * r, w = h * 0.32f;
                dl->AddTriangleFilled(ImVec2(x - w, y), ImVec2(x, y - h), ImVec2(x + w, y), l.col);
            }
        }
        // mist above each ridge line
        float my = size.y * l.base;
        dl->AddRectFilledMultiColor(ImVec2(0, my - size.y * 0.06f), ImVec2(size.x, my + size.y * 0.02f),
                                    IM_COL32(140, 170, 145, 0), IM_COL32(140, 170, 145, 0), IM_COL32(140, 170, 145, 20), IM_COL32(140, 170, 145, 20));
    }
}

void draw_background(ImDrawList *dl, ImVec2 size, Backgrounds &b)
{
    if (b.cur.tex) {
        float t = (float)(ImGui::GetTime() - b.changedAt), fade = std::min(1.0f, t / 1.5f);
        dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(6, 13, 9, 255));
        if (fade < 1.0f) {
            if (b.prev.tex) draw_cover(dl, b.prev, size, 1.0f);
            else draw_landscape(dl, size);      // the first picture fades in over the drawn backdrop
        }
        draw_cover(dl, b.cur, size, fade);
    } else {
        draw_landscape(dl, size);
    }
    // darken for legibility: lighter at the top (banner), darker behind the settings
    bool pic = b.cur.tex != nullptr;   // pictures are brighter than the drawn landscape
    ImU32 a0 = IM_COL32(3, 9, 5, pic ? 120 : 50), a1 = IM_COL32(3, 9, 5, pic ? 110 : 130);
    if (pic) {                         // and pale ones (sketches on white paper, mist) need more
        auto extra = [](const Picture &p) { return p.tex ? std::min(130.0f, std::max(0.0f, (p.lum - 70.0f) * 1.4f)) : 0.0f; };
        float fade = std::min(1.0f, (float)(ImGui::GetTime() - b.changedAt) / 1.5f);
        float e = extra(b.prev) * (1 - fade) + extra(b.cur) * fade;
        if (e > 0) dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(3, 9, 5, (int)e));
    }
    if (pic) {
        // the picture mostly clear (the settings panel has its own backing): darker only behind the banner's text
        // at the top and the buttons at the bottom
        float top = S(150), bottom = S(110);
        ImU32 band = IM_COL32(3, 9, 5, 135), clear = IM_COL32(3, 9, 5, 35);
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, top), band, band, clear, clear);
        dl->AddRectFilled(ImVec2(0, top), ImVec2(size.x, size.y - bottom), clear);
        dl->AddRectFilledMultiColor(ImVec2(0, size.y - bottom), size, clear, clear, band, band);
    } else {
        dl->AddRectFilledMultiColor(ImVec2(0, 0), size, a0, a0, a1, a1);
    }
    orn_window_frame(dl, size);   // carved stone border (ornament.cpp)
}
