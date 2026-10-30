/*
 * ROTK Fix - gamepad layer
 *
 * The game reads controllers through DirectInput 8 and matches each one by
 * name against hand-written profiles in Input.ini (2003-era pads only).
 * Modern controllers fall back to a "generic joystick" guess that leaves
 * triggers and Start unusable.
 *
 * This file wraps the game's IDirectInput8A and adds virtual controllers,
 * "ROTK Fix Gamepad" (and "ROTK Fix Gamepad 2" for co-op), with a fixed layout
 * that the player can remap in the launcher. Its state comes from SDL3, which
 * recognises Xbox, PlayStation, Switch and most other controllers, so every
 * brand looks identical to the game. Real game controllers can be hidden from
 * the game so it does not pick them up twice. Keyboard and mouse are passed
 * through untouched.
 *
 * Virtual layout (DirectInput button index -> Input.ini JSBUTTON_n is index+1):
 *   1 South (Xbox A)   2 East (B)   3 West (X)   4 North (Y)
 *   5 Left bumper      6 Right bumper   7 Back/Select   8 Start
 *   9 Left stick click 10 Right stick click  11 Left trigger  12 Right trigger
 *   13 D-pad up  14 D-pad down  15 D-pad left  16 D-pad right
 *   Axes: X/Y = left stick, Rx/Ry = right stick, Z = left trigger, Rz = right trigger
 *   POV 0 = D-pad
 */
#define DIRECTINPUT_VERSION 0x0800
#define CINTERFACE
#include "rotkfix.h"
#include <dinput.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <SDL3/SDL.h>

#ifndef DIDFT_TYPEMASK
#define DIDFT_TYPEMASK 0x000000FF
#endif

/* ------------------------------------------------------------------ */
/* Settings                                                             */

static int g_padEnabled   = 1;
static int g_hideReal     = 1;
static int g_deadzonePct  = 12;   /* radial stick deadzone, percent */
static int g_triggerPct   = 30;   /* trigger press threshold for the trigger buttons */
static int g_logInput     = 1;
static int g_secondPad    = 1;    /* offer "ROTK Fix Gamepad 2" for player 2 (co-op) */
static int g_rumblePct    = 100;  /* controller vibration strength, 0 = off */
static int g_hidePointer  = 1;    /* [Gamepad] HideMousePointer: hide the menu pointer while a controller is used */

/* Button remapping: each virtual button (slot) is driven by one physical
 * control. Physical controls are named by their Xbox position (A = bottom face
 * button on every brand). Default: slot i <- physical i. */
enum { PH_A, PH_B, PH_X, PH_Y, PH_LB, PH_RB, PH_BACK, PH_START, PH_LS, PH_RS, PH_LT, PH_RT,
       PH_UP, PH_DOWN, PH_LEFT, PH_RIGHT, NUM_PHYS };
static const char *const k_physId[NUM_PHYS] = { "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LS", "RS", "LT", "RT",
                                                "Up", "Down", "Left", "Right" };
#define MAX_PADS 2
/* per player: [GamepadButtons] for player 1, [GamepadButtons2] for player 2 */
static int g_phys[MAX_PADS][NUM_PHYS] = { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
                                          { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 } };
/* remappable game actions -> slot */
static const struct { const char *key; int slot; } k_actions[] = {
    { "SpeedAttack", PH_A }, { "PhysicalAttack", PH_B }, { "Parry", PH_X }, { "FierceAttack", PH_Y },
    { "JumpBack", PH_LB }, { "Action", PH_RB }, { "Special", PH_LS }, { "RangedAttack", PH_LT }, { "KillingMove", PH_RT } };

/* On-screen prompt names: 0 auto (from the connected controller), 1 Xbox,
 * 2 PlayStation, 3 Nintendo, 4 the game's own ("Button 7") */
enum { PS_AUTO, PS_XBOX, PS_PLAYSTATION, PS_NINTENDO, PS_ORIGINAL };
static int g_promptStyle = PS_AUTO;

static int phys_from_name(const char *v)
{
    for (int i = 0; i < NUM_PHYS; i++) if (lstrcmpiA(v, k_physId[i]) == 0) return i;
    return -1;
}

void input_load_settings(const char *ini)
{
    char v[32];
    for (int p = 0; p < MAX_PADS; p++) {
        const char *sec = p ? "GamepadButtons2" : "GamepadButtons";
        for (size_t i = 0; i < sizeof k_actions / sizeof k_actions[0]; i++) {
            int sl = k_actions[i].slot;
            /* player 2 without a layout of its own uses player 1's */
            const char *def = k_physId[p ? g_phys[0][sl] : sl];
            GetPrivateProfileStringA(sec, k_actions[i].key, def, v, sizeof v, ini);
            int ph = phys_from_name(v);
            if (ph >= 0) g_phys[p][sl] = ph;
            else logf_("gamepad: unknown button \"%s\" for %s (player %d), keeping %s", v, k_actions[i].key, p + 1, def);
        }
    }
    GetPrivateProfileStringA("Gamepad", "ButtonNames", "auto", v, sizeof v, ini);
    g_promptStyle = !lstrcmpiA(v, "xbox") ? PS_XBOX : !lstrcmpiA(v, "playstation") ? PS_PLAYSTATION :
                    !lstrcmpiA(v, "nintendo") ? PS_NINTENDO : !lstrcmpiA(v, "original") ? PS_ORIGINAL : PS_AUTO;

    g_padEnabled  = GetPrivateProfileIntA("Gamepad", "Enabled", 1, ini);
    g_hideReal    = GetPrivateProfileIntA("Gamepad", "HideRealControllers", 1, ini);
    g_deadzonePct = GetPrivateProfileIntA("Gamepad", "StickDeadzone", 12, ini);
    g_triggerPct  = GetPrivateProfileIntA("Gamepad", "TriggerThreshold", 30, ini);
    g_logInput    = GetPrivateProfileIntA("Gamepad", "LogInput", g_detailLog, ini);
    g_secondPad   = GetPrivateProfileIntA("Gamepad", "SecondController", 1, ini);
    g_hidePointer = GetPrivateProfileIntA("Gamepad", "HideMousePointer", 1, ini) != 0;
    g_rumblePct   = GetPrivateProfileIntA("Gamepad", "Vibration", 100, ini);
    if (g_rumblePct < 0) g_rumblePct = 0;
    if (g_rumblePct > 100) g_rumblePct = 100;
    if (g_deadzonePct < 0) g_deadzonePct = 0;
    if (g_deadzonePct > 50) g_deadzonePct = 50;
    if (g_triggerPct < 5) g_triggerPct = 5;
    if (g_triggerPct > 95) g_triggerPct = 95;
    for (int p = 0; p < MAX_PADS; p++) {
        char b[320] = "";
        for (size_t i = 0; i < sizeof k_actions / sizeof k_actions[0]; i++) {
            int sl = k_actions[i].slot;
            if (g_phys[p][sl] != sl) { char e[48]; wsprintfA(e, " %s=%s", k_actions[i].key, k_physId[g_phys[p][sl]]); lstrcatA(b, e); }
        }
        if (b[0]) logf_("gamepad: player %d buttons remapped:%s", p + 1, b);
    }
}

/* ------------------------------------------------------------------ */
/* SDL3, loaded at runtime from SDL3.dll in the game folder              */

static HMODULE g_sdl;
static int     g_sdlState;  /* 0 = not tried, 1 = ready, -1 = unavailable */
static __typeof__(&SDL_SetHint)          p_SDL_SetHint;
static __typeof__(&SDL_Init)             p_SDL_Init;
static __typeof__(&SDL_GetError)         p_SDL_GetError;
static __typeof__(&SDL_GetGamepads)      p_SDL_GetGamepads;
static __typeof__(&SDL_free)             p_SDL_free;
static __typeof__(&SDL_OpenGamepad)      p_SDL_OpenGamepad;
static __typeof__(&SDL_CloseGamepad)     p_SDL_CloseGamepad;
static __typeof__(&SDL_GamepadConnected) p_SDL_GamepadConnected;
static __typeof__(&SDL_UpdateGamepads)   p_SDL_UpdateGamepads;
static __typeof__(&SDL_GetGamepadAxis)   p_SDL_GetGamepadAxis;
static __typeof__(&SDL_GetGamepadButton) p_SDL_GetGamepadButton;
static __typeof__(&SDL_GetGamepadName)   p_SDL_GetGamepadName;
static __typeof__(&SDL_GetGamepadType)   p_SDL_GetGamepadType;
static __typeof__(&SDL_GetGamepadID)     p_SDL_GetGamepadID;
static __typeof__(&SDL_RumbleGamepad)    p_SDL_RumbleGamepad;

/* Player 1's controller is the first one connected, player 2's the next.
 * A controller keeps its player until it is disconnected. */
static SDL_Gamepad *g_pads[MAX_PADS];
#define g_pad (g_pads[0])
static DWORD        g_lastScan;

static int sdl_ready(void)
{
    if (g_sdlState) return g_sdlState > 0;
    g_sdlState = -1;
    char p[MAX_PATH];
    path_in_dir(p, "SDL3.dll");
    g_sdl = LoadLibraryA(p);
    if (!g_sdl) {
        logf_("gamepad: SDL3.dll not found in game folder (error %lu): the ROTK Fix Gamepad gets no input and real "
              "controllers are left to the game; start the game from Athelas to put SDL3.dll back", GetLastError());
        return 0;
    }
#define LOAD(fn) if (!(p_##fn = (__typeof__(p_##fn))(void *)GetProcAddress(g_sdl, #fn))) { logf_("gamepad: SDL3.dll missing " #fn); return 0; }
    LOAD(SDL_SetHint) LOAD(SDL_Init) LOAD(SDL_GetError) LOAD(SDL_GetGamepads) LOAD(SDL_free)
    LOAD(SDL_OpenGamepad) LOAD(SDL_CloseGamepad) LOAD(SDL_GamepadConnected) LOAD(SDL_UpdateGamepads)
    LOAD(SDL_GetGamepadAxis) LOAD(SDL_GetGamepadButton) LOAD(SDL_GetGamepadName) LOAD(SDL_GetGamepadType) LOAD(SDL_GetGamepadID) LOAD(SDL_RumbleGamepad)
#undef LOAD
    /* SDL has no window of its own here: accept input even when it thinks
     * the app is in the background, and read raw input on its own thread. */
    p_SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    p_SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    if (!p_SDL_Init(SDL_INIT_GAMEPAD)) {
        logf_("gamepad: SDL_Init failed: %s (the ROTK Fix Gamepad gets no input; real controllers are left to the game)",
              p_SDL_GetError());
        return 0;
    }
    g_sdlState = 1;
    logf_("gamepad: SDL3 ready");
    return 1;
}

static void pad_update(void)
{
    if (!sdl_ready()) return;
    p_SDL_UpdateGamepads();
    int want = g_secondPad ? MAX_PADS : 1, missing = 0;
    for (int p = 0; p < want; p++) {
        if (g_pads[p] && !p_SDL_GamepadConnected(g_pads[p])) {
            logf_("gamepad: player %d's controller disconnected", p + 1);
            p_SDL_CloseGamepad(g_pads[p]);
            g_pads[p] = NULL;
        }
        if (!g_pads[p]) missing++;
    }
    if (!missing) return;
    DWORD now = GetTickCount();
    if (g_lastScan && now - g_lastScan < 500) return;
    g_lastScan = now;
    int n = 0;
    SDL_JoystickID *ids = p_SDL_GetGamepads(&n);
    if (!ids) return;
    for (int i = 0; i < n; i++) {
        int used = 0;
        for (int p = 0; p < want; p++) if (g_pads[p] && p_SDL_GetGamepadID(g_pads[p]) == ids[i]) used = 1;
        if (used) continue;
        int slot = -1;
        for (int p = 0; p < want && slot < 0; p++) if (!g_pads[p]) slot = p;
        if (slot < 0) break;
        g_pads[slot] = p_SDL_OpenGamepad(ids[i]);
        if (g_pads[slot]) {
            const char *name = p_SDL_GetGamepadName(g_pads[slot]);
            logf_("gamepad: player %d uses \"%s\" (%d controller(s) connected)", slot + 1, name ? name : "?", n);
        }
    }
    p_SDL_free(ids);
}

/* ------------------------------------------------------------------ */
/* Virtual device state                                                 */

enum { AX_X, AX_Y, AX_Z, AX_RX, AX_RY, AX_RZ, NUM_AXES };
#define NUM_BUTTONS 16
#define NUM_OBJS (NUM_AXES + 1 + NUM_BUTTONS)

typedef struct {
    int32_t axis[NUM_AXES];     /* -32768..32767 sticks, 0..32767 triggers */
    DWORD   pov;                /* hundredths of a degree, or 0xFFFFFFFF   */
    BYTE    btn[NUM_BUTTONS];
} PadState;

static void apply_radial_deadzone(int32_t *x, int32_t *y)
{
    double fx = *x / 32767.0, fy = *y / 32767.0;
    double mag = sqrt(fx * fx + fy * fy);
    double dz = g_deadzonePct / 100.0;
    if (mag <= dz) { *x = *y = 0; return; }
    double scaled = (mag - dz) / (1.0 - dz);
    if (scaled > 1.0) scaled = 1.0;
    fx = fx / mag * scaled; fy = fy / mag * scaled;
    *x = (int32_t)lround(fx * 32767.0);
    *y = (int32_t)lround(fy * 32767.0);
}

static void rumble_refresh(int player);
static volatile DWORD g_padActiveTick;   /* last time a controller button or stick moved */
static void read_pad(PadState *s, int player)
{
    memset(s, 0, sizeof *s);
    s->pov = 0xFFFFFFFF;
    pad_update();
    SDL_Gamepad *pad = player >= 0 && player < MAX_PADS ? g_pads[player] : NULL;
    if (!pad) return;
    s->axis[AX_X]  = p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    s->axis[AX_Y]  = p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    s->axis[AX_RX] = p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
    s->axis[AX_RY] = p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
    s->axis[AX_Z]  = p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    s->axis[AX_RZ] = p_SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    apply_radial_deadzone(&s->axis[AX_X], &s->axis[AX_Y]);
    apply_radial_deadzone(&s->axis[AX_RX], &s->axis[AX_RY]);

    static const SDL_GamepadButton map[NUM_PHYS] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START,
        SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID,   /* triggers, below */
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    };
    BYTE phys[NUM_PHYS] = { 0 };
    for (int i = 0; i < NUM_PHYS; i++)
        if (map[i] != SDL_GAMEPAD_BUTTON_INVALID && p_SDL_GetGamepadButton(pad, map[i])) phys[i] = 0x80;
    int32_t thr = 32767 * g_triggerPct / 100;
    if (s->axis[AX_Z]  > thr) phys[PH_LT] = 0x80;
    if (s->axis[AX_RZ] > thr) phys[PH_RT] = 0x80;
    for (int i = 0; i < NUM_BUTTONS; i++) s->btn[i] = phys[g_phys[player][i]];
    {   /* note controller activity (hides the game's mouse pointer, see below) */
        static DWORD lastSig[MAX_PADS];
        DWORD sig = 0;
        for (int i = 0; i < NUM_PHYS; i++) if (phys[i]) sig |= 1u << i;
        if (s->axis[AX_X] > 16000 || s->axis[AX_X] < -16000) sig |= 1u << 20;
        if (s->axis[AX_Y] > 16000 || s->axis[AX_Y] < -16000) sig |= 1u << 21;
        if (sig != lastSig[player]) { lastSig[player] = sig; if (sig) g_padActiveTick = GetTickCount() | 1; }
    }

    /* Movies are skipped with Start; let the bottom face button (A / Cross)
     * skip them too. Only a press that starts while a movie is playing counts,
     * so the same press does not carry over into the menu that follows. */
    static int aSkip[MAX_PADS], aWasDown[MAX_PADS];
    int movie = graphics_movie_playing();
    if (phys[PH_A] && !aWasDown[player] && movie) aSkip[player] = 1;
    if (!phys[PH_A] || !movie) aSkip[player] = 0;
    aWasDown[player] = phys[PH_A] != 0;
    if (aSkip[player]) s->btn[PH_START] = 0x80;
    rumble_refresh(player);

    int up = s->btn[12] != 0, dn = s->btn[13] != 0, lt = s->btn[14] != 0, rt = s->btn[15] != 0;
    if (up && dn) up = dn = 0;
    if (lt && rt) lt = rt = 0;
    if      (up && rt) s->pov = 4500;
    else if (dn && rt) s->pov = 13500;
    else if (dn && lt) s->pov = 22500;
    else if (up && lt) s->pov = 31500;
    else if (up) s->pov = 0;
    else if (rt) s->pov = 9000;
    else if (dn) s->pov = 18000;
    else if (lt) s->pov = 27000;
}

/* ------------------------------------------------------------------ */
/* Virtual device object table                                          */

/* Our own GUIDs (randomly generated for this project) */
static const GUID GUID_RotkPadInstance = { 0x6b1f3c2a, 0x8e4d, 0x4f61, { 0x9a, 0x2c, 0x51, 0x7e, 0x0d, 0x33, 0xa4, 0x10 } };
static const GUID GUID_RotkPadProduct  = { 0x6b1f3c2b, 0x8e4d, 0x4f61, { 0x9a, 0x2c, 0x51, 0x7e, 0x0d, 0x33, 0xa4, 0x10 } };
static const GUID GUID_RotkPad2Instance = { 0x6b1f3c2c, 0x8e4d, 0x4f61, { 0x9a, 0x2c, 0x51, 0x7e, 0x0d, 0x33, 0xa4, 0x10 } };
static const GUID GUID_RotkPad2Product  = { 0x6b1f3c2d, 0x8e4d, 0x4f61, { 0x9a, 0x2c, 0x51, 0x7e, 0x0d, 0x33, 0xa4, 0x10 } };
/* The game gives each player the controller whose name matches their layout
 * in Input.ini (JOYSTICK_ID), comparing whole names, so the two differ. */
static const char *pad_name(int player) { return player == 1 ? "ROTK Fix Gamepad 2" : "ROTK Fix Gamepad"; }
static int pad_from_guid(REFGUID g)
{
    if (!g) return -1;
    if (IsEqualGUID(g, &GUID_RotkPadInstance)) return 0;
    if (g_secondPad && IsEqualGUID(g, &GUID_RotkPad2Instance)) return 1;
    return -1;
}
#define PAD_DEVTYPE (DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8))

typedef struct {
    const GUID *guid;
    DWORD type;        /* DIDFT_* | instance */
    DWORD defOfs;      /* offset in c_dfDIJoystick */
    const char *name;
} ObjDesc;

static ObjDesc g_objs[NUM_OBJS];

static void init_objs(void)
{
    static int done;
    if (done) return;
    done = 1;
    static const GUID *axg[NUM_AXES] = { &GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis, &GUID_RyAxis, &GUID_RzAxis };
    static const char *axn[NUM_AXES] = { "Left Stick X", "Left Stick Y", "Left Trigger", "Right Stick X", "Right Stick Y", "Right Trigger" };
    static const DWORD axo[NUM_AXES] = { 0, 4, 8, 12, 16, 20 };
    static const char *bn[NUM_BUTTONS] = { "A", "B", "X", "Y", "Left Bumper", "Right Bumper", "Back", "Start",
        "Left Stick Click", "Right Stick Click", "Left Trigger Button", "Right Trigger Button",
        "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right" };
    int k = 0;
    for (int i = 0; i < NUM_AXES; i++, k++)
        g_objs[k] = (ObjDesc){ axg[i], DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(i), axo[i], axn[i] };
    g_objs[k++] = (ObjDesc){ &GUID_POV, DIDFT_POV | DIDFT_MAKEINSTANCE(0), 32, "D-pad" };
    for (int i = 0; i < NUM_BUTTONS; i++, k++)
        g_objs[k] = (ObjDesc){ &GUID_Button, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(i), 48 + i, bn[i] };
}

static int obj_kind(int o)   /* 0 axis, 1 pov, 2 button */
{
    return o < NUM_AXES ? 0 : (o == NUM_AXES ? 1 : 2);
}

/* ------------------------------------------------------------------ */
/* Virtual IDirectInputDevice8A                                         */

#define MAX_FMT 256
#define MAX_EVENTS 256

typedef struct {
    IDirectInputDevice8AVtbl *lpVtbl;
    LONG  ref;
    int   player;               /* 0 = player 1, 1 = player 2 */
    int   acquired;
    HANDLE event;
    /* data format mapping */
    DWORD dataSize;
    int   nFmt;
    int   fmtObj[MAX_FMT];      /* our object index, or -1 */
    DWORD fmtOfs[MAX_FMT];
    DWORD fmtType[MAX_FMT];
    int   objFmt[NUM_OBJS];     /* format entry for each object, or -1 */
    /* per-axis properties */
    LONG  rmin[NUM_AXES], rmax[NUM_AXES];
    DWORD deadzone[NUM_AXES], saturation[NUM_AXES];
    /* buffered input */
    DWORD bufSize;
    DIDEVICEOBJECTDATA events[MAX_EVENTS];
    int   evHead, evCount, overflow;
    DWORD seq;
    DWORD lastVal[NUM_OBJS];
    int   haveLast;
} VPad;

static IDirectInputDevice8AVtbl g_vpadVtbl;
static int g_wrapCount;      /* how many times the game created DirectInput */
static volatile int g_vpadOpened;   /* the game is using the ROTK Fix Gamepad */
static int verbose(void) { return g_logInput && g_wrapCount <= 2; }

static void log_once(const char *what)
{
    /* first-call log for each method, to learn how the game uses the device */
    static const char *seen[64];
    static int nseen;
    if (!g_logInput) return;
    for (int i = 0; i < nseen; i++) if (seen[i] == what) return;
    if (nseen < 64) seen[nseen++] = what;
    logf_("gamepad: game called %s", what);
}

static DWORD axis_value(VPad *d, int a, int32_t raw)
{
    /* raw: sticks -32768..32767, triggers 0..32767 */
    double v;   /* -1..1 */
    if (a == AX_Z || a == AX_RZ) v = raw / 32767.0 * 2.0 - 1.0;
    else v = raw < 0 ? raw / 32768.0 : raw / 32767.0;
    if (v < -1) v = -1;
    if (v > 1) v = 1;
    /* DirectInput-style per-axis deadzone/saturation (0..10000) */
    if (d->deadzone[a] || d->saturation[a] != 10000) {
        double dz = d->deadzone[a] / 10000.0, sat = d->saturation[a] / 10000.0;
        double m = fabs(v);
        if (sat <= dz) sat = dz + 0.0001;
        m = m <= dz ? 0 : (m >= sat ? 1 : (m - dz) / (sat - dz));
        v = v < 0 ? -m : m;
    }
    double lo = d->rmin[a], hi = d->rmax[a];
    return (DWORD)(LONG)lround(lo + (v + 1.0) * 0.5 * (hi - lo));
}

static DWORD obj_value(VPad *d, const PadState *s, int o)
{
    switch (obj_kind(o)) {
    case 0: return axis_value(d, o, s->axis[o]);
    case 1: return s->pov;
    default: return s->btn[o - NUM_AXES - 1];
    }
}

static void push_event(VPad *d, DWORD ofs, DWORD val)
{
    if (!d->bufSize) return;
    int cap = d->bufSize < MAX_EVENTS ? (int)d->bufSize : MAX_EVENTS;
    if (d->evCount >= cap) { d->overflow = 1; return; }
    int i = (d->evHead + d->evCount) % MAX_EVENTS;
    d->events[i].dwOfs = ofs;
    d->events[i].dwData = val;
    d->events[i].dwTimeStamp = GetTickCount();
    d->events[i].dwSequence = ++d->seq;
    d->events[i].uAppData = 0;
    d->evCount++;
}

static void vpad_poll(VPad *d, PadState *out)
{
    PadState s;
    read_pad(&s, d->player);
    if (d->bufSize) {
        for (int o = 0; o < NUM_OBJS; o++) {
            int f = d->objFmt[o];
            if (f < 0) continue;
            DWORD v = obj_value(d, &s, o);
            if (d->haveLast && v != d->lastVal[o]) push_event(d, d->fmtOfs[f], v);
            d->lastVal[o] = v;
        }
        d->haveLast = 1;
    }
    if (out) *out = s;
}

/* find an object by DIPH_BYOFFSET / DIPH_BYID */
static int find_obj(VPad *d, DWORD obj, DWORD how)
{
    if (how == DIPH_BYOFFSET) {
        for (int f = 0; f < d->nFmt; f++)
            if (d->fmtObj[f] >= 0 && d->fmtOfs[f] == obj) return d->fmtObj[f];
        return -1;
    }
    if (how == DIPH_BYID) {
        for (int o = 0; o < NUM_OBJS; o++)
            if ((g_objs[o].type & (DIDFT_TYPEMASK | DIDFT_INSTANCEMASK)) == (obj & (DIDFT_TYPEMASK | DIDFT_INSTANCEMASK)))
                return o;
        return -1;
    }
    return -1;
}

static HRESULT WINAPI vp_QueryInterface(IDirectInputDevice8A *This, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectInputDevice8A) ||
        IsEqualGUID(riid, &IID_IDirectInputDevice2A) || IsEqualGUID(riid, &IID_IDirectInputDevice7A) ||
        IsEqualGUID(riid, &IID_IDirectInputDeviceA)) {
        *ppv = This; ((VPad *)This)->ref++; return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI vp_AddRef(IDirectInputDevice8A *This) { return ++((VPad *)This)->ref; }
static ULONG WINAPI vp_Release(IDirectInputDevice8A *This)
{
    VPad *d = (VPad *)This;
    ULONG r = --d->ref;
    if (!r) HeapFree(GetProcessHeap(), 0, d);
    return r;
}

static HRESULT WINAPI vp_GetCapabilities(IDirectInputDevice8A *This, LPDIDEVCAPS caps)
{
    (void)This; log_once("GetCapabilities");
    if (!caps || caps->dwSize < sizeof(DIDEVCAPS_DX3)) return DIERR_INVALIDPARAM;
    DWORD sz = caps->dwSize;
    memset(caps, 0, sz);
    caps->dwSize = sz;
    caps->dwFlags = DIDC_ATTACHED | DIDC_EMULATED;
    caps->dwDevType = PAD_DEVTYPE;
    caps->dwAxes = NUM_AXES;
    caps->dwButtons = NUM_BUTTONS;
    caps->dwPOVs = 1;
    return DI_OK;
}

static void fill_obj_instance(VPad *d, int o, LPDIDEVICEOBJECTINSTANCEA oi)
{
    DWORD sz = oi->dwSize;
    memset(oi, 0, sz);
    oi->dwSize = sz;
    oi->guidType = *g_objs[o].guid;
    oi->dwOfs = d->objFmt[o] >= 0 ? d->fmtOfs[d->objFmt[o]] : g_objs[o].defOfs;
    oi->dwType = g_objs[o].type;
    oi->dwFlags = obj_kind(o) == 0 ? DIDOI_ASPECTPOSITION : 0;
    lstrcpynA(oi->tszName, g_objs[o].name, sizeof oi->tszName);
}

static HRESULT WINAPI vp_EnumObjects(IDirectInputDevice8A *This, LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ref, DWORD flags)
{
    VPad *d = (VPad *)This;
    log_once("EnumObjects");
    for (int o = 0; o < NUM_OBJS; o++) {
        DWORD t = g_objs[o].type;
        if (flags != DIDFT_ALL && !(DIDFT_GETTYPE(t) & DIDFT_GETTYPE(flags))) continue;
        DIDEVICEOBJECTINSTANCEA oi;
        oi.dwSize = sizeof oi;
        fill_obj_instance(d, o, &oi);
        if (cb(&oi, ref) == DIENUM_STOP) break;
    }
    return DI_OK;
}

static HRESULT WINAPI vp_GetProperty(IDirectInputDevice8A *This, REFGUID prop, LPDIPROPHEADER ph)
{
    VPad *d = (VPad *)This;
    log_once("GetProperty");
    if (!ph) return DIERR_INVALIDPARAM;
    if (prop == DIPROP_BUFFERSIZE) { ((LPDIPROPDWORD)ph)->dwData = d->bufSize; return DI_OK; }
    if (prop == DIPROP_RANGE || prop == DIPROP_DEADZONE || prop == DIPROP_SATURATION) {
        int o = ph->dwHow == DIPH_DEVICE ? 0 : find_obj(d, ph->dwObj, ph->dwHow);
        if (o < 0 || obj_kind(o) != 0) return DIERR_OBJECTNOTFOUND;
        if (prop == DIPROP_RANGE) { ((LPDIPROPRANGE)ph)->lMin = d->rmin[o]; ((LPDIPROPRANGE)ph)->lMax = d->rmax[o]; }
        else ((LPDIPROPDWORD)ph)->dwData = prop == DIPROP_DEADZONE ? d->deadzone[o] : d->saturation[o];
        return DI_OK;
    }
    if (prop == DIPROP_PRODUCTNAME || prop == DIPROP_INSTANCENAME) {
        MultiByteToWideChar(CP_ACP, 0, pad_name(d->player), -1, ((LPDIPROPSTRING)ph)->wsz, MAX_PATH);
        return DI_OK;
    }
    if (prop == DIPROP_GRANULARITY) { ((LPDIPROPDWORD)ph)->dwData = 1; return DI_OK; }
    if (prop == DIPROP_AXISMODE) { ((LPDIPROPDWORD)ph)->dwData = DIPROPAXISMODE_ABS; return DI_OK; }
    return DIERR_UNSUPPORTED;
}

static HRESULT WINAPI vp_SetProperty(IDirectInputDevice8A *This, REFGUID prop, LPCDIPROPHEADER ph)
{
    VPad *d = (VPad *)This;
    log_once("SetProperty");
    if (!ph) return DIERR_INVALIDPARAM;
    if (prop == DIPROP_BUFFERSIZE) {
        d->bufSize = ((LPCDIPROPDWORD)ph)->dwData;
        if (verbose()) logf_("gamepad: buffer size %lu", (unsigned long)d->bufSize);
        return DI_OK;
    }
    if (prop == DIPROP_RANGE || prop == DIPROP_DEADZONE || prop == DIPROP_SATURATION) {
        int first = 0, last = NUM_AXES - 1;
        if (ph->dwHow != DIPH_DEVICE) {
            int o = find_obj(d, ph->dwObj, ph->dwHow);
            if (o < 0) return DIERR_OBJECTNOTFOUND;
            if (obj_kind(o) != 0) return DI_OK;
            first = last = o;
        }
        for (int a = first; a <= last; a++) {
            if (prop == DIPROP_RANGE) { d->rmin[a] = ((LPCDIPROPRANGE)ph)->lMin; d->rmax[a] = ((LPCDIPROPRANGE)ph)->lMax; }
            else if (prop == DIPROP_DEADZONE) d->deadzone[a] = ((LPCDIPROPDWORD)ph)->dwData > 10000 ? 10000 : ((LPCDIPROPDWORD)ph)->dwData;
            else d->saturation[a] = ((LPCDIPROPDWORD)ph)->dwData > 10000 ? 10000 : ((LPCDIPROPDWORD)ph)->dwData;
        }
        if (verbose()) {
            if (prop == DIPROP_RANGE) logf_("gamepad: range %ld..%ld (how=%lu obj=%lu)", (long)((LPCDIPROPRANGE)ph)->lMin, (long)((LPCDIPROPRANGE)ph)->lMax, (unsigned long)ph->dwHow, (unsigned long)ph->dwObj);
            else logf_("gamepad: %s %lu (how=%lu obj=%lu)", prop == DIPROP_DEADZONE ? "deadzone" : "saturation", (unsigned long)((LPCDIPROPDWORD)ph)->dwData, (unsigned long)ph->dwHow, (unsigned long)ph->dwObj);
        }
        return DI_OK;
    }
    return DI_OK;   /* accept and ignore anything else (axis mode, autocenter, ...) */
}

static HRESULT WINAPI vp_Acquire(IDirectInputDevice8A *This)
{
    VPad *d = (VPad *)This;
    log_once("Acquire");
    if (!d->dataSize) return DIERR_INVALIDPARAM;
    if (d->acquired) return S_FALSE;
    d->acquired = 1;
    d->haveLast = 0;
    return DI_OK;
}
static HRESULT WINAPI vp_Unacquire(IDirectInputDevice8A *This)
{
    VPad *d = (VPad *)This;
    log_once("Unacquire");
    if (!d->acquired) return DI_NOEFFECT;
    d->acquired = 0;
    return DI_OK;
}

static HRESULT WINAPI vp_GetDeviceState(IDirectInputDevice8A *This, DWORD cb, LPVOID data)
{
    VPad *d = (VPad *)This;
    log_once("GetDeviceState");
    if (!d->acquired) return DIERR_NOTACQUIRED;
    if (!data || cb != d->dataSize) return DIERR_INVALIDPARAM;
    PadState s;
    vpad_poll(d, &s);
    memset(data, 0, cb);
    for (int f = 0; f < d->nFmt; f++) {
        BYTE *p = (BYTE *)data + d->fmtOfs[f];
        int o = d->fmtObj[f];
        if (o < 0) {
            /* unmatched slots: POVs must read as centred */
            if (DIDFT_GETTYPE(d->fmtType[f]) & DIDFT_POV && d->fmtOfs[f] + 4 <= cb) *(DWORD *)p = 0xFFFFFFFF;
            continue;
        }
        DWORD v = obj_value(d, &s, o);
        if (obj_kind(o) == 2) { if (d->fmtOfs[f] < cb) *p = (BYTE)v; }
        else if (d->fmtOfs[f] + 4 <= cb) *(DWORD *)p = v;
    }
    return DI_OK;
}

static HRESULT WINAPI vp_GetDeviceData(IDirectInputDevice8A *This, DWORD cbObj, LPDIDEVICEOBJECTDATA out, LPDWORD inout, DWORD flags)
{
    VPad *d = (VPad *)This;
    log_once("GetDeviceData");
    if (!d->acquired) return DIERR_NOTACQUIRED;
    if (!d->bufSize) return DIERR_NOTBUFFERED;
    if (!inout) return DIERR_INVALIDPARAM;
    vpad_poll(d, NULL);
    DWORD want = *inout, n = 0;
    while (n < want && (int)n < d->evCount) {
        if (out) memcpy((BYTE *)out + n * cbObj, &d->events[(d->evHead + n) % MAX_EVENTS], cbObj < sizeof(DIDEVICEOBJECTDATA) ? cbObj : sizeof(DIDEVICEOBJECTDATA));
        n++;
    }
    if (!(flags & DIGDD_PEEK)) { d->evHead = (d->evHead + n) % MAX_EVENTS; d->evCount -= n; }
    *inout = n;
    if (d->overflow) { d->overflow = 0; return DI_BUFFEROVERFLOW; }
    return DI_OK;
}

static HRESULT WINAPI vp_SetDataFormat(IDirectInputDevice8A *This, LPCDIDATAFORMAT fmt)
{
    VPad *d = (VPad *)This;
    if (!fmt || !fmt->rgodf) return DIERR_INVALIDPARAM;
    if (d->acquired) return DIERR_ACQUIRED;
    init_objs();
    int used[NUM_OBJS] = {0};
    d->dataSize = fmt->dwDataSize;
    d->nFmt = fmt->dwNumObjs < MAX_FMT ? (int)fmt->dwNumObjs : MAX_FMT;
    for (int o = 0; o < NUM_OBJS; o++) d->objFmt[o] = -1;
    int matched = 0;
    for (int f = 0; f < d->nFmt; f++) {
        const DIOBJECTDATAFORMAT *odf = &fmt->rgodf[f];
        d->fmtOfs[f] = odf->dwOfs;
        d->fmtType[f] = odf->dwType;
        d->fmtObj[f] = -1;
        DWORD wantType = DIDFT_GETTYPE(odf->dwType);
        DWORD inst = DIDFT_GETINSTANCE(odf->dwType);
        int anyInst = (odf->dwType & DIDFT_INSTANCEMASK) == DIDFT_ANYINSTANCE;
        for (int o = 0; o < NUM_OBJS; o++) {
            if (used[o]) continue;
            if (!(DIDFT_GETTYPE(g_objs[o].type) & wantType)) continue;
            if (odf->pguid && !IsEqualGUID(odf->pguid, g_objs[o].guid)) continue;
            if (!anyInst && DIDFT_GETINSTANCE(g_objs[o].type) != inst) continue;
            used[o] = 1;
            d->fmtObj[f] = o;
            d->objFmt[o] = f;
            matched++;
            break;
        }
    }
    if (verbose())
        logf_("gamepad: data format %lu bytes, %d entries, %d mapped to the virtual pad",
              (unsigned long)d->dataSize, d->nFmt, matched);
    return DI_OK;
}

static HRESULT WINAPI vp_SetEventNotification(IDirectInputDevice8A *This, HANDLE h)
{
    log_once("SetEventNotification");
    ((VPad *)This)->event = h;
    return DI_OK;
}
static HRESULT WINAPI vp_SetCooperativeLevel(IDirectInputDevice8A *This, HWND w, DWORD f)
{
    (void)This; (void)w; (void)f; log_once("SetCooperativeLevel");
    return DI_OK;
}
static HRESULT WINAPI vp_GetObjectInfo(IDirectInputDevice8A *This, LPDIDEVICEOBJECTINSTANCEA oi, DWORD obj, DWORD how)
{
    VPad *d = (VPad *)This;
    log_once("GetObjectInfo");
    if (!oi) return DIERR_INVALIDPARAM;
    init_objs();
    int o = find_obj(d, obj, how);
    if (o < 0) return DIERR_OBJECTNOTFOUND;
    fill_obj_instance(d, o, oi);
    return DI_OK;
}
static void fill_dev_instance(LPDIDEVICEINSTANCEA di, int player)
{
    DWORD sz = di->dwSize;
    memset(di, 0, sz);
    di->dwSize = sz;
    di->guidInstance = player == 1 ? GUID_RotkPad2Instance : GUID_RotkPadInstance;
    di->guidProduct = player == 1 ? GUID_RotkPad2Product : GUID_RotkPadProduct;
    di->dwDevType = PAD_DEVTYPE;
    lstrcpynA(di->tszInstanceName, pad_name(player), sizeof di->tszInstanceName);
    lstrcpynA(di->tszProductName, pad_name(player), sizeof di->tszProductName);
}
static HRESULT WINAPI vp_GetDeviceInfo(IDirectInputDevice8A *This, LPDIDEVICEINSTANCEA di)
{
    log_once("GetDeviceInfo");
    if (!di) return DIERR_INVALIDPARAM;
    fill_dev_instance(di, ((VPad *)This)->player);
    return DI_OK;
}
static HRESULT WINAPI vp_RunControlPanel(IDirectInputDevice8A *This, HWND w, DWORD f) { (void)This; (void)w; (void)f; return DI_OK; }
static HRESULT WINAPI vp_Initialize(IDirectInputDevice8A *This, HINSTANCE h, DWORD v, REFGUID g) { (void)This; (void)h; (void)v; (void)g; return DI_OK; }
static HRESULT WINAPI vp_CreateEffect(IDirectInputDevice8A *This, REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT *pe, LPUNKNOWN u)
{ (void)This; (void)g; (void)e; (void)u; log_once("CreateEffect (rumble, not yet supported)"); if (pe) *pe = NULL; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI vp_EnumEffects(IDirectInputDevice8A *This, LPDIENUMEFFECTSCALLBACKA cb, LPVOID r, DWORD t) { (void)This; (void)cb; (void)r; (void)t; log_once("EnumEffects"); return DI_OK; }
static HRESULT WINAPI vp_GetEffectInfo(IDirectInputDevice8A *This, LPDIEFFECTINFOA e, REFGUID g) { (void)This; (void)e; (void)g; return DIERR_DEVICENOTREG; }
static HRESULT WINAPI vp_GetForceFeedbackState(IDirectInputDevice8A *This, LPDWORD s) { (void)This; (void)s; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI vp_SendForceFeedbackCommand(IDirectInputDevice8A *This, DWORD f) { (void)This; (void)f; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI vp_EnumCreatedEffectObjects(IDirectInputDevice8A *This, LPDIENUMCREATEDEFFECTOBJECTSCALLBACK cb, LPVOID r, DWORD f) { (void)This; (void)cb; (void)r; (void)f; return DI_OK; }
static HRESULT WINAPI vp_Escape(IDirectInputDevice8A *This, LPDIEFFESCAPE e) { (void)This; (void)e; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI vp_Poll(IDirectInputDevice8A *This)
{
    VPad *d = (VPad *)This;
    log_once("Poll");
    if (!d->acquired) return DIERR_NOTACQUIRED;
    if (d->bufSize) vpad_poll(d, NULL);
    return DI_OK;
}
static HRESULT WINAPI vp_SendDeviceData(IDirectInputDevice8A *This, DWORD cb, LPCDIDEVICEOBJECTDATA o, LPDWORD n, DWORD f) { (void)This; (void)cb; (void)o; (void)n; (void)f; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI vp_EnumEffectsInFile(IDirectInputDevice8A *This, LPCSTR fn, LPDIENUMEFFECTSINFILECALLBACK cb, LPVOID r, DWORD f) { (void)This; (void)fn; (void)cb; (void)r; (void)f; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI vp_WriteEffectToFile(IDirectInputDevice8A *This, LPCSTR fn, DWORD n, LPDIFILEEFFECT e, DWORD f) { (void)This; (void)fn; (void)n; (void)e; (void)f; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI vp_BuildActionMap(IDirectInputDevice8A *This, LPDIACTIONFORMATA a, LPCSTR u, DWORD f) { (void)This; (void)a; (void)u; (void)f; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI vp_SetActionMap(IDirectInputDevice8A *This, LPDIACTIONFORMATA a, LPCSTR u, DWORD f) { (void)This; (void)a; (void)u; (void)f; return DIERR_UNSUPPORTED; }
static HRESULT WINAPI vp_GetImageInfo(IDirectInputDevice8A *This, LPDIDEVICEIMAGEINFOHEADERA h) { (void)This; (void)h; return DIERR_UNSUPPORTED; }

static IDirectInputDevice8AVtbl g_vpadVtbl = {
    vp_QueryInterface, vp_AddRef, vp_Release, vp_GetCapabilities, vp_EnumObjects, vp_GetProperty, vp_SetProperty,
    vp_Acquire, vp_Unacquire, vp_GetDeviceState, vp_GetDeviceData, vp_SetDataFormat, vp_SetEventNotification,
    vp_SetCooperativeLevel, vp_GetObjectInfo, vp_GetDeviceInfo, vp_RunControlPanel, vp_Initialize, vp_CreateEffect,
    vp_EnumEffects, vp_GetEffectInfo, vp_GetForceFeedbackState, vp_SendForceFeedbackCommand,
    vp_EnumCreatedEffectObjects, vp_Escape, vp_Poll, vp_SendDeviceData, vp_EnumEffectsInFile, vp_WriteEffectToFile,
    vp_BuildActionMap, vp_SetActionMap, vp_GetImageInfo,
};

static IDirectInputDevice8A *vpad_create(int player)
{
    init_objs();
    VPad *d = (VPad *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *d);
    if (!d) return NULL;
    d->lpVtbl = &g_vpadVtbl;
    d->ref = 1;
    d->player = player;
    for (int a = 0; a < NUM_AXES; a++) { d->rmin[a] = 0; d->rmax[a] = 65535; d->saturation[a] = 10000; }
    for (int o = 0; o < NUM_OBJS; o++) d->objFmt[o] = -1;
    return (IDirectInputDevice8A *)d;
}

/* ------------------------------------------------------------------ */
/* IDirectInput8A wrapper                                               */

typedef struct {
    IDirectInput8AVtbl *lpVtbl;
    IDirectInput8A *real;
    LONG ref;
} DI8Wrap;

static IDirectInput8AVtbl g_di8Vtbl;
#define REAL(This) (((DI8Wrap *)(This))->real)

static int is_game_controller(DWORD devType)
{
    BYTE t = GET_DIDEVICE_TYPE(devType);
    return t >= DI8DEVTYPE_JOYSTICK && t <= DI8DEVTYPE_1STPERSON;
}

static HRESULT WINAPI di_QueryInterface(IDirectInput8A *This, REFIID riid, void **ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectInput8A)) {
        *ppv = This; ((DI8Wrap *)This)->ref++; return S_OK;
    }
    return IDirectInput8_QueryInterface(REAL(This), riid, ppv);
}
static ULONG WINAPI di_AddRef(IDirectInput8A *This) { return ++((DI8Wrap *)This)->ref; }
static ULONG WINAPI di_Release(IDirectInput8A *This)
{
    DI8Wrap *w = (DI8Wrap *)This;
    ULONG r = --w->ref;
    if (!r) { IDirectInput8_Release(w->real); HeapFree(GetProcessHeap(), 0, w); }
    return r;
}
/* ------------------------------------------------------------------ */
/* Hotkeys                                                               */
/*
 * The game reads the keyboard through DirectInput, which can hide key
 * presses from GetAsyncKeyState. Watch the game's own keyboard reads instead
 * and count presses (key going down) per DirectInput key code.
 */
typedef HRESULT (WINAPI *KbGetState_t)(IDirectInputDevice8A *, DWORD, LPVOID);
typedef HRESULT (WINAPI *KbGetData_t)(IDirectInputDevice8A *, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
static KbGetState_t g_realKbGetState;
static KbGetData_t  g_realKbGetData;
static IDirectInputDevice8A *volatile g_kbd;
static BYTE          g_keyDown[256];
static volatile LONG g_keyPresses[256];
static BYTE          g_modHotkeys[256];

void input_register_hotkey(int dik)
{
    if (dik >= 0 && dik <= 255) {
        g_modHotkeys[dik] = 1;
        logf_("input: registered dynamic mod hotkey (DIK %#x)", dik);
    }
}

/* ROTK Fix's own hotkeys and dynamically registered mod hotkeys are hidden
 * from the game, so pressing them does not also count as "any key" (which skips cutscenes). */
static int is_hotkey(DWORD dik)
{
    if (dik <= 255 && g_modHotkeys[dik]) return 1;

    /* ROTK Fix core hotkeys */
    return dik == 0x2B /* \ screenshot */ || dik == 0xB7 /* Print Screen */
        || dik == 0x1A /* [ sharpening */ || dik == 0x1B /* ] */ || dik == 0x43 /* F9 */;
}

static void key_event(DWORD dik, int down)
{
    if (dik > 255) return;
    if (down && !g_keyDown[dik]) {
        if (is_hotkey(dik)) {
            logf_("keyboard: hotkey (code %#lx) pressed", (unsigned long)dik);
        } else {
            static int nlog;
            if (nlog++ < 20) dlogf_("keyboard: key %#lx pressed", (unsigned long)dik);
        }
        InterlockedIncrement(&g_keyPresses[dik]);
    }
    g_keyDown[dik] = (BYTE)(down != 0);
}

int input_keyboard_seen(void) { return g_kbd != NULL; }

/* One synthetic key press (down, then up on the next read), sent to the game
 * through its own keyboard reads - used to skip the opening logos. */
static volatile DWORD g_injDik, g_injDue;
static volatile int   g_injState;          /* 0 idle, 1 waiting, 2 down sent */
void input_inject_key_press(DWORD dik, DWORD delayMs)
{
    g_injDik = dik;
    g_injDue = GetTickCount() + delayMs;
    g_injState = 1;
}
static int inj_step(void)                  /* 1 = report key down now, 0 = nothing, -1 = report key up */
{
    if (g_injState == 1 && (LONG)(GetTickCount() - g_injDue) >= 0) { g_injState = 2; return 1; }
    if (g_injState == 2) { g_injState = 0; return -1; }
    return 0;
}

int input_take_key_presses(int dik)
{
    if (dik < 0 || dik > 255) return 0;
    return (int)InterlockedExchange(&g_keyPresses[dik], 0);
}

void input_simulate_key_press(int dik)
{
    if (dik >= 0 && dik <= 255) InterlockedIncrement(&g_keyPresses[dik]);
}

int input_is_key_down(int dik)
{
    if (dik < 0 || dik > 255) return 0;
    return g_keyDown[dik] != 0;
}

static HRESULT WINAPI kb_GetDeviceState(IDirectInputDevice8A *This, DWORD cb, LPVOID data)
{
    HRESULT hr = g_realKbGetState(This, cb, data);
    if (SUCCEEDED(hr) && This == g_kbd && data && cb >= 256 && g_injState && g_injDik < 256) {
        if (inj_step() == 1) ((BYTE *)data)[g_injDik] = 0x80;
    }
    if (SUCCEEDED(hr) && This == g_kbd && data && cb >= 256) {
        BYTE *b = (BYTE *)data;
        for (int k = 0; k < 256; k++) {
            if (b[k] & 0x80 || g_keyDown[k]) key_event((DWORD)k, b[k] & 0x80);
            if (is_hotkey((DWORD)k)) b[k] = 0;
        }
    }
    return hr;
}

static HRESULT WINAPI kb_GetDeviceData(IDirectInputDevice8A *This, DWORD cbObj, LPDIDEVICEOBJECTDATA rg, LPDWORD n, DWORD fl)
{
    DWORD cap = n ? *n : 0;
    HRESULT hr = g_realKbGetData(This, cbObj, rg, n, fl);
    if (SUCCEEDED(hr) && This == g_kbd && rg && n && cbObj >= sizeof(DIDEVICEOBJECTDATA_DX3)) {
        DWORD out = 0;
        for (DWORD i = 0; i < *n; i++) {
            DIDEVICEOBJECTDATA *e = (DIDEVICEOBJECTDATA *)((BYTE *)rg + i * cbObj);
            if (!(fl & DIGDD_PEEK)) key_event(e->dwOfs, e->dwData & 0x80);
            if (is_hotkey(e->dwOfs)) continue;                 /* hide from the game */
            if (out != i) memmove((BYTE *)rg + out * cbObj, e, cbObj);
            out++;
        }
        if (g_injState && !(fl & DIGDD_PEEK) && out < cap) {
            int st = inj_step();
            if (st) {
                DIDEVICEOBJECTDATA *e = (DIDEVICEOBJECTDATA *)((BYTE *)rg + out * cbObj);
                memset(e, 0, cbObj);
                e->dwOfs = g_injDik;
                e->dwData = st == 1 ? 0x80 : 0;
                e->dwTimeStamp = GetTickCount();
                out++;
            }
        }
        *n = out;
    }
    return hr;
}

static void hook_keyboard(IDirectInputDevice8A *d)
{
    void **vt = *(void ***)d;
    DWORD old;
    VirtualProtect(&vt[9], 2 * sizeof(void *), PAGE_EXECUTE_READWRITE, &old);
    if (vt[9] != (void *)kb_GetDeviceState) { g_realKbGetState = (KbGetState_t)vt[9]; vt[9] = (void *)kb_GetDeviceState; }
    if (vt[10] != (void *)kb_GetDeviceData) { g_realKbGetData = (KbGetData_t)vt[10]; vt[10] = (void *)kb_GetDeviceData; }
    VirtualProtect(&vt[9], 2 * sizeof(void *), old, &old);
    static int logged;
    if (!logged++) logf_("keyboard: watching the game's keyboard for hotkeys");
    g_kbd = d;
}

/* Windowed mode: the game takes the mouse exclusively, which hides the
 * Windows cursor everywhere, so the window cannot be moved or left. Share
 * the mouse instead (the game still gets all movement and buttons while it
 * is in front). */
typedef HRESULT (WINAPI *SetCoop_t)(IDirectInputDevice8A *, HWND, DWORD);
static SetCoop_t g_realSetCoop;
static IDirectInputDevice8A *volatile g_mouse;
static HRESULT WINAPI ms_SetCooperativeLevel(IDirectInputDevice8A *This, HWND w, DWORD f)
{
    if (This == g_mouse && graphics_is_windowed() && (f & DISCL_EXCLUSIVE)) {
        static int logged;
        if (!logged++) logf_("mouse: windowed mode - sharing the mouse so the Windows cursor can leave the game");
        f = (f & ~(DWORD)DISCL_EXCLUSIVE) | DISCL_NONEXCLUSIVE;
    }
    return g_realSetCoop(This, w, f);
}
static void hook_mouse(IDirectInputDevice8A *d)
{
    void **vt = *(void ***)d;
    DWORD old;
    VirtualProtect(&vt[13], sizeof(void *), PAGE_EXECUTE_READWRITE, &old);
    if (vt[13] != (void *)ms_SetCooperativeLevel) { g_realSetCoop = (SetCoop_t)vt[13]; vt[13] = (void *)ms_SetCooperativeLevel; }
    VirtualProtect(&vt[13], sizeof(void *), old, &old);
    g_mouse = d;
}

static HRESULT WINAPI di_CreateDevice(IDirectInput8A *This, REFGUID g, LPDIRECTINPUTDEVICE8A *dev, LPUNKNOWN u)
{
    int vp = g_padEnabled ? pad_from_guid(g) : -1;
    if (vp >= 0) {
        if (!dev) return DIERR_INVALIDPARAM;
        *dev = vpad_create(vp);
        if (vp == 0) g_vpadOpened = 1;
        if (verbose()) logf_("gamepad: game opened %s", pad_name(vp));
        return *dev ? DI_OK : DIERR_OUTOFMEMORY;
    }
    HRESULT hr = IDirectInput8_CreateDevice(REAL(This), g, dev, u);
    if (SUCCEEDED(hr) && g && dev && *dev && IsEqualGUID(g, &GUID_SysKeyboard)) hook_keyboard(*dev);
    if (SUCCEEDED(hr) && g && dev && *dev && IsEqualGUID(g, &GUID_SysMouse)) hook_mouse(*dev);
    return hr;
}

typedef struct { LPDIENUMDEVICESCALLBACKA cb; LPVOID ref; int stopped; } EnumCtx;
static BOOL CALLBACK enum_filter(LPCDIDEVICEINSTANCEA di, LPVOID p)
{
    EnumCtx *c = (EnumCtx *)p;
    /* real controllers are hidden only while the ROTK Fix Gamepad works: without SDL3 (missing or failed, issue
     * #15) the game gets them as it would without ROTK Fix */
    if (g_padEnabled && g_hideReal && is_game_controller(di->dwDevType) && sdl_ready()) {
        static int logged;
        if (g_logInput && logged < 8) { logged++; logf_("gamepad: hiding real controller \"%s\" from the game", di->tszInstanceName); }
        return DIENUM_CONTINUE;
    }
    BOOL r = c->cb(di, c->ref);
    if (r == DIENUM_STOP) c->stopped = 1;
    return r;
}
static HRESULT WINAPI di_EnumDevices(IDirectInput8A *This, DWORD type, LPDIENUMDEVICESCALLBACKA cb, LPVOID ref, DWORD flags)
{
    if (!cb) return DIERR_INVALIDPARAM;
    if (verbose()) logf_("gamepad: game enumerating devices (type=%lu flags=%lu)", (unsigned long)type, (unsigned long)flags);
    int wantsPad = type == DI8DEVCLASS_ALL || type == DI8DEVCLASS_GAMECTRL || type == DI8DEVTYPE_GAMEPAD;
    if (g_padEnabled && wantsPad && !(flags & DIEDFL_FORCEFEEDBACK)) {
        for (int p = 0; p < (g_secondPad ? MAX_PADS : 1); p++) {
            DIDEVICEINSTANCEA di;
            di.dwSize = sizeof di;
            fill_dev_instance(&di, p);
            if (cb(&di, ref) == DIENUM_STOP) return DI_OK;
        }
    }
    /* Asking Windows for real game controllers takes 60-150 ms, and the game
     * rebuilds its input on every screen change, which shows up as a hitch.
     * When real controllers are hidden anyway there is nothing to ask for. */
    int controllerOnly = type == DI8DEVCLASS_GAMECTRL ||
                         (type >= DI8DEVTYPE_JOYSTICK && type <= DI8DEVTYPE_1STPERSON);
    if (g_padEnabled && g_hideReal && controllerOnly && sdl_ready()) return DI_OK;
    EnumCtx c = { cb, ref, 0 };
    LARGE_INTEGER t0, t1, fq; QueryPerformanceCounter(&t0);
    HRESULT hr = IDirectInput8_EnumDevices(REAL(This), type, enum_filter, &c, flags);
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&fq);
    if (verbose()) logf_("gamepad: real device scan took %.1f ms", (t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart);
    return hr;
}
static HRESULT WINAPI di_GetDeviceStatus(IDirectInput8A *This, REFGUID g)
{
    if (g_padEnabled && pad_from_guid(g) >= 0) return DI_OK;
    return IDirectInput8_GetDeviceStatus(REAL(This), g);
}
static HRESULT WINAPI di_RunControlPanel(IDirectInput8A *This, HWND w, DWORD f) { return IDirectInput8_RunControlPanel(REAL(This), w, f); }
static HRESULT WINAPI di_Initialize(IDirectInput8A *This, HINSTANCE h, DWORD v) { return IDirectInput8_Initialize(REAL(This), h, v); }
static HRESULT WINAPI di_FindDevice(IDirectInput8A *This, REFGUID g, LPCSTR n, LPGUID out) { return IDirectInput8_FindDevice(REAL(This), g, n, out); }
static HRESULT WINAPI di_EnumDevicesBySemantics(IDirectInput8A *This, LPCSTR u, LPDIACTIONFORMATA a, LPDIENUMDEVICESBYSEMANTICSCBA cb, LPVOID r, DWORD f)
{ return IDirectInput8_EnumDevicesBySemantics(REAL(This), u, a, cb, r, f); }
static HRESULT WINAPI di_ConfigureDevices(IDirectInput8A *This, LPDICONFIGUREDEVICESCALLBACK cb, LPDICONFIGUREDEVICESPARAMSA p, DWORD f, LPVOID r)
{ return IDirectInput8_ConfigureDevices(REAL(This), cb, p, f, r); }

static IDirectInput8AVtbl g_di8Vtbl = {
    di_QueryInterface, di_AddRef, di_Release, di_CreateDevice, di_EnumDevices, di_GetDeviceStatus,
    di_RunControlPanel, di_Initialize, di_FindDevice, di_EnumDevicesBySemantics, di_ConfigureDevices,
};

#include <shlobj.h>
/* The game keeps the player's saved controls in My Documents\ROTK.ini. If they
 * were saved for a different controller, the game keeps using that layout
 * instead of the ROTK Fix Gamepad profile. Report it so it can be fixed. */
static void check_user_ini(void)
{
    char ini[MAX_PATH];
    if (!safe_folder_path(CSIDL_PERSONAL, ini, sizeof ini)) return;
    /* the game keeps it in a subfolder named by string resource 1007 of
     * rotk.exe ("LOTR The Return of the King (tm) Data" in English copies) */
    char sub[160];
    if (LoadStringA(GetModuleHandleA(NULL), 1007, sub, sizeof sub) > 0) {
        strncat(ini, "\\", MAX_PATH - strlen(ini) - 1);
        strncat(ini, sub, MAX_PATH - strlen(ini) - 1);
    }
    strncat(ini, "\\ROTK.ini", MAX_PATH - strlen(ini) - 1);
    char map[64] = "", id[128] = "";
    GetPrivateProfileStringA("input0", "map", "", map, sizeof map, ini);
    GetPrivateProfileStringA("input_JS_BASE_0", "JOYSTICK_ID", "", id, sizeof id, ini);
    logf_("gamepad: player settings file: %s", ini);
    logf_("gamepad: player 1 saved layout: map=%s, controller=\"%s\"%s", map[0] ? map : "(none)", id,
          strcmp(map, "ROTKFixPad") == 0 ? "" : "  <- not the ROTK Fix Gamepad layout");
}

typedef HRESULT (WINAPI *DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID *, LPVOID);

HRESULT input_wrap_dinput8(void *realCreate, HINSTANCE h, DWORD v, REFIID r, LPVOID *o, LPVOID u)
{
    DI8Create_t create = (DI8Create_t)realCreate;
    if (!create) return E_FAIL;
    HRESULT hr = create(h, v, r, o, u);
    if (FAILED(hr) || !o || !*o) return hr;
    if (!g_padEnabled) { logf_("gamepad: disabled in ROTKFix.ini, passing DirectInput through"); return hr; }
    if (!IsEqualGUID(r, &IID_IDirectInput8A)) {
        logf_("gamepad: game asked for a non-ANSI DirectInput interface; passing through unchanged");
        return hr;
    }
    DI8Wrap *w = (DI8Wrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *w);
    if (!w) return hr;
    w->lpVtbl = &g_di8Vtbl;
    w->real = (IDirectInput8A *)*o;
    w->ref = 1;
    *o = w;
    g_wrapCount++;
    if (g_wrapCount == 1) {
        logf_("gamepad: DirectInput wrapped (version %#lx); ROTK Fix Gamepad available", (unsigned long)v);
        check_user_ini();
    } else if (g_wrapCount == 3) {
        logf_("gamepad: the game rebuilds its input on screen changes; further details not logged");
    }
    return hr;
}

/* ------------------------------------------------------------------ */
/* Button names in on-screen prompts                                    */
/*
 * The game names a controller button "Button" + its number (0x4a80c0, called
 * with eax = binding, cl = 1 for display text), so tutorials and hints say
 * "Button 7". With the ROTK Fix Gamepad the numbers always mean the same
 * buttons, so they are replaced with the real names for the connected
 * controller's brand (or the brand chosen in the launcher). Remapped buttons
 * are named by the physical button the player now presses.
 *
 * Prompts look up "the first binding" of an action (0x4a96d0), which can be a
 * keyboard key ("Press F1") even when playing on a controller. While player 1
 * is on the controller, the controller binding is preferred.
 */
#define ADDR_BIND_NAME   0x004A80C0u   /* const char *(eax = binding, cl = display) */
#define ADDR_ACTION_NAME 0x004A96D0u   /* const char *__cdecl (player, map, action, nth, display) */
#define ADDR_BIND_TABLE  0x008E7EF8u   /* bindings: [(player*3+map)*0xC230 + i*0x38], action id at +0x34 */
#define ADDR_BIND_COUNT  0x008E85F8u
#define ADDR_NAME_BUFFER 0x005C2500u   /* where the game builds names */
#define BIND_STRIDE 0x38
#define MAP_STRIDE  0xC230

static int prompt_style(void)
{
    if (g_promptStyle != PS_AUTO) return g_promptStyle;
    /* player 1's controller decides; if only another player's is connected, that one */
    SDL_Gamepad *pad = g_pad;
    for (int p = 1; !pad && p < MAX_PADS; p++) pad = g_pads[p];
    if (pad && p_SDL_GetGamepadType) {
        switch (p_SDL_GetGamepadType(pad)) {
        case SDL_GAMEPAD_TYPE_PS3: case SDL_GAMEPAD_TYPE_PS4: case SDL_GAMEPAD_TYPE_PS5: return PS_PLAYSTATION;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            return PS_NINTENDO;
        default: break;
        }
    }
    return PS_XBOX;
}

static const char *phys_label(int ph, int style)
{
    static const char *const xbox[NUM_PHYS] = { "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "Left Stick", "Right Stick",
        "LT", "RT", "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right" };
    static const char *const ps[NUM_PHYS] = { "Cross", "Circle", "Square", "Triangle", "L1", "R1", "Share", "Options", "L3", "R3",
        "L2", "R2", "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right" };
    /* SDL reports Nintendo buttons by position: bottom = B, right = A, left = Y, top = X */
    static const char *const nin[NUM_PHYS] = { "B", "A", "Y", "X", "L", "R", "Minus", "Plus", "Left Stick", "Right Stick",
        "ZL", "ZR", "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right" };
    if (ph < 0 || ph >= NUM_PHYS) return NULL;
    if (style == PS_PLAYSTATION) {
        if (ph == PH_BACK && g_pad && p_SDL_GetGamepadType && p_SDL_GetGamepadType(g_pad) == SDL_GAMEPAD_TYPE_PS5) return "Create";
        return ps[ph];
    }
    return style == PS_NINTENDO ? nin[ph] : xbox[ph];
}

/* name for virtual button index (0-based), or NULL to keep the game's text */
static const char *__cdecl pad_button_label(DWORD index)
{
    if (!g_vpadOpened || index >= NUM_BUTTONS) return NULL;
    int style = prompt_style();
    if (style == PS_ORIGINAL) return NULL;
    /* a button picture if the fonts have them (see icons.c), else the name */
    const char *n = icons_label(g_phys[0][index], style - PS_XBOX);   /* prompts are shown for player 1 */
    if (!n) n = phys_label(g_phys[0][index], style);
    if (!n) return NULL;
    /* return it the way the game does: in its shared name buffer */
    char *buf = (char *)ADDR_NAME_BUFFER;
    lstrcpynA(buf, n, 32);
    return buf;
}

typedef const char *(__cdecl *ActionName_t)(int, int, int, int, int);
static ActionName_t g_actionNameOrig;

static const char *__cdecl action_name_hook(int player, int map, int action, int nth, int display)
{
    player &= 0xFF; map &= 0xFF; display &= 0xFF;   /* passed as bytes */
    if (display && nth == 0 && player == 0 && map >= 0 && map < 3 && g_vpadOpened && g_pad && g_promptStyle != PS_ORIGINAL) {
        BYTE *base = (BYTE *)(DWORD_PTR)((player * 3 + map) * MAP_STRIDE);
        int count = *(int *)(base + ADDR_BIND_COUNT);
        for (int i = 0; i < count && i < 256; i++) {
            BYTE *b = base + ADDR_BIND_TABLE + i * BIND_STRIDE;
            if (*(int *)(b + 0x34) != action) continue;
            if (*(int *)b == 4 && *(int *)(b + 0x14) == 0) {         /* controller button */
                const char *n = pad_button_label(*(DWORD *)(b + 0x1C));
                if (n) return n;
            }
        }
    }
    return g_actionNameOrig(player, map, action, nth, display);
}

/* ---- "F1  Help" at the bottom of menu screens ----
 * 0x4ff896 builds it as "%s  %s" from two fixed texts (0x88 = "F1", 0x4b =
 * "Help"). With a controller in use, the first becomes the controller's Help
 * button (the left stick click in our layout: virtual button 9, slot 8). */
#define ADDR_HELP_KEYNAME 0x004FF896u   /* push 0x88; call 0x4fd7c0; add esp,4 */
#define ADDR_GAME_TEXT    0x004FD7C0u   /* const char *__cdecl text(int id) */
static const char *__cdecl help_key_name(void)
{
    const char *n = (g_pad && g_vpadOpened) ? pad_button_label(8) : NULL;
    return n ? n : ((const char *(__cdecl *)(int))ADDR_GAME_TEXT)(0x88);
}

/* ---- other menu hints ----
 * 0x4ff850 picks the key text for a menu hint by the element's PS2-era name:
 * "x" -> text 0x69 (Enter), "tri" -> 0x4e (Esc), "circle" -> 0x89 (F2,
 * Level Info). The two calls that fetch it (0x4ff974, 0x4ff9aa) go through
 * here: with a controller, the matching button of our menu layout instead
 * (MenuSelect = A slot 0, Back = B slot 1, LevelInfo = Y slot 3). */
static const DWORD k_menuKeyCalls[2] = { 0x004FF974u, 0x004FF9AAu };
static const char *__cdecl menu_key_text(int id)
{
    int slot = id == 0x69 ? 0 : id == 0x4e ? 1 : id == 0x89 ? 3 : -1;
    const char *n = (slot >= 0 && g_pad && g_vpadOpened) ? pad_button_label((DWORD)slot) : NULL;
    return n ? n : ((const char *(__cdecl *)(int))ADDR_GAME_TEXT)(id);
}

/* ---- the game's own mouse pointer in menus ----
 * 0x501850 draws it (pointer element in eax, position as two float args).
 * While a controller is being used it is not drawn; moving the mouse brings
 * it back. Our controller layout moves menus with the D-pad/stick, not the
 * pointer, so the pointer only moves when the mouse does. */
#define ADDR_POINTER_DRAW 0x00501850u
static int __cdecl pointer_hidden(DWORD x, DWORD y)
{
    static DWORD lastX, lastY, mouseTick;
    if (x != lastX || y != lastY) { lastX = x; lastY = y; mouseTick = GetTickCount() | 1; }
    return g_hidePointer && g_pad && g_padActiveTick && (LONG)(g_padActiveTick - mouseTick) > 0;
}

/* ---- the special ability: "Button 7" / "Button 8" / "Buttons 7+8" ----
 * The shop and the Options screen name the special ability's two buttons
 * with fixed text: sprintf("%s 7"), ("%s 8") at 0x4ff71c / 0x4ff744 and
 * ("%s 7+8", after an '@' the screen puts first) at 0x4ffb3f, text 0xE5
 * being "Button(s)". They never ask for the binding, so the button-name hook
 * above doesn't see them; with a controller in use they now name (or show)
 * the buttons too: virtual buttons 7 and 8 are slots 6 and 7 (#34). */
#define ADDR_SPRINTF 0x00511450u
typedef int (__cdecl *Sprintf_t)(char *, const char *, ...);
static const char *__cdecl special_button(int which)   /* 0: "7", 1: "8" */
{
    char *buf = (char *)(DWORD_PTR)(which ? 0x008E3758u : 0x008E37A8u);
    const char *n = (g_pad && g_vpadOpened) ? pad_button_label((DWORD)(6 + which)) : NULL;
    if (n) { lstrcpynA(buf, n, 32); return buf; }
    ((Sprintf_t)ADDR_SPRINTF)(buf, which ? "%s 8" : "%s 7", ((const char *(__cdecl *)(int))ADDR_GAME_TEXT)(0xE5));
    return buf;
}
static void __cdecl special_buttons(char *dst)   /* dst: after the '@' */
{
    char a[32] = "", b[32] = "";
    const char *n = (g_pad && g_vpadOpened) ? pad_button_label(6) : NULL;
    if (n) lstrcpynA(a, n, sizeof a);
    n = (g_pad && g_vpadOpened) ? pad_button_label(7) : NULL;
    if (n) lstrcpynA(b, n, sizeof b);
    if (a[0] && b[0]) ((Sprintf_t)ADDR_SPRINTF)(dst, "%s + %s", a, b);
    else ((Sprintf_t)ADDR_SPRINTF)(dst, "%s 7+8", ((const char *(__cdecl *)(int))ADDR_GAME_TEXT)(0xE5));
}
/* The Options screen's controls list names the special ability from its own text ("Buttons %d + %d", text 0x26,
 * kWinFE_InptCfg_SpecAbGPadDscFmt) at 0x5063b0: sprintf(dst, text(0x26), 7, 8). With the virtual pad open, the
 * controller column shows the two buttons the way it shows every other action's (#34). */
#define ADDR_OPTIONS_SPECIAL 0x005063B0u
static void __cdecl options_special(int unused, char *dst)
{
    (void)unused;
    char a[32] = "", b[32] = "";
    const char *n = g_vpadOpened ? pad_button_label(6) : NULL;
    if (n) lstrcpynA(a, n, sizeof a);
    n = g_vpadOpened ? pad_button_label(7) : NULL;
    if (n) lstrcpynA(b, n, sizeof b);
    if (a[0] && b[0]) ((Sprintf_t)ADDR_SPRINTF)(dst, "%s + %s", a, b);
    else ((Sprintf_t)ADDR_SPRINTF)(dst, ((const char *(__cdecl *)(int))ADDR_GAME_TEXT)(0x26), 7, 8);
}
static int patch_options_special(void)
{
    /* push 8; push 7; push 0x26; call text; add esp,4; push eax; mov eax,[esp+14h]; push eax; call sprintf */
    static const BYTE sig[] = { 0x6A, 0x08, 0x6A, 0x07, 0x6A, 0x26, 0xE8, 0x05, 0x74, 0xFF, 0xFF, 0x83, 0xC4, 0x04, 0x50,
                                0x8B, 0x44, 0x24, 0x14, 0x50, 0xE8, 0x87, 0xB0, 0x00, 0x00 };
    if (!readable((void *)ADDR_OPTIONS_SPECIAL, sizeof sig) || memcmp((void *)ADDR_OPTIONS_SPECIAL, sig, sizeof sig)) return 0;
    BYTE j[5] = { 0xE9 };
    *(LONG *)(j + 1) = (LONG)((DWORD_PTR)&options_special - (ADDR_OPTIONS_SPECIAL + 5));
    return write_code((void *)ADDR_OPTIONS_SPECIAL, j, sizeof j);
}
static void patch_special_buttons(void)
{
    /* push 0xe5; call text; push eax; push fmt; push buf; call sprintf; add esp,0x10; mov eax,buf */
    static const BYTE sig7[] = { 0x68, 0xE5, 0, 0, 0, 0xE8, 0x9A, 0xE0, 0xFF, 0xFF, 0x50, 0x68, 0xD0, 0x59, 0x54, 0x00 };
    static const BYTE sig8[] = { 0x68, 0xE5, 0, 0, 0, 0xE8, 0x72, 0xE0, 0xFF, 0xFF, 0x50, 0x68, 0xC8, 0x59, 0x54, 0x00 };
    static const BYTE sigBoth[] = { 0x68, 0xE5, 0, 0, 0, 0xE8, 0x77, 0xDC, 0xFF, 0xFF, 0x50, 0x68, 0xF0, 0x59, 0x54, 0x00,
                                    0x47, 0x57, 0xE8, 0xFA, 0x18, 0x01, 0x00, 0x83, 0xC4, 0x10 };
    int n = 0;
    struct { DWORD at; const BYTE *sig; int which; } one[2] = { { 0x004FF71Cu, sig7, 0 }, { 0x004FF744u, sig8, 1 } };
    for (int i = 0; i < 2; i++) {
        /* 0x22 bytes, ending in "jmp 0x4ff780": push which; call; add esp,4; jmp 0x4ff780 */
        if (!readable((void *)one[i].at, 0x24) || memcmp((void *)one[i].at, one[i].sig, 16) || ((BYTE *)one[i].at)[0x22] != 0xEB) continue;
        BYTE c[0x22];
        memset(c, 0x90, sizeof c);
        c[0] = 0x6A; c[1] = (BYTE)one[i].which;
        c[2] = 0xE8; *(LONG *)(c + 3) = (LONG)((DWORD_PTR)&special_button - (one[i].at + 7));
        c[7] = 0x83; c[8] = 0xC4; c[9] = 0x04;
        c[10] = 0xE9; *(LONG *)(c + 11) = (LONG)(0x004FF780u - (one[i].at + 15));
        if (write_code((void *)one[i].at, c, sizeof c)) n++;
    }
    const DWORD at = 0x004FFB3Fu;
    if (readable((void *)at, sizeof sigBoth) && !memcmp((void *)at, sigBoth, sizeof sigBoth)) {
        BYTE c[sizeof sigBoth];
        memset(c, 0x90, sizeof c);
        c[0] = 0x47;                                     /* inc edi (past the '@') */
        c[1] = 0x57;                                     /* push edi */
        c[2] = 0xE8; *(LONG *)(c + 3) = (LONG)((DWORD_PTR)&special_buttons - (at + 7));
        c[7] = 0x83; c[8] = 0xC4; c[9] = 0x04;           /* add esp,4 */
        if (write_code((void *)at, c, sizeof c)) n++;
    }
    n += patch_options_special();
    if (n == 4) logf_("gamepad: special ability buttons named for the controller");
    else logf_("gamepad: special ability button text not recognised (%d of 4)", n);
}

static void patch_menu_bits(void)
{
    static const BYTE sigHelp[13] = { 0x68, 0x88, 0x00, 0x00, 0x00, 0xE8, 0x20, 0xDF, 0xFF, 0xFF, 0x83, 0xC4, 0x04 };
    static const BYTE sigPtr[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0 };
    if (readable((void *)ADDR_HELP_KEYNAME, sizeof sigHelp) && !memcmp((void *)ADDR_HELP_KEYNAME, sigHelp, sizeof sigHelp)) {
        BYTE c[13];
        memset(c, 0x90, sizeof c);
        c[0] = 0xE8; *(LONG *)(c + 1) = (LONG)((DWORD_PTR)&help_key_name - (ADDR_HELP_KEYNAME + 5));
        if (write_code((void *)ADDR_HELP_KEYNAME, c, sizeof c)) logf_("gamepad: menu Help hint follows the controller");
    } else logf_("gamepad: menu Help hint code not recognised");

    int n = 0;
    for (int i = 0; i < 2; i++) {
        BYTE *c = (BYTE *)k_menuKeyCalls[i];
        if (!readable(c, 5) || c[0] != 0xE8 || (DWORD)(k_menuKeyCalls[i] + 5 + *(LONG *)(c + 1)) != ADDR_GAME_TEXT) continue;
        BYTE call[5] = { 0xE8 };
        *(LONG *)(call + 1) = (LONG)((DWORD_PTR)&menu_key_text - (k_menuKeyCalls[i] + 5));
        if (write_code(c, call, 5)) n++;
    }
    if (n == 2) logf_("gamepad: menu Select/Back/Level Info hints follow the controller");
    else logf_("gamepad: menu hint code not recognised (%d of 2)", n);

    if (!readable((void *)ADDR_POINTER_DRAW, sizeof sigPtr) || memcmp((void *)ADDR_POINTER_DRAW, sigPtr, sizeof sigPtr)) {
        logf_("gamepad: mouse pointer code not recognised");
        return;
    }
    BYTE *t = (BYTE *)code_alloc(48);
    if (!t) return;
    BYTE *p = t, *jnz;
    *p++ = 0x60;                                                   /* pushad */
    *p++ = 0xFF; *p++ = 0x74; *p++ = 0x24; *p++ = 0x28;           /* push [esp+28h] (y) */
    *p++ = 0xFF; *p++ = 0x74; *p++ = 0x24; *p++ = 0x28;           /* push [esp+28h] (x) */
    *p++ = 0xE8; *(LONG *)p = (LONG)((DWORD_PTR)&pointer_hidden - ((DWORD_PTR)p + 4)); p += 4;
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x08;                         /* add esp,8 */
    *p++ = 0x85; *p++ = 0xC0;                                      /* test eax,eax */
    *p++ = 0x61;                                                   /* popad (flags kept) */
    *p++ = 0x75; jnz = p++;                                        /* jnz skip */
    memcpy(p, sigPtr, sizeof sigPtr); p += sizeof sigPtr;          /* original first instructions */
    *p++ = 0xE9; *(LONG *)p = (LONG)((ADDR_POINTER_DRAW + 6) - ((DWORD_PTR)p + 4)); p += 4;
    *jnz = (BYTE)(p - (jnz + 1));
    *p++ = 0xC3;                                                   /* skip: ret */
    BYTE j[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
    *(LONG *)(j + 1) = (LONG)((DWORD_PTR)t - (ADDR_POINTER_DRAW + 5));
    if (write_code((void *)ADDR_POINTER_DRAW, j, 6)) logf_("gamepad: mouse pointer hides while a controller is used");
}

void input_patch_prompts(void)
{
    static const BYTE sigBind[5] = { 0x84, 0xC9, 0x56, 0x8B, 0xF0 };              /* test cl,cl; push esi; mov esi,eax */
    static const BYTE sigAction[5] = { 0x0F, 0xB6, 0x44, 0x24, 0x04 };            /* movzx eax, byte [esp+4] */
    if (!g_padEnabled) return;
    patch_menu_bits();
    patch_special_buttons();
    if (!readable((void *)ADDR_BIND_NAME, 5) || memcmp((void *)ADDR_BIND_NAME, sigBind, 5) ||
        !readable((void *)ADDR_ACTION_NAME, 5) || memcmp((void *)ADDR_ACTION_NAME, sigAction, 5) ||
        !readable((void *)(ADDR_BIND_NAME + 0x92), 10) ||
        memcmp((void *)(ADDR_BIND_NAME + 0x92), "\x8B\x46\x1C\x40\x50\x68\xF4\xB0\x53\x00", 10)) {
        logf_("gamepad: button-name code not recognised; prompts keep the game's own names");
        return;
    }
    BYTE *t = (BYTE *)code_alloc(128);
    if (!t) return;

    /* 1. button names: stub in front of 0x4a80c0 */
    BYTE *p = t, *jz1, *jne1, *jne2, *jz2;
    *p++ = 0x84; *p++ = 0xC9;                                   /* test cl,cl */
    *p++ = 0x74; jz1 = p++;                                     /* jz orig */
    *p++ = 0x83; *p++ = 0x38; *p++ = 0x04;                      /* cmp dword [eax],4 */
    *p++ = 0x75; jne1 = p++;                                    /* jne orig */
    *p++ = 0x83; *p++ = 0x78; *p++ = 0x14; *p++ = 0x00;         /* cmp dword [eax+14h],0 */
    *p++ = 0x75; jne2 = p++;                                    /* jne orig */
    *p++ = 0x51; *p++ = 0x52; *p++ = 0x50;                      /* push ecx; push edx; push eax */
    *p++ = 0xFF; *p++ = 0x70; *p++ = 0x1C;                      /* push dword [eax+1Ch] */
    *p++ = 0xE8; *(LONG *)p = (LONG)((DWORD_PTR)&pad_button_label - ((DWORD_PTR)p + 4)); p += 4;
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;                      /* add esp,4 */
    *p++ = 0x85; *p++ = 0xC0;                                   /* test eax,eax */
    *p++ = 0x74; jz2 = p++;                                     /* jz restore */
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;                      /* add esp,4 (saved eax) */
    *p++ = 0x5A; *p++ = 0x59; *p++ = 0xC3;                      /* pop edx; pop ecx; ret */
    *jz2 = (BYTE)(p - (jz2 + 1));
    *p++ = 0x58; *p++ = 0x5A; *p++ = 0x59;                      /* restore: pop eax; pop edx; pop ecx */
    *jz1 = (BYTE)(p - (jz1 + 1)); *jne1 = (BYTE)(p - (jne1 + 1)); *jne2 = (BYTE)(p - (jne2 + 1));
    memcpy(p, sigBind, 5); p += 5;                              /* original first instructions */
    *p++ = 0xE9; *(LONG *)p = (LONG)((ADDR_BIND_NAME + 5) - ((DWORD_PTR)p + 4)); p += 4;

    /* 2. trampoline to the original action-name lookup */
    BYTE *orig = p;
    memcpy(p, sigAction, 5); p += 5;
    *p++ = 0xE9; *(LONG *)p = (LONG)((ADDR_ACTION_NAME + 5) - ((DWORD_PTR)p + 4));
    g_actionNameOrig = (ActionName_t)(void *)orig;
    FlushInstructionCache(GetCurrentProcess(), t, 128);

    BYTE jmp[5] = { 0xE9 };
    *(LONG *)(jmp + 1) = (LONG)((DWORD_PTR)t - (ADDR_BIND_NAME + 5));
    int ok = write_code((void *)ADDR_BIND_NAME, jmp, 5);
    *(LONG *)(jmp + 1) = (LONG)((DWORD_PTR)&action_name_hook - (ADDR_ACTION_NAME + 5));
    ok &= write_code((void *)ADDR_ACTION_NAME, jmp, 5);
    static const char *const sn[] = { "auto", "Xbox", "PlayStation", "Nintendo", "original" };
    if (ok) logf_("gamepad: on-screen prompts use controller button names (ButtonNames=%s)", sn[g_promptStyle]);
}

/* ------------------------------------------------------------------ */
/* Vibration                                                            */
/*
 * The game still has the Xbox version's rumble calls, but on PC they lead to
 * empty functions:
 *   0x4a9350 (player, seconds, strength 0-255)  light rumble
 *   0x4a9340 (player, seconds)                  heavy rumble (0 = stop)
 *   0x4a9360 (player, 2)                        stop (player 5 = everyone)
 *   0x4a9370 (player) -> bool                   vibration enabled?
 *   0x4a9380 (player, on)                       vibration on/off
 * Player is 0 or 1; 5 means all players. They are filled in here and sent to
 * the controllers through SDL (light = high-frequency motor, heavy = low).
 */
#define ADDR_RUMBLE_HEAVY  0x004A9340u
#define ADDR_RUMBLE_LIGHT  0x004A9350u
#define ADDR_RUMBLE_STOP   0x004A9360u
#define ADDR_RUMBLE_ISON   0x004A9370u
#define ADDR_RUMBLE_SETON  0x004A9380u

typedef struct { DWORD lightUntil, heavyUntil, resendAt; WORD light, heavy; } Rumble;
static Rumble g_rumble[MAX_PADS];
static int    g_rumbleOn = 1;     /* the game's own on/off */
static volatile LONG g_rumbleEvents;

static void rumble_send(int p)
{
    if (p < 0 || p >= MAX_PADS || !g_pads[p] || !p_SDL_RumbleGamepad) return;
    DWORD now = GetTickCount();
    Rumble *r = &g_rumble[p];
    WORD lo = (LONG)(r->heavyUntil - now) > 0 ? r->heavy : 0;
    WORD hi = (LONG)(r->lightUntil - now) > 0 ? r->light : 0;
    DWORD ms = 0;
    if (lo) ms = r->heavyUntil - now;
    if (hi && r->lightUntil - now > ms) ms = r->lightUntil - now;
    /* when one motor ends before the other, send again at that moment */
    r->resendAt = lo && hi ? (r->heavyUntil - now < r->lightUntil - now ? r->heavyUntil : r->lightUntil) : 0;
    p_SDL_RumbleGamepad(g_pads[p], lo, hi, ms);
}
static void rumble_refresh(int p)
{
    if (p >= 0 && p < MAX_PADS && g_rumble[p].resendAt && (LONG)(GetTickCount() - g_rumble[p].resendAt) >= 0) rumble_send(p);
}

static int rumble_players(int player, int *first, int *last)
{
    if (player == 5) { *first = 0; *last = MAX_PADS - 1; return 1; }
    if (player < 0 || player >= MAX_PADS) return 0;
    *first = *last = player;
    return 1;
}

static WORD rumble_amp(int strength255)
{
    if (strength255 < 0) strength255 = 0;
    if (strength255 > 255) strength255 = 255;
    return (WORD)(strength255 * 257u * (unsigned)g_rumblePct / 100u);
}

static DWORD rumble_ms(float seconds)
{
    if (!(seconds > 0.0f)) return 0;
    if (seconds > 5.0f) seconds = 5.0f;   /* never let a pad buzz for long by mistake */
    return (DWORD)(seconds * 1000.0f);
}

static void __cdecl rumble_light(int player, float seconds, int strength)
{
    int a, b;
    if (!g_rumbleOn || !g_rumblePct || !rumble_players(player & 0xFF, &a, &b)) return;
    if (g_rumbleEvents++ < 10) dlogf_("gamepad: rumble light player %d %.2fs strength %d", player & 0xFF, seconds, strength & 0xFF);
    for (int p = a; p <= b; p++) {
        g_rumble[p].light = rumble_amp(strength & 0xFF);
        g_rumble[p].lightUntil = GetTickCount() + rumble_ms(seconds);
        rumble_send(p);
    }
}
static void __cdecl rumble_heavy(int player, float seconds)
{
    int a, b;
    if (!g_rumbleOn || !g_rumblePct || !rumble_players(player & 0xFF, &a, &b)) return;
    if (g_rumbleEvents++ < 10) dlogf_("gamepad: rumble heavy player %d %.2fs", player & 0xFF, seconds);
    for (int p = a; p <= b; p++) {
        g_rumble[p].heavy = rumble_amp(255);
        g_rumble[p].heavyUntil = GetTickCount() + rumble_ms(seconds);
        rumble_send(p);
    }
}
static void __cdecl rumble_stop(int player, int how)
{
    (void)how;
    int a, b;
    if (!rumble_players(player & 0xFF, &a, &b)) return;
    for (int p = a; p <= b; p++) { g_rumble[p].lightUntil = g_rumble[p].heavyUntil = GetTickCount(); rumble_send(p); }
}
static int __cdecl rumble_is_on(int player) { (void)player; return g_rumbleOn && g_rumblePct > 0; }
static void __cdecl rumble_set_on(int player, int on)
{
    (void)player;
    g_rumbleOn = (on & 0xFF) != 0;
    if (!g_rumbleOn) rumble_stop(5, 0);
}

void input_patch_rumble(void)
{
    static const struct { DWORD addr; BYTE sig[3]; int n; void *fn; } stubs[] = {
        { ADDR_RUMBLE_HEAVY, { 0xC3, 0x90, 0x90 }, 3, (void *)rumble_heavy },
        { ADDR_RUMBLE_LIGHT, { 0xC3, 0x90, 0x90 }, 3, (void *)rumble_light },
        { ADDR_RUMBLE_STOP,  { 0xC3, 0x90, 0x90 }, 3, (void *)rumble_stop },
        { ADDR_RUMBLE_ISON,  { 0x32, 0xC0, 0xC3 }, 3, (void *)rumble_is_on },
        { ADDR_RUMBLE_SETON, { 0xC3, 0x90, 0x90 }, 3, (void *)rumble_set_on },
    };
    if (!g_padEnabled) return;
    for (size_t i = 0; i < sizeof stubs / sizeof stubs[0]; i++)
        if (!readable((void *)stubs[i].addr, 8) || memcmp((void *)stubs[i].addr, stubs[i].sig, stubs[i].n) ||
            *(BYTE *)(stubs[i].addr + 5) != 0x90) {
            logf_("gamepad: rumble code not recognised; vibration off");
            return;
        }
    int ok = 1;
    for (size_t i = 0; i < sizeof stubs / sizeof stubs[0]; i++) {
        BYTE jmp[5] = { 0xE9 };
        *(LONG *)(jmp + 1) = (LONG)((DWORD_PTR)stubs[i].fn - (stubs[i].addr + 5));
        ok &= write_code((void *)stubs[i].addr, jmp, 5);
    }
    if (ok) logf_("gamepad: vibration connected (strength %d%%)", g_rumblePct);
}
