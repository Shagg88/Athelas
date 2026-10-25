/* ROTK Fix - shared declarations */
#ifndef ROTKFIX_H
#define ROTKFIX_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* rotkfix.c */
void logf_(const char *fmt, ...);
extern int g_detailLog;   /* [Log] Detailed=1: testing diagnostics */
#define dlogf_(...) do { if (g_detailLog) logf_(__VA_ARGS__); } while (0)
extern char g_dir[MAX_PATH];
void path_in_dir(char *out, const char *name);
int  write_code(void *addr, const void *bytes, size_t n);
int  readable(const void *addr, size_t n);
void *code_alloc(size_t n);
int  safe_folder_path(int csidl, char *out, size_t n);   /* a special folder's path as ANSI, short-named if needed; 0 if none */   /* executable memory for trampolines; only during start-up patching */

void limiter_present_backstop(void);
/* slow-frame diagnostics: what happened during each slow frame */
enum { HITCH_FILES, HITCH_READKB, HITCH_DRAWS, HITCH_DRAWS3D, HITCH_TEXNEW, HITCH_VBNEW, HITCH_VBKB, HITCH_READUS, HITCH_N };
extern volatile LONG g_hitch[HITCH_N];
void hitch_present(LONGLONG t0, LONGLONG t1);   /* QPC ticks around the real Present */
void limiter_cap_to_refresh(unsigned hz);
enum { MOVIE_FIT_STRETCH, MOVIE_FIT_FILM, MOVIE_FIT_43 };
int  movie_fit(void);
extern volatile LONG g_inMenuLevel; /* the front end is loaded (menus), not a playable level */
extern volatile LONG g_levelSeq;   /* changes when the game opens a new level file (for per-level log lines) */

/* graphics.c */
void    graphics_load_settings(const char *ini);
int     graphics_enabled(void);
int     graphics_is_windowed(void);
int     graphics_movie_playing(void);   /* a movie frame was drawn in the last 150 ms */

/* post.c */
void    post_load_settings(const char *ini);
int     post_attach_dxvk_d3d8(HMODULE d3d8);

/* sound.c */
void    sound_load_settings(const char *ini);
void    sound_patch(void);
void    sound_late_patch(void);   /* disc exe: from Direct3DCreate8 */

/* gameplay.c */
void    gameplay_load_settings(const char *ini);
void    gameplay_patch(void);
typedef int (*RF_HitFilter_t)(void *who, int *amount, void *by);
typedef void (*RF_FrameHook_t)(void);
typedef void (*RF_RenderHook_t)(void *d3d9dev);
void    RF_RegisterHitFilter(RF_HitFilter_t fn);
int     RF_IsPlayer(const void *entity);
int     RF_DealHit(void *who, int amount, void *by);
void    RF_ShowNotification(const char *title, const char *desc);
void    RF_ShowNotificationEx(const char *category, const char *title, const char *desc);
void    RF_Log(const char *fmt, ...);
int     RF_IsLogEnabled(void);
void    RF_RegisterFrameHook(RF_FrameHook_t fn);
void    RF_RunFrameHooks(void);
void    RF_RegisterRenderHook(RF_RenderHook_t fn);
void    RF_RunRenderHooks(void *dev);
void    RF_RegisterResetHook(RF_RenderHook_t fn);
void    RF_RunResetHooks(void *dev);
int     RF_TakeKeyPress(int dik);
int     RF_IsKeyDown(int dik);
void    RF_SimulateKeyPress(int dik);
void    RF_RegisterHotkey(int dik);

/* achieve.c */
void    achieve_load_settings(const char *ini);
void    achieve_start(void);                 /* DllMain: the list, the saved state, the progress and Discord threads */
void    achieve_file(const char *name);       /* each file the game opens */
void    achieve_enemy_defeated(void);         /* gameplay.c */
void    achieve_push_note(const char *title, const char *desc);
void    achieve_push_note_ex(const char *category, const char *title, const char *desc);
struct IDirect3DDevice9;
void    achieve_draw(struct IDirect3DDevice9 *dev);   /* post.c: the note over the finished frame */
void    achieve_release(void);                /* post.c: the device is reset or made again */

/* textures.c */
void    textures_load_settings(const char *ini);
void    textures_on_create(void *dev, void *realCreateTexture, void *tex);
void   *textures_bind(void *dev, void *tex, int skip);   /* returns the texture to bind */
void    textures_note_2d(const void *tex);
void    textures_on_release(void *tex);

/* input.c */
void    input_load_settings(const char *ini);
void    input_register_hotkey(int dik);
int     input_keyboard_seen(void);
void    input_inject_key_press(DWORD dik, DWORD delayMs);
int     input_take_key_presses(int dik);   /* DirectInput key code, e.g. 0x43 = F9 */
void    input_simulate_key_press(int dik);
int     input_is_key_down(int dik);
void    input_patch_prompts(void);
void    icons_load_settings(const char *ini);
void    icons_patch(void);                 /* button pictures in the game's fonts */
const char *icons_label(int phys, int style);   /* style 0 Xbox, 1 PlayStation, 2 Nintendo */
int     icons_is_font_texture(const void *tex);   /* a texture the game draws text from */
void    input_patch_rumble(void);          /* the game's Xbox vibration calls -> controllers */         /* controller button names in on-screen prompts */
HRESULT input_wrap_dinput8(void *realCreate, HINSTANCE h, DWORD v, REFIID r, LPVOID *o, LPVOID u);

#endif
