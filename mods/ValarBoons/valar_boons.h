#ifndef VALAR_BOONS_H
#define VALAR_BOONS_H

#define CINTERFACE
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>

/* ----------------------------------------------------------------------------
 * ROTKFix Modding API Prototypes
 * ---------------------------------------------------------------------------- */
typedef int  (*RF_HitFilter_t)(void *who, int *amount, void *by);
typedef void (*RF_FrameHook_t)(void);
typedef void (*RF_RenderHook_t)(void *d3d9dev);
typedef void (*RF_RegisterHitFilter_t)(RF_HitFilter_t fn);
typedef void (*RF_RegisterFrameHook_t)(RF_FrameHook_t fn);
typedef void (*RF_RegisterRenderHook_t)(RF_RenderHook_t fn);
typedef void (*RF_RegisterResetHook_t)(RF_RenderHook_t fn);
typedef int  (*RF_IsPlayer_t)(const void *entity);
typedef int  (*RF_DealHit_t)(void *who, int amount, void *by);
typedef void (*RF_ShowNotification_t)(const char *title, const char *desc);
typedef void (*RF_ShowNotificationEx_t)(const char *category, const char *title, const char *desc);
typedef void (*RF_Log_t)(const char *fmt, ...);
typedef int  (*RF_IsLogEnabled_t)(void);
typedef int  (*RF_TakeKeyPress_t)(int dik);
typedef int  (*RF_IsKeyDown_t)(int dik);
typedef void (*RF_RegisterHotkey_t)(int dik);

extern RF_RegisterHitFilter_t   g_RegisterHitFilter;
extern RF_RegisterFrameHook_t   g_RegisterFrameHook;
extern RF_RegisterRenderHook_t  g_RegisterRenderHook;
extern RF_RegisterResetHook_t   g_RegisterResetHook;
extern RF_IsPlayer_t            g_IsPlayer;
extern RF_DealHit_t             g_DealHit;
extern RF_ShowNotification_t    g_ShowNotification;
extern RF_ShowNotificationEx_t  g_ShowNotificationEx;
extern RF_Log_t                 g_Log;
extern RF_IsLogEnabled_t        g_IsLogEnabled;
extern RF_TakeKeyPress_t        g_TakeKeyPress;
extern RF_IsKeyDown_t           g_IsKeyDown;
extern RF_RegisterHotkey_t      g_RegisterHotkey;

/* ----------------------------------------------------------------------------
 * Constants & Boon Data Model
 * ---------------------------------------------------------------------------- */
#define MAX_BOONS 16
#define MAX_CURVE 8
#define MAX_KEYS_PER_BOON 6
#define MAX_GROUPS 16

typedef struct {
    char name[48];
    char title[64];
    char desc[128];
    char keyStr[32];
    int  diks[MAX_KEYS_PER_BOON];
    int  dikCount;
    int  group;
    BYTE colorR, colorG, colorB;
    float duration;            /* seconds (0 = toggleable until manually turned off) */
    float cooldownCurve[MAX_CURVE];
    int   cooldownCurveCount;

    /* Combat modifiers */
    int damageMultiplier;      /* percentage (e.g. 160 = +60% damage, 100 = default) */
    int flatDamage;            /* flat bonus damage on player attack */
    int damageReduction;       /* percentage (e.g. 75 = 75% incoming damage absorbed) */
    int maxDamageClamped;      /* max incoming hit damage clamp (0 = disabled) */
    int holyShockDamage;       /* retribution holy shock dealt to attackers */

    /* Runtime State */
    volatile int   active;
    volatile DWORD activeUntil;
    volatile DWORD cooldownUntil;
    int            useCount;
} BoonConfig;

extern BoonConfig g_boons[MAX_BOONS];
extern int g_boonCount;

extern int g_enableAllDiks[MAX_KEYS_PER_BOON];
extern int g_enableAllDikCount;
extern int g_resetDiks[MAX_KEYS_PER_BOON];
extern int g_resetDikCount;

extern int g_showHud;
extern int g_showAura;

/* Group Shared Cooldown Tracking */
extern volatile DWORD g_groupCooldownUntil[MAX_GROUPS];
extern int g_groupUseCount[MAX_GROUPS];

/* Visual Hit & Attack Flash Timers */
extern volatile DWORD g_attackFlashUntil;
extern volatile DWORD g_defendFlashUntil;

extern char g_modLogPath[MAX_PATH];
extern char g_iniPath[MAX_PATH];

/* ----------------------------------------------------------------------------
 * Function Declarations across Modules
 * ---------------------------------------------------------------------------- */

/* valar_main.c */
void valar_log(const char *fmt, ...);
void notify_ex(const char *category, const char *title, const char *desc);
int  scale_val(int amount, int pct);

/* valar_config.c */
void load_boon_configs(const char *ini);

/* valar_logic.c */
float get_cooldown_duration(BoonConfig *b);
void  activate_boon(int idx, int viaEnableAll);
void  trigger_enable_all(void);
void  trigger_reset(void);
int   valar_hit_filter(void *who, int *amount, void *by);
void  valar_frame_hook(void);

/* valar_render.c */
void valar_render_hook(void *d3d9dev);
void valar_reset_hook(void *d3d9dev);

#endif /* VALAR_BOONS_H */
