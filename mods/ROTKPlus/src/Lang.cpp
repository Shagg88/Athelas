// Lang.cpp - the mod's texts in the game's language
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// The text the mod shows to the player is short: the two main menu entries ("Co-op: N players", "Versus: N players"),
// "Player N" in the hero selection screen, and the winner banner. They are chosen per language:
//  - [General] Language in ROTKPlus.ini: auto (default), en, it, fr, de, es, or any code that has a section in
//    ROTKPlus.lang.ini;
//  - with auto, the language is recognised from the text the game itself gives for the label "charSelPlayer1Sel"
//    ("Player 1", "Giocatore 1", "Joueur 1", "Spieler 1", "Jugador 1"), obtained with the game's text function
//    0x4E3F00(key, 0x47EF50(0)): there is no need to know where the game keeps its language index;
//  - ROTKPlus.lang.ini (optional, next to rotk.exe) has one section per language ([it], [en], ...) with the keys
//    Coop, Pvp, Players, Player, Winner, Draw, which override the built-in ones. Texts use the placeholders
//    {label} (name of the menu entry), {n} (a number) and {hero} (hero name).
// The built-in translations are written without accented letters (the menu font of the game may not have them).
// English and Italian are verified; French, German and Spanish are not (nobody has tried them in the game).
#include "ROTKPlus.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <string>

namespace {

struct Table { const char* code; const char* texts[6]; };   // Coop, Pvp, Players, Player, Winner, Draw

const Table kTables[] = {
    { "en", { "Co-op", "Versus", "{label}: {n} players", "Player {n}", "Player {n} wins: {hero}", "Draw" } },
    { "it", { "Cooperativo", "Sfida", "{label}: {n} giocatori", "Giocatore {n}", "Vince il giocatore {n}: {hero}", "Pareggio" } },
    { "fr", { "Cooperatif", "Defi", "{label} : {n} joueurs", "Joueur {n}", "Le joueur {n} gagne : {hero}", "Egalite" } },
    { "de", { "Koop", "Duell", "{label}: {n} Spieler", "Spieler {n}", "Spieler {n} gewinnt: {hero}", "Unentschieden" } },
    { "es", { "Cooperativo", "Desafio", "{label}: {n} jugadores", "Jugador {n}", "Gana el jugador {n}: {hero}", "Empate" } },
};
const char* const kKeys[6] = { "Coop", "Pvp", "Players", "Player", "Winner", "Draw" };

std::string g_lang;       // chosen language code, empty = not decided yet
bool        g_forced;     // decided by the ini file
std::string g_gameLabel;  // "Player 1" as the game writes it

// the game's text for a key (0x4E3F00)
bool GameTextRaw(const char* key, char* out, size_t n)
{
    out[0] = 0;
    bool ok = false;
    Guard([&] {
        const int lang = ((int(__cdecl*)(int))0x47EF50)(0);
        const char* t = ((const char*(__cdecl*)(const char*, int))0x4E3F00)(key, lang);
        if (t && !IsBadStringPtrA(t, 64)) { strncpy_s(out, n, t, _TRUNCATE); ok = true; }
    });
    return ok;
}

std::string GameText(const char* key)
{
    char buf[96];
    return GameTextRaw(key, buf, sizeof buf) ? std::string(buf) : std::string();
}

void Detect()
{
    if (!g_lang.empty()) return;
    char ini[MAX_PATH], buf[32];
    sprintf_s(ini, "%s\\ROTKPlus.ini", GameDir());
    GetPrivateProfileStringA("General", "Language", "auto", buf, sizeof buf, ini);
    for (char* p = buf; *p; ++p) *p = (char)tolower((unsigned char)*p);
    if (strcmp(buf, "auto")) { g_lang = buf; g_forced = true; Log("language: %s (from ROTKPlus.ini)", buf); return; }
    const std::string t = GameText("charSelPlayer1Sel");
    if (t.empty() || t == "charSelPlayer1Sel") return;   // the game's texts are not ready yet: try again later
    g_gameLabel = t;
    const char* code = "en";
    if (strstr(t.c_str(), "iocator")) code = "it";
    else if (strstr(t.c_str(), "oueur")) code = "fr";
    else if (strstr(t.c_str(), "pieler")) code = "de";
    else if (strstr(t.c_str(), "ugador")) code = "es";
    g_lang = code;
    Log("language: %s (recognised from the game's text \"%s\")", code, t.c_str());
}

const char* Builtin(const std::string& lang, int id)
{
    for (const Table& t : kTables) if (lang == t.code) return t.texts[id];
    return kTables[0].texts[id];   // English
}

std::string Lookup(int id)
{
    Detect();
    const std::string lang = g_lang.empty() ? "en" : g_lang;
    char ini[MAX_PATH], buf[160];
    sprintf_s(ini, "%s\\ROTKPlus.lang.ini", GameDir());
    GetPrivateProfileStringA(lang.c_str(), kKeys[id], "", buf, sizeof buf, ini);
    if (buf[0]) return buf;
    return Builtin(lang, id);
}

void Replace(std::string& s, const char* token, const std::string& value)
{
    const size_t len = strlen(token);
    for (size_t at = s.find(token); at != std::string::npos; at = s.find(token, at + value.size())) s.replace(at, len, value);
}

}  // namespace

std::string LangCode()
{
    Detect();
    return g_lang.empty() ? "en" : g_lang;
}

std::string LangGameText(const char* key) { return GameText(key); }

// id: 0 Coop, 1 Pvp, 2 Players, 3 Player, 4 Winner, 5 Draw
std::string LangText(int id, int n, const char* hero, const char* label)
{
    Detect();
    if (id == 3 && !g_gameLabel.empty() && !g_forced) {   // "Player N" exactly as the game writes it
        std::string g = g_gameLabel;
        const size_t at = g.rfind('1');
        if (at != std::string::npos) { g[at] = (char)('0' + n); return g; }
    }
    std::string s = Lookup(id);
    Replace(s, "{label}", label ? label : "");
    Replace(s, "{n}", std::to_string(n));
    Replace(s, "{hero}", hero ? hero : "");
    return s;
}
