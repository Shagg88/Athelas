/* Valar Boons - Dynamic Configuration Loader
 *
 * Parses ValarBoons.ini, DirectInput DIK key names, cooldown progression curves,
 * and RGB aura color definitions.
 */

#include "valar_boons.h"

static int parse_single_dik(const char *key)
{
    while (*key == ' ' || *key == '\t') key++;
    char clean[32] = {0};
    int len = 0;
    while (*key && *key != ' ' && *key != '\t' && *key != ',' && len < 30) {
        clean[len++] = (char)toupper((unsigned char)*key++);
    }
    clean[len] = '\0';
    if (!len) return 0;

    /* Numbers 0-9 */
    if (len == 1 && clean[0] >= '0' && clean[0] <= '9') {
        return clean[0] == '0' ? 0x0B : (0x02 + (clean[0] - '1'));
    }
    /* Function keys F1-F12 */
    if (clean[0] == 'F' && len >= 2 && isdigit((unsigned char)clean[1])) {
        int f = atoi(&clean[1]);
        if (f >= 1 && f <= 10) return 0x3A + f; /* F1=0x3B, F10=0x44 */
        if (f == 11) return 0x57;
        if (f == 12) return 0x58;
    }
    /* Numpad 0-9 */
    if (!strncmp(clean, "NUMPAD", 6) || !strncmp(clean, "NUM", 3)) {
        const char *digit = clean + (!strncmp(clean, "NUMPAD", 6) ? 6 : 3);
        if (*digit >= '0' && *digit <= '9') {
            int d = *digit - '0';
            static const int numpadDiks[10] = { 0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49 };
            return numpadDiks[d];
        }
    }
    /* Letters A-Z */
    if (len == 1 && clean[0] >= 'A' && clean[0] <= 'Z') {
        static const int letterDiks[26] = {
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
            0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C
        };
        return letterDiks[clean[0] - 'A'];
    }
    if (!strcmp(clean, "SPACE")) return 0x39;
    if (!strcmp(clean, "TAB")) return 0x0F;
    if (!strcmp(clean, "ENTER") || !strcmp(clean, "RETURN")) return 0x1C;
    if (!strcmp(clean, "ESCAPE") || !strcmp(clean, "ESC")) return 0x01;

    return 0;
}

static int parse_key_list(const char *str, int *outArray, int maxKeys)
{
    int count = 0;
    const char *p = str;
    while (*p && count < maxKeys) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        int dik = parse_single_dik(p);
        if (dik > 0) outArray[count++] = dik;
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return count;
}

static int parse_cooldown_curve(const char *str, float *outCurve, int maxItems)
{
    int count = 0;
    const char *p = str;
    while (*p && count < maxItems) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        float v = (float)atof(p);
        if (v < 0.0f) v = 0.0f;
        outCurve[count++] = v;
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return count;
}

static void parse_color_rgb(const char *str, BYTE *r, BYTE *g, BYTE *b)
{
    *r = 255; *g = 215; *b = 0;
    int cr = 255, cg = 215, cb = 0;
    if (sscanf(str, "%d , %d , %d", &cr, &cg, &cb) >= 3 ||
        sscanf(str, "%d %d %d", &cr, &cg, &cb) >= 3) {
        if (cr < 0) cr = 0; if (cr > 255) cr = 255;
        if (cg < 0) cg = 0; if (cg > 255) cg = 255;
        if (cb < 0) cb = 0; if (cb > 255) cb = 255;
        *r = (BYTE)cr; *g = (BYTE)cg; *b = (BYTE)cb;
    }
}

void load_boon_configs(const char *ini)
{
    char buf[256];

    g_boonCount = GetPrivateProfileIntA("General", "BoonCount", 3, ini);
    if (g_boonCount < 1) g_boonCount = 1;
    if (g_boonCount > MAX_BOONS) g_boonCount = MAX_BOONS;

    GetPrivateProfileStringA("General", "EnableAllKey", "4, F4, Num4", buf, sizeof buf, ini);
    g_enableAllDikCount = parse_key_list(buf, g_enableAllDiks, MAX_KEYS_PER_BOON);
    for (int k = 0; k < g_enableAllDikCount; k++) {
        if (g_RegisterHotkey) g_RegisterHotkey(g_enableAllDiks[k]);
    }

    GetPrivateProfileStringA("General", "ResetKey", "0, F10, Num0", buf, sizeof buf, ini);
    g_resetDikCount = parse_key_list(buf, g_resetDiks, MAX_KEYS_PER_BOON);
    for (int k = 0; k < g_resetDikCount; k++) {
        if (g_RegisterHotkey) g_RegisterHotkey(g_resetDiks[k]);
    }

    g_showHud  = GetPrivateProfileIntA("General", "ShowHud", 1, ini);
    g_showAura = GetPrivateProfileIntA("General", "ShowAura", 1, ini);


    for (int i = 0; i < g_boonCount; i++) {
        char sec[32];
        snprintf(sec, sizeof sec, "Boon%d", i + 1);

        BoonConfig *b = &g_boons[i];
        memset(b, 0, sizeof(BoonConfig));

        char defName[32], defTitle[32], defKey[32];
        snprintf(defName, sizeof defName, "Boon %d", i + 1);
        snprintf(defTitle, sizeof defTitle, "Power %d", i + 1);
        snprintf(defKey, sizeof defKey, "%d", (i + 1) % 10);

        GetPrivateProfileStringA(sec, "Name", defName, b->name, sizeof b->name, ini);
        GetPrivateProfileStringA(sec, "Title", defTitle, b->title, sizeof b->title, ini);
        GetPrivateProfileStringA(sec, "Description", "Blessing of the Valar", b->desc, sizeof b->desc, ini);

        GetPrivateProfileStringA(sec, "Key", defKey, buf, sizeof buf, ini);
        snprintf(b->keyStr, sizeof b->keyStr, "%s", buf);
        b->dikCount = parse_key_list(buf, b->diks, MAX_KEYS_PER_BOON);
        for (int k = 0; k < b->dikCount; k++) {
            if (g_RegisterHotkey) g_RegisterHotkey(b->diks[k]);
        }

        b->group = GetPrivateProfileIntA(sec, "Group", 0, ini);
        if (b->group < 0) b->group = 0;
        if (b->group >= MAX_GROUPS) b->group = MAX_GROUPS - 1;

        GetPrivateProfileStringA(sec, "Color", "0, 230, 255", buf, sizeof buf, ini);
        parse_color_rgb(buf, &b->colorR, &b->colorG, &b->colorB);

        GetPrivateProfileStringA(sec, "Duration", "15.0", buf, sizeof buf, ini);
        b->duration = (float)atof(buf);
        if (b->duration < 0.0f) b->duration = 0.0f;

        GetPrivateProfileStringA(sec, "Cooldown", "10.0", buf, sizeof buf, ini);
        b->cooldownCurveCount = parse_cooldown_curve(buf, b->cooldownCurve, MAX_CURVE);
        if (b->cooldownCurveCount == 0) {
            b->cooldownCurve[0] = 10.0f;
            b->cooldownCurveCount = 1;
        }

        b->damageMultiplier = GetPrivateProfileIntA(sec, "DamageMultiplier", 100, ini);
        b->flatDamage       = GetPrivateProfileIntA(sec, "FlatDamage", 0, ini);
        b->damageReduction  = GetPrivateProfileIntA(sec, "DamageReduction", 0, ini);
        b->maxDamageClamped = GetPrivateProfileIntA(sec, "MaxDamageClamped", 0, ini);
        b->holyShockDamage  = GetPrivateProfileIntA(sec, "HolyShockDamage", 0, ini);

        /* Default first boon active at start for initial test convenience */
        b->active = (i == 0) ? 1 : 0;
        b->activeUntil = 0;
        b->cooldownUntil = 0;
        b->useCount = 0;

        valar_log("mods [ValarBoons]: Loaded Boon %d [%s] Title='%s' Group=%d Color=(%d,%d,%d) Duration=%.1fs CD_Curve=(count=%d, max=%.1fs)",
                  i + 1, b->name, b->title, b->group, b->colorR, b->colorG, b->colorB,
                  b->duration, b->cooldownCurveCount, b->cooldownCurve[b->cooldownCurveCount - 1]);
    }
}
