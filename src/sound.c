/*
 * ROTK Fix - 3D sound and EAX reverb
 *
 * The game's sound uses DirectSound 3D with Creative EAX: it loads eax.dll and
 * calls EAXDirectSoundCreate8. Windows has had no hardware DirectSound 3D or
 * EAX since Vista, so on a modern PC the game loses its environmental reverb
 * and much of its 3D positioning. DSOAL (LGPL) implements DirectSound 3D and
 * EAX in software on top of OpenAL Soft (LGPL), with optional HRTF for
 * headphones.
 *
 * eax.dll creates DirectSound through COM, which always loads Windows' own
 * dsound.dll, so DSOAL can't simply be dropped next to the game. Instead the
 * game's GetProcAddress (import slot in rotk.exe) is wrapped: when it asks
 * eax.dll for EAXDirectSoundCreate8, it gets ROTK Fix's version, which makes
 * the DirectSound object with DSOAL from ROTKFix\sound. If DSOAL can't load,
 * the game's own eax.dll is used as before.
 */
#include "rotkfix.h"
#include <stdio.h>
#include <string.h>

#define ADDR_IAT_GETPROCADDRESS 0x005351A4u   /* rotk.exe import slot */

enum { SOUND_OFF, SOUND_SPEAKERS, SOUND_HEADPHONES };
static int g_soundMode = SOUND_SPEAKERS;

typedef FARPROC (WINAPI *GetProcAddress_t)(HMODULE, LPCSTR);
typedef HRESULT (WINAPI *DSCreate8_t)(const GUID *, void **, void *);
static GetProcAddress_t g_realGetProcAddress;
static DSCreate8_t      g_gameEaxCreate;      /* eax.dll's EAXDirectSoundCreate8 */
static DSCreate8_t      g_dsoalCreate;        /* DSOAL's DirectSoundCreate8 */
static int              g_dsoalTried;
static int              g_needEaxExport;      /* disc exe: hook eax.dll's export table */

void sound_load_settings(const char *ini)
{
    char v[32];
    GetPrivateProfileStringA("Sound", "Mode", "speakers", v, sizeof v, ini);
    g_soundMode = !lstrcmpiA(v, "off") ? SOUND_OFF : !lstrcmpiA(v, "headphones") ? SOUND_HEADPHONES : SOUND_SPEAKERS;
}

static void write_alsoft_ini(const char *dir)
{
    char p[MAX_PATH];
    snprintf(p, sizeof p, "%s\\alsoft.ini", dir);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fputs("# Written by ROTK Fix at every launch from ROTKFix.ini ([Sound] Mode) - edit that instead.\n[general]\n", f);
    if (g_soundMode == SOUND_HEADPHONES)
        fputs("channels = stereo\nstereo-mode = headphones\nstereo-encoding = hrtf\n", f);
    else
        fputs("stereo-mode = speakers\n", f);
    fclose(f);
    SetEnvironmentVariableA("ALSOFT_CONF", p);
}

static int load_dsoal(void)
{
    if (g_dsoalTried) return g_dsoalCreate != NULL;
    g_dsoalTried = 1;
    char dir[MAX_PATH], drv[MAX_PATH], ds[MAX_PATH];
    snprintf(dir, sizeof dir, "%s\\ROTKFix\\sound", g_dir);
    snprintf(drv, sizeof drv, "%s\\dsoal-aldrv.dll", dir);
    snprintf(ds, sizeof ds, "%s\\dsound.dll", dir);
    if (GetFileAttributesA(ds) == INVALID_FILE_ATTRIBUTES) { logf_("sound: ROTKFix\\sound\\dsound.dll not found; using the game's own sound"); return 0; }
    write_alsoft_ini(dir);
    /* DSOAL loads its OpenAL driver by name ("dsoal-aldrv.dll"); loading it
     * first from our folder makes that name resolve to this copy. */
    HMODULE hd = LoadLibraryExA(drv, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    HMODULE hs = hd ? LoadLibraryExA(ds, NULL, LOAD_WITH_ALTERED_SEARCH_PATH) : NULL;
    if (!hd || !hs) {
        DWORD e = GetLastError();
        logf_("sound: could not load DSOAL (error %lu)%s; using the game's own sound", e,
              e == ERROR_MOD_NOT_FOUND ? " - the Microsoft Visual C++ 2015-2022 x86 runtime may be missing" : "");
        return 0;
    }
    g_dsoalCreate = (DSCreate8_t)(void *)GetProcAddress(hs, "DirectSoundCreate8");   /* our own import, never hooked */
    if (!g_dsoalCreate) logf_("sound: DSOAL has no DirectSoundCreate8; using the game's own sound");
    return g_dsoalCreate != NULL;
}

static HRESULT WINAPI rf_EAXDirectSoundCreate8(const GUID *dev, void **out, void *outer)
{
    if (load_dsoal()) {
        HRESULT hr = g_dsoalCreate(dev, out, outer);
        if (SUCCEEDED(hr)) {
            logf_("sound: DirectSound 3D and EAX through DSOAL / OpenAL Soft (%s)",
                  g_soundMode == SOUND_HEADPHONES ? "headphones, HRTF" : "speakers");
            return hr;
        }
        logf_("sound: DSOAL could not start (%#lx); using the game's own sound", (unsigned long)hr);
    }
    return g_gameEaxCreate ? g_gameEaxCreate(dev, out, outer) : E_FAIL;
}

static FARPROC WINAPI rf_GetProcAddress(HMODULE m, LPCSTR name)
{
    FARPROC p = g_realGetProcAddress(m, name);
    if (p && (DWORD_PTR)name > 0xFFFF && !strcmp(name, "EAXDirectSoundCreate8") && g_soundMode != SOUND_OFF) {
        g_gameEaxCreate = (DSCreate8_t)(void *)p;
        return (FARPROC)(void *)rf_EAXDirectSoundCreate8;
    }
    return p;
}

void sound_patch(void)
{
    if (g_soundMode == SOUND_OFF) { logf_("sound: [Sound] Mode=off, the game's own sound"); return; }
    void **slot = (void **)ADDR_IAT_GETPROCADDRESS;
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    void *real = (void *)GetProcAddress(k32, "GetProcAddress");
    HMODULE owner = NULL;
    if (!readable(slot, sizeof *slot) || !*slot ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)*slot, &owner) ||
        (*slot != real && owner != k32 && owner != GetModuleHandleA("kernelbase.dll"))) {
        /* the original disc exe: SafeDisc routes the game's imports through
         * its own code. Hook eax.dll's export instead, once graphics start
         * (sound_late_patch): the game looks the function up after that. */
        g_needEaxExport = 1;
        logf_("sound: disc version - 3D sound attaches when the game starts");
        return;
    }
    g_realGetProcAddress = (GetProcAddress_t)*slot;
    void *hook = (void *)rf_GetProcAddress;
    if (write_code(slot, &hook, sizeof hook)) logf_("sound: ready to restore 3D sound and EAX (%s)", g_soundMode == SOUND_HEADPHONES ? "headphones" : "speakers");
}

/* Disc exe: send eax.dll's EAXDirectSoundCreate8 to our function. The game
 * looks the function up through SafeDisc's import code, so the address it
 * gets must stay inside eax.dll: the function's first instruction becomes a
 * jump to us, and a small trampoline runs the original for the fallback.
 * The game loads "<game folder>\\eax.dll" by full path, so loading the same
 * path here gives the same module; our reference keeps it loaded. Called
 * from Direct3DCreate8 (not under the loader lock), before the game's sound
 * starts. */
void sound_late_patch(void)
{
    static int done;
    if (!g_needEaxExport || done) return;
    done = 1;
    char p[MAX_PATH];
    path_in_dir(p, "eax.dll");
    HMODULE m = LoadLibraryA(p);
    BYTE *fn = m ? (BYTE *)(void *)GetProcAddress(m, "EAXDirectSoundCreate8") : NULL;
    /* Creative's eax.dll: mov eax,[esp+0Ch] (4 bytes); push ebp (1 byte) */
    static const BYTE prologue[5] = { 0x8B, 0x44, 0x24, 0x0C, 0x55 };
    if (!fn || !readable(fn, 5) || memcmp(fn, prologue, 5)) {
        logf_("sound: eax.dll not recognised; using the game's own sound");
        return;
    }
    BYTE *tr = (BYTE *)VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return;
    memcpy(tr, prologue, 5);                                      /* original first instructions */
    tr[5] = 0xE9;                                                 /* jmp back into eax.dll */
    *(LONG *)(tr + 6) = (LONG)((DWORD_PTR)(fn + 5) - ((DWORD_PTR)tr + 10));
    DWORD old;
    VirtualProtect(tr, 16, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), tr, 16);
    g_gameEaxCreate = (DSCreate8_t)(void *)tr;
    if (!g_realGetProcAddress)   /* load_dsoal looks DSOAL up with it */
        g_realGetProcAddress = (GetProcAddress_t)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProcAddress");
    BYTE jmp[5] = { 0xE9, 0, 0, 0, 0 };
    *(LONG *)(jmp + 1) = (LONG)((DWORD_PTR)&rf_EAXDirectSoundCreate8 - ((DWORD_PTR)fn + 5));
    if (write_code(fn, jmp, 5))
        logf_("sound: ready to restore 3D sound and EAX (%s; disc version)", g_soundMode == SOUND_HEADPHONES ? "headphones" : "speakers");
}
