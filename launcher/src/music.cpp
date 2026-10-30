// Athelas - the front screen's music (see music.h)
#include "music.h"
#include "library.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>

// ---- reading a file of the game: from its folder, or from inside its disc image
static bool read_game_file(const std::string &game, const char *rel, std::vector<uint8_t> &out, size_t maxBytes)
{
    out.clear();
    std::string path = game;
    unsigned long long at = 0, len = 0;
    DWORD attr = GetFileAttributesA(game.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        uint32_t sz = 0;
        if (!iso_locate(game, rel, at, sz)) return false;
        len = sz;
    } else path = game + "\\" + rel;
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    if (!len) {
        LARGE_INTEGER s;
        if (!GetFileSizeEx(f, &s)) { CloseHandle(f); return false; }
        len = (unsigned long long)s.QuadPart;
    }
    if (len > maxBytes || len < 64) { CloseHandle(f); return false; }
    LARGE_INTEGER p; p.QuadPart = (LONGLONG)at;
    SetFilePointerEx(f, p, NULL, FILE_BEGIN);
    out.resize((size_t)len);
    size_t got = 0;
    while (got < out.size()) {
        DWORD n = 0, want = (DWORD)std::min<size_t>(out.size() - got, 1 << 24);
        if (!ReadFile(f, out.data() + got, want, &n, NULL) || !n) break;
        got += n;
    }
    CloseHandle(f);
    if (got != out.size()) { out.clear(); return false; }
    return true;
}
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// ---- Xbox ADPCM: 36-byte IMA blocks per channel, 64 samples each (the header's and 63 of the 64 nibbles)
static const int kIndex[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
static const int kStep[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
    29794, 32767 };
static void xbox_adpcm(const uint8_t *b, size_t len, int ch, std::vector<int16_t> &out)
{
    size_t bs = 36u * ch, nb = len / bs;
    out.resize(nb * 64 * ch);
    for (size_t k = 0; k < nb; k++) {
        const uint8_t *blk = b + k * bs;
        int16_t *o = &out[k * 64 * ch];
        for (int c = 0; c < ch; c++) {
            int pred = (int16_t)rd16(blk + c * 4), idx = std::min<int>(blk[c * 4 + 2], 88);
            o[c] = (int16_t)pred;
            const uint8_t *d = blk + ch * 4;
            for (int w = 0; w < 8; w++)
                for (int j = 0; j < 8; j++) {
                    int s = w * 8 + j + 1;
                    if (s >= 64) break;
                    int n = (d[(w * ch + c) * 4 + j / 2] >> ((j & 1) * 4)) & 15;
                    int step = kStep[idx], diff = step >> 3;
                    if (n & 1) diff += step >> 2;
                    if (n & 2) diff += step >> 1;
                    if (n & 4) diff += step;
                    pred = std::max(-32768, std::min(32767, n & 8 ? pred - diff : pred + diff));
                    idx = std::max(0, std::min(88, idx + kIndex[n]));
                    o[s * ch + c] = (int16_t)pred;
                }
        }
    }
}

// The Two Towers: wave bank version 2 (WBND): segments at +8 (offset, length) x4: bank data, entries (24 bytes:
// flags, format, play offset, play length, loop), -, wave data. Format: tag (bits 0-1: 0 PCM, 1 Xbox ADPCM),
// channels (2-4), rate (5-22). The bank's cues (S_SB_INTRFACE.xsb) are upgrades, credits, the main menu, the score
// screen, in the entries' order: the main menu's is the third.
static bool load_tt(const std::string &game, std::vector<int16_t> &pcm, int &channels, int &rate)
{
    std::vector<uint8_t> d;
    if (!read_game_file(game, "SOUND\\S_WB_INTRFACE.xwb", d, 64u << 20) || memcmp(d.data(), "WBND", 4) || rd32(&d[4]) != 2) return false;
    uint32_t entOff = rd32(&d[16]), entLen = rd32(&d[20]), dataOff = rd32(&d[32]);
    if (entLen < 3 * 24 || (uint64_t)entOff + entLen > d.size()) return false;
    const uint8_t *e = &d[entOff + 2 * 24];
    uint32_t fmt = rd32(e + 4), off = rd32(e + 8), len = rd32(e + 12);
    channels = (fmt >> 2) & 7; rate = (fmt >> 5) & 0x3FFFF;
    if ((fmt & 3) != 1 || channels < 1 || channels > 2 || rate < 8000 || (uint64_t)dataOff + off + len > d.size()) return false;
    xbox_adpcm(&d[dataOff + off], len, channels, pcm);
    return !pcm.empty();
}

// The Third Age: the front end's streamed sound, Data\Game\e98c\e98c03.sas, is its menu music: 32 KB chunks, each a
// 296-byte header (zeros) and 902 Xbox ADPCM blocks of one channel (mono, 36 bytes), the left and right channels'
// chunks taking turns (found by which chunk carries on where the last one ended). 44.1 kHz, about 4 minutes.
static bool load_ta(const std::string &game, std::vector<int16_t> &pcm, int &channels, int &rate)
{
    std::vector<uint8_t> d;
    if (!read_game_file(game, "Data\\Game\\e98c\\e98c03.sas", d, 64u << 20)) return false;
    const size_t kChunk = 32768, kHead = 296, kBlocks = 902, kSamples = kBlocks * 64;
    size_t pairs = d.size() / (2 * kChunk);
    if (!pairs) return false;
    pcm.assign(pairs * kSamples * 2, 0);
    std::vector<int16_t> one;
    for (size_t k = 0; k < pairs * 2; k++) {
        const uint8_t *c = &d[k * kChunk];
        for (size_t i = 0; i < kHead; i++) if (c[i]) return false;        // not the layout expected
        if (c[kHead + 2] > 88 || c[kHead + 3]) return false;
        xbox_adpcm(c + kHead, kBlocks * 36, 1, one);
        if (one.size() != kSamples) return false;
        int16_t *o = &pcm[(k / 2) * kSamples * 2 + (k & 1)];
        for (size_t i = 0; i < kSamples; i++) o[i * 2] = one[i];
    }
    size_t first = 0, n = pcm.size() / 2;                                 // the silence before the music
    while (first < n && abs(pcm[first * 2]) < 64 && abs(pcm[first * 2 + 1]) < 64) first++;
    if (first >= n) return false;
    pcm.erase(pcm.begin(), pcm.begin() + first * 2);
    channels = 2; rate = 44100;
    return true;
}

// part of a game file (from its folder or inside its disc image): len bytes at off
static bool read_game_range(const std::string &game, const char *rel, unsigned long long off, size_t len,
                            std::vector<uint8_t> &out, unsigned long long *fileSize = nullptr)
{
    out.clear();
    std::string path = game;
    unsigned long long at = 0, total = 0;
    DWORD attr = GetFileAttributesA(game.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        uint32_t sz = 0;
        if (!iso_locate(game, rel, at, sz)) return false;
        total = sz;
    } else path = game + "\\" + rel;
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    if (!total) {
        LARGE_INTEGER s;
        if (!GetFileSizeEx(f, &s)) { CloseHandle(f); return false; }
        total = (unsigned long long)s.QuadPart;
    }
    if (fileSize) *fileSize = total;
    if (off >= total) { CloseHandle(f); return false; }
    len = (size_t)std::min<unsigned long long>(len, total - off);
    LARGE_INTEGER p; p.QuadPart = (LONGLONG)(at + off);
    SetFilePointerEx(f, p, NULL, FILE_BEGIN);
    out.resize(len);
    size_t got = 0;
    while (got < len) {
        DWORD n = 0, want = (DWORD)std::min<size_t>(len - got, 1 << 24);
        if (!ReadFile(f, out.data() + got, want, &n, NULL) || !n) break;
        got += n;
    }
    CloseHandle(f);
    if (got != len) { out.clear(); return false; }
    return true;
}

// The Fellowship of the Ring, first choice: the Shire theme, MSX_Hobbiton_Explore.wav in Scenes\Media\Music2.xpa
// (the game's DirectMusic archive). Its index: per track a name, and 0x108 bytes after it the offset of the track's
// RIFF WAVE (16-bit PCM). Only that track is read (the archive is 400 MB).
static bool load_fotr_named(const std::string &game, const char *name, std::vector<int16_t> &pcm, int &channels, int &rate);
static bool load_fotr_shire(const std::string &game, std::vector<int16_t> &pcm, int &channels, int &rate)
{
    return load_fotr_named(game, "MSX_Hobbiton_Explore.wav", pcm, channels, rate);
}
static bool load_fotr_named(const std::string &game, const char *name, std::vector<int16_t> &pcm, int &channels, int &rate)
{
    static const char *rel = "Scenes\\Media\\Music2.xpa";
    std::vector<uint8_t> h;
    if (!read_game_range(game, rel, 0, 0x40000, h)) return false;
    const uint8_t *hit = nullptr;
    size_t nl = strlen(name);
    for (size_t i = 0; i + nl + 0x10c <= h.size() && !hit; i++)
        if (!memcmp(&h[i], name, nl + 1) && (i == 0 || h[i - 1] < 0x20 || h[i - 1] >= 0x7F)) hit = &h[i];
    if (!hit) return false;
    uint32_t riff = rd32(hit + 0x108);
    std::vector<uint8_t> w;
    if (!read_game_range(game, rel, riff, 4096, w) || w.size() < 44 || memcmp(&w[0], "RIFF", 4) || memcmp(&w[8], "WAVE", 4)) return false;
    size_t o = 12, dataAt = 0;
    uint32_t dataLen = 0;
    int tag = 0, bits = 0;
    while (o + 8 <= w.size()) {
        uint32_t cl = rd32(&w[o + 4]);
        if (!memcmp(&w[o], "fmt ", 4) && o + 24 <= w.size()) { tag = rd16(&w[o + 8]); channels = rd16(&w[o + 10]); rate = (int)rd32(&w[o + 12]); bits = rd16(&w[o + 22]); }
        if (!memcmp(&w[o], "data", 4)) { dataAt = o + 8; dataLen = cl; break; }
        o += 8 + cl + (cl & 1);
    }
    if (!dataAt || tag != 1 || bits != 16 || channels < 1 || channels > 2 || rate < 8000) return false;
    size_t cap = (size_t)rate * channels * 2 * 600;   // (the Shire's is 6 min 22 s; at most 10 minutes is kept)
    std::vector<uint8_t> raw;
    if (!read_game_range(game, rel, riff + dataAt, std::min<size_t>(dataLen, cap), raw) || raw.size() < (size_t)rate * 4) return false;
    pcm.resize(raw.size() / 2);
    memcpy(pcm.data(), raw.data(), pcm.size() * 2);
    return true;
}

// The Fellowship of the Ring, otherwise: an XMV movie: a file header (next packet size, this one's size, max size, "xobX",
// version, width, height, duration, audio track count, then 12 bytes a track: compression, channels, rate, bits, flags),
// then packets: next packet's size, the video's size (low 23 bits) and 4 more bytes, the size of each track's audio
// (low 23 bits), then the video data and each track's audio in turn. Track 0 is PCM here.
static bool load_fotr(const std::string &game, std::vector<int16_t> &pcm, int &channels, int &rate)
{
    std::vector<uint8_t> d;
    if (!read_game_file(game, "Movies\\start_menu.xmv", d, 128u << 20) || d.size() < 48 || memcmp(&d[12], "xobX", 4)) return false;
    uint32_t next = rd32(&d[0]), pos = rd32(&d[4]);
    unsigned tracks = rd16(&d[32]);
    if (tracks < 1 || tracks > 8 || 36 + tracks * 12 > d.size()) return false;
    unsigned comp = rd16(&d[36]);
    channels = rd16(&d[38]); rate = (int)rd32(&d[40]);
    unsigned bits = rd16(&d[44]);
    if ((comp != 1 && comp != 0x69) || (comp == 1 && bits != 16) || channels < 1 || channels > 2 || rate < 8000) return false;
    std::vector<uint8_t> audio;
    for (int guard = 0; (uint64_t)pos + 12 + tracks * 4 <= d.size() && next && guard < 100000 && audio.size() < d.size(); guard++) {
        uint32_t p = pos, nsize = rd32(&d[p]);
        uint32_t vsize = rd32(&d[p + 4]) & 0x7FFFFF;
        p += 12;
        uint32_t a0 = rd32(&d[p]) & 0x7FFFFF;
        p += tracks * 4;
        uint64_t at = (uint64_t)p + vsize;
        if (at >= d.size()) break;
        a0 = (uint32_t)std::min<uint64_t>(a0, d.size() - at);   // the last packet can run past the end of the file
        audio.insert(audio.end(), d.begin() + at, d.begin() + at + a0);
        if ((uint64_t)pos + next > d.size()) break;
        pos += next;
        next = nsize;
    }
    if (comp == 0x69) xbox_adpcm(audio.data(), audio.size(), channels, pcm);
    else { pcm.resize(audio.size() / 2); memcpy(pcm.data(), audio.data(), pcm.size() * 2); }
    return pcm.size() > (size_t)rate;
}

// ---- playing: one track at a time, looped, faded in and out, mixed in SDL's audio callback (44.1 kHz stereo)
struct Track {
    std::vector<int16_t> pcm;      // stereo frames at kRate (resampled when needed)
    size_t pos = 0;
};
static const int kRate = 44100;
static const float kVolume = 0.42f;
static SDL_AudioStream *g_stream = nullptr;
static SDL_Mutex *g_lock = nullptr;
static Track *g_cur = nullptr, *g_old = nullptr;   // playing, and fading out
static float g_gainCur = 0, g_gainOld = 0;
static bool g_enabled = true, g_paused = false;
static MusicTrack g_want = MUSIC_NONE;
static std::string g_wantGame;
static SDL_AtomicInt g_gen;                        // bumped when the wanted track changes (a stale decode is dropped)
static SDL_Condition *g_wake = nullptr;             // the reader of the wanted track (see reader)
static bool g_quit = false;
static SDL_Thread *g_reader = nullptr;
static int SDLCALL reader(void *);

static void SDLCALL feed(void *, SDL_AudioStream *s, int need, int)
{
    int frames = need / 4;
    std::vector<int16_t> out((size_t)frames * 2, 0);
    SDL_LockMutex(g_lock);
    float target = g_enabled && !g_paused ? 1.0f : 0.0f;
    const float in = 1.0f / (kRate * 1.2f), outStep = 1.0f / (kRate * 0.6f);
    for (int i = 0; i < frames; i++) {
        float l = 0, r = 0;
        if (g_cur && !g_cur->pcm.empty()) {
            g_gainCur = g_gainCur < target ? std::min(target, g_gainCur + in) : std::max(target, g_gainCur - outStep);
            const int16_t *f = &g_cur->pcm[g_cur->pos * 2];
            l += f[0] * g_gainCur; r += f[1] * g_gainCur;
            if (++g_cur->pos * 2 >= g_cur->pcm.size()) g_cur->pos = 0;
        }
        if (g_old) {
            g_gainOld = std::max(0.0f, g_gainOld - outStep);
            const int16_t *f = &g_old->pcm[g_old->pos * 2];
            l += f[0] * g_gainOld; r += f[1] * g_gainOld;
            if (++g_old->pos * 2 >= g_old->pcm.size()) g_old->pos = 0;
            if (g_gainOld <= 0) { delete g_old; g_old = nullptr; }
        }
        out[i * 2] = (int16_t)std::max(-32768.0f, std::min(32767.0f, l * kVolume));
        out[i * 2 + 1] = (int16_t)std::max(-32768.0f, std::min(32767.0f, r * kVolume));
    }
    SDL_UnlockMutex(g_lock);
    SDL_PutAudioStreamData(s, out.data(), frames * 4);
}

bool music_init()
{
    if (g_stream) return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
    g_lock = SDL_CreateMutex();
    g_wake = SDL_CreateCondition();
    SDL_AudioSpec spec{ SDL_AUDIO_S16, 2, kRate };
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, nullptr);
    if (!g_stream) return false;
    g_reader = SDL_CreateThread(reader, "music", nullptr);
    SDL_ResumeAudioStreamDevice(g_stream);
    return true;
}

// one reader, for the latest wanted track only: turning the carousel quickly (an arrow key held) asks for many, and
// each read is tens of megabytes in a 32-bit process; it waits until the choice has stood a moment
struct Job { MusicTrack t = MUSIC_NONE; std::string game, name; int gen = 0; };
static Job g_job;                    // the latest request (under g_lock); gen 0: none
static Track *decode(const Job &j)
{
    std::vector<int16_t> pcm;
    int ch = 2, rate = kRate;
    bool ok = false;
    for (size_t at = 0; !ok && at < j.game.size();) {   // each copy of the game in turn (one per line)
        size_t nl = j.game.find('\n', at);
        std::string g = j.game.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? j.game.size() : nl + 1;
        pcm.clear(); ch = 2; rate = kRate;
        ok = j.t == MUSIC_TT ? load_tt(g, pcm, ch, rate)
           : j.t == MUSIC_FOTR ? (load_fotr_shire(g, pcm, ch, rate) || (pcm.clear(), ch = 2, rate = kRate, load_fotr(g, pcm, ch, rate)))
           : j.t == MUSIC_FOTR_NAMED ? load_fotr_named(g, j.name.c_str(), pcm, ch, rate)
           : j.t == MUSIC_TA ? load_ta(g, pcm, ch, rate)
           : false;
        if (j.gen != SDL_GetAtomicInt(&g_gen)) return nullptr;   // (another box chosen meanwhile)
    }
    if (!ok || j.gen != SDL_GetAtomicInt(&g_gen)) return nullptr;
    Track *t = new Track();
    if (ch == 2 && rate == kRate) t->pcm.swap(pcm);   // (a long track: no second copy)
    else {
    size_t n = pcm.size() / ch, m = (size_t)((double)n * kRate / rate);
    t->pcm.resize(m * 2);
    for (size_t i = 0; i < m; i++) {                    // to stereo at 44.1 kHz (nearest sample: same rate here)
        size_t s = std::min(n - 1, (size_t)((double)i * rate / kRate));
        t->pcm[i * 2] = pcm[s * ch];
        t->pcm[i * 2 + 1] = pcm[s * ch + (ch > 1 ? 1 : 0)];
    }
    }
    if (const char *dump = getenv("ATHELAS_MUSIC_DUMP")) {   // testing: the decoded track, raw 44.1 kHz stereo
        std::string fn = std::string(dump) + (j.t == MUSIC_TT ? "\\tt.raw" : j.t == MUSIC_TA ? "\\ta.raw" : "\\fotr.raw");
        if (FILE *f = fopen(fn.c_str(), "wb")) { fwrite(t->pcm.data(), 2, t->pcm.size(), f); fclose(f); }
    }
    return t;
}
static int SDLCALL reader(void *)
{
    SDL_LockMutex(g_lock);
    for (;;) {
        while (!g_quit && !g_job.gen) SDL_WaitCondition(g_wake, g_lock);
        if (g_quit) break;
        Job j = g_job;
        // settle: a newer request in the next 300 ms replaces this one
        if (SDL_WaitConditionTimeout(g_wake, g_lock, 300) || g_quit || g_job.gen != j.gen) continue;
        g_job.gen = 0;
        SDL_UnlockMutex(g_lock);
        Track *t = decode(j);
        SDL_LockMutex(g_lock);
        if (t && !g_quit && j.gen == SDL_GetAtomicInt(&g_gen)) {
            delete g_old;
            g_old = g_cur; g_gainOld = g_gainCur;
            g_cur = t; g_gainCur = 0;
            t = nullptr;
        }
        delete t;
    }
    SDL_UnlockMutex(g_lock);
    return 0;
}

static std::string g_wantName;
static void play(MusicTrack t, const std::string &game, const std::string &name);
void music_play(MusicTrack t, const std::string &game) { play(t, game, std::string()); }
void music_play_named(const std::string &game, const std::string &name) { play(MUSIC_FOTR_NAMED, game, name); }
std::string music_now() { return g_want == MUSIC_FOTR_NAMED ? g_wantName : std::string(); }
static void play(MusicTrack t, const std::string &game, const std::string &name)
{
    if (!g_stream || (t == g_want && game == g_wantGame && name == g_wantName)) return;
    g_want = t; g_wantGame = game; g_wantName = name;
    int gen = SDL_AddAtomicInt(&g_gen, 1) + 1;
    SDL_LockMutex(g_lock);
    if (g_cur) {                                   // what plays fades out now; the new one fades in when it is read
        delete g_old;
        g_old = g_cur; g_gainOld = g_gainCur;
        g_cur = nullptr; g_gainCur = 0;
    }
    if (t != MUSIC_NONE && !game.empty()) { g_job.t = t; g_job.game = game; g_job.name = name; g_job.gen = gen; }
    else g_job.gen = 0;
    SDL_SignalCondition(g_wake);
    SDL_UnlockMutex(g_lock);
}

void music_enable(bool on) { if (g_lock) { SDL_LockMutex(g_lock); g_enabled = on; SDL_UnlockMutex(g_lock); } else g_enabled = on; }
void music_pause(bool paused) { if (g_lock) { SDL_LockMutex(g_lock); g_paused = paused; SDL_UnlockMutex(g_lock); } }

void music_shutdown()
{
    if (g_lock) { SDL_LockMutex(g_lock); g_quit = true; SDL_SignalCondition(g_wake); SDL_UnlockMutex(g_lock); }
    if (g_reader) SDL_WaitThread(g_reader, nullptr);   // (a read in progress finishes first: under a second)
    g_reader = nullptr;
    if (g_stream) SDL_DestroyAudioStream(g_stream);    // the callback has stopped after this
    g_stream = nullptr;
    delete g_cur; delete g_old;
    g_cur = g_old = nullptr;
}

// the archive's index: each track's name ends ".wav"; the first copy of the game that has the archive
std::vector<std::string> music_fotr_list(const std::string &game)
{
    std::vector<std::string> out;
    for (size_t at = 0; out.empty() && at < game.size();) {
        size_t nl = game.find('\n', at);
        std::string g = game.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? game.size() : nl + 1;
        std::vector<uint8_t> h;
        if (!read_game_range(g, "Scenes\\Media\\Music2.xpa", 0, 0x40000, h)) continue;
        for (size_t i = 4; i + 5 <= h.size(); i++) {
            if (memcmp(&h[i], ".wav", 4) && memcmp(&h[i], ".WAV", 4)) continue;
            if (h[i + 4]) continue;
            size_t b = i;
            while (b > 0 && h[b - 1] >= 0x20 && h[b - 1] < 0x7F && i - b < 120) b--;
            if (i - b < 2) continue;
            std::string n((const char *)&h[b], i + 4 - b);
            if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        }
    }
    return out;
}
