// Audio.cpp - diagnostics of the game's audio streams (music, voices)
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// With 3 or 4 players the music stops in the middle of a level (with the game's own sound it comes back after a while,
// with ROTK Fix's DSOAL / OpenAL Soft it does not). What is known of the audio engine (EA, "Audio\Engine"):
//  - DirectSound 8 (EAXDirectSoundCreate8 from eax.dll if present, 0x464710); 50 voices, a number fixed in the code
//    (0x4628C0 answers 0x32), each one a DirectSound buffer (table 0x5A0950, 0x30 bytes per voice) plus 2 shared buffers
//    (0x5A08F0). Voices 0-9 belong to the streams, 10-49 to the sound effects (pools 0x5A1760 and 0x5A1738, made at
//    0x469EB0). An effect that finds fewer than 9 free voices takes one of equal or lower priority (0x469F00);
//  - streams (music, spoken lines): at most 5 at a time (pool 0x5A165C of 5 entries of 0xD4 bytes, made by 0x469660;
//    active list at 0x5A1650, entries linked through +4). Entry: +8 its definition (+1 kind, +9 base priority), +0x64
//    volume, +0x78 state, +0xD2 priority in use (base priority x volume); free places of the pool at 0x5A1668 (a stream
//    is created by 0x468760 only if it is above 0). Before a stream plays, 0x469360 computes its priority and calls
//    0x469750(priority, kind), which stops a playing stream of the same kind with lower or equal priority (answer 2);
//    if it answers 1 (none stopped), at most one place is free and the priority is at least 51, 0x469800(priority)
//    stops a stream of any kind with a lower priority. Both do nothing (answer 1) when no place is free. The music is
//    probably stopped there, by spoken lines of the heroes: to be confirmed by this log.
// The cause was elsewhere: the sound banks overflow their arrays with 3 or 4 heroes (MoveSoundBanks below, the fix).
// The rest of this file only writes to the log ([Diag] AudioLog=1, the default in the development builds): the list of
// streams whenever one is added or removed, and the calls of 0x469750 / 0x469800 that stopped a stream or found only
// streams of higher priority.
#include "ROTKPlus.h"
#include <stdio.h>
#include <string.h>
#include <intrin.h>

static bool g_on;
static int  g_lines;
static const int kMaxLines = 3000;   // a level (test of 04/11/2026: a session's versus games used them all before the
                                     // Black Gate's boss)

void AudioNewLevel()
{
    if (g_on && g_lines >= kMaxLines) Log("audio: the log starts again for the new level");
    g_lines = 0;
}

// + the factors of its voices' volume. 0x466E60 computes gain = 0x99D6A0[group] (0 if the group's bit is set in the
// mask 0x99D69C) x the entry's +0x60, the group being +3 of the sound definition at +0x30, and calls the entry's volume
// function (+0x34: 0x4669C0 / 0x466A40 / 0x466B40 / 0x466C50) with the emitter (+0x2C) and the gain; the last two also
// multiply by the entry's +0x64 when 0x5A15FB is 0. Each voice (pointers at +0xC, count +0x7F) then gets its level
// byte (+0x14) << 7 x the gain (0x469AC0 -> 0x463740). Fifth test of 30/10/2026: with 4 players every stream of kind 2
// played at volume 0 and the one of kind 0 was fine, the opposite of the tests before; which factor is 0 is unknown.
struct StreamInfo { uint32_t entry, def; int state; int kind, base, eff; float vol; int id0, id1; uint32_t em;
                    float a60, a68; int group; float groupGain; uint32_t fn; int level[2]; };

// the active streams, at most 8 (the game has 5); returns how many
static int ReadStreams(StreamInfo* out, int max)
{
    int n = 0;
    Guard([&] {
        uint32_t e = *(const uint32_t*)0x5A1650;
        for (int guard = 0; e && guard < 16 && n < max; ++guard) {
            StreamInfo& s = out[n++];
            s.entry = e;
            s.def = *(const uint32_t*)(e + 8);
            s.state = *(const int*)(e + 0x78);
            s.vol = *(const float*)(e + 0x64);
            s.eff = *(const uint8_t*)(e + 0xD2);
            s.id0 = *(const uint8_t*)(e + 0x74);
            s.id1 = *(const uint8_t*)(e + 0x75);
            s.kind = s.def ? *(const uint8_t*)(s.def + 1) : -1;
            s.base = s.def ? *(const uint8_t*)(s.def + 9) : -1;
            s.em = *(const uint32_t*)(e + 0x2C);
            s.a60 = *(const float*)(e + 0x60);
            s.a68 = *(const float*)(e + 0x68);
            s.fn = *(const uint32_t*)(e + 0x34);
            const uint32_t sdef = *(const uint32_t*)(e + 0x30);
            s.group = sdef ? *(const uint8_t*)(sdef + 3) : -1;
            s.groupGain = s.group >= 0 && s.group < 32 ? *(const float*)(0x99D6A0 + s.group * 4) : -1.0f;
            s.level[0] = s.level[1] = -1;
            const int voices = *(const uint8_t*)(e + 0x7F);
            for (int v = 0; v < 2 && v < voices; ++v) {
                const uint32_t vp = *(const uint32_t*)(e + 0xC + v * 4);
                if (vp) s.level[v] = *(const uint8_t*)(vp + 0x14);
            }
            e = *(const uint32_t*)(e + 4);
        }
    });
    return n;
}

static void Describe(const StreamInfo* s, int n, char* buf, size_t size)
{
    buf[0] = 0;
    size_t len = 0;
    for (int i = 0; i < n && len + 100 < size; ++i)
        // short, so that five streams fit in one log line (1024 characters): v = +0x64, a = +0x60, b = +0x68,
        // g = group:its gain, f = volume function, lv = level bytes of the first two voices
        len += _snprintf_s(buf + len, size - len, _TRUNCATE,
                           "%s[%04X k%d p%d/%d st%d id%d/%d v%.2f a%.2f b%.2f g%d:%.2f f%X lv%d/%d]", i ? " " : "",
                           s[i].entry & 0xFFFF, s[i].kind, s[i].base, s[i].eff, s[i].state, s[i].id0, s[i].id1, s[i].vol,
                           s[i].a60, s[i].a68, s[i].group, s[i].groupGain, s[i].fn & 0xFFFF, s[i].level[0], s[i].level[1]);
}

static void LogStreams(const char* why)
{
    if (g_lines >= kMaxLines) return;
    StreamInfo s[8];
    const int n = ReadStreams(s, 8);
    char buf[1024];
    Describe(s, n, buf, sizeof buf);
    int freeSlots = -1, flag3d = -1, flagVol = -1, banks = -1;
    uint32_t mask = 0;
    Guard([&] {
        freeSlots = *(const int*)0x5A1668;
        mask = *(const uint32_t*)0x99D69C;
        flag3d = *(const uint8_t*)0x5A15FB;
        flagVol = *(const uint8_t*)0x5A12DA;
        banks = *(const uint8_t*)0x5A15F9;   // next sound bank slot (MoveSoundBanks)
    });
    ++g_lines;
    Log("audio: %s: %d streams, %d places free (mask %X, flags %d %d, banks %d) %s", why, n, freeSlots, mask, flag3d,
        flagVol, banks, buf);
    if (g_lines == kMaxLines) Log("audio: log limit reached (%d lines)", kMaxLines);
}

// The DirectSound buffers of the stream voices (0-9 of the table 0x5A0950, 0x30 bytes each; the buffer at +0x24, or in
// one of the 2 shared entries of 0x5A08F0 when bits 18-19 of +0 say so). Test of 30/10/2026: the music stream (kind 0)
// stayed in the list, "playing" (state 6), until the end of the level while the music could not be heard: so the
// trouble is below the streams, in the buffer or in the data that feeds it. Every half second: is each buffer playing
// (IDirectSoundBuffer::GetStatus), and does its play cursor move (GetCurrentPosition)? Written when that changes.
typedef long(__stdcall* DsbGetStatus)(void*, unsigned long*);
typedef long(__stdcall* DsbGetPos)(void*, unsigned long*, unsigned long*);
typedef long(__stdcall* DsbGetVolume)(void*, long*);

// buf, DirectSound status, play cursor, DirectSound volume (hundredths of dB); and the engine's own view of the voice:
// flags (+0, 0x10000 = 3D voice), volume 0..0x3FFF (+0x10, set by 0x463740) and distance factor (+0x18, set by
// 0x463D80 with the voice's position, which the engine computes itself relative to the camera: the DirectSound listener
// is created once, 0x462F6F, and never moved)
struct VoiceState { uint32_t buf; unsigned long status, play; long vol; int moving; uint32_t flags; int engVol; float factor; };

static bool ReadVoice(int i, VoiceState& v)
{
    v = VoiceState{ 0, 0, 0, 0, -1, 0, 0, 0.0f };
    return Guard([&] {
        const uint8_t* e = (const uint8_t*)(0x5A0950 + i * 0x30);
        const uint32_t shared = (*(const uint32_t*)e >> 18) & 3;
        if (shared) e = (const uint8_t*)(0x5A08F0 + (shared - 1) * 0x30);
        v.flags = *(const uint32_t*)e;
        v.engVol = *(const int16_t*)(e + 0x10);
        v.factor = *(const float*)(e + 0x18);
        v.buf = *(const uint32_t*)(e + 0x24);
        if (!v.buf) return;
        void** vt = *(void***)(uintptr_t)v.buf;
        unsigned long write = 0;
        if (((DsbGetStatus)vt[9])((void*)(uintptr_t)v.buf, &v.status) < 0) v.status = 0xFFFFFFFF;
        if (((DsbGetPos)vt[4])((void*)(uintptr_t)v.buf, &v.play, &write) < 0) v.play = 0xFFFFFFFF;
        if (((DsbGetVolume)vt[6])((void*)(uintptr_t)v.buf, &v.vol) < 0) v.vol = 1;   // hundredths of dB, 0 = full
    });
}

static void WatchStreamVoices()
{
    static DWORD next;
    static VoiceState last[10];
    static int lastMoving[10];
    static bool first = true;
    const DWORD now = GetTickCount();
    if ((int)(now - next) < 0) return;
    next = now + 500;
    for (int i = 0; i < 10 && g_lines < kMaxLines; ++i) {
        VoiceState v;
        if (!ReadVoice(i, v)) continue;
        // playing (DSBSTATUS_PLAYING = 1) with the cursor where it was half a second ago: stalled
        const int moving = !v.buf ? -1 : !(v.status & 1) ? 0 : v.play != last[i].play ? 1 : 2;
        if (first || v.buf != last[i].buf || (v.status & 7) != (last[i].status & 7) || moving != lastMoving[i] ||
            (v.buf && (v.vol > last[i].vol + 300 || v.vol < last[i].vol - 300)) ||   // a volume change of more than 3 dB
            (v.buf && (v.factor > last[i].factor + 0.1f || v.factor < last[i].factor - 0.1f))) {
            if (!first || v.buf) {
                ++g_lines;
                Log("audio: stream voice %d: buffer %08X status %lX (%s) cursor %lu volume %.1f dB; engine: flags %X%s "
                    "volume %d distance factor %.2f", i, v.buf, v.status,
                    !v.buf ? "no buffer" : moving == 0 ? "stopped" : moving == 1 ? "playing" : "PLAYING BUT STILL", v.play,
                    v.vol / 100.0, v.flags, (v.flags & 0x10000) ? " (3D)" : "", v.engVol, v.factor);
            }
        }
        last[i] = v;
        lastMoving[i] = moving;
    }
    first = false;
}

// The voices of the sound effects (10-49). Fifth test of 30/10/2026: with 4 players nothing could be heard at the start
// of the level but some crows, until the first cutscene. How many play, and how many of them at engine volume 0.
static void LogEffectVoices()
{
    int playing = 0, silent = 0, lo = 0x7FFF, hi = -1;
    for (int i = 10; i < 50; ++i) {
        VoiceState v;
        if (!ReadVoice(i, v) || !v.buf || !(v.status & 1)) continue;
        ++playing;
        if (v.engVol <= 0) ++silent;
        if (v.engVol < lo) lo = v.engVol;
        if (v.engVol > hi) hi = v.engVol;
    }
    ++g_lines;
    if (playing) Log("audio: effect voices: %d playing, %d of them at engine volume 0 (volumes %d..%d)", playing, silent, lo, hi);
    else Log("audio: effect voices: none playing");
}

// Submix attenuations (test of 30/10/2026: the music's buffers played at -100 dB from the start of the level). The
// engine keeps one attenuation per group of sounds, 16 groups: current value at 0x561C04, target 0x561C44, step
// 0x561C84, saved 0x561CC4, all floats (0x462170(group, value, seconds, save) sets them). Three callers: the level
// scripts ("audio: set submix attn", 0x45DFB0 -> command 0x13 of 0x461930 -> call at 0x461C43), the defaults at every
// start (0x462761: 0.3 / 0.4 / 0.8 / 1.0), and the defeat sequence 0x43FE40, which fades groups 0, 1, 13, 14 and 15
// to 0 (0x462490, call at 0x43FEDA, flag 0x10 of 0x561BF0) until the player who started it leaves the "die" state
// (0x434FA0 -> 0x462590 at 0x434FF6, only for that player: 0x43FE10 compares it with 0x579B1C).
// Every audio command (0x461930(emitter, command, n, a, b, c): the scripts' "audio: play / play ig stream / set attn /
// pause / stop / play vo ..." handlers at 0x45DB30..0x45DFB0 call it, and so does the game's code). Fourth test of
// 30/10/2026: the stream of kind 0 plays at volume 0 from the start, the music heard is a stream of kind 2 at priority
// 90; what decides those volumes is still unknown. Written, with the caller, for every command but the plain plays of
// sound effects (1 and 9, very frequent), which are written only when a level script asks for them (the "audio: play"
// handler 0x45DB30, caller 0x45DB6C): they are the numbers [PvP] Cheer_<level> needs (Pvp.cpp), such as the crowd of the
// Black Gate.
static NAKED void AudioCmdOrig()   // sub esp,8; push ebx; mov ebx,[esp+10h]
{
    ASM_BEGIN
    ASM(sub  esp, 8)
    ASM(push ebx)
    ASM(mov  ebx, dword ptr [esp + 0x10])
    ASM(push 0x461938)
    ASM(ret)
    ASM_END
}

typedef void(__cdecl* AudioCmdFn)(uint32_t, int, int, float, float, float);

static void __cdecl AudioCmd(uint32_t emitter, int cmd, int n, float a, float b, float c)
{
    // the same command repeated with the same values (an emitter updated every frame) is written once
    static uint32_t lastEmitter; static int lastCmd = -1, lastN; static float lastA, lastB, lastC;
    const bool same = cmd == lastCmd && (emitter & 0xFF) == lastEmitter && n == lastN && a == lastA && b == lastB && c == lastC;
    lastEmitter = emitter & 0xFF; lastCmd = cmd; lastN = n; lastA = a; lastB = b; lastC = c;
    const uintptr_t from = (uintptr_t)_ReturnAddress();
    const bool script = from >= 0x45DB30 && from < 0x45E000;   // the level scripts' audio handlers
    if (((cmd != 1 && cmd != 9) || script) && !same && g_lines < kMaxLines) {
        ++g_lines;
        Log("audio: command %d emitter %d n %d (%.2f %.2f %.2f) from %08X%s", cmd, (int)(emitter & 0xFF), n, a, b, c,
            (unsigned)from, script ? " (level script)" : "");
    }
    GameFn((AudioCmdFn)&AudioCmdOrig)(emitter, cmd, n, a, b, c);
}

typedef void(__cdecl* SubmixFn)(uint32_t, float, float, int);
typedef void(__cdecl* VoidFn)();

static void __cdecl SubmixFromScript(uint32_t group, float value, float secs, int save)
{
    if (g_lines < kMaxLines) {
        ++g_lines;
        Log("audio: level script sets group %u to %.2f in %.1f s%s", group & 0xFF, value, secs, save ? " (saved)" : "");
    }
    GameFn((SubmixFn)0x462170)(group, value, secs, save);
}

static void __cdecl DuckForDefeat()
{
    if (g_lines < kMaxLines) {
        ++g_lines;
        Log("audio: defeat sequence started by character %08X: groups 0, 1, 13, 14, 15 fade to 0",
            *(const uint32_t*)0x579B1C);
    }
    GameFn((VoidFn)0x462490)();
}

static void __cdecl RestoreAfterDefeat()
{
    if (g_lines < kMaxLines) { ++g_lines; Log("audio: the player of the defeat sequence got up: groups restored"); }
    GameFn((VoidFn)0x462590)();
}

static void WatchSubmixes()
{
    static DWORD next;
    static float lastCur[16], lastTarget[16];
    static uint32_t lastFlags;
    static bool first = true;
    const DWORD now = GetTickCount();
    if ((int)(now - next) < 0) return;
    next = now + 500;
    float cur[16], target[16];
    uint32_t flags;
    if (!Guard([&] {
            memcpy(cur, (const void*)0x561C04, sizeof cur);
            memcpy(target, (const void*)0x561C44, sizeof target);
            flags = *(const uint32_t*)0x561BF0;
        }))
        return;
    bool changed = first || flags != lastFlags;
    for (int i = 0; i < 16; ++i)
        if (cur[i] > lastCur[i] + 0.05f || cur[i] < lastCur[i] - 0.05f || target[i] != lastTarget[i]) changed = true;
    if (changed && g_lines < kMaxLines) {
        char buf[400];
        size_t len = 0;
        for (int i = 0; i < 16; ++i)
            len += _snprintf_s(buf + len, sizeof buf - len, _TRUNCATE, " %d:%.2f%s", i, cur[i],
                               target[i] != cur[i] ? "->" : "");
        ++g_lines;
        Log("audio: groups (flags %X):%s", flags, buf);
    }
    memcpy(lastCur, cur, sizeof cur);
    memcpy(lastTarget, target, sizeof target);
    lastFlags = flags;
    first = false;
}

// every frame (Screens.cpp): the list of streams when a stream is added or removed (not at every change of state: with
// four heroes the spoken lines come often, and the log must last until the music stops)
void AudioFrame()
{
    if (!g_on || g_lines >= kMaxLines) return;
    WatchStreamVoices();
    WatchSubmixes();
    static uint32_t last;
    StreamInfo s[8];
    const int n = ReadStreams(s, 8);
    uint32_t sig = 2166136261u;
    for (int i = 0; i < n; ++i) sig = (sig ^ s[i].entry) * 16777619u;
    if (sig == last) {
        // while a stream plays, every 3 s too, with the effect voices
        static DWORD next;
        if (n && (int)(GetTickCount() - next) >= 0) {
            next = GetTickCount() + 3000;
            LogStreams("streams (every 3 s)");
            if (g_lines < kMaxLines) LogEffectVoices();
        }
        return;
    }
    last = sig;
    LogStreams("streams now");
}

// the game's calls that stop a stream to make room for a new one (in 0x469360)
typedef int(__cdecl* StealSameFn)(int, int);
typedef int(__cdecl* StealAnyFn)(int);

static int __cdecl StealSame(int prio, int kind)
{
    char before[1024];
    StreamInfo s[8];
    Describe(s, ReadStreams(s, 8), before, sizeof before);
    const int r = GameFn((StealSameFn)0x469750)(prio, kind);
    if (g_lines < kMaxLines && r != 0) {   // 0 = no stream of that kind: the common case, not written
        ++g_lines;
        Log("audio: new stream of kind %d priority %d: same-kind check -> %s (before: %s)", kind & 0xFF, prio & 0xFF,
            r == 2 ? "STOPPED one" : r == 1 ? "all higher, not stopped" : "none of that kind", before);
        if (r == 2) LogStreams("after the stop");
    }
    return r;
}

static int __cdecl StealAny(int prio)
{
    char before[1024];
    StreamInfo s[8];
    Describe(s, ReadStreams(s, 8), before, sizeof before);
    const int r = GameFn((StealAnyFn)0x469800)(prio);
    if (g_lines < kMaxLines) {
        ++g_lines;
        Log("audio: new stream priority %d, places almost over: any-kind check -> %s (before: %s)", prio & 0xFF,
            r == 2 ? "STOPPED one" : r == 1 ? "all higher, not stopped" : "no stream", before);
        if (r == 2) LogStreams("after the stop");
    }
    return r;
}

// The sound banks. Every package (.scw) brings its sound headers (chunk "SHDR" / "shdr": 0x4E0EC1 -> 0x466450(slot,
// data)) and its samples ("samp": 0x4664E0). Slots 0-2 are fixed; a higher slot in the file means "the next one", from
// a counter at 0x5A15F9 that starts at 3 (0x466260, 0x46637B) and has no limit. The banks are kept in arrays of 7
// entries at 0x99D660 and 0x99D680 (cleared with rep stosd, ecx = 7, at 0x4662A0 and 0x4668D0), plus 0x99D640, written
// when a bank is freed. Each hero's package adds a bank: past slot 6 the bank's pointer lands on the mute mask of the
// sound groups (0x99D69C, slot 7) and on the gains of groups 0 and 1 (0x99D6A0, slots 8 and 9), and 0x99D660 runs into
// 0x99D680. A pointer in the mask silences the groups whose bits it happens to have (the music, the voices, the
// effects: a different set each time) until the game rewrites the mask, at a cutscene or a big fight. Sixth test of
// 30/10/2026: mask 0x0B007840 from the loading of a 4-player level, 0 with 2 players. The three arrays move here, 32
// entries each: every instruction that addresses them gets the new address, the two clears count 32.
static uint32_t g_bank640[32], g_bank660[32], g_bank680[32];

static bool MoveSoundBanks()
{
    // the 32-bit addresses inside the game's instructions (address of the value, its value)
    static const struct { uint32_t at, old; } refs[] = {
        { 0x465EF2 + 2, 0x99D640 },
        { 0x465F49 + 2, 0x99D660 }, { 0x4662A9 + 1, 0x99D660 }, { 0x4666E8 + 2, 0x99D660 }, { 0x466779 + 3, 0x99D660 },
        { 0x4668D9 + 1, 0x99D660 },
        { 0x465EEA + 2, 0x99D680 }, { 0x465F3F + 2, 0x99D680 }, { 0x466128 + 2, 0x99D684 }, { 0x4662B7 + 1, 0x99D680 },
        { 0x466480 + 3, 0x99D680 }, { 0x4664A5 + 3, 0x99D680 }, { 0x4664F1 + 3, 0x99D680 }, { 0x466533 + 3, 0x99D680 },
        { 0x46658C + 3, 0x99D680 }, { 0x4665CC + 3, 0x99D680 }, { 0x4666D8 + 2, 0x99D680 }, { 0x46672C + 3, 0x99D680 },
        { 0x4667A3 + 3, 0x99D680 }, { 0x4667DC + 3, 0x99D680 }, { 0x466888 + 3, 0x99D680 }, { 0x4668E7 + 1, 0x99D680 },
        { 0x46692E + 3, 0x99D680 },
    };
    static const uint32_t counts[] = { 0x4662A4 + 1, 0x4662B2 + 1, 0x4668D4 + 1, 0x4668E2 + 1 };   // mov ecx, 7
    // all or nothing: half of the instructions on the new arrays and half on the old ones would be worse than the bug
    bool expected = true;
    if (!Guard([&] {
            for (const auto& r : refs) if (*(const uint32_t*)r.at != r.old) { Log("sound banks: unexpected code at %08X", r.at); expected = false; return; }
            for (uint32_t c : counts) if (*(const uint32_t*)c != 7) { Log("sound banks: unexpected code at %08X", c); expected = false; return; }
            // the banks already loaded (the front end's) come along
            memcpy(g_bank640, (const void*)0x99D640, 8 * 4);
            memcpy(g_bank660, (const void*)0x99D660, 7 * 4);
            memcpy(g_bank680, (const void*)0x99D680, 7 * 4);
        }) || !expected)
        return false;
    for (int i = 7; i < 32; ++i) g_bank660[i] = 0xFFFFFFFF;   // what the clear writes
    bool ok = true;
    for (const auto& r : refs) {
        const uint32_t base = r.old < 0x99D660 ? 0x99D640 : r.old < 0x99D680 ? 0x99D660 : 0x99D680;
        const uint32_t to = (uint32_t)(uintptr_t)(base == 0x99D640 ? g_bank640 : base == 0x99D660 ? g_bank660 : g_bank680);
        ok &= PatchDword(r.at, r.old, to + (r.old - base));
    }
    for (uint32_t c : counts) ok &= PatchDword(c, 7, 32);
    return ok;
}

void AudioBanksInstall(bool on)
{
    if (!on) { Log("sound banks: the game's 7 places ([Coop] SoundBanks=0): with extra heroes sounds can go silent"); return; }
    Log("sound banks: %s", MoveSoundBanks() ? "32 places instead of 7" : "NOT moved (the game's 7 places stay)");
}

void AudioInstall(bool on)
{
    g_on = on;
    if (!on) { Log("audio diagnostics: off ([Diag] AudioLog=0)"); return; }
    bool ok = PatchCall(0x46942A, 0x469750, (const void*)&StealSame);
    ok &= PatchCall(0x46944A, 0x469800, (const void*)&StealAny);
    ok &= PatchCall(0x461C43, 0x462170, (const void*)&SubmixFromScript);
    ok &= PatchCall(0x43FEDA, 0x462490, (const void*)&DuckForDefeat);
    ok &= PatchCall(0x434FF6, 0x462590, (const void*)&RestoreAfterDefeat);
    static const uint8_t cmdOld[] = { 0x83, 0xEC, 0x08, 0x53, 0x8B, 0x5C, 0x24, 0x10 };
    ok &= PatchJmp(0x461930, cmdOld, sizeof cmdOld, (const void*)&AudioCmd);
    Log("audio diagnostics: %s (streams and their stops in the log, at most %d lines)", ok ? "active" : "NOT installed",
        kMaxLines);
}
