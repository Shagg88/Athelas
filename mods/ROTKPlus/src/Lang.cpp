// Lang.cpp - the mod's texts in the game's language
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// The text the mod shows to the player is short: the two main menu entries ("Co-op: N players", "Versus: N players",
// "Versus: 2 vs 2, heroes only"), "Player N" in the hero selection screen, the teams and the winner banner. They are
// chosen per language:
//  - [General] Language in ROTKPlus.ini: auto (default), en, it, fr, de, es, or any code that has a section in
//    ROTKPlus.lang.ini;
//  - with auto, the language is recognised from the text the game itself gives for the label "charSelPlayer1Sel"
//    ("Player 1", "Giocatore 1", "Joueur 1", "Spieler 1", "Jugador 1"), obtained with the game's text function
//    0x4E3F00(key, 0x47EF50(0)): there is no need to know where the game keeps its language index;
//  - ROTKPlus.lang.ini (optional, next to rotk.exe) has one section per language ([it], [en], ...) with the keys
//    Coop, Pvp, Players, Player, Winner, Draw, Teams, HeroesOnly, TeamWinner, Team, And, which override the built-in
//    ones. Texts use the placeholders {label} (name of the menu entry, or the text it is added to), {n} (a number) and
//    {hero} (hero name, or the name added in And).
// The built-in translations are written without accented letters (the menu font of the game may not have them).
// English and Italian are verified; French, German and Spanish are not (nobody has tried them in the game).
#include "ROTKPlus.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <string>

namespace {

// Coop, Pvp, Players, Player, Winner, Draw, Teams, HeroesOnly, TeamWinner, Team, And, TeamSun, TeamMoon, and the versus
// game's messages: KnockDown, Fell, Out, RoundWinner, RoundDraw, Round, CupWinner, OutShort, TimeUp, TeamCupWinner (a
// team's name takes an article in some languages), Elim, and the players' boxes: LifeOne, Lives, PointOne, Points. The
// two teams of a 2 vs 2 are the Sun's and the Moon's (in Tolkien's tales Arien guides the Sun and Tilion the Moon): a
// pair that fits any language; ROTKPlus.lang.ini can give them other names.
struct Table { const char* code; const char* texts[29]; };

const Table kTables[] = {
    { "en", { "Co-op", "Versus", "{label}: {n} players", "Player {n}", "Player {n} wins: {hero}", "Draw",
              "{label}: 2 vs 2", "{label}, heroes only", "{label} wins", "{label}: {hero}", "{label} and {hero}",
              "Team of the Sun", "Team of the Moon",
              "{label} downs {hero}", "{label} falls", "{label} is out", "Round {n}: {label} wins", "Round {n}: draw",
              "Round {n}", "{label} wins the Cup!", "out", "Time is up", "{label} wins the Cup!", "{label} knocks out {hero}",
              "1 life", "{n} lives", "1 point", "{n} points", "Sudden death: the next knock-down wins!" } },
    { "it", { "Cooperativo", "Sfida", "{label}: {n} giocatori", "Giocatore {n}", "Vince il giocatore {n}: {hero}", "Pareggio",
              "{label}: 2 contro 2", "{label}, solo eroi", "Vince la {label}", "{label}: {hero}", "{label} e {hero}",
              "Squadra del Sole", "Squadra della Luna",
              "{label} abbatte {hero}", "{label} cade", "{label} e' fuori", "Round {n}: vince {label}", "Round {n}: pareggio",
              "Round {n}", "{label} vince la Coppa!", "fuori", "Tempo scaduto", "La {label} vince la Coppa!", "{label} elimina {hero}",
              "1 vita", "{n} vite", "1 punto", "{n} punti", "Spareggio: il prossimo abbattimento vince!" } },
    { "fr", { "Cooperatif", "Defi", "{label} : {n} joueurs", "Joueur {n}", "Le joueur {n} gagne : {hero}", "Egalite",
              "{label} : 2 contre 2", "{label}, heros seuls", "L'{label} gagne",
              "{label} : {hero}", "{label} et {hero}",
              "Equipe du Soleil", "Equipe de la Lune",
              "{label} abat {hero}", "{label} tombe", "{label} est elimine", "Manche {n} : {label} gagne", "Manche {n} : egalite",
              "Manche {n}", "{label} gagne la Coupe !", "elimine", "Temps ecoule", "L'{label} gagne la Coupe !",
              "{label} elimine {hero}", "1 vie", "{n} vies", "1 point", "{n} points", "Mort subite : la prochaine mise a terre gagne !" } },
    { "de", { "Koop", "Duell", "{label}: {n} Spieler", "Spieler {n}", "Spieler {n} gewinnt: {hero}", "Unentschieden",
              "{label}: 2 gegen 2", "{label}, nur Helden", "{label} gewinnt", "{label}: {hero}", "{label} und {hero}",
              "Team der Sonne", "Team des Mondes",
              "{label} streckt {hero} nieder", "{label} faellt", "{label} ist raus", "Runde {n}: {label} gewinnt", "Runde {n}: unentschieden",
              "Runde {n}", "{label} gewinnt den Pokal!", "raus", "Zeit abgelaufen", "{label} gewinnt den Pokal!",
              "{label} schaltet {hero} aus", "1 Leben", "{n} Leben", "1 Punkt", "{n} Punkte", "Sudden Death: der naechste Niederschlag gewinnt!" } },
    { "es", { "Cooperativo", "Desafio", "{label}: {n} jugadores", "Jugador {n}", "Gana el jugador {n}: {hero}", "Empate",
              "{label}: 2 contra 2", "{label}, solo heroes", "Gana el {label}", "{label}: {hero}", "{label} y {hero}",
              "Equipo del Sol", "Equipo de la Luna",
              "{label} derriba a {hero}", "{label} cae", "{label} queda fuera", "Ronda {n}: gana {label}", "Ronda {n}: empate",
              "Ronda {n}", "{label} gana la Copa!", "fuera", "Tiempo agotado", "El {label} gana la Copa!",
              "{label} elimina a {hero}", "1 vida", "{n} vidas", "1 punto", "{n} puntos", "Muerte subita: el proximo derribo gana!" } },
};
const char* const kKeys[29] = { "Coop", "Pvp", "Players", "Player", "Winner", "Draw", "Teams", "HeroesOnly", "TeamWinner",
                                "Team", "And", "TeamSun", "TeamMoon", "KnockDown", "Fell", "Out", "RoundWinner", "RoundDraw",
                                "Round", "CupWinner", "OutShort", "TimeUp", "TeamCupWinner", "Elim", "LifeOne", "Lives",
                                "PointOne", "Points", "SuddenDeath" };

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

// id: 0 Coop, 1 Pvp, 2 Players, 3 Player, 4 Winner, 5 Draw, 6 Teams, 7 HeroesOnly, 8 TeamWinner, 9 Team, 10 And,
// 11 TeamSun, 12 TeamMoon, 13 KnockDown, 14 Fell, 15 Out, 16 RoundWinner, 17 RoundDraw, 18 Round, 19 CupWinner,
// 20 OutShort, 21 TimeUp, 22 TeamCupWinner, 23 Elim, 24 LifeOne, 25 Lives, 26 PointOne, 27 Points, 28 SuddenDeath
std::string LangText(int id, int n, const char* hero, const char* label)
{
    if (id < 0 || id > 28) return "";
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
