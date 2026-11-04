// Config.cpp - reads ROTKPlus.ini (in the game folder)
// Language: C++17, MSVC or mingw-w64 g++, Win32.
#include "ROTKPlus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Config g_cfg;

static const char* const kHeroes[9] = { "aragorn", "legolas", "gimli", "gandalf", "sam", "frodo", "pippin", "merry", "faramir" };

static void IniPath(char* out, size_t n) { sprintf_s(out, n, "%s\\ROTKPlus.ini", GameDir()); }

// The co-op section used to be called [Coop3] (when the mod had three players at most): it is still read if the
// key is missing from [Coop].
int IniInt(const char* section, const char* key, int def)
{
    char ini[MAX_PATH]; IniPath(ini, sizeof ini);
    if (!strcmp(section, "Coop")) def = GetPrivateProfileIntA("Coop3", key, def, ini);
    return GetPrivateProfileIntA(section, key, def, ini);
}

void IniWriteInt(const char* section, const char* key, int value)
{
    char ini[MAX_PATH], num[16]; IniPath(ini, sizeof ini);
    sprintf_s(num, "%d", value);
    WritePrivateProfileStringA(section, key, num, ini);
}

static void IniString(const char* key, const char* def, char* out, size_t n)
{
    char ini[MAX_PATH]; IniPath(ini, sizeof ini);
    char legacy[64];
    GetPrivateProfileStringA("Coop3", key, def, legacy, sizeof legacy, ini);
    GetPrivateProfileStringA("Coop", key, legacy, out, (DWORD)n, ini);
}

void LoadConfig()
{
    char buf[64];
#if defined(ROTKPLUS_MOD)
    // as a mod of Athelas ROTKPlus is turned on and off in the launcher's Mods tab: without a ROTKPlus.ini it is on
    const int coopDefault = 1;
#else
    const int coopDefault = 0;   // the DivxMediaLib.dll install: off without a ROTKPlus.ini (README, Installation)
#endif
    g_cfg.coop = IniInt("Coop", "Enabled", coopDefault) != 0;
    IniString("Hero", "auto", buf, sizeof buf);
    for (char* p = buf; *p; ++p) *p = (char)tolower((unsigned char)*p);
    g_cfg.hero3 = -1;
    for (int i = 0; i < 9; ++i)
        if (!strcmp(buf, kHeroes[i])) g_cfg.hero3 = i;
    g_cfg.input3 = IniInt("Coop", "Input", 2);
    if (g_cfg.input3 < 0 || g_cfg.input3 > 3) g_cfg.input3 = 2;
    g_cfg.pads = IniInt("Coop", "Pad", 1) != 0;
    g_cfg.kbFallback = IniInt("Coop", "KeyboardFallback", 0) != 0;
    IniString("PadName", "", g_cfg.pad3Name, sizeof g_cfg.pad3Name);
    g_cfg.hud = IniInt("Coop", "Hud", 1) != 0;
    IniString("HudX", "0", buf, sizeof buf);     g_cfg.hudDx = (float)atof(buf);
    IniString("HudY", "-0.72", buf, sizeof buf); g_cfg.hudDy = (float)atof(buf);
    g_cfg.hudRow = IniInt("Coop", "HudRow", 0) != 0;
    IniString("HudScale", "0.66", buf, sizeof buf); g_cfg.hudScale = (float)atof(buf);
    if (g_cfg.hudScale < 0.3f || g_cfg.hudScale > 1.0f) g_cfg.hudScale = 0.66f;
    Log("ini: [Coop] Enabled=%d Hero=%d Input=%d Pad=%d PadName=%s Hud=%d HudX=%.2f HudY=%.2f", g_cfg.coop,
        g_cfg.hero3, g_cfg.input3, g_cfg.pads, g_cfg.pad3Name, g_cfg.hud, g_cfg.hudDx, g_cfg.hudDy);
}
