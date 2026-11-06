// Players.cpp - names of the players and their statistics
// Language: C++17, MSVC or mingw-w64 g++, Win32.
//
// Who is playing. Confirming Single, Co-op or Versus first asks who takes each place ("Who is playing?", below): the
// statistics are per name, and a person is not always the same player number.
//
// Names. The main menu gets a "Fellowship" entry (a hidden entry of the game made visible, Online or PlayTrailer, see
// Screens.cpp). Confirming it opens a page drawn by the mod over the menu's background: the menu's entries are hidden
// and our own texts are drawn with copies of a menu label (type 0x82, so the menu's font), after the screen's root is
// drawn (0x4F6690 draws *0x8E2F68 and then *0x8E2F74 through 0x4FD590, hooked in Hud.cpp). Pages: the four players
// ("Player 1: name"), Statistics (four players at a time, then each one's heroes), Records, Last game, Manage names
// (rename, merge two names, delete). Left/right on a player's row picks one of the names already used; confirm opens the
// editor: a keyboard types directly (the active Windows keyboard layout, so accented letters come out right), a controller
// uses the letters on the screen. The menus' actions (0 up, 2 down, 4 right, 5 left, 7 confirm, 8 back, from the loop
// 0x4EAA60 through the handler 0x4EA0F0, hooked in Screens.cpp) are taken by the page while it is open. Names are kept in
// ROTKPlus_players.ini next to the game ([Names] P1..P4 and Known); an empty name means "Player N" in the game's language.
// The names are also shown above the players' heads and next to their HUD boxes (Hud.cpp), in the hero selection
// (Screens.cpp) and in the Versus winner banner (Pvp.cpp). The third and fourth player's controllers (opened by ROTKPlus,
// Pads.cpp) are recognised by SDL's serial number or device path: a controller that had a name gets it back ([PadNames]).
//
// Statistics, per name, added up over the sessions ([Stats.<name>] in ROTKPlus_players.ini, key=value lines), written only
// when a level is left (a crash in the middle of a level loses that level, never corrupts the totals):
//  - levels played and won (a "Retry" is still the same level), time in the levels, favourite hero (the one played in
//    most levels and versus games), and per hero: levels, versus games, wins, kills, assists, deaths (Hero<n>,
//    Hero<n>Pvp, Hero<n>Wins, Hero<n>Kills...). Players without a name are guests: no statistics;
//  - kills, assists, deaths. Every hit goes through 0x40CF80(victim, damage, attacker) (ROTK Fix hooks its entry); the
//    calls of the enemies' hit handlers (0x4031BC in 0x403040, 0x404D12 and 0x404D5B in 0x404CB0) and of the players'
//    (0x43A1A9, attacker from [edi] of the 0x259 event) are hooked here. The attacker is a player, or one of its objects
//    (0x4019E0, as 0x403040 itself does to credit the kill). Arrows and spears name their thrower as the attacker, so a
//    kill at range is told by the hit's kind (see RangedKind); a killing move too. A kill is the hit that takes the victim's
//    life (+0x90) from above zero to zero or below; an assist goes to every other player who hit the same enemy in the
//    15 seconds before the kill. Deaths: the player's "die" state (Pvp.cpp, DieEnter);
//  - at a victory, the experience of the level and the kills by rating (Perfect, Excellent, Good, Fair) from the hero's
//    record (0x5BE05C + hero * 0x3C, dwords 8 and 11-14, see Support.cpp);
//  - Versus: games, wins, players knocked down by this player, own falls;
//  - records of a single level (Best*: kills, Perfect kills, assists; FastestPvpWin in seconds).
// At the end of a level a short summary is drawn at the top of the score screen (mode 2) and kept as "Last game".
#include "ROTKPlus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>

extern uint32_t g_players[8];                 // Coop.cpp
bool     PvpActive();                         // Pvp.cpp
uint8_t* MenuTextTemplate(bool highlighted);  // Screens.cpp: a label of the main menu to copy
std::string PadKeyOfSlot(int slot);           // Pads.cpp: a stable key of the controller of slot 2 or 3, "" if none

static bool  g_enabled;
static bool  g_headNames = true, g_hudNames = false;
static bool  g_sumOn;            // [Players] ScoreSummary: the level summary on the score screen
static bool  g_askWho = true;     // [Players] AskWhoPlays: "Who is playing?" before every game
static bool  g_multiPage = true;  // [Players] MultiplayerPage: one "Multiplayer" entry and its page (needs AskWhoPlays)
static float g_nameScale = 0.8f, g_sumScale = 0.8f, g_sumY = 0.02f;

static const char* const kHeroNames[9] = { "Aragorn", "Legolas", "Gimli", "Gandalf", "Sam", "Frodo", "Pippin", "Merry", "Faramir" };

static int ScreenMode() { return *(const int*)0x562AC0; }

// --- texts ----------------------------------------------------------------------------------------------------
// English, Italian, French, German and Spanish are built in (the languages of LangCode); any key can be overridden in
// ROTKPlus.lang.ini ([it], [en]...) with the names below. No accented letters: the menu font may not have them. The
// French, German and Spanish texts and the ratings' names (as the game calls them) are to be checked by a native
// speaker.
enum Txt {
    T_MENU, T_TITLE, T_STATS, T_BACK, T_EDIT, T_EDIT_HELP, T_SPACE, T_DELETE, T_DONE, T_STATS_TITLE, T_STATS_HELP, T_NONE,
    S_LEVELS, S_WINS, S_TIME, S_KILLS, S_RANGED, S_FINISH, S_SOLO, S_MULTI, S_ASSISTS, S_DEATHS, S_PERFECT, S_EXCELLENT, S_GOOD, S_FAIR, S_EXP,
    S_HERO, S_PVP_GAMES, S_PVP_WINS, S_PVP_KILLS, S_PVP_FALLS, S_PVP_ELIMS, S_PVP_ROUNDS, S_PVP_ROUND_WINS,
    S_PVP_TOURNEYS, S_PVP_CUPS, S_PVP_TEAM, S_PVP_TIMED, S_PVP_BEST_TIMED, F_ORC, F_URUK, F_URUKHAI, F_BERSERKER, F_DEAD, F_EAST, F_SPIDER,
    F_TROLL, F_MUMAK, F_GOLLUM, F_SHELOB, F_GORBAG, F_KINGDEAD, F_OTHER, C_HEWER, C_JUDGMENT, C_BALROG, C_DARK, C_CLEAVER, C_LIGHTNING, C_HAMMER, C_SWIFT, C_BANE,
    C_RISING, S_SPEAR, S_PARRY_HITS, S_PARRY_ARROWS, S_LADDERS, P_HEAL30, P_HEAL60, P_ARROWS, P_EXP,
    S_LEVELS_WON, F_NAZGUL, F_MOUTH, V_SOLDIERS, V_SHIELD, V_ARCHERS, V_CAPTAINS, S_BURNED,
    TB_GAMES, TB_LEVELS, TB_FIGHT, TB_FOES, TB_BOSSES, TB_PVP, T_CARD, T_CARD_HELP, T_CARD_HEROES, T_CARD_FOES,
    T_CARD_BOSSES, T_CARDS, T_SEC_WHO, T_SEC_STATS, T_SEC_NAMES, T_MAIN_HELP, T_PANEL_OPEN, T_PANEL_GUEST,
    T_PANEL_TYPE, T_PANEL_MANAGE, T_CARD_BURNED, T_REC_LEVEL, T_REC_TOTAL, T_REC_FAST, T_REC_MOST, T_LAST_COLS,
    T_LAST_PVP_COLS, T_LAST_TOTAL, T_BACK_HELP, T_CONFIRM_HELP, T_ADD_NAME, T_LEVEL_PEL, T_LEVEL_PAL1, T_LEVEL_PAL2,
    T_WINNER, T_RECORDS, T_LAST, T_MANAGE, T_RENAME, T_MERGE, T_REMOVE,
    T_ASK_REMOVE, T_ASK_MERGE, T_YES, T_NO, T_PICK, T_RENAME_TITLE, T_DETAIL, T_DETAIL_COLS, T_DETAIL_HELP, T_LAST_NONE,
    T_LAST_COOP, T_LAST_LOST, T_LAST_PVP, T_SUM_LINE, T_SUM_PVP_LINE, T_SUM_BEST, T_SUM_TITLE, R_KILLS, R_PERFECT,
    R_ASSISTS, R_PVP_FAST, R_TOT_KILLS, R_TOT_WINS, R_PVP_WINS, T_LIST_HELP, T_NO_NAMES, T_WHO_TITLE, T_WHO_START,
    T_WHO_GUEST, T_WHO_NEW, T_WHO_HELP, T_WHO_EDIT, T_MP_TITLE, T_MP_GAME, T_MP_MODE, T_MP_CAMPAIGN, T_MP_FFA, T_MP_TEAMS,
    T_MP_PLAYERS, T_MP_ENEMIES, T_MP_ENEMIES_ON, T_MP_HEROES, T_MP_LIVES, T_MP_SPLIT, T_MP_SPLIT_VAL, T_MP_SUN, T_MP_MOON,
    T_MP_TEAMS_HEAD, T_MP_NOTE_MAP, T_MP_NOTE_ARENA, T_MP_HELP, T_MP_RULE, T_MP_RULE_LAST, T_MP_RULE_TIME, T_MP_TIME,
    T_MP_MINUTES, T_MP_ROUNDS, T_MP_SINGLE, T_MP_BEST, S_KICKS, S_SPEED_HITS, S_FIERCE_HITS, S_SHIELDS, T_CARD_DOWNS,
    T_SUM_BEST_PVP, T_LAST_ENEMIES, S_VS_DOWNS, T_TO_MAP, T_CHOOSE, T_THOUSANDS, T_HERO0, T_COUNT = T_HERO0 + 9
};
struct TxtDef { const char* key; const char* en; const char* it; const char* fr; const char* de; const char* es; };
static const TxtDef kTxt[] = {
    { "PlayersMenu", "Fellowship", "Compagnia", "Communaute", "Gefaehrten", "Comunidad" },
    { "PlayersTitle", "Fellowship", "Compagnia", "Communaute", "Gefaehrten", "Comunidad" },
    { "PlayersStats", "Comparison", "Confronto", "Comparaison", "Vergleich", "Comparacion" },
    { "PlayersBack", "Back", "Indietro", "Retour", "Zurueck", "Volver" },
    { "PlayersEdit", "Name of {player}", "Nome di {player}", "Nom de {player}", "Name von {player}", "Nombre de {player}" },
    { "PlayersEditHelp", "Back or Esc: cancel. Keyboard: type, Enter to confirm", "Indietro o Esc: annulla. Tastiera: scrivi, Invio conferma",
      "Retour ou Echap : annuler. Clavier : tapez, Entree valide", "Zurueck oder Esc: abbrechen. Tastatur: tippen, Eingabe bestaetigt",
      "Volver o Esc: cancela. Teclado: escribe, Intro confirma" },
    { "PlayersSpace", "Space", "Spazio", "Espace", "Leerzeichen", "Espacio" },
    { "PlayersDelete", "Delete", "Cancella", "Effacer", "Loeschen", "Borrar" },
    { "PlayersDone", "Done", "Fatto", "Termine", "Fertig", "Hecho" },
    { "StatsTitle", "Player statistics", "Statistiche dei giocatori", "Statistiques des joueurs", "Spielerstatistik",
      "Estadisticas de los jugadores" },
    { "StatsHelp", "Left/right: tab   L1/R1: player   Up/down: scroll   Confirm: profile   Back: return",
      "Sinistra/destra: scheda   L1/R1: giocatore   Su/giu: scorri   Conferma: profilo   Indietro: esci",
      "Gauche/droite : onglet   L1/R1 : joueur   Haut/bas : defiler   Valider : fiche   Retour : quitter",
      "Links/rechts: Seite   L1/R1: Spieler   Hoch/runter: blaettern   Bestaetigen: Profil   Zurueck: verlassen",
      "Izquierda/derecha: pestana   L1/R1: jugador   Arriba/abajo: desplazar   Confirmar: ficha   Volver: salir" },
    { "StatsNone", "No statistics yet", "Ancora nessuna statistica", "Pas encore de statistiques", "Noch keine Statistiken",
      "Aun no hay estadisticas" },
    { "StatLevels", "Levels played", "Livelli giocati", "Niveaux joues", "Gespielte Level", "Niveles jugados" },
    { "StatWins", "Levels won", "Livelli vinti", "Niveaux gagnes", "Gewonnene Level", "Niveles ganados" },
    { "StatTime", "Time played", "Tempo di gioco", "Temps de jeu", "Spielzeit", "Tiempo de juego" },
    { "StatKills", "Kills", "Uccisioni", "Ennemis tues", "Besiegte Gegner", "Bajas" },
    { "StatRanged", "of which ranged", "di cui a distanza", "dont a distance", "davon aus der Ferne", "de ellas a distancia" },
    { "StatKillingMoves", "of which killing moves", "di cui colpi di grazia", "dont coups de grace", "davon Gnadenstoesse",
      "de ellas golpes de gracia" },
    { "StatSolo", "of which playing alone", "di cui da solo", "dont en solo", "davon allein", "de ellas en solitario" },
    { "StatMulti", "Record: enemies in one blow", "Record: nemici in un colpo", "Record : ennemis d'un coup",
      "Rekord: Gegner mit einem Schlag", "Record: enemigos de un golpe" },
    { "StatAssists", "Assists", "Assist", "Assistances", "Assists", "Asistencias" },
    { "StatDeaths", "Deaths", "Morti", "Morts", "Tode", "Muertes" },
    { "StatPerfect", "Perfect", "Superbo", "Parfait", "Perfekt", "Perfecto" },
    { "StatExcellent", "Excellent", "Ottimo", "Excellent", "Exzellent", "Excelente" },
    { "StatGood", "Good", "Buono", "Bien", "Gut", "Bueno" },
    { "StatFair", "Fair", "Medio", "Correct", "Ordentlich", "Regular" },
    { "StatExp", "Experience", "Esperienza", "Experience", "Erfahrung", "Experiencia" },
    { "StatHero", "Favourite hero", "Eroe preferito", "Heros prefere", "Lieblingsheld", "Heroe favorito" },
    { "StatPvpGames", "Versus games", "Sfide giocate", "Defis joues", "Duelle gespielt", "Desafios jugados" },
    { "StatPvpWins", "Versus wins", "Sfide vinte", "Defis gagnes", "Duelle gewonnen", "Desafios ganados" },
    { "StatPvpKills", "Versus: opponents down", "Sfida: avversari abbattuti", "Defi : adversaires a terre",
      "Duell: Gegner besiegt", "Desafio: rivales derribados" },
    { "StatPvpFalls", "Versus: own falls", "Sfida: cadute", "Defi : chutes", "Duell: eigene Niederlagen", "Desafio: caidas" },
    { "StatPvpElims", "Versus: opponents eliminated", "Sfida: avversari eliminati", "Defi : adversaires elimines",
      "Duell: Gegner ausgeschaltet", "Desafio: rivales eliminados" },
    { "StatPvpRounds", "Rounds played", "Round giocati", "Manches jouees", "Runden gespielt", "Rondas jugadas" },
    { "StatPvpRoundWins", "Rounds won", "Round vinti", "Manches gagnees", "Runden gewonnen", "Rondas ganadas" },
    { "StatPvpTourneys", "Tournaments played", "Tornei giocati", "Tournois joues", "Turniere gespielt", "Torneos jugados" },
    { "StatPvpCups", "Cups won", "Coppe vinte", "Coupes gagnees", "Pokale gewonnen", "Copas ganadas" },
    { "StatPvpTeam", "Team games won", "Sfide a squadre vinte", "Defis par equipes gagnes", "Teamduelle gewonnen",
      "Desafios por equipos ganados" },
    { "StatPvpTimed", "Timed games won", "Sfide a tempo vinte", "Defis au temps gagnes", "Duelle auf Zeit gewonnen",
      "Desafios a tiempo ganados" },
    { "StatPvpBestTimed", "Most knock-downs in a timed game", "Record di abbattuti a tempo", "Record de mises a terre au temps",
      "Rekord Niederschlaege auf Zeit", "Record de derribos a tiempo" },
    { "FoeOrcs", "Orcs", "Orchetti", "Orques", "Orks", "Orcos" },
    { "FoeUruks", "Uruks", "Uruk", "Uruks", "Uruks", "Uruks" },
    { "FoeUrukHai", "Uruk-hai", "Uruk-hai", "Uruk-hai", "Uruk-hai", "Uruk-hai" },
    { "FoeBerserkers", "Berserkers", "Berserker", "Berserkers", "Berserker", "Berserkers" },
    { "FoeDead", "The Dead", "Morti", "Morts", "Tote", "Muertos" },
    { "FoeEasterlings", "Easterlings", "Esterling", "Orientaux", "Ostlinge", "Orientales" },
    { "FoeSpiders", "Spiders", "Ragni", "Araignees", "Spinnen", "Aranas" },
    { "FoeTrolls", "Trolls", "Troll", "Trolls", "Trolle", "Trolls" },
    { "FoeMumakil", "Mumakil", "Olifanti", "Oliphants", "Olifanten", "Olifantes" },
    { "FoeGollum", "Boss: Gollum", "Boss: Gollum", "Boss : Gollum", "Boss: Gollum", "Jefe: Gollum" },
    // the bosses of Sam's levels (campaign of 02/11/2026): type 0x49, 320 of life, the end of Shelob's lair; type 0x3D,
    // 500 of life, killed at the top of the tower of Cirith Ungol (Gorbag, to be confirmed on screen)
    { "FoeShelob", "Boss: Shelob", "Boss: Shelob", "Boss : Arachne", "Boss: Kankra", "Jefe: Ella-Larana" },
    { "FoeGorbag", "Boss: Gorbag", "Boss: Gorbag", "Boss : Gorbag", "Boss: Gorbag", "Jefe: Gorbag" },
    // type 0x4D, killed at the end of the Paths of the Dead (02/11/2026); the icon is the portrait 200 (FoeKingDead.raw)
    { "FoeKingDead", "Boss: King of the Dead", "Boss: Re dei Morti", "Boss : Roi des Morts", "Boss: Koenig der Toten",
      "Jefe: Rey de los Muertos" },
    { "FoeOther", "Other enemies", "Altri nemici", "Autres ennemis", "Andere Gegner", "Otros enemigos" },
    // the combos' names: Italian as the game's own combo list (screenshot of 01/11/2026), English as the game's; French,
    // German and Spanish to be checked against those versions of the game
    { "ComboHewer", "Orc Hewer", "Squarciaorchetti", "Orc Hewer", "Orc Hewer", "Orc Hewer" },
    { "ComboJudgment", "Final Judgment", "Giudizio finale", "Final Judgment", "Final Judgment", "Final Judgment" },
    { "ComboBalrog", "Balrog's Gambit", "Impeto del Balrog", "Balrog's Gambit", "Balrog's Gambit", "Balrog's Gambit" },
    { "ComboDark", "Dark Deliverance", "Oscuro presagio", "Dark Deliverance", "Dark Deliverance", "Dark Deliverance" },
    { "ComboCleaver", "Shield Cleaver", "Frantumascudi", "Shield Cleaver", "Shield Cleaver", "Shield Cleaver" },
    { "ComboLightning", "Lightning Strike", "Colpo fulminante", "Lightning Strike", "Lightning Strike", "Lightning Strike" },
    { "ComboHammer", "Helm's Hammer", "Martello di Helm", "Helm's Hammer", "Helm's Hammer", "Helm's Hammer" },
    { "ComboSwift", "Swift Justice", "Giustizia fulminea", "Swift Justice", "Swift Justice", "Swift Justice" },
    { "ComboBane", "Banes (parry and kill)", "Flagelli (parata e uccisione)", "Banes (parade et coup)",
      "Banes (Parade und Todesstoss)", "Banes (parada y remate)" },
    // added on 02/11/2026 (campaign on SteamOS): the attack while getting up, kills with thrown spears, hits
    // parried, ladders, things picked up. The pick-ups are named by what they give (0x43A080, event
    // 0xD5): which of them are the "pools" and the "orbs" on screen is still to be matched with a play-through.
    { "ComboRising", "Rising Attack", "Vendetta nascente", "Attaque en se relevant", "Aufsteh-Angriff",
      "Ataque al levantarse" },
    { "StatSpear", "of which with spears", "di cui con le lance", "dont a la lance", "davon mit Speeren",
      "de ellas con lanzas" },
    { "StatParryHits", "Blows parried", "Colpi parati", "Coups pares", "Schlaege pariert", "Golpes parados" },
    { "StatParryArrows", "Arrows parried", "Frecce parate", "Fleches parees", "Pfeile pariert", "Flechas paradas" },
    { "StatLadders", "Ladders pushed down", "Scale buttate giu", "Echelles renversees", "Leitern umgestossen",
      "Escaleras derribadas" },
    { "StatPickHeal30", "Health picked up (+30)", "Salute raccolta (+30)", "Sante ramassee (+30)",
      "Gesundheit aufgehoben (+30)", "Salud recogida (+30)" },
    { "StatPickHeal60", "Health picked up (+60)", "Salute raccolta (+60)", "Sante ramassee (+60)",
      "Gesundheit aufgehoben (+60)", "Salud recogida (+60)" },
    // 4 (5, 6, 8: never seen) adds 20 to +0xD0 -> +0x68, capped at +0x6A: the number by the sword in the HUD, the shots
    // of the ranged attack (screenshots of 02/11/2026 on the Pelennor: 26, a quiver picked up, 30)
    { "StatPickArrows", "Arrows picked up (+20)", "Frecce raccolte (+20)", "Fleches ramassees (+20)",
      "Pfeile aufgehoben (+20)", "Flechas recogidas (+20)" },
    { "StatPickExp", "Experience orbs (+1000)", "Sfere di esperienza (+1000)", "Spheres d'experience (+1000)",
      "Erfahrungskugeln (+1000)", "Esferas de experiencia (+1000)" },
    // the heading of the levels won, one row per level with the game's own name (02/11/2026)
    { "StatLevelsWon", "Levels completed:", "Livelli completati:", "Niveaux termines :", "Abgeschlossene Level:",
      "Niveles completados:" },
    // the end of the campaign (02/11/2026 evening): the Black Gate's Nazgul on foot (type 0x52, 150 of life, three) and its
    // boss, the Mouth of Sauron (0x53, 1200; the icon is his portrait in the HUD, 25c0a0)
    { "FoeNazgul", "Nazgul", "Nazgul", "Nazgul", "Nazgul", "Nazgul" },
    { "FoeMouth", "Boss: Mouth of Sauron", "Boss: Bocca di Sauron", "Boss : Bouche de Sauron", "Boss: Mund Saurons",
      "Jefe: Boca de Sauron" },
    // the kinds of a people, under its row (02/11/2026: "Easterlings without and with a shield, their bosses...")
    { "FoeSoldiers", "soldiers", "soldati", "soldats", "Soldaten", "soldados" },
    { "FoeShield", "with a shield", "con lo scudo", "avec bouclier", "mit Schild", "con escudo" },
    { "FoeArchers", "archers", "arcieri", "archers", "Bogenschuetzen", "arqueros" },
    { "FoeCaptains", "captains", "capitani", "capitaines", "Hauptleute", "capitanes" },
    { "StatBurned", "Enemies set on fire", "Nemici incendiati", "Ennemis enflammes", "Gegner in Brand gesteckt",
      "Enemigos incendiados" },
    // the tabs of the statistics (02/11/2026)
    { "TabGames", "Games", "Partite", "Parties", "Spiele", "Partidas" },
    { "TabLevels", "Levels", "Livelli", "Niveaux", "Level", "Niveles" },
    { "TabFight", "Fights", "Lotta", "Combats", "Kaempfe", "Combates" },
    { "TabFoes", "Enemies", "Nemici", "Ennemis", "Gegner", "Enemigos" },
    { "TabBosses", "Bosses and combos", "Boss e combo", "Boss et combos", "Bosse und Combos", "Jefes y combos" },
    { "TabPvp", "Versus", "Sfide", "Defis", "Duelle", "Desafios" },
    // one player's card, between the statistics and the heroes (02/11/2026)
    { "PlayersCard", "Profile of {name}", "Scheda di {name}", "Fiche de {name}", "Profil von {name}", "Ficha de {name}" },
    { "PlayersCardHelp", "Left/right: another player   Confirm: heroes   Back: return",
      "Sinistra/destra: altro giocatore   Conferma: eroi   Indietro: esci",
      "Gauche/droite : autre joueur   Valider : heros   Retour : quitter",
      "Links/rechts: anderer Spieler   Bestaetigen: Helden   Zurueck: verlassen",
      "Izquierda/derecha: otro jugador   Confirmar: heroes   Volver: salir" },
    { "PlayersCardHeroes", "Heroes (levels)", "Eroi (livelli)", "Heros (niveaux)", "Helden (Level)", "Heroes (niveles)" },
    { "PlayersCardFoes", "Enemies", "Nemici", "Ennemis", "Gegner", "Enemigos" },
    { "PlayersCardBosses", "Bosses", "Boss", "Boss", "Bosse", "Jefes" },
    // the Fellowship page in groups, with a preview on the right (02/11/2026)
    { "PlayersCards", "Player profiles", "Schede dei giocatori", "Fiches des joueurs", "Spielerprofile",
      "Fichas de los jugadores" },
    { "PlayersWhoHead", "Who plays", "Chi gioca", "Qui joue", "Wer spielt", "Quien juega" },
    { "PlayersStatsHead", "Statistics", "Statistiche", "Statistiques", "Statistiken", "Estadisticas" },
    { "PlayersNamesHead", "Names", "Nomi", "Noms", "Namen", "Nombres" },
    { "PlayersHelp", "Up/down: choose   Left/right: change name   Confirm: open   Back: return",
      "Su/giu: scegli   Sinistra/destra: cambia nome   Conferma: apri   Indietro: esci",
      "Haut/bas : choisir   Gauche/droite : changer de nom   Valider : ouvrir   Retour : quitter",
      "Hoch/runter: waehlen   Links/rechts: Name wechseln   Bestaetigen: oeffnen   Zurueck: verlassen",
      "Arriba/abajo: elegir   Izquierda/derecha: cambiar nombre   Confirmar: abrir   Volver: salir" },
    { "PlayersOpenCard", "Confirm: open the profile", "Conferma: apri la scheda", "Valider : ouvrir la fiche",
      "Bestaetigen: Profil oeffnen", "Confirmar: abrir la ficha" },
    { "PlayersGuestNote", "Plays without statistics", "Gioca senza statistiche", "Joue sans statistiques",
      "Spielt ohne Statistiken", "Juega sin estadisticas" },
    { "PlayersTypeName", "Confirm: type a name", "Conferma: scrivi un nome", "Valider : ecrire un nom",
      "Bestaetigen: Namen eingeben", "Confirmar: escribir un nombre" },
    { "PlayersManageNote", "Rename, merge, delete", "Rinomina, unisci, elimina", "Renommer, fusionner, supprimer",
      "Umbenennen, vereinen, loeschen", "Renombrar, unir, eliminar" },
    // the enemies set on fire in a box of the player's card (the row's own name does not fit there in every language)
    { "CardBurned", "Set on fire", "Incendiati", "Enflammes", "Angezuendet", "Incendiados" },
    // the Records page in groups of boxes, the Last game as a table, the confirmations' buttons side by side (03/11/2026)
    { "RecordsInLevel", "In a level", "In un livello", "En un niveau", "In einem Level", "En un nivel" },
    { "RecordsOverall", "Overall", "In tutto", "Au total", "Insgesamt", "En total" },
    { "RecordsFastest", "Fastest win", "Vittoria lampo", "Victoire eclair", "Schnellster Sieg", "Victoria rapida" },
    { "RecordsMost", "Most records: {name} ({n})", "Piu' record: {name} ({n})", "Le plus de records : {name} ({n})",
      "Meiste Rekorde: {name} ({n})", "Mas records: {name} ({n})" },
    { "LastCols", "Kills  Assists  Deaths", "Uccisioni  Assist  Morti", "Tues  Assist.  Morts", "Besiegt  Assists  Tode",
      "Bajas  Asist.  Muertes" },
    { "LastPvpCols", "Knocked down  Falls  Rounds won", "Abbattuti  Cadute  Round vinti", "A terre  Chutes  Manches",
      "Besiegt  Niederlagen  Runden", "Derribados  Caidas  Rondas" },
    { "LastTotal", "Total", "Totale", "Total", "Gesamt", "Total" },
    { "PlayersBackHelp", "Back: return", "Indietro: esci", "Retour : quitter", "Zurueck: verlassen", "Volver: salir" },
    { "PlayersConfirmHelp", "Left/right: choose   Confirm: select   Back: return",
      "Sinistra/destra: scegli   Conferma: seleziona   Indietro: esci",
      "Gauche/droite : choisir   Valider : selectionner   Retour : quitter",
      "Links/rechts: waehlen   Bestaetigen: auswaehlen   Zurueck: verlassen",
      "Izquierda/derecha: elegir   Confirmar: seleccionar   Volver: salir" },
    // Manage names: a new name without giving it to a place (test of 03/11/2026: not only with the places' arrows)
    { "PlayersAddName", "Add a name", "Aggiungi nome", "Ajouter un nom", "Namen hinzufuegen", "Anadir nombre" },
    // three levels whose code is not a key of the game's texts (test of 03/11/2026: their rows had no name)
    { "LevelPelennor", "Pelennor Fields", "Campi del Pelennor", "Champs du Pelennor", "Pelennor-Felder", "Campos del Pelennor" },
    { "LevelPalantirSaruman", "Palantir of Saruman", "Palantir di Saruman", "Palantir de Saroumane", "Palantir Sarumans",
      "Palantir de Saruman" },
    { "LevelPalantirSauron", "Palantir of Sauron", "Palantir di Sauron", "Palantir de Sauron", "Palantir Saurons",
      "Palantir de Sauron" },
    { "PlayersWinner", "{name} wins: {hero}", "Vince {name}: {hero}", "{name} gagne : {hero}", "{name} gewinnt: {hero}",
      "Gana {name}: {hero}" },
    { "PlayersRecords", "Records", "Record", "Records", "Rekorde", "Records" },
    { "PlayersLast", "Last game", "Ultima partita", "Derniere partie", "Letztes Spiel", "Ultima partida" },
    { "PlayersManage", "Manage names", "Gestione nomi", "Gerer les noms", "Namen verwalten", "Gestionar nombres" },
    { "PlayersRename", "Rename", "Rinomina", "Renommer", "Umbenennen", "Renombrar" },
    { "PlayersMerge", "Merge with another name", "Unisci con un altro nome", "Fusionner avec un autre nom",
      "Mit anderem Namen zusammenfuehren", "Unir con otro nombre" },
    { "PlayersRemove", "Delete", "Elimina", "Supprimer", "Loeschen", "Eliminar" },
    { "PlayersAskRemove", "Delete {name} and all of its statistics?", "Eliminare {name} e tutte le sue statistiche?",
      "Supprimer {name} et toutes ses statistiques ?", "{name} und alle Statistiken loeschen?",
      "Eliminar a {name} y todas sus estadisticas?" },
    { "PlayersAskMerge", "Add {name} to {other} and remove {name}?", "Aggiungere {name} a {other} ed eliminare {name}?",
      "Ajouter {name} a {other} et supprimer {name} ?", "{name} zu {other} hinzufuegen und {name} loeschen?",
      "Anadir {name} a {other} y eliminar {name}?" },
    { "PlayersYes", "Yes", "Si", "Oui", "Ja", "Si" },
    { "PlayersNo", "No", "No", "Non", "Nein", "No" },
    { "PlayersPick", "Merge {name} with:", "Unisci {name} con:", "Fusionner {name} avec :", "{name} zusammenfuehren mit:",
      "Unir {name} con:" },
    { "PlayersRenameTitle", "New name for {name}", "Nuovo nome per {name}", "Nouveau nom pour {name}",
      "Neuer Name fuer {name}", "Nuevo nombre para {name}" },
    { "PlayersDetail", "Heroes of {name}", "Eroi di {name}", "Heros de {name}", "Helden von {name}", "Heroes de {name}" },
    { "PlayersDetailCols", "Levels  Won  Versus  Kills  Assists  Deaths", "Livelli  Vinti  Sfide  Uccisioni  Assist  Morti",
      "Niveaux  Gagnes  Defis  Tues  Assist.  Morts", "Level  Gewonnen  Duelle  Besiegt  Assists  Tode",
      "Niveles  Ganados  Desafios  Bajas  Asist.  Muertes" },
    { "PlayersDetailHelp", "Left/right: another player   Back: return", "Sinistra/destra: altro giocatore   Indietro: esci",
      "Gauche/droite : autre joueur   Retour : quitter", "Links/rechts: anderer Spieler   Zurueck: verlassen",
      "Izquierda/derecha: otro jugador   Volver: salir" },
    { "PlayersLastNone", "No game yet", "Ancora nessuna partita", "Pas encore de partie", "Noch kein Spiel",
      "Aun no hay partidas" },
    { "PlayersLastCoop", "Level won", "Livello vinto", "Niveau gagne", "Level gewonnen", "Nivel ganado" },
    { "PlayersLastLost", "Level left", "Livello lasciato", "Niveau quitte", "Level verlassen", "Nivel abandonado" },
    { "PlayersLastPvp", "Versus game", "Sfida", "Defi", "Duell", "Desafio" },
    { "PlayersSumLine", "{name} ({hero}): {k} kills, {a} assists, {d} deaths",
      "{name} ({hero}): {k} uccisioni, {a} assist, {d} morti", "{name} ({hero}) : {k} tues, {a} assist., {d} morts",
      "{name} ({hero}): {k} besiegt, {a} Assists, {d} Tode", "{name} ({hero}): {k} bajas, {a} asist., {d} muertes" },
    { "PlayersSumPvpLine", "{name} ({hero}): {k} knocked down, {d} falls", "{name} ({hero}): {k} abbattuti, {d} cadute",
      "{name} ({hero}) : {k} a terre, {d} chutes", "{name} ({hero}): {k} besiegt, {d} Niederlagen",
      "{name} ({hero}): {k} derribados, {d} caidas" },
    { "PlayersSumBest", "Most kills: {name}", "Piu' uccisioni: {name}", "Le plus de victimes : {name}",
      "Meiste besiegte Gegner: {name}", "Mas bajas: {name}" },
    { "PlayersSumTitle", "This level", "Questo livello", "Ce niveau", "Dieses Level", "Este nivel" },
    { "RecordKills", "Most kills in a level", "Piu' uccisioni in un livello", "Le plus d'ennemis tues en un niveau",
      "Meiste besiegte Gegner in einem Level", "Mas bajas en un nivel" },
    { "RecordPerfect", "Most Perfect kills in a level", "Piu' Superbo in un livello", "Le plus de Parfait en un niveau",
      "Meiste Perfekt in einem Level", "Mas Perfecto en un nivel" },
    { "RecordAssists", "Most assists in a level", "Piu' assist in un livello", "Le plus d'assistances en un niveau",
      "Meiste Assists in einem Level", "Mas asistencias en un nivel" },
    { "RecordPvpFast", "Fastest Versus win", "Sfida vinta piu' in fretta", "Defi gagne le plus vite", "Schnellster Duellsieg",
      "Desafio ganado mas rapido" },
    { "RecordTotKills", "Most kills overall", "Piu' uccisioni in tutto", "Le plus d'ennemis tues au total",
      "Meiste besiegte Gegner insgesamt", "Mas bajas en total" },
    { "RecordTotWins", "Most levels won", "Piu' livelli vinti", "Le plus de niveaux gagnes", "Meiste gewonnene Level",
      "Mas niveles ganados" },
    { "RecordPvpWins", "Most Versus wins", "Piu' sfide vinte", "Le plus de defis gagnes", "Meiste Duellsiege",
      "Mas desafios ganados" },
    { "PlayersListHelp", "Up/down: choose   Confirm: select   Back: return",
      "Su/giu': scegli   Conferma: seleziona   Indietro: esci", "Haut/bas : choisir   Valider : selectionner   Retour : quitter",
      "Hoch/runter: waehlen   Bestaetigen: auswaehlen   Zurueck: verlassen",
      "Arriba/abajo: elegir   Confirmar: seleccionar   Volver: salir" },
    { "PlayersNoNames", "No names yet", "Ancora nessun nome", "Pas encore de noms", "Noch keine Namen", "Aun no hay nombres" },
    { "WhoTitle", "Who is playing?", "Chi gioca?", "Qui joue ?", "Wer spielt?", "Quien juega?" },
    { "WhoStart", "Start", "Inizia", "Commencer", "Start", "Empezar" },
    { "WhoGuest", "Guest", "Ospite", "Invite", "Gast", "Invitado" },
    { "WhoNew", "New name...", "Nuovo nome...", "Nouveau nom...", "Neuer Name...", "Nuevo nombre..." },
    { "WhoHelp", "Left/right: name   Confirm: start   Back: cancel", "Sinistra/destra: nome   Conferma: inizia   Indietro: annulla",
      "Gauche/droite : nom   Valider : commencer   Retour : annuler", "Links/rechts: Name   Bestaetigen: Start   Zurueck: abbrechen",
      "Izquierda/derecha: nombre   Confirmar: empezar   Volver: cancelar" },
    { "WhoEdit", "New name for {player}", "Nuovo nome per {player}", "Nouveau nom pour {player}", "Neuer Name fuer {player}",
      "Nuevo nombre para {player}" },
    { "MultiTitle", "Multiplayer", "Multigiocatore", "Multijoueur", "Mehrspieler", "Multijugador" },
    { "MultiGame", "The game", "La partita", "La partie", "Das Spiel", "La partida" },
    { "MultiMode", "Mode", "Modalita'", "Mode", "Modus", "Modo" },
    { "MultiCampaign", "Campaign", "Campagna", "Campagne", "Kampagne", "Historia" },
    { "MultiFfa", "Everybody against everybody", "Tutti contro tutti", "Tous contre tous", "Jeder gegen jeden", "Todos contra todos" },
    { "MultiTeams", "Teams", "A squadre", "Par equipes", "Teams", "Por equipos" },
    { "MultiPlayers", "Players", "Giocatori", "Joueurs", "Spieler", "Jugadores" },
    { "MultiEnemies", "Enemies", "Nemici", "Ennemis", "Gegner", "Enemigos" },
    { "MultiEnemiesOn", "Yes", "Si'", "Oui", "Ja", "Si" },
    { "MultiHeroesOnly", "Heroes only", "Solo eroi", "Heros seuls", "Nur Helden", "Solo heroes" },
    { "MultiLives", "Lives", "Vite", "Vies", "Leben", "Vidas" },
    { "MultiSplit", "Teams", "Squadre", "Equipes", "Teams", "Equipos" },
    { "MultiSplitValue", "{a} and {b} against {c} and {d}", "{a} e {b} contro {c} e {d}", "{a} et {b} contre {c} et {d}",
      "{a} und {b} gegen {c} und {d}", "{a} y {b} contra {c} y {d}" },
    { "MultiSun", "Sun", "Sole", "Soleil", "Sonne", "Sol" },      // a place's team, short (the full names: Lang.cpp)
    { "MultiMoon", "Moon", "Luna", "Lune", "Mond", "Luna" },
    { "MultiTeamsHead", "The teams", "Le squadre", "Les equipes", "Die Teams", "Los equipos" },
    { "MultiMapNote", "The level is chosen on the map", "Il livello si sceglie sulla mappa", "Le niveau se choisit sur la carte",
      "Das Level wird auf der Karte gewaehlt", "El nivel se elige en el mapa" },
    { "MultiArenaNote", "The arena is chosen on the map", "L'arena si sceglie sulla mappa", "L'arene se choisit sur la carte",
      "Die Arena wird auf der Karte gewaehlt", "La arena se elige en el mapa" },
    { "MultiHelp", "Up/down: entry   Left/right: change   Shoulders or Tab: other box   Confirm: start   Back: menu",
      "Su/giu': voce   Sinistra/destra: cambia   Dorsali o Tab: altro riquadro   Conferma: inizia   Indietro: menu",
      "Haut/bas : choix   Gauche/droite : changer   Gachettes ou Tab : autre cadre   Valider : commencer   Retour : menu",
      "Hoch/runter: Eintrag   Links/rechts: aendern   Schultertasten oder Tab: anderer Kasten   Bestaetigen: Start   Zurueck: Menue",
      "Arriba/abajo: opcion   Izquierda/derecha: cambiar   Gatillos o Tab: otro cuadro   Confirmar: empezar   Volver: menu" },
    { "MultiRule", "Victory", "Vittoria", "Victoire", "Sieg", "Victoria" },
    { "MultiRuleLast", "Last one standing", "Ultimo in piedi", "Dernier debout", "Letzter steht", "Ultimo en pie" },
    { "MultiRuleTime", "Timed", "A tempo", "Au temps", "Auf Zeit", "A tiempo" },
    { "MultiTime", "Time", "Tempo", "Temps", "Zeit", "Tiempo" },
    { "MultiMinutes", "{n} minutes", "{n} minuti", "{n} minutes", "{n} Minuten", "{n} minutos" },
    { "MultiRounds", "Tournament", "Torneo", "Tournoi", "Turnier", "Torneo" },
    { "MultiSingle", "Single game", "Partita singola", "Partie unique", "Einzelspiel", "Partida unica" },
    { "MultiBestOf", "Best of {n}", "Al meglio di {n}", "Au meilleur des {n}", "Best of {n}", "Al mejor de {n}" },
    // the kicks (04/11/2026): the physical attack, a kick or a shove, counted when thrown and when it hits someone
    { "StatKicks", "Kicks (landed / thrown)", "Calci (a segno / tirati)", "Coups de pied (touches / donnes)",
      "Tritte (Treffer / gesamt)", "Patadas (acertadas / dadas)" },
    // the attacks that hit an opponent (an enemy, or another player in a versus game), one per swing, and the enemies'
    // shields broken (05/11/2026)
    { "StatSpeedHits", "Speed attacks that hit", "Attacchi veloci a segno", "Attaques rapides reussies",
      "Schnelle Angriffe getroffen", "Ataques rapidos acertados" },
    { "StatFierceHits", "Fierce attacks that hit", "Attacchi feroci a segno", "Attaques puissantes reussies",
      "Wuchtige Angriffe getroffen", "Ataques fieros acertados" },
    { "StatShields", "Shields broken", "Scudi frantumati", "Boucliers brises", "Schilde zerschlagen", "Escudos rotos" },
    { "CardDowns", "Opponents down", "Avversari abbattuti", "Adversaires a terre", "Gegner besiegt", "Rivales derribados" },
    { "PlayersSumBestPvp", "Most knock-downs: {name}", "Piu' abbattuti: {name}", "Le plus de mises a terre : {name}",
      "Meiste Niederschlaege: {name}", "Mas derribos: {name}" },
    { "LastWithEnemies", "with the enemies", "con i nemici", "avec les ennemis", "mit den Gegnern", "con los enemigos" },
    // who knocked down whom (the Comparison's Versus tab: the column's player downed {name} so many times), and the Last
    // game shown on the map after a versus game (05/11/2026)
    { "StatVsDowns", "Downed {name}", "Ha abbattuto {name}", "A mis a terre {name}", "{name} besiegt", "Derribo a {name}" },
    { "PlayersToMap", "Back to the map", "Torna alla mappa", "Retour a la carte", "Zurueck zur Karte", "Volver al mapa" },
    { "PlayersChoose", "choose your hero", "scegli l'eroe", "choisissez votre heros", "waehle deinen Helden", "elige tu heroe" },
    { "PlayersThousands", ",", ".", " ", ".", "." },   // between the thousands of a number (an ini can't hold a lone space)
    { "HeroAragorn", "Aragorn", "Aragorn", "Aragorn", "Aragorn", "Aragorn" },
    { "HeroLegolas", "Legolas", "Legolas", "Legolas", "Legolas", "Legolas" },
    { "HeroGimli", "Gimli", "Gimli", "Gimli", "Gimli", "Gimli" },
    { "HeroGandalf", "Gandalf", "Gandalf", "Gandalf", "Gandalf", "Gandalf" },
    { "HeroSam", "Sam", "Sam", "Sam", "Sam", "Sam" },
    { "HeroFrodo", "Frodo", "Frodo", "Frodon", "Frodo", "Frodo" },
    { "HeroPippin", "Pippin", "Pipino", "Pippin", "Pippin", "Pippin" },
    { "HeroMerry", "Merry", "Merry", "Merry", "Merry", "Merry" },
    { "HeroFaramir", "Faramir", "Faramir", "Faramir", "Faramir", "Faramir" },
};
static_assert(sizeof kTxt / sizeof kTxt[0] == T_COUNT, "one text per id");

// The texts are read once per opening of the page (the page is drawn every frame: no file reads while drawing)
static std::string g_txtCache[T_COUNT];
static std::string g_playerLabel[4];   // "Player N" in the game's language
static bool        g_txtReady;

// "Player N" in the game's language. LangText(3) answers what the hero selection shows ("Player 1, select your
// character"): the text is cut right after the number.
static std::string PlayerLabel(int n)
{
    const std::string s = LangText(3, n);
    const size_t at = s.find((char)('0' + n));
    return at != std::string::npos && at < 24 ? s.substr(0, at + 1) : s;
}

static void LoadTexts()
{
    const std::string code = LangCode();
    char ini[MAX_PATH], buf[160];
    sprintf_s(ini, "%s\\ROTKPlus.lang.ini", GameDir());
    for (int id = 0; id < T_COUNT; ++id) {
        GetPrivateProfileStringA(code.c_str(), kTxt[id].key, "", buf, sizeof buf, ini);
        g_txtCache[id] = buf[0] ? buf : code == "it" ? kTxt[id].it : code == "fr" ? kTxt[id].fr :
                         code == "de" ? kTxt[id].de : code == "es" ? kTxt[id].es : kTxt[id].en;
    }
    for (int i = 0; i < 4; ++i) g_playerLabel[i] = PlayerLabel(i + 1);
    g_txtReady = true;
}

static const std::string& Txt(int id)
{
    if (!g_txtReady) LoadTexts();
    return g_txtCache[id];
}

static std::string Num(int v);   // a number as the language writes it (drawing, below)

static std::string Replace(std::string s, const char* token, const std::string& v)
{
    for (size_t at = s.find(token); at != std::string::npos; at = s.find(token, at + v.size())) s.replace(at, strlen(token), v);
    return s;
}

// --- the file ROTKPlus_players.ini ------------------------------------------------------------------------------
static void PlayersIni(char* out, size_t n) { sprintf_s(out, n, "%s\\ROTKPlus_players.ini", GameDir()); }

typedef std::map<std::string, int> Vals;   // one [Stats.<name>] section

static std::string Section(const std::string& name) { return "Stats." + name; }

// all key=value pairs of a section (a function of its own: big buffer, no C++ objects on the error paths)
static std::vector<std::pair<std::string, std::string>> ReadPairs(const std::string& section)
{
    std::vector<std::pair<std::string, std::string>> out;
    char ini[MAX_PATH];
    PlayersIni(ini, sizeof ini);
    static char buf[32768];
    const DWORD n = GetPrivateProfileSectionA(section.c_str(), buf, sizeof buf, ini);
    for (const char* p = buf; p < buf + n && *p; p += strlen(p) + 1) {
        const char* eq = strchr(p, '=');
        if (eq && eq > p) out.emplace_back(std::string(p, eq - p), std::string(eq + 1));
    }
    return out;
}

static void WritePairs(const std::string& section, const std::vector<std::pair<std::string, std::string>>& pairs)
{
    char ini[MAX_PATH];
    PlayersIni(ini, sizeof ini);
    std::string block;
    for (const auto& kv : pairs) { block += kv.first + "=" + kv.second; block.push_back('\0'); }
    block.push_back('\0');
    WritePrivateProfileSectionA(section.c_str(), block.c_str(), ini);   // replaces the whole section
}

static void DeleteSection(const std::string& section)
{
    char ini[MAX_PATH];
    PlayersIni(ini, sizeof ini);
    WritePrivateProfileStringA(section.c_str(), nullptr, nullptr, ini);
}

static Vals ReadVals(const std::string& name)
{
    Vals v;
    for (const auto& kv : ReadPairs(Section(name))) v[kv.first] = atoi(kv.second.c_str());
    return v;
}

static void WriteVals(const std::string& name, const Vals& v)
{
    std::vector<std::pair<std::string, std::string>> pairs;
    for (const auto& kv : v) pairs.emplace_back(kv.first, std::to_string(kv.second));
    WritePairs(Section(name), pairs);
}

static int Get(const Vals& v, const std::string& key) { const auto it = v.find(key); return it == v.end() ? 0 : it->second; }
static std::string HeroKey(int h, const char* what) { return "Hero" + std::to_string(h) + what; }
// a player's knock-downs of another one: "PvpDown.<name>", the name in small letters (the ini's names are one person
// whatever the capitals) and without what an ini key cannot hold
static std::string DownsKey(const std::string& name)
{
    std::string k = "PvpDown.";
    for (char ch : name) {
        const unsigned char u = (unsigned char)ch;
        k.push_back(u < 0x20 || ch == '=' || ch == ';' || ch == '[' || ch == ']' ? '_' : u < 0x80 ? (char)tolower(u) : ch);
    }
    return k;
}

// a + b: counters add up, records keep the best (Best* the largest, Fastest* the smallest above zero)
static void MergeVals(Vals& into, const Vals& add)
{
    for (const auto& kv : add) {
        int& d = into[kv.first];
        if (!kv.first.compare(0, 4, "Best")) d = (std::max)(d, kv.second);
        else if (!kv.first.compare(0, 7, "Fastest")) d = d > 0 && kv.second > 0 ? (std::min)(d, kv.second) : (std::max)(d, kv.second);
        else d += kv.second;
    }
}

// names that have statistics
static std::vector<std::string> StatNames()
{
    std::vector<std::string> found;
    char ini[MAX_PATH];
    PlayersIni(ini, sizeof ini);
    static char names[16384];
    const DWORD n = GetPrivateProfileSectionNamesA(names, sizeof names, ini);
    for (const char* p = names; p < names + n && *p; p += strlen(p) + 1)
        if (!strncmp(p, "Stats.", 6) && p[6]) found.push_back(p + 6);
    std::sort(found.begin(), found.end(), [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
    return found;
}

// --- names ------------------------------------------------------------------------------------------------------
static const int kMaxName = 12;
static char g_name[4][kMaxName + 1];          // custom names, "" = "Player N"
static std::vector<std::string> g_known;     // names used before, for left/right

// the ini functions do not tell "alex" from "Alex" ([Stats.alex] and [Stats.Alex] are one section), so names
// are compared the same way: one person, whatever the capitals
static bool SameName(const std::string& a, const std::string& b) { return !_stricmp(a.c_str(), b.c_str()); }
static bool HasName(const std::vector<std::string>& v, const std::string& n)
{
    for (const std::string& x : v) if (SameName(x, n)) return true;
    return false;
}

static std::vector<std::string> AllNames();
static void RenameName(const std::string& from, const std::string& to);

static void LoadNames()
{
    char ini[MAX_PATH], buf[1024], key[8];
    PlayersIni(ini, sizeof ini);
    for (int i = 0; i < 4; ++i) {
        sprintf_s(key, "P%d", i + 1);
        GetPrivateProfileStringA("Names", key, "", g_name[i], sizeof g_name[i], ini);
    }
    GetPrivateProfileStringA("Names", "Known", "", buf, sizeof buf, ini);
    g_known.clear();
    for (char* ctx = nullptr, *t = strtok_s(buf, "|", &ctx); t; t = strtok_s(nullptr, "|", &ctx))
        if (*t && strlen(t) <= (size_t)kMaxName && !HasName(g_known, t))
            g_known.push_back(t);
}

static void SaveNames()
{
    char ini[MAX_PATH], key[8];
    PlayersIni(ini, sizeof ini);
    for (int i = 0; i < 4; ++i) {
        sprintf_s(key, "P%d", i + 1);
        WritePrivateProfileStringA("Names", key, g_name[i], ini);
    }
    std::string all;
    for (const std::string& k : g_known) { if (!all.empty()) all += "|"; all += k; }
    WritePrivateProfileStringA("Names", "Known", all.c_str(), ini);
}

bool PlayerHasName(int slot) { return slot >= 0 && slot < 4 && g_enabled && g_name[slot][0]; }

// the name of a player as shown: the custom one, or "Player N" in the game's language
std::string PlayerName(int slot)
{
    if (PlayerHasName(slot)) return g_name[slot];
    return PlayerLabel(slot + 1);
}

// the controller of an extra player (slots 2 and 3) remembers the name given to that player
static void RememberPadName(int slot)
{
    if (slot < 2 || !g_name[slot][0]) return;
    const std::string key = PadKeyOfSlot(slot);
    if (key.empty()) return;
    char ini[MAX_PATH];
    PlayersIni(ini, sizeof ini);
    WritePrivateProfileStringA("PadNames", key.c_str(), g_name[slot], ini);
}

static void SetName(int slot, const std::string& name)
{
    // a name already known with other capitals ("alex" typed as "Alex"): the same person, written the new way
    for (const std::string& k : AllNames())
        if (SameName(k, name) && k != name) { RenameName(k, name); break; }
    // a name another player has: the two players swap names (never two players with one name)
    const std::string old = g_name[slot];
    for (int s = 0; s < 4; ++s)
        if (s != slot && !name.empty() && SameName(g_name[s], name)) {
            strncpy_s(g_name[s], old.c_str(), _TRUNCATE);
            RememberPadName(s);
            Log("players: player %d is now \"%s\"", s + 1, PlayerName(s).c_str());
        }
    strncpy_s(g_name[slot], name.c_str(), _TRUNCATE);
    if (!name.empty() && !HasName(g_known, name)) g_known.push_back(name);
    SaveNames();
    RememberPadName(slot);
    Log("players: player %d is now \"%s\"", slot + 1, PlayerName(slot).c_str());
}

// from Pads.cpp when a controller is opened for the third or fourth player: a known controller brings its name back
void PlayersPadOpened(int slot, const std::string& key)
{
    if (!g_enabled || slot < 2 || slot > 3 || key.empty()) return;
    if (g_askWho) return;   // with "Who is playing?" the controller's name is only proposed in its row (OpenLineup)
    char ini[MAX_PATH], name[kMaxName + 1];
    PlayersIni(ini, sizeof ini);
    GetPrivateProfileStringA("PadNames", key.c_str(), "", name, sizeof name, ini);
    if (!name[0] || SameName(name, g_name[slot])) return;
    for (int s = 0; s < 4; ++s) if (s != slot && SameName(g_name[s], name)) return;   // that name is playing elsewhere
    strncpy_s(g_name[slot], name, _TRUNCATE);
    SaveNames();
    Log("players: the controller of player %d is known: name \"%s\"", slot + 1, name);
}

// in the [PadNames] section, the controllers that had the name "from" get "to" ("" = forget them)
static void RenamePads(const std::string& from, const std::string& to)
{
    auto pairs = ReadPairs("PadNames");
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& kv : pairs) {
        if (SameName(kv.second, from)) { if (to.empty()) continue; kv.second = to; }
        out.push_back(kv);
    }
    WritePairs("PadNames", out);
}

// in the last line-ups ([Lineup1]..[Lineup4], "Who is playing?"), the name "from" becomes "to" ("" = a guest)
static void RenameInLineups(const std::string& from, const std::string& to)
{
    for (int n = 1; n <= 4; ++n) {
        const std::string sec = "Lineup" + std::to_string(n);
        auto pairs = ReadPairs(sec);
        bool changed = false;
        for (auto& kv : pairs)
            if (!kv.second.empty() && SameName(kv.second, from)) { kv.second = to; changed = true; }
        if (changed) WritePairs(sec, pairs);   // two equal names after a merge: the second becomes a guest when read
    }
}

// --- managing names: rename, merge, delete ----------------------------------------------------------------------
static void ReplaceName(const std::string& from, const std::string& to)
{
    for (int s = 0; s < 4; ++s) {
        if (!SameName(from, g_name[s])) continue;
        bool taken = false;   // "to" already plays elsewhere: this place is left without a name, not two places with one
        for (int o = 0; o < 4; ++o) if (o != s && !to.empty() && !SameName(from, to) && SameName(g_name[o], to)) taken = true;
        strncpy_s(g_name[s], taken ? "" : to.c_str(), _TRUNCATE);
    }
    std::vector<std::string> k;
    for (const std::string& n : g_known) {
        const std::string m = SameName(n, from) ? to : n;
        if (!m.empty() && !HasName(k, m)) k.push_back(m);
    }
    g_known = k;
    SaveNames();
    RenamePads(from, to);
    RenameInLineups(from, to);
}

// the other players' knock-downs of "from" become knock-downs of "to" (a rename or a merge), or go ("to" empty: deleted)
static void MoveDownKeys(const std::string& from, const std::string& to)
{
    const std::string kf = DownsKey(from), kt = to.empty() ? std::string() : DownsKey(to);
    for (const std::string& n : StatNames()) {
        Vals v = ReadVals(n);
        bool changed = false;
        const auto it = v.find(kf);
        if (it != v.end()) {
            const int c = it->second;
            v.erase(it);
            if (!kt.empty() && !SameName(n, to)) v[kt] += c;
            changed = true;
        }
        if (!kt.empty() && SameName(n, to) && v.erase(kt)) changed = true;   // (from's own knock-downs of to: itself now)
        if (changed) WriteVals(n, v);
    }
}

// adds the statistics of "from" to those of "to" and removes "from" (also used for renaming onto an existing name)
static void MergeNames(const std::string& from, std::string to)
{
    if (from == to || to.empty()) return;
    if (SameName(from, to)) {
        // only the capitals change: one section for the ini functions, so its lines move as they are (adding them
        // to "themselves" would double them, then deleting "from" would delete everything)
        const auto pairs = ReadPairs(Section(from));
        DeleteSection(Section(from));
        if (!pairs.empty()) WritePairs(Section(to), pairs);
        ReplaceName(from, to);
        Log("players: \"%s\" written \"%s\"", from.c_str(), to.c_str());
        return;
    }
    for (const std::string& k : AllNames()) if (SameName(k, to)) { to = k; break; }   // "ALEX" onto "Alex"
    Vals into = ReadVals(to);
    MergeVals(into, ReadVals(from));
    if (!into.empty()) WriteVals(to, into);   // two names without statistics: no empty section (a row of zeros)
    DeleteSection(Section(from));
    ReplaceName(from, to);
    MoveDownKeys(from, to);
    Log("players: \"%s\" merged into \"%s\"", from.c_str(), to.c_str());
}

static void RenameName(const std::string& from, const std::string& to)
{
    if (to.empty() || from == to) return;
    MergeNames(from, to);   // with a new name the merge is a plain move
    Log("players: \"%s\" renamed \"%s\"", from.c_str(), to.c_str());
}

static void RemoveName(const std::string& name)
{
    DeleteSection(Section(name));
    ReplaceName(name, "");
    MoveDownKeys(name, "");
    Log("players: \"%s\" deleted with its statistics", name.c_str());
}

// every name: those with statistics and those used before
static std::vector<std::string> AllNames()
{
    std::vector<std::string> all = StatNames();
    for (const std::string& k : g_known) if (!HasName(all, k)) all.push_back(k);
    std::sort(all.begin(), all.end(), [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
    return all;
}

// --- statistics: collection --------------------------------------------------------------------------------------
struct Live {
    bool present; int hero, kills, assists, deaths, pvpKills, pvpFalls, ranged, finish, multi, bestMulti; DWORD lastKill;
    int pvpElims, rounds, roundWins;              // versus: opponents eliminated, rounds played and won
    int spear, parryHits, parryArrows, pick[9];   // spear kills, hits parried, things picked up by type (1-8)
    int ladders;                                  // ladders pushed down near the player
    int burned;                                   // enemies the player set on fire
    DWORD lastSpear;                              // when the player last threw a spear (state 0x44)
    int kicks, kicksHit, pvpKicks;                // kicks thrown (state 0x1E), landed, landed on an opponent (versus)
    DWORD lastKickHit;                            // when a kick of the player last hit someone
    int speedHits, fierceHits, shields;           // speed and fierce attacks that hit an opponent, shields broken
    DWORD swingHit;                               // the state entry (g_stateSince) of the last swing counted
    int downs[4];                                 // versus: the knock-downs of each other player
};
static Live  g_live[4];
static bool  g_inLevel;      // a game level is in progress (from the package loading to leaving mode 0)
static bool  g_seenPlay;     // mode 0 (playing) was reached since the loading
static bool  g_levelPvp;     // it is a versus game
static bool  g_levelSolo;    // it is a single-player game (Single in the main menu)
static int   g_pvpWinner = -2;   // -2 versus not over, -1 draw, 99 a team won, otherwise the winner's slot
static unsigned g_pvpWinners;    // the winners' slots as bits (one player, or a team)
static bool     g_pvpTeamGame, g_pvpTimedGame, g_pvpTourney;   // what kind of versus game it was
static DWORD g_levelStart;
struct HitInfo { DWORD at[4]; };
static std::unordered_map<uint32_t, HitInfo> g_hits;   // enemy -> when each player last hit it
static int g_killsLogged;

static int PlayerSlot(uint32_t c)
{
    if (!c) return -1;
    for (int i = 0; i < 4; ++i) if (g_players[i] == c) return i;
    return -1;
}

static int SafeLife(uint32_t c)
{
    int v = 0;
    Guard([&] { v = *(const int*)(c + 0x90); });
    return v;
}

// the player an object belongs to (0x4019E0, ECX = entity, nothing on the stack: called as __fastcall), 0 if none
static uint32_t OwnerRaw(uint32_t e)
{
    return ((uint32_t(__fastcall*)(uint32_t))0x4019E0)(e);
}

// slot of the player who made a hit (the player itself or the owner of its arrow, spear...), -1 if none
static int AttackerSlot(uint32_t a)
{
    if (!a) return -1;
    const int s = PlayerSlot(a);
    if (s >= 0) return s;
    uint32_t o = 0;
    int16_t kind = 0;
    if (!Guard([&] { kind = *(const int16_t*)(a + 0x7A); if (kind != 99) o = OwnerRaw(a); })) return -1;
    if (kind == 99) return -1;   // a character of the players' kind that is not in the array
    return PlayerSlot(o);
}

// The kind of a hit (the 0x259 event's data {attacker, kind, damage, -1, object}, EDI at 0x404D12 and 0x404D5B in
// 0x404CB0, which most enemies' handlers use): 0xD the killing move (R2 on an enemy on the ground, 9999 of damage;
// 0x43C697), 0xE-0x11 a projectile (0x4547C0: arrows, thrown axes and daggers, Gandalf's 0xF / 0x11), 0x12 and 0x14 a
// thrown spear (0x452FE0, 1000 of damage). Arrows and spears name their thrower as the attacker (test of 31/10/2026:
// five spear kills counted as melee), so the kind tells them apart; -1 = not known (the other sites)
static bool RangedKind(int k) { return (k >= 0xE && k <= 0x12) || k == 0x14; }

static int g_rangedLogged;
// The complete log of a level (02/11/2026: "collect every log you may need"): with [Log] Detail=1 every kill, hit
// taken, state entered, pick-up, ladder, rare script call and animation tag is written with its time, up to
// 40000 lines a level (a long level makes a few MB), so that later questions can be answered from the logs.
static int g_traceLogged;
static bool TraceOk() { return LogDetail() && g_traceLogged++ < 40000; }

// Enemies felled by one blow (the devastating attack, a wide combo...): kills by the same player less than 0.4 s apart
// are one blow (a blow's kills come in a few frames; two separate blows are further apart). The level's best is kept.
static void NoteMulti(int a, DWORD now);
static std::unordered_map<uint32_t, DWORD> g_killedAt;   // enemies whose kill was counted, and when (the game's credit
                                                        // that follows is not counted again)
static int g_creditLogged;

// --- kills by kind of enemy (first step: counted by the game's character type, +0x78; the names of the types are
// still to be found, so each type is described once in the log). The players' heroes have types 8, 0x22, 0xD, 0x1E,
// 0xC, 4, 0x44, 0x43, 0x59 (ENGINE_NOTES); an orc is 3 (LockOn, Pvp.cpp). Saved per name as Foe<type in hex>.
static std::map<int, int> g_foes[4];   // this level: type -> kills, per player
static std::string g_levelCode;        // the level's code (PlayersLevelStart)

static int FoeType(uint32_t c)
{
    int v = -1;
    Guard([&] { v = *(const int16_t*)(c + 0x78); });
    return v;
}

// Memory that can be read without an access violation. The search for names below follows any dword as a pointer, and
// a handled access violation still reaches the vectored crash logger (Main.cpp), which writes only the first three of a
// session: they would read as crashes and hide a real one. So the memory is asked first (VirtualQuery).
static bool Readable(uint32_t a, uint32_t len)
{
    if (a < 0x10000) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)(uintptr_t)a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                         PAGE_EXECUTE_WRITECOPY))) return false;
    return (uintptr_t)a + len <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
}

// 4 to 40 printable characters ending with a zero: a name
static bool NameAt(uint32_t p, char out[41])
{
    if (!Readable(p, 41)) return false;
    bool ok = false;
    Guard([&] {
        const char* t = (const char*)(uintptr_t)p;
        int n = 0;
        while (n < 40 && t[n] >= 0x20 && t[n] < 0x7F) ++n;
        if (n < 4 || t[n] != 0) return;
        memcpy(out, t, n);
        out[n] = 0;
        ok = true;
    });
    return ok;
}

// a dword of the game's memory, false if it cannot be read
static bool ReadU32(uint32_t a, uint32_t& v)
{
    if (!Readable(a, 4)) return false;
    return Guard([&] { v = *(const uint32_t*)(uintptr_t)a; });
}

static int MaxLife(uint32_t c)
{
    int v = 0;
    Guard([&] { v = *(const int16_t*)(*(const uint32_t*)(c + 0xD0) + 0x48); });
    return v;
}

// the first time a type of enemy is killed in this session: its type, life and the names its record points to
// (directly, or one pointer further), to tell which enemy it is
static void DescribeFoe(uint32_t victim, int type, int life)
{
    static std::map<int, bool> described;
    if (type < 0 || described[type] || described.size() > 64) return;
    described[type] = true;
    std::string names;
    int found = 0;
    char buf[41];
    for (uint32_t off = 0; off < 0x200 && found < 4; off += 4) {
        uint32_t p = 0;
        if (!ReadU32(victim + off, p)) break;
        if (NameAt(p, buf)) { names += " +" + std::to_string(off) + "=\"" + buf + "\""; ++found; continue; }
        for (uint32_t o2 = 0; o2 < 0x40 && found < 4; o2 += 4) {
            uint32_t q = 0;
            if (!ReadU32(p + o2, q)) break;
            if (NameAt(q, buf)) { names += " +" + std::to_string(off) + "+" + std::to_string(o2) + "=\"" + buf + "\""; ++found; }
        }
    }
    Log("players: enemy type %X (level %s, life %d of %d):%s", type, g_levelCode.c_str(), life, MaxLife(victim),
        names.empty() ? " no names" : names.c_str());
}

static void NoteMulti(int a, DWORD now)
{
    Live& l = g_live[a];
    l.multi = l.lastKill && now - l.lastKill < 400 ? l.multi + 1 : 1;
    l.lastKill = now ? now : 1;
    if (l.multi > l.bestMulti) l.bestMulti = l.multi;
    if (l.multi == 3) Log("players: player %d fells 3 or more enemies in one blow", a + 1);
}

static void OnHit(uint32_t victim, uint32_t attacker, int after, int amount, int before, int kind)
{
    const int a = AttackerSlot(attacker);
    if (a < 0) return;
    const DWORD now = GetTickCount();
    const int v = PlayerSlot(victim);
    if (v >= 0) {   // a player hit by another player (versus mode)
        // a knock-down is the hit that takes the life from above zero to zero or below (a hit on someone already down
        // is not one more: it would count twice in the statistics and in the points of a timed game)
        const bool down = before > 0 && after < 1;
        if (v != a && g_levelPvp && down) {
            ++g_live[a].pvpKills;
            ++g_live[a].downs[v];
            Log("players: %s knocks down %s", PlayerName(a).c_str(), PlayerName(v).c_str());
        }
        // the feed, the points of a timed game, the crowd (Pvp.cpp)
        if (v != a && g_levelPvp && amount > 0) PvpOnPlayerHit(a, v, down);
        return;
    }
    if (g_hits.size() > 4096) {   // safety: forget enemies not hit for a minute
        for (auto it = g_hits.begin(); it != g_hits.end();) {
            DWORD last = 0;
            for (DWORD t : it->second.at) if (t && now - t < 60000) last = t;
            it = last ? std::next(it) : g_hits.erase(it);
        }
    }
    HitInfo& h = g_hits[victim];
    h.at[a] = now ? now : 1;
    if (after >= 1) return;
    ++g_live[a].kills;
    // at range: a kind of an arrow or a spear, or an object of the player as the attacker
    const bool ranged = RangedKind(kind) || PlayerSlot(attacker) < 0;
    if (ranged) ++g_live[a].ranged;
    if (kind == 0x12 || kind == 0x14) ++g_live[a].spear;   // a thrown spear (0x452FE0)
    // the killing move (the finishing blow on an enemy on the ground), always lethal: its kind, or its 9999 of damage
    // (the value Pvp.cpp halves in versus; logs of 31/10/2026: 8 of Aragorn's 20 kills, all in melee)
    const bool finish = !ranged && (kind == 0xD || amount >= 9999);
    if (finish) ++g_live[a].finish;
    const int foe = FoeType(victim);
    if (foe >= 0) ++g_foes[a][foe];
    NoteMulti(a, now);
    DescribeFoe(victim, foe, before);
    int assisted = 0;
    for (int i = 0; i < 4; ++i)
        if (i != a && h.at[i] && now - h.at[i] <= 15000) { ++g_live[i].assists; ++assisted; }
    g_hits.erase(victim);
    if (g_killedAt.size() > 512) g_killedAt.clear();
    g_killedAt[victim] = now ? now : 1;
    // the first 20 kills of a level, and up to 20 more at range (rarer, to check the kinds)
    if (TraceOk()) {
        Log("players: kill by player %d%s (enemy %08X type %X, %s, kind %X, damage %d of %d life)", a + 1,
            assisted ? ", with assists" : "", victim, foe, ranged ? "ranged" : finish ? "killing move" : "melee", kind,
            amount, before);
    }
}

// replaces "call 0x40CF80" at the hooked sites: the hit itself is the game's (and ROTK Fix's), unchanged
typedef char(__cdecl* DamageFn)(uint32_t, int, uint32_t);
static const uint32_t* volatile g_hitData ASM_NAME(g_hitData);   // the hit's data at 0x404D12 / 0x404D5B (DamageWithData), else null
static char __cdecl Damage(uint32_t victim, int amount, uint32_t attacker) ASM_NAME(Damage);

static char __cdecl Damage(uint32_t victim, int amount, uint32_t attacker)
{
    const uint32_t* data = g_hitData;
    g_hitData = nullptr;
    int kind = -1;
    // the data's own attacker must be this one ({attacker, kind, damage...}), or the kind is not trusted
    if (data) Guard([&] { if (data[0] == attacker) kind = (int)data[1]; });
    const int before = victim ? SafeLife(victim) : 0;
    const char r = GameFn((DamageFn)0x40CF80)(victim, amount, attacker);
    if (g_enabled && g_inLevel && victim && attacker && before > 0)
        OnHit(victim, attacker, SafeLife(victim), amount, before, kind);
    return r;
}

// --- kicks (04/11/2026) -------------------------------------------------------------------------------------------
// The physical attack (state 0x1E "phys") is a melee hit of kind 6 without damage (log of 04/11/2026: Faramir's 14
// kicks, 9 of them "hit by ... kind 6, damage 0" on another player), so the damage hook never sees it. 0x4185C0 sends
// every melee hit, to enemies and players alike, with 0x401780(victim, 0x259, data {attacker, kind, damage, ...},
// &result, attacker) at 0x418758: that call is replaced. A hit of kind 6 by a player is a kick that landed, once a kick
// (the same player's hits within 0.4 s are one kick on several targets); on another player, a kick to an opponent.
// The same call also tells the speed and fierce attacks that hit (05/11/2026): the hit has landed when the victim's
// handler leaves the result at 1 (0x418729 sets it; a shield that stops the blow clears it, 0x41C4A0 "shieldhit"), the
// victim is an enemy or, in a versus game, another player, and the attacker is in a speed attack (states 0x08-0x0D) or
// a fierce one (0x0E, 0x0F, 0x10, 0x1D): one per swing (per entry of the state, g_stateSince), whatever it hits.
typedef uint32_t(__cdecl* SendEventFn)(uint32_t, int, int*, char*, uint32_t);
static DWORD g_stateSince[4];          // when each player entered its current state (NoteCombos)
static int   CharStateOf(uint32_t c);  // (further down, with the combos)
static void  NoteCombos();

// a character of the enemy side: bit 27 of +0x8C (a character) without bit 0 of +0x5E8 (the heroes' side), as the
// shields' handler 0x41C4A0 tells the sides apart
static bool IsEnemyChar(uint32_t c)
{
    bool e = false;
    Guard([&] { e = *(const int16_t*)(c + 0x7A) != 99 && (*(const uint32_t*)(c + 0x8C) >> 27 & 1) && !(*(const uint8_t*)(c + 0x5E8) & 1); });
    return e;
}

static uint32_t __cdecl MeleeHit(uint32_t victim, int ev, int* data, char* result, uint32_t attacker)
{
    const uint32_t r = GameFn((SendEventFn)0x401780)(victim, ev, data, result, attacker);
    if (g_enabled && g_inLevel && ev == 0x259 && data && attacker)
        Guard([&] {
            const int a = PlayerSlot(attacker);
            if (a < 0 || (result && !*result)) return;   // not a player's, or stopped
            const int v = PlayerSlot(victim);
            if (v == a || (v >= 0 && !g_levelPvp) || (v < 0 && !IsEnemyChar(victim))) return;   // not an opponent
            Live& l = g_live[a];
            const DWORD now = GetTickCount();
            if (data[1] == 6) {   // a kick
                if (!l.lastKickHit || now - l.lastKickHit > 400) ++l.kicksHit;
                l.lastKickHit = now | 1;
                if (v >= 0) ++l.pvpKicks;
                return;
            }
            NoteCombos();   // (the state may have changed since the frame began)
            const int st = CharStateOf(attacker);
            const bool speed = st >= 0x08 && st <= 0x0D, fierce = st == 0x0E || st == 0x0F || st == 0x10 || st == 0x1D;
            if ((!speed && !fierce) || l.swingHit == g_stateSince[a]) return;
            l.swingHit = g_stateSince[a];
            ++(speed ? l.speedHits : l.fierceHits);
        });
    return r;
}

// --- shields broken (05/11/2026) ---------------------------------------------------------------------------------
// The hit handlers of the enemies with a shield (0x41C4A0 orcs, 0x421720 the others; both with an EBP frame, the event
// at [ebp+0xC]) keep the shield's strength at +0x808: the hit that takes it to zero hides the shield in the model
// ("01SHIELD") and starts the effect "orcshieldbreak", 0x401650 at 0x41C941 and 0x4016B0 at 0x421A0C. Those two calls go
// through a stub that first credits the break to the hit's attacker (the event's data {attacker, kind, ...}: a player, or
// the owner of its arrow or spear), whatever the attack (fierce, special, the last blow of a combo).
static void __cdecl NoteShieldBreak(uint32_t ev) ASM_NAME(NoteShieldBreak);
static void __cdecl NoteShieldBreak(uint32_t ev)
{
    if (!g_enabled || !g_inLevel || !ev) return;
    Guard([&] {
        if (*(const int*)(ev + 8) != 0x259) return;
        const uint32_t* data = *(const uint32_t* const*)(ev + 0xC);
        const int a = data ? AttackerSlot(data[0]) : -1;
        if (a < 0) return;
        ++g_live[a].shields;
        if (TraceOk()) Log("players: player %d breaks a shield (kind %X)", a + 1, data[1]);
    });
}

static NAKED void ShieldBreakA()   // in place of "call 0x401650" at 0x41C941
{
    ASM_BEGIN
    ASM(push dword ptr [ebp + 0xC])
    ASM(call NoteShieldBreak)
    ASM(add  esp, 4)
    ASM(push 0x401650)
    ASM(ret)
    ASM_END
}

static NAKED void ShieldBreakB()   // in place of "call 0x4016B0" at 0x421A0C
{
    ASM_BEGIN
    ASM(push dword ptr [ebp + 0xC])
    ASM(call NoteShieldBreak)
    ASM(add  esp, 4)
    ASM(push 0x4016B0)
    ASM(ret)
    ASM_END
}

// at 0x404D12 and 0x404D5B EDI is the hit's data: noted, then the call goes on to Damage with the same stack
static NAKED void DamageWithData()
{
    ASM_BEGIN
    ASM(mov  dword ptr [g_hitData], edi)
    ASM(jmp  Damage)
    ASM_END
}

// The game credits a kill to a player (HeroIndex.cpp, 0x40D110: index in EAX = the player's slot, +0x690). Some kills
// never reach the hooked hits: the hits on trolls and bosses are applied with no attacker (0x41FE56 in 0x41FD20,
// 0x426CA5...) and the game gives the kill to a player afterwards (log of 01/11/2026: the Palantirs' trolls were not
// counted, 357 kills against the game's 360). Those are counted here, once per enemy: not when a hit already counted it
// in the last 3 seconds (the game's credit follows the hit), nor twice (the "share experience" code credits both).
void __cdecl PlayersGameKill(int index, uint32_t victim)
{
    if (!g_enabled || !g_inLevel || !victim || index < 0 || index > 3 || PlayerSlot(victim) >= 0) return;
    if (SafeLife(victim) > 0) return;   // a credit for a living enemy is not a kill
    const DWORD now = GetTickCount();
    const auto it = g_killedAt.find(victim);
    if (it != g_killedAt.end() && now - it->second < 3000) return;
    if (g_killedAt.size() > 512) g_killedAt.clear();
    g_killedAt[victim] = now ? now : 1;
    ++g_live[index].kills;
    NoteMulti(index, now);
    const int foe = FoeType(victim);
    if (foe >= 0) ++g_foes[index][foe];
    DescribeFoe(victim, foe, 0);
    // a troll or a boss felled within 4 s of the player's spear throw: the spear did it (their hits have no attacker,
    // see above; the trolls of the Southern Gate and of Minas Tirith fall to spears)
    const bool spear = g_live[index].lastSpear && now - g_live[index].lastSpear < 4000;
    if (spear) { ++g_live[index].ranged; ++g_live[index].spear; }
    if (TraceOk()) {
        Log("players: kill by player %d credited by the game (enemy %08X, type %X): no hit seen%s", index + 1, victim, foe,
            spear ? ", after a spear throw" : "");
    }
}

// --- the levels won, by name ------------------------------------------------------------------------------------
// The levels of the campaign in its order (the file codes of Data/Game; "pat02" also holds pat03 and "cir00" cir01,
// loaded as the same level) and the two Palantirs. A level won counts as Won_<code>, unless it was ended with F9 (the
// test key of [Diag] VictoryKey): skipping a level is not completing it. On the page each level is a row with the game's
// own name for it (the code is also its text key, see PlayersLevelStart).
static const char* const kLevels[] = { "hel01", "ise01", "min00", "min04", "pat01", "pat02", "min02", "pel02", "osg01",
                                       "she01", "cir00", "bla01", "cra01", "pal01", "pal02" };
enum { ROW_LEVEL = 3000 };   // rows ROW_LEVEL + i of the statistics page are the levels of kLevels
static bool g_levelF9;       // F9 ended this level

void PlayersVictoryKey()
{
    if (!g_inLevel) return;
    g_levelF9 = true;
    Log("players: level %s ended with F9 (not counted as completed)", g_levelCode.c_str());
}

// The level's name in the game's own words: the game's text for its code, with what is not a letter at its ends and
// any control character taken away. Three codes are not keys of the game's texts (03/11/2026: the Pelennor and the two
// Palantir had empty rows; 04/11/2026: the game answered "pel02_ITA" and "pal01_ITA", the key and its language, for a
// text it does not have), so those have a name of ours; any other code with no usable text shows the code. What the
// game answered is written to the log once per session, to see what it gives.
static std::string LevelName(int i)
{
    static std::string names[sizeof kLevels / sizeof *kLevels];
    if (names[i].empty()) {
        const std::string raw = LangGameText(kLevels[i]);
        std::string t, shown;
        for (unsigned char c : raw) {
            t += c < 0x20 ? ' ' : (char)c;
            if (c >= 0x20 && c < 0x7F) shown += (char)c;
            else { char hex[8]; sprintf_s(hex, "\\x%02X", c); shown += hex; }
        }
        while (!t.empty() && t.back() == ' ') t.pop_back();
        while (!t.empty() && t.front() == ' ') t.erase(0, 1);
        bool letter = false;
        for (unsigned char c : t) letter |= (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0;
        const char* code = kLevels[i];
        const std::string missing = std::string(code) + "_";   // "pel02_ITA": the key back, with the language
        if (!letter || t == code || t.compare(0, missing.size(), missing) == 0)
            t = !strcmp(code, "pel02") ? Txt(T_LEVEL_PEL) : !strcmp(code, "pal01") ? Txt(T_LEVEL_PAL1)
              : !strcmp(code, "pal02") ? Txt(T_LEVEL_PAL2) : std::string(code);
        names[i] = t;
        Log("players: level name %s: the game's text \"%s\", shown \"%s\"", code, shown.c_str(), t.c_str());
    }
    return names[i];
}

// --- the heroes' own event handler (0x43A080, put at +0x384 of every hero by 0x43BA2D) ---------------------------
// Two of its events are counted here, around the game's own handling, which is not changed:
//  - (0x259, a hit, is dropped when +0xD0 -> +0x4D8 is set: that is Gandalf's shield, "GAN_Shield1" in 0x4307D0, not a
//    parry; log of 02/11/2026 afternoon: no parry counted in four levels. The parries are counted from the states, in
//    NoteCombos.)
//  - 0xD5, something picked up (the hero alive): its type is the event's first float, and the game writes 1.0 to the
//    second when it took effect: 1 health +30, 2 health +60, 4 5 6 8 +20 to +0xD0 -> +0x68 (the shots of the ranged
//    attack, capped at +0x6A: arrows), 7 experience +1000. A thing that keeps sending it is counted once every 3 seconds; the
//    things dropped by enemies all come from no sender (-1, log of 02/11/2026), so for them it is 0.3 s.
typedef char(__cdecl* HeroEventFn)(uint32_t, uint32_t, uint32_t);
static int g_eventsLogged;   // log lines of this level (parries and pick-ups)

// the event's id, and the data of a pick-up
static int EventId(uint32_t ev)
{
    int v = -1;
    Guard([&] { v = *(const int*)(ev + 8); });
    return v;
}

static bool PickupData(uint32_t ev, float& type, float& done, int& sender)
{
    return Guard([&] {
        type = **(const float* const*)(ev + 0xC);
        done = **(const float* const*)(ev + 0x10);
        sender = *(const int*)(ev + 0x18);
    });
}

// a hit on a hero (before the game handles it): who, from what, the kind and the damage, and the life before
static void NoteHitTaken(uint32_t hero, uint32_t ev)
{
    const int s = PlayerSlot(hero);
    if (s < 0) return;
    uint32_t data = 0, from = 0, kind = 0, dmg = 0;
    if (!ReadU32(ev + 0xC, data) || !ReadU32(data, from) || !ReadU32(data + 4, kind) || !ReadU32(data + 8, dmg)) return;
    const int type = from && Readable(from + 0x78, 2) ? FoeType(from) : -1;
    if (TraceOk()) Log("players: player %d hit by %08X type %X, kind %X, damage %d, life %d", s + 1, from, type, kind,
                       (int)dmg, SafeLife(hero));
}

// the shots of the ranged attack, the number by the sword in the HUD (+0xD0 -> +0x68)
static int Shots(uint32_t hero)
{
    uint32_t d = 0;
    if (!ReadU32(hero + 0xD0, d) || !Readable(d + 0x68, 2)) return -1;
    int v = -1;
    Guard([&] { v = *(const int16_t*)(uintptr_t)(d + 0x68); });
    return v;
}

static char __cdecl HeroEvent(uint32_t self, uint32_t hero, uint32_t ev)
{
    int id = -1;
    if (g_enabled && g_inLevel) id = EventId(ev);
    if (id == 0x259) NoteHitTaken(hero, ev);
    const int lifeWas = id == 0xD5 ? SafeLife(hero) : 0, shotsWas = id == 0xD5 ? Shots(hero) : 0;
    const char r = GameFn((HeroEventFn)0x43A080)(self, hero, ev);
    const int s = id >= 0 ? PlayerSlot(hero) : -1;
    if (s < 0) return r;
    int& logged = g_eventsLogged;   // per level
    if (id == 0xD5) {
        float type = 0.0f, done = 0.0f;
        int sender = 0;
        if (!PickupData(ev, type, done, sender)) return r;
        const int t = (int)(type + 0.5f);
        static std::map<std::pair<int, int>, DWORD> recent;   // (sender, type) -> when it was counted
        const DWORD now = GetTickCount();
        DWORD& last = recent[{ sender, t }];
        const bool count = done == 1.0f && t >= 1 && t <= 8 && (!last || now - last > (sender == -1 ? 300u : 3000u));
        if (count) { last = now | 1; ++g_live[s].pick[t]; }
        if (recent.size() > 256) recent.clear();
        static std::map<std::pair<int, int>, int> idle;   // (sender, type) -> times without effect
        if (g_eventsLogged == 0) idle.clear();
        if ((count || (done != 1.0f && ++idle[{ sender, t }] <= 5)) && TraceOk()) {
            ++logged;
            Log("players: player %d picks up type %d from %X (%s), life %d -> %d, shots %d -> %d", s + 1, t, sender,
                count ? "counted" : done == 1.0f ? "again within 3 s" : "no effect", lifeWas, SafeLife(hero), shotsWas,
                Shots(hero));
        }
    }
    return r;
}

// --- enemies set on fire (02/11/2026) ---------------------------------------------------------------------------
// When a character catches fire its handler sets the burning time (+0x854), who set it on fire (+0x850: the attacker if
// it is a hero, otherwise a hero picked by 0x4018A0 / 0x4101A0) and, when it is not burning already (+0x8AC = 0), puts
// the "CharFire" effect on it: 0x41A640(character, joint, "CharFire", &+0x8AC) at 0x403111, 0x405755, 0x405DA2 and
// 0x41BDA2; the trolls' handler (0x41FD20, hits of kind 0x13 / 0x14) calls 0x401650("CharFire", joint, character, 0) at
// 0x41FE01, and its attacker is the one of the hit, *(+0x8F8) + 4. Those calls are replaced: the enemy is counted for
// that hero (once in 30 s: the fire lasts 20), then the effect is put on as before.
static std::unordered_map<uint32_t, DWORD> g_burnedAt;   // this level: enemy -> when it was counted

static void NoteFire(uint32_t c, bool troll)
{
    if (!g_enabled || !g_inLevel || !c || PlayerSlot(c) >= 0) return;
    uint32_t by = 0, hit = 0;
    if (troll) { if (ReadU32(c + 0x8F8, hit)) ReadU32(hit + 4, by); }
    else ReadU32(c + 0x850, by);
    const int s = by && Readable(by, 0x700) ? AttackerSlot(by) : -1;
    const DWORD now = GetTickCount();
    DWORD& last = g_burnedAt[c];
    const bool count = s >= 0 && (!last || now - last > 30000);
    if (count) { last = now | 1; ++g_live[s].burned; }
    if (g_burnedAt.size() > 512) g_burnedAt.clear();
    if (TraceOk()) Log("players: enemy %08X type %X set on fire by %08X (player %d)%s", c, FoeType(c), by, s + 1,
                       count ? "" : ", not counted");
}

typedef uint32_t(__cdecl* AttachFxFn)(uint32_t, uint32_t, uint32_t, uint32_t);
static uint32_t __cdecl CharFire(uint32_t c, uint32_t joint, uint32_t fx, uint32_t slot)
{
    NoteFire(c, false);
    return GameFn((AttachFxFn)0x41A640)(c, joint, fx, slot);
}

static uint32_t __cdecl TrollFire(uint32_t fx, uint32_t joint, uint32_t c, uint32_t x)
{
    NoteFire(c, true);
    return GameFn((AttachFxFn)0x401650)(fx, joint, c, x);
}

// --- ladders pushed down ---------------------------------------------------------------------------------------
// A siege ladder's animation has the tag "ladder kill on" when it falls (the climbers die): the game's handler, 0x45D910
// (registered with that name at 0x45E45C), only sets 0x100 at +0x5E8 of the part's owner (+0xB8). It is replaced by the
// same code plus a count for the player nearest to the ladder (positions at +0x30, +0x34, +0x38), once per ladder in 3 s.
// The first 30 of a level are written with the distance, to check the counting against what was seen on screen.
static int g_laddersLogged;

static bool Position(uint32_t e, float p[3])
{
    return Guard([&] { p[0] = *(const float*)(e + 0x30); p[1] = *(const float*)(e + 0x34); p[2] = *(const float*)(e + 0x38); });
}

static void NoteLadder(uint32_t ladder)
{
    if (!g_enabled || !g_inLevel) return;
    static uint32_t last; static DWORD lastAt;
    const DWORD now = GetTickCount();
    if (ladder == last && now - lastAt < 3000) return;
    last = ladder; lastAt = now;
    float lp[3];
    if (!Position(ladder, lp)) return;
    int best = -1; float bestD = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float pp[3];
        if (!g_players[i] || !Position(g_players[i], pp)) continue;
        const float d = sqrtf((pp[0] - lp[0]) * (pp[0] - lp[0]) + (pp[1] - lp[1]) * (pp[1] - lp[1]) + (pp[2] - lp[2]) * (pp[2] - lp[2]));
        if (best < 0 || d < bestD) { best = i; bestD = d; }
    }
    if (best >= 0) ++g_live[best].ladders;
    if (TraceOk()) {
        ++g_laddersLogged;
        Log("players: a ladder falls (%08X), nearest player %d at %.1f", ladder, best + 1, bestD);
    }
}

static void __cdecl LadderKillOn(uint32_t part, uint32_t)
{
    uint32_t e = 0;
    if (!Guard([&] {
            e = *(const uint32_t*)(part + 0xB8);
            if (e) *(uint32_t*)(e + 0x5E8) |= 0x100;
        }))
        return;
    if (e) NoteLadder(e);
}

// --- what the level does, for finding catapults, explosions and the like (02/11/2026) --------------------------
// The catapults change nothing in the hero (log of the Southern Gate: no state while firing): the level's script moves
// them. So, with [Log] Detail=1, two things are written with the time, to be matched with the screenshots:
//  - the script calls (0x48ED90, hooked in Support.cpp): each function (its id and code) the first 3 times in a level,
//    with its first three values; at the end of the level the rare ones (at most 60 calls) with how many times;
//  - the animation tags (0x45E280, the dispatcher of "radius damage", "Parts: Explode", "shake camera", "ArrowShoot"...,
//    53 names registered at 0x45E3xx): each name per type of owner the first 3 times, and all of them counted at the end.
//    Sound, footsteps and collision tags are only counted.
static std::map<uint64_t, int> g_calls;            // this level: (id << 32 | code) -> calls
static std::map<std::string, int> g_tags;          // this level: "name/type" -> times

static bool CallArgs(const float* a, float out[3])
{
    if (!a || !Readable((uint32_t)(uintptr_t)a, 12)) return false;
    return Guard([&] { out[0] = a[0]; out[1] = a[1]; out[2] = a[2]; });
}

void PlayersScriptCall(int id, uint32_t code, const float* args)
{
    if (!g_enabled || !g_inLevel || !LogDetail()) return;
    const uint64_t key = (uint64_t)(uint32_t)id << 32 | code;
    int& n = g_calls[key];
    if (++n > 100 || !TraceOk()) return;
    float v[3];
    if (CallArgs(args, v)) Log("trace: script %X/%08X (%g %g %g)", id, code, v[0], v[1], v[2]);
    else Log("trace: script %X/%08X", id, code);
}

// Siege engines are not counted (03/11/2026). In the Pelennor a ballista is an object of type 0x11 whose animation has
// "Parts: Hide Part" when it fires (the bolt leaves) and "Parts: Show Part" when it is loaded again; the Southern Gate's
// catapult fires with no tag at all, only with calls of the level's script (0/97 on object 4009, 2.7 s later 0/3F on
// 4008, with one shot only: not enough to tell which call is the shot). Counting the ballistas alone was half a
// statistic, so the row was taken out; with Detail=1 the traces (tags here, the script calls above) still show both.
static void NoteTag(uint32_t part, uint32_t tag)
{
    if (!g_enabled || !g_inLevel || !LogDetail()) return;
    uint32_t namep = 0, owner = 0;
    char name[41];
    if (!ReadU32(tag + 4, namep) || !NameAt(namep, name)) return;
    ReadU32(part + 0xB8, owner);
    const int type = owner && Readable(owner + 0x78, 2) ? FoeType(owner) : -1;
    char key[64];
    sprintf_s(key, "%s/%X", name, type);
    int& n = g_tags[key];
    ++n;
    static const char* const quiet[] = { "audio:", "play SFX", "stop SFX", "Enable SFX", "footsteps", "weapon collision",
                                         "collision on", "collision off", "hold last", "controller buttons" };
    for (const char* q : quiet) if (!strncmp(name, q, strlen(q))) return;
    if (n > 100 || !TraceOk()) return;
    Log("trace: tag \"%s\" on %08X (type %X)", name, owner, type);
}

static NAKED void AnimTagOrig()   // 0x45E280: mov al,[5A5BB0h]
{
    ASM_BEGIN
    ASM(mov  al, byte ptr ds:[0x5A5BB0])
    ASM(push 0x45E285)
    ASM(ret)
    ASM_END
}

static void __cdecl AnimTag(uint32_t part, uint32_t tag)
{
    NoteTag(part, tag);
    GameFn((void(__cdecl*)(uint32_t, uint32_t))&AnimTagOrig)(part, tag);
}

// The effects the game puts on characters and objects (03/11/2026): 0x401650(name, joint, owner, x), 25 callers (fire,
// sparks, "CatHitBig" of the Southern Gate's catapults, explosions...). The campaign's catapults were fired before the
// trace existed and leave nothing else in the logs (no state, no pick-up), so with [Log] Detail=1 every effect's name is
// written with its owner's type the first 3 times in a level, and all of them counted at the end (as the tags).
static void NoteEffect(uint32_t namep, uint32_t owner)
{
    if (!g_enabled || !g_inLevel || !LogDetail()) return;
    char name[41];
    if (!NameAt(namep, name)) return;
    const int type = owner && Readable(owner + 0x78, 2) ? FoeType(owner) : -1;
    char key[64];
    sprintf_s(key, "fx %s/%X", name, type);
    if (++g_tags[key] > 3 || !TraceOk()) return;
    Log("trace: effect \"%s\" on %08X (type %X)", name, owner, type);
}

static NAKED void EffectOrig()   // 0x401650: sub esp,10h; push ebx; mov ebx,[esp+20h]
{
    ASM_BEGIN
    ASM(sub  esp, 0x10)
    ASM(push ebx)
    ASM(mov  ebx, dword ptr [esp + 0x20])
    ASM(push 0x401658)
    ASM(ret)
    ASM_END
}

static uint32_t __cdecl Effect(uint32_t name, uint32_t joint, uint32_t owner, uint32_t x)
{
    NoteEffect(name, owner);
    return GameFn((uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t))&EffectOrig)(name, joint, owner, x);
}

// the end of a level: the rare script calls and all the tags, a dozen per line
static void LogTrace()
{
    if (!LogDetail()) return;
    std::string line;
    int k = 0;
    for (const auto& c : g_calls) {
        char item[48];
        sprintf_s(item, " %X/%08X x%d", (int)(c.first >> 32), (uint32_t)c.first, c.second);
        line += item;
        if (++k % 12 == 0) { Log("trace: script calls of the level:%s", line.c_str()); line.clear(); }
    }
    if (!line.empty()) Log("trace: script calls of the level:%s", line.c_str());
    line.clear(); k = 0;
    for (const auto& t : g_tags) {
        line += " " + t.first + " x" + std::to_string(t.second);
        if (++k % 10 == 0) { Log("trace: tags of the level:%s", line.c_str()); line.clear(); }
    }
    if (!line.empty()) Log("trace: tags of the level:%s", line.c_str());
}

// from Pvp.cpp (DieEnter), co-op and versus
void PlayersOnFall(uint32_t c)
{
    const int s = PlayerSlot(c);
    if (!g_enabled || !g_inLevel || s < 0) return;
    if (g_levelPvp) ++g_live[s].pvpFalls; else ++g_live[s].deaths;
}

// from Pvp.cpp when a versus game ends: the winners' slots as bits (0 = a draw), the kind of game (counted when the
// level is left) and how it ended, with its teams (the Last game)
static PvpResult g_pvpRes;
static int       g_pvpTeam[4] = { -1, -1, -1, -1 };

void PlayersPvpOver(const PvpResult& r)
{
    if (!g_inLevel || !g_levelPvp) return;
    g_pvpWinner = r.winners ? 99 : -1;
    g_pvpWinners = r.winners;
    g_pvpTeamGame = r.teams;
    g_pvpTimedGame = r.timed;
    g_pvpTourney = r.rounds > 1;
    g_pvpRes = r;
    for (int s = 0; s < 4; ++s) g_pvpTeam[s] = PvpTeamOf(s);
}

// from Pvp.cpp at the end of every round of a tournament (test of 04/11/2026: a single game counted as a round, the
// same number as the games): who played it, who won it
void PlayersPvpRound(unsigned winners)
{
    if (!g_enabled || !g_inLevel || !g_levelPvp) return;
    for (int s = 0; s < 4; ++s) {
        if (!g_live[s].present) continue;
        ++g_live[s].rounds;
        if (winners >> s & 1) ++g_live[s].roundWins;
    }
}

// from Pvp.cpp: a player knocked down an opponent on its last life
void PlayersPvpElim(int attacker)
{
    if (g_enabled && g_inLevel && g_levelPvp && attacker >= 0 && attacker < 4) ++g_live[attacker].pvpElims;
}

// --- end-of-level summary ("Last game") ------------------------------------------------------------------------
// a row: name, hero, kills (knock-downs in a versus game), assists, deaths (falls); a versus game's team (-1 none), won,
// rounds won (a tournament), kicks to opponents
struct SumRow { std::string name; int hero, k, a, d, team = -1; bool win = false; int rounds = 0, kicks = 0; };
static std::vector<SumRow> g_sum;
static std::string g_sumLevel;   // the level's code
static std::string g_sumMode, g_sumResult;   // a versus game: what kind ("Teams, heroes only, ...") and how it ended
static bool g_sumTourney;
static int  g_sumKind;      // 0 none, 1 level won, 2 level left, 3 versus game
static bool g_sumShow;      // draw it on the score screen that follows
static uint8_t g_sumTpl[0xB0];   // the HUD's text element, copied while the level is played (Hud.cpp)
static bool    g_sumTplOk;

static void SaveSummary()
{
    std::vector<std::pair<std::string, std::string>> p;
    p.emplace_back("Kind", std::to_string(g_sumKind));
    p.emplace_back("Level", g_sumLevel);
    p.emplace_back("Count", std::to_string(g_sum.size()));
    if (!g_sumMode.empty()) p.emplace_back("Mode", g_sumMode);
    if (!g_sumResult.empty()) p.emplace_back("Result", g_sumResult);
    if (g_sumTourney) p.emplace_back("Tournament", "1");
    for (size_t i = 0; i < g_sum.size(); ++i) {
        const SumRow& r = g_sum[i];
        p.emplace_back("R" + std::to_string(i), r.name + "|" + std::to_string(r.hero) + "|" + std::to_string(r.k) + "|" +
                                                std::to_string(r.a) + "|" + std::to_string(r.d) + "|" + std::to_string(r.team) +
                                                "|" + (r.win ? "1" : "0") + "|" + std::to_string(r.rounds) + "|" +
                                                std::to_string(r.kicks));
    }
    WritePairs("LastGame", p);
}

static void LoadSummary()
{
    g_sum.clear();
    g_sumKind = 0;
    g_sumLevel.clear();
    g_sumMode.clear();
    g_sumResult.clear();
    g_sumTourney = false;
    for (const auto& kv : ReadPairs("LastGame")) {
        if (kv.first == "Kind") g_sumKind = atoi(kv.second.c_str());
        else if (kv.first == "Level") g_sumLevel = kv.second;
        else if (kv.first == "Mode") g_sumMode = kv.second;
        else if (kv.first == "Result") g_sumResult = kv.second;
        else if (kv.first == "Tournament") g_sumTourney = kv.second == "1";
        else if (kv.first.size() > 1 && kv.first[0] == 'R' && isdigit((unsigned char)kv.first[1])) {
            SumRow r{ "", -1, 0, 0, 0 };
            char buf[160];
            strncpy_s(buf, kv.second.c_str(), _TRUNCATE);
            char* ctx = nullptr;
            const char* f[9] = {};
            for (int i = 0; i < 9; ++i) f[i] = strtok_s(i ? nullptr : buf, "|", &ctx);
            if (!f[4]) continue;
            r.name = f[0]; r.hero = atoi(f[1]); r.k = atoi(f[2]); r.a = atoi(f[3]); r.d = atoi(f[4]);
            if (f[8]) { r.team = atoi(f[5]); r.win = atoi(f[6]) != 0; r.rounds = atoi(f[7]); r.kicks = atoi(f[8]); }   // (since 1.4.0)
            g_sum.push_back(r);
        }
    }
}

static std::string SumLine(const SumRow& r)
{
    std::string s = Txt(g_sumKind == 3 ? T_SUM_PVP_LINE : T_SUM_LINE);
    s = Replace(s, "{name}", r.name);
    s = Replace(s, "{hero}", r.hero >= 0 && r.hero <= 8 ? Txt(T_HERO0 + r.hero) : std::string("?"));
    s = Replace(s, "{k}", Num(r.k));
    s = Replace(s, "{a}", Num(r.a));
    s = Replace(s, "{d}", Num(r.d));
    return s;
}

// --- statistics: storage at the end of a level ------------------------------------------------------------------
static bool ReadRecord(int hero, uint32_t out[15])
{
    return Guard([&] { memcpy(out, (const void*)(0x5BE05C + hero * 0x3C), 15 * sizeof(uint32_t)); });
}

static int ReadHeroVar(uintptr_t a) { const int v = *(const int*)a; return v < 0 || v > 8 ? -1 : v; }

// who plays and with which hero, noted every frame while the level is played: when the level end is seen (often on the
// score screen) the game has already taken players 2-4 out of g_players (test of 30/10/2026: only player 1 was saved)
// --- combos (first step: counted by the player's state; the combos' names are still to be matched) ----------------
// The player's states are in the table 0x561060 ({id, name, enter, update, leave, events}, 0x18 bytes each): the combos
// end in the states "combo_1".."combo_12" (ids 0x11-0x1C); the banes are probably "LINK_1" (0x48) or the "ECSO"/"CCSO"
// states (0x3C-0x41). Every frame of a level each player's state is read (+0xD0 -> +4 -> id, as Coop.cpp does); entering
// one of those states is counted (per player, saved per name as Combo<id in hex>) and, the first time in a session,
// written in the log with the state's name and the hero, to match each state with the combo's name.
static std::map<int, int> g_combos[4];   // this level: state id -> times, per player
static int g_lastState[4] = { -1, -1, -1, -1 };

static int CharStateOf(uint32_t c)
{
    int v = -1;
    Guard([&] {
        const uint8_t* d = *(const uint8_t* const*)(c + 0xD0);
        const int* st = *(const int* const*)(d + 4);
        v = st ? *st : *(const int*)(d + 0x10C);
    });
    return v;
}

// 0x2C "rise_attack": the attack while getting up ("Vendetta nascente" in the game's list, upgradeRising1; log of
// 02/11/2026: 31 times in Shelob's lair, where Sam was knocked down 46 times)
static bool ComboState(int st) { return (st >= 0x11 && st <= 0x1C) || st == 0x48 || (st >= 0x3C && st <= 0x41) || st == 0x2C; }

static const char* StateName(int st)
{
    for (const uint32_t* e = (const uint32_t*)0x561060; e < (const uint32_t*)0x5617F8; e += 6)
        if ((int)e[0] == st && e[1]) return (const char*)(uintptr_t)e[1];
    return "?";
}

// The states before a combo tell which buttons made it (speed_attack = quick attack, fierce_attack = fierce attack,
// phys = physical attack, kill = killing move): Orc Hewer is quick, quick, fierce; Swift Justice four quick attacks...
// So each player's last states are kept, and the first three times a combo state is entered in a session the log shows
// the way to it ("speed_attack_1 > speed_attack_2 > combo_7"): enough to give every combo state its name.
static int g_trail[4][6];   // each player's last different states (-1 = none), the latest last

// Every other state too, to find the ones of pushing a ladder down or using a catapult (02/11/2026: the player takes
// a screenshot at that moment, the log gives the state entered at that time). With [Log] Detail=1 the first two times
// each state is entered in a level are written with the way to it, and at the end of the level every player's states
// with how many times they were entered: a state entered as many times as the ladders pushed down is the one.
static std::map<int, int> g_states[4];     // this level: state id -> times entered, per player
static std::map<int, int> g_stateLogged;   // this level: state id -> times written in the log

static std::string Trail(int s)
{
    std::string way;
    for (int i = 0; i < 6; ++i)
        if (g_trail[s][i] >= 0) way += std::string(StateName(g_trail[s][i])) + " > ";
    return way;
}

static void NoteCombos()
{
    static std::map<int, int> logged;
    for (int s = 0; s < 4; ++s) {
        const uint32_t c = g_players[s];
        const int st = c ? CharStateOf(c) : -1;
        if (st != g_lastState[s]) {
            g_stateSince[s] = GetTickCount() | 1;
            if (st >= 0) ++g_states[s][st];
            if (st == 0x44) g_live[s].lastSpear = GetTickCount() | 1;   // spear_throw
            if (st == 0x1E) ++g_live[s].kicks;   // phys: the physical attack (Aragorn's kick, a shove with the staff...)
            // a blow parried: the guard's reactions parry_rear, parry_sweep, parry_left (2), parry_right (2); parry_0,
            // parry_1 and parry_alt are the guard itself (logs of 02/11/2026: they come before LINK_1, the banes)
            if (st >= 0x22 && st <= 0x27) ++g_live[s].parryHits;
            if (st > 3 && !ComboState(st) && LogDetail()) {
                if (g_stateLogged[st] < 2) {
                    ++g_stateLogged[st];
                    Log("players: state %X %s by player %d (%s): %s%s", st, StateName(st), s + 1,
                        g_live[s].hero >= 0 && g_live[s].hero <= 8 ? kHeroNames[g_live[s].hero] : "?", Trail(s).c_str(),
                        StateName(st));
                } else if (TraceOk()) Log("st %d %X %s", s + 1, st, StateName(st));
            }
            if (ComboState(st)) {
                ++g_combos[s][st];
                // the ECSO / CCSO states (any object the hero works: a winch, the oil at the Southern Gate...) every
                // time, to match them with what the player did
                if (logged[st] < (st >= 0x3C && st <= 0x41 ? 30 : 3) || TraceOk()) {
                    ++logged[st];
                    Log("players: combo state %X by player %d (%s): %s%s", st, s + 1,
                        g_live[s].hero >= 0 && g_live[s].hero <= 8 ? kHeroNames[g_live[s].hero] : "?", Trail(s).c_str(),
                        StateName(st));
                }
            }
            if (st >= 0) {   // the trail: the states of the last moments (idle and walking start it again)
                if (st <= 3) for (int& t : g_trail[s]) t = -1;
                else { memmove(g_trail[s], g_trail[s] + 1, 5 * sizeof(int)); g_trail[s][5] = st; }
            }
        }
        g_lastState[s] = st;
    }
}

static void NotePlayers()
{
    const bool coop = *(const int*)0x562A98 == 1;
    const int heroes[4] = { ReadHeroVar(0x562A84), coop ? ReadHeroVar(0x562A88) : -1, HeroOfSlot(2), HeroOfSlot(3) };
    for (int s = 0; s < 4; ++s)
        if (heroes[s] >= 0 && (s == 0 || g_players[s] != 0)) { g_live[s].present = true; g_live[s].hero = heroes[s]; }
}

// from Coop.cpp (LoadPackages) when a game level (not the menu) is being loaded; level = the code of its file
// ("min02" for Data/Game/Min/Min02.scw), which is also the key of its name in the game's texts ("Portale meridionale")
void PlayersLevelStart(const char* level)
{
    if (!g_enabled) return;
    if (g_inLevel) {
        // loaded again without passing through the score screen, the map or the menus: "Retry" after a defeat. Still
        // the same level: the counters go on (one level played, the kills and deaths of every attempt)
        g_seenPlay = false;
        g_hits.clear();
        Log("players: level loaded again (retry), the counters go on");
        return;
    }
    for (Live& l : g_live) l = Live{ false, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    for (auto& f : g_foes) f.clear();
    for (auto& c : g_combos) c.clear();
    for (auto& c : g_states) c.clear();
    g_stateLogged.clear();
    for (auto& t : g_trail) for (int& x : t) x = -1;
    g_hits.clear();
    g_levelCode = level ? level : "";
    g_inLevel = true;
    g_seenPlay = false;
    // the versus mode stays "on" until Co-op is confirmed again: only a co-op game is a versus game
    g_levelPvp = PvpActive() && *(const int*)0x562A98 == 1;
    g_levelSolo = *(const int*)0x562A98 != 1;   // the co-op flag: Single leaves it at 0
    g_pvpWinners = 0;
    g_pvpTeamGame = g_pvpTimedGame = g_pvpTourney = false;
    g_pvpWinner = -2;
    g_pvpRes = PvpResult();
    for (int& t : g_pvpTeam) t = -1;
    g_levelStart = GetTickCount();
    g_killsLogged = 0;
    g_rangedLogged = 0;
    g_creditLogged = 0;
    g_eventsLogged = 0;
    g_laddersLogged = 0;
    g_calls.clear();
    g_tags.clear();
    g_traceLogged = 0;
    g_burnedAt.clear();
    g_levelF9 = false;
    g_killedAt.clear();
    g_sumShow = false;
    g_sumTplOk = false;   // copied again from this level's HUD
    Log("players: level start %s (%s), names %s / %s / %s / %s", g_levelCode.c_str(),
        g_levelPvp ? "versus" : g_levelSolo ? "single" : "co-op", PlayerName(0).c_str(), PlayerName(1).c_str(),
        PlayerName(2).c_str(), PlayerName(3).c_str());
}

// from Screens.cpp (SetMode) when the level is left: won = towards the score screen
void PlayersLevelLeave(bool won)
{
    if (!g_enabled || !g_inLevel) return;
    LogTrace();
    g_inLevel = false;
    const int secs = (int)((GetTickCount() - g_levelStart) / 1000);
    if (ScreenMode() == 0) NotePlayers();   // left from inside the level (SetMode): the players are still there
    std::vector<SumRow> rows;
    for (int s = 0; s < 4; ++s) {
        Live& l = g_live[s];
        if (!l.present || l.hero < 0 || l.hero > 8) continue;
        const std::string name = PlayerName(s);
        const int h = l.hero;
        // a player without a name is a guest: in the summary, but no statistics (otherwise "Player 3" would collect
        // the statistics of whoever sat in that place)
        const bool keep = PlayerHasName(s);
        Vals v = keep ? ReadVals(name) : Vals();
        auto add = [&](const std::string& k, int d) { if (d) v[k] += d; };
        auto best = [&](const std::string& k, int x) { if (x > Get(v, k)) v[k] = x; };
        add("Seconds", secs);
        add(HeroKey(h, g_levelPvp ? "Pvp" : ""), 1);   // Hero<n> = levels with that hero, Hero<n>Pvp = versus games
        add("Kills", l.kills);            add(HeroKey(h, "Kills"), l.kills);
        add("RangedKills", l.ranged);
        add("KillingMoves", l.finish);
        add("SpearKills", l.spear);
        add("ParriedHits", l.parryHits);
        add("ParriedArrows", l.parryArrows);
        // the ladders: "ladder kill on" (NoteLadder); the siege engines are not counted (NoteTag)
        add("Ladders", l.ladders);
        add("Burned", l.burned);
        add("Kicks", l.kicks);
        add("KicksHit", l.kicksHit);
        add("SpeedHits", l.speedHits);
        add("FierceHits", l.fierceHits);
        add("ShieldsBroken", l.shields);
        Log("players: %s, kicks %d (landed %d, on opponents %d), speed attacks that hit %d, fierce %d, shields broken %d",
            name.c_str(), l.kicks, l.kicksHit, l.pvpKicks, l.speedHits, l.fierceHits, l.shields);
        std::string picks;
        for (int t = 1; t <= 8; ++t)
            if (l.pick[t]) { add("Pick" + std::to_string(t), l.pick[t]); picks += " " + std::to_string(t) + "x" + std::to_string(l.pick[t]); }
        Log("players: %s, spear kills %d, parried %d blows and %d arrows, ladders %d, set on fire %d, picked up:%s",
            name.c_str(), l.spear, l.parryHits, l.parryArrows, l.ladders, l.burned, picks.empty() ? " nothing" : picks.c_str());
        best("BestMulti", l.bestMulti);
        if (g_levelSolo) add("SoloKills", l.kills);   // the kills of the levels played alone (test of 31/10/2026)
        std::string foes;
        for (const auto& f : g_foes[s]) {
            char key[16];
            sprintf_s(key, "Foe%02X", f.first);
            add(key, f.second);
            foes += " " + std::string(key + 3) + "x" + std::to_string(f.second);
        }
        if (!foes.empty()) Log("players: %s, kills by enemy type:%s", name.c_str(), foes.c_str());
        std::string combos;
        for (const auto& c : g_combos[s]) {
            char key[16];
            sprintf_s(key, "Combo%02X", c.first);
            add(key, c.second);
            combos += " " + std::string(key + 5) + "x" + std::to_string(c.second);
        }
        if (!combos.empty()) Log("players: %s, combos by state:%s", name.c_str(), combos.c_str());
        if (LogDetail() && !g_states[s].empty()) {   // all the states entered, a dozen per line (lines are 1 KB at most)
            std::string line;
            int n = 0;
            for (const auto& c : g_states[s]) {
                char item[64];
                sprintf_s(item, " %X %s x%d", c.first, StateName(c.first), c.second);
                line += item;
                if (++n % 12 == 0) { Log("players: %s, states entered:%s", name.c_str(), line.c_str()); line.clear(); }
            }
            if (!line.empty()) Log("players: %s, states entered:%s", name.c_str(), line.c_str());
        }
        add("Assists", l.assists);        add(HeroKey(h, "Assists"), l.assists);
        add("Deaths", l.deaths);          add(HeroKey(h, "Deaths"), l.deaths);
        if (g_levelPvp) {
            if (g_pvpWinner != -2) {
                const bool winner = (g_pvpWinners >> s & 1) != 0;
                add("PvpGames", 1);
                if (winner) {
                    add("PvpWins", 1);
                    const int fast = Get(v, "FastestPvpWin");   // (a game of one round with lives)
                    if (!g_pvpTourney && !g_pvpTimedGame && secs > 0 && (fast == 0 || secs < fast)) v["FastestPvpWin"] = secs;
                }
                if (g_pvpTourney) { add("PvpTourneys", 1); if (winner) add("PvpCups", 1); }
                if (g_pvpTeamGame) { add("PvpTeamGames", 1); if (winner) add("PvpTeamWins", 1); }
                if (g_pvpTimedGame) { add("PvpTimedGames", 1); if (winner) add("PvpTimedWins", 1); best("BestTimedKills", l.pvpKills); }
            }
            add("PvpKills", l.pvpKills);   add(HeroKey(h, "PvpKills"), l.pvpKills);
            add("PvpFalls", l.pvpFalls);   add(HeroKey(h, "PvpFalls"), l.pvpFalls);
            add("PvpElims", l.pvpElims);
            add("PvpRounds", l.rounds);
            add("PvpRoundWins", l.roundWins);
            add("PvpKicks", l.pvpKicks);
            for (int o = 0; o < 4; ++o)   // who downed whom: only another player with a name (a guest has no statistics)
                if (o != s && l.downs[o] > 0 && PlayerHasName(o) && !SameName(PlayerName(o), name))
                    add(DownsKey(PlayerName(o)), l.downs[o]);
        } else {
            add("Levels", 1);
            best("BestKills", l.kills);
            best("BestAssists", l.assists);
            uint32_t r[15];
            if (won && ReadRecord(h, r)) {
                add("Wins", 1);
                add(HeroKey(h, "Wins"), 1);
                if (!g_levelF9 && !g_levelCode.empty()) add("Won_" + g_levelCode, 1);
                add("Experience", (int)*(const float*)(r + 8));
                add("Perfect", (int)r[11]); add("Excellent", (int)r[12]); add("Good", (int)r[13]); add("Fair", (int)r[14]);
                best("BestPerfect", (int)r[11]);
            }
        }
        if (keep) WriteVals(name, v);
        SumRow row{ name, h, g_levelPvp ? l.pvpKills : l.kills, l.assists, g_levelPvp ? l.pvpFalls : l.deaths };
        if (g_levelPvp) {
            row.team = g_pvpTeam[s];
            row.win = (g_pvpWinners >> s & 1) != 0;
            row.rounds = l.roundWins;
            row.kicks = l.pvpKicks;
        }
        rows.push_back(row);
        Log("players: %s (%s) %s after %d s: kills %d (killing moves %d), assists %d, deaths %d, versus knock-downs %d, falls %d%s",
            name.c_str(), kHeroNames[h], g_levelPvp ? "versus game" : won ? "level won" : "level left",
            secs, l.kills, l.finish, l.assists, l.deaths, l.pvpKills, l.pvpFalls, keep ? "" : " (no name: not saved)");
    }
    g_hits.clear();
    if (!rows.empty()) {
        g_sum = rows;
        g_sumKind = g_levelPvp ? 3 : won ? 1 : 2;
        g_sumLevel = g_levelCode;
        g_sumMode.clear();
        g_sumResult.clear();
        g_sumTourney = false;
        if (g_levelPvp && g_pvpWinner != -2) {   // "Teams, heroes only, timed 2 minutes, best of 3"
            const PvpResult& r = g_pvpRes;
            std::string m = Txt(r.teams ? T_MP_TEAMS : T_MP_FFA) + ", " + Txt(r.heroesOnly ? T_MP_HEROES : T_LAST_ENEMIES) + ", ";
            if (r.timed) m += Txt(T_MP_RULE_TIME) + " " + Replace(Txt(T_MP_MINUTES), "{n}", std::to_string(r.minutes));
            else m += Txt(T_MP_RULE_LAST) + " " + LangText(r.lives == 1 ? 24 : 25, r.lives);
            if (r.rounds > 1) m += ", " + Replace(Txt(T_MP_BEST), "{n}", std::to_string(r.rounds));
            g_sumMode = m;
            g_sumResult = r.text;
            g_sumTourney = r.rounds > 1;
        }
        SaveSummary();
        g_sumShow = won;   // the score screen comes only after a victory (a versus game ends with one too)
    }
}

// --- the page in the main menu --------------------------------------------------------------------------------
enum Page { P_CLOSED, P_MAIN, P_EDIT, P_STATS, P_DETAIL, P_RECORDS, P_LAST, P_MANAGE, P_ACTION, P_PICK, P_CONFIRM, P_LINEUP,
            P_CARD };
static Page g_page = P_CLOSED;
static void GlintFrame(int mode);     // the menu's glint on the pages (below, with the drawing)
enum { ROW_STATS = 4, ROW_CARDS, ROW_RECORDS, ROW_LAST, ROW_MANAGE, ROW_BACK, MAIN_ROWS };
static Page g_cardBack = P_STATS;     // where Back goes from a player's card (the statistics or this page)
static bool  g_recap;                 // the Last game over the map after a versus game, until confirm or back
// its button, over the map's own "F1 Help" (test of 05/11/2026: under the button, "F1 Help" and a help line that said
// the same as the button)
static const float kRecapBw = 0.26f, kRecapY = 0.875f, kRecapBx = 0.37f;
static DWORD g_recapAt;
static bool  g_recapDrawn;            // (the first time it is drawn, in the log)
static bool g_slotNew[4];             // the place shows "New name..." (confirm opens the editor; nothing changes before)
static int  g_row;                    // P_MAIN row
static int  g_editSlot;               // the editor: 0..3 a player's name, -1 renaming g_actName, kEditAdd a new name
static const int kEditAdd = -2;
static bool g_editLineup;             // the editor writes a row of "Who is playing?" (g_editSlot = the row)
// "Who is playing?": one row per player of the game about to start; a row holds a name, "" = guest, kNewName = "New
// name..." (confirm opens the editor). Rows g_lineupN and g_lineupN + 1 are Start and Back.
static const char* const kNewName = "\x01";
static std::string g_lineup[4];
static int  g_lineupN;
static int  g_lineupRow;
static bool g_lineupDone;             // Start was confirmed: Screens.cpp runs the menu entry's own confirmation
static int  g_lineupLogged;
// the Multiplayer page (Who is playing? with the game's mode and rules on the left; below, "Multiplayer")
enum { MP_CAMPAIGN, MP_FFA, MP_TEAMS };
enum { MR_MODE = 10, MR_PLAYERS, MR_SPLIT, MR_ENEMIES, MR_RULE, MR_LIVES, MR_TIME, MR_ROUNDS, MR_START = 20, MR_BACK };   // its rows (0..3 the places)
static bool g_multi;                  // the line-up page is the Multiplayer page
static int  g_mpMode, g_mpCoopN = 3, g_mpFfaN = 2, g_mpLives = 2, g_mpSplit;
static bool g_mpEnemies = true, g_mpTimed;
static int  g_mpMinutes = 3, g_mpRounds = 1;
static char g_edit[kMaxName + 1];
static int  g_gridR, g_gridC;         // on-screen letters
struct Totals { std::string name; Vals v; };
static std::vector<Totals> g_table;   // statistics page
static int  g_statsSel;               // selected player on the statistics page
enum { TAB_COUNT = 6 };               // the tabs of the statistics page (kTabRows, DrawStats)
static int  g_statsTab;               // the tab shown (kept while the game runs)
static int  g_statsFirst;             // a tab longer than the page scrolled: its first row shown (DrawStats keeps it in range)
static int  g_statsFirstMax;          // the last first row that still fills the page (DrawStats; 0 = the tab fits)
static void TabStep(int d) { g_statsTab = (g_statsTab + d + TAB_COUNT) % TAB_COUNT; g_statsFirst = 0; MenuIdleReset(); }
static void PlayerStep(int d)
{
    const int n = (int)g_table.size();
    if (n) g_statsSel = (g_statsSel + d + n) % n;
    MenuIdleReset();
}
static std::vector<std::string> g_list;   // names for P_MANAGE / P_PICK
static int  g_listSel;
static std::string g_actName;         // name chosen in P_MANAGE
static int  g_actRow;                 // P_ACTION: 0 rename, 1 merge, 2 delete, 3 back
static int  g_confirm;                // P_CONFIRM: 1 delete, 2 merge into g_list[g_listSel]
static std::string g_mergeTo;
static int  g_yes;                    // P_CONFIRM: 0 yes, 1 no
static DWORD g_typedAt;               // last key typed on the keyboard (the menu's own actions are ignored right after)
static float g_scale = 0.8f, g_tableScale = 0.6f;

static const char* const kGrid[4] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789-.'!" };
static const int kGridRows = 5;       // the four rows above and the row Space / Delete / Done

bool PlayersPageOpen() { return g_page != P_CLOSED; }

static void LoadTable()
{
    g_table.clear();
    const std::vector<std::string> found = StatNames();
    // the current players first, in their order, then everybody else
    std::vector<std::string> order;
    for (int s = 0; s < 4; ++s) {
        const std::string nm = PlayerName(s);
        if (HasName(found, nm) && !HasName(order, nm)) order.push_back(nm);
    }
    for (const std::string& f : found) if (!HasName(order, f)) order.push_back(f);
    for (const std::string& nm : order) g_table.push_back(Totals{ nm, ReadVals(nm) });
    // [Debug] StatsTest=1: three made-up players with long names and very large numbers, to check the pages' layout
    if (IniInt("Debug", "StatsTest", 0)) {
        static const char* const fake[3] = { "Massimiliano", "Bartholomew", "Eowyn" };
        for (int i = 0; i < 3; ++i) {
            Vals v;
            v["Levels"] = 1234 * (i + 1); v["Wins"] = 987 * (i + 1); v["Seconds"] = 3600 * 1234 * (i + 1) + 59 * 60;
            v["Kills"] = 123456 * (i + 1); v["RangedKills"] = 45678; v["KillingMoves"] = 34567; v["SoloKills"] = 56789; v["Foe03"] = 23456; v["Foe2C"] = 3456; v["Foe4E"] = 9; v["Assists"] = 23456; v["Deaths"] = 3456; v["Perfect"] = 12345;
            v["Excellent"] = 23456; v["Good"] = 34567; v["Fair"] = 45678; v["Experience"] = 9876543;
            v["PvpGames"] = 4567; v["PvpWins"] = 2345; v["PvpKills"] = 34567; v["PvpFalls"] = 12345;
            v["BestKills"] = 999; v["BestPerfect"] = 888; v["BestAssists"] = 777; v["FastestPvpWin"] = 61;
            for (int h = 0; h < 9; ++h) {
                v[HeroKey(h, "")] = 1000 + h; v[HeroKey(h, "Wins")] = 900 + h; v[HeroKey(h, "Pvp")] = 100;
                v[HeroKey(h, "Kills")] = 99999; v[HeroKey(h, "Assists")] = 8888; v[HeroKey(h, "Deaths")] = 777;
            }
            g_table.push_back(Totals{ fake[i], v });
        }
    }
    if (g_statsSel >= (int)g_table.size()) g_statsSel = 0;
}

static bool g_keyWas[256];
static bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// the keys held right now count as already seen: the Enter that opened the editor must not also close it
static void SnapshotKeys() { for (int vk = 8; vk < 0xE0; ++vk) g_keyWas[vk] = KeyDown(vk); }

static void Open()
{
    LoadNames();
    g_txtReady = false;   // texts read again at every opening (ROTKPlus.lang.ini may have changed)
    g_page = P_MAIN;
    g_row = 0;
    g_statsSel = 0;
    for (bool& b : g_slotNew) b = false;
    LoadTable();     // the previews on the right (DrawMain) read these: no file reads while drawing
    LoadSummary();
    g_typedAt = GetTickCount();   // the second confirm of the click (or key) that opened it is dropped
    Log("players: page opened");
}

static void Close()
{
    g_page = P_CLOSED;
    g_typedAt = GetTickCount();   // nor does the click that closed it reach the menu under it
    Log("players: page closed");
}

static void OpenManage()
{
    LoadTable();   // the favourite heroes and the times of the list, the preview
    g_list = AllNames();
    if (g_listSel > (int)g_list.size()) g_listSel = 0;   // (the size itself: "Add a name", after the names)
    g_page = P_MANAGE;
}

static void BeginEdit(int slot, const std::string& start)
{
    g_editSlot = slot;
    g_editLineup = false;
    strncpy_s(g_edit, start.c_str(), _TRUNCATE);
    g_gridR = g_gridC = 0;
    g_page = P_EDIT;
    SnapshotKeys();
}

// --- "Who is playing?" ------------------------------------------------------------------------------------------
// Confirming Single, Co-op or Versus in the main menu opens this page first (Screens.cpp, MenuAction): one row per
// player of the game, filled with the last line-up used for that number of players ([Lineup1]..[Lineup4] in
// ROTKPlus_players.ini), so with the same people one confirmation is enough. Left/right changes a row's name among the
// known ones not taken by the other rows, then Guest (no name: "Player N", no statistics) and New name... (the editor).
// Player 1's controller moves the cursor and changes any row; the other controllers change only their own row (the
// menu loop 0x4EAA60 tells which slot an action comes from, [0x8E2111]). Start gives the names to the players, keeps the
// line-up and lets Screens.cpp run the entry's own confirmation; Back returns to the menu.
static bool RealName(const std::string& v) { return !v.empty() && v != kNewName; }

static bool LineupTaken(int row, const std::string& name)
{
    for (int r = 0; r < g_lineupN; ++r) if (r != row && RealName(g_lineup[r]) && SameName(g_lineup[r], name)) return true;
    return false;
}

// a name as written in the list of known names, "" if it is not known (deleted or merged meanwhile)
static std::string KnownSpelling(const std::vector<std::string>& all, const std::string& v)
{
    if (v.empty()) return "";
    for (const std::string& k : all) if (SameName(k, v)) return k;
    return "";
}

// rows from..n-1 of a line-up of n players get the last line-up's names for them (with none yet, those of the
// Fellowship page), and a controller of player 3 or 4 that remembers a name proposes it in its own row
static void FillLineup(int from, int n)
{
    const std::vector<std::string> all = AllNames();
    char ini[MAX_PATH], sec[16], key[8], buf[64];
    PlayersIni(ini, sizeof ini);
    sprintf_s(sec, "Lineup%d", n);
    for (int r = from; r < n; ++r) g_lineup[r].clear();   // (what they held before must not look taken)
    for (int r = from; r < n; ++r) {
        sprintf_s(key, "P%d", r + 1);
        GetPrivateProfileStringA(sec, key, "\x02", buf, sizeof buf, ini);
        const std::string v = KnownSpelling(all, strcmp(buf, "\x02") ? buf : g_name[r]);
        g_lineup[r] = !v.empty() && !LineupTaken(r, v) ? v : "";
    }
    for (int r = (std::max)(2, from); r < n; ++r) {
        const std::string pad = PadKeyOfSlot(r);
        if (pad.empty()) continue;
        GetPrivateProfileStringA("PadNames", pad.c_str(), "", buf, sizeof buf, ini);
        const std::string v = KnownSpelling(all, buf);
        if (!v.empty() && !LineupTaken(r, v)) g_lineup[r] = v;
    }
}

bool PlayersOpenLineup(int n)
{
    if (!g_enabled || !g_askWho || n < 1 || n > 4) return false;
    LoadNames();
    LoadTable();   // the favourite heroes' portraits and the preview (DrawLineup)
    g_txtReady = false;
    g_typedAt = GetTickCount();   // the second confirm of the click that chose the entry is dropped
    for (std::string& r : g_lineup) r.clear();
    g_lineupN = n;
    FillLineup(0, n);
    g_multi = false;
    g_lineupRow = n;   // on Start
    g_lineupDone = false;
    g_lineupLogged = 0;
    g_page = P_LINEUP;
    Log("players: who is playing? (%d) proposed: %s / %s / %s / %s", n, g_lineup[0].c_str(), g_lineup[1].c_str(),
        g_lineup[2].c_str(), g_lineup[3].c_str());
    return true;
}

// --- Multiplayer: the game's mode, its rules and who plays, on one page (04/11/2026) ----------------------------------
// With [Players] MultiplayerPage=1 (default) the main menu's Co-op entry becomes "Multiplayer" and the Versus entry is
// not shown (Screens.cpp): confirming it opens this page instead of Who is playing?. On the left the game: the mode
// (campaign, everybody against everybody, teams), the players (2 to 4; four in teams), the pairs of the teams, the
// enemies (or the heroes only) and the lives; under them the teams, or where the level is chosen. On the right who
// plays: the places of Who is playing? (each with its team in a 2 vs 2); under the two boxes Start and Back. Up/down go
// through the rows of one box and on to Start and Back (test of 04/11/2026: one long list across the two boxes was
// awkward), the shoulder buttons, Tab and Page Up / Page Down go to the other box, left/right change the selected row,
// confirm starts the game from any row (but a "New name..." place, which opens the editor); the other controllers change
// their own place.
// The choices stay in ROTKPlus.ini ([Multiplayer] Mode, [Coop] Players; the versus ones Pvp.cpp writes, PvpConfigure);
// Start hands them to Screens.cpp (PlayersMultiChoice), which runs the Co-op entry's own confirmation.
static const int kSplitTeam1[3] = { 12, 13, 14 };   // the two players of team 1 ([PvP] Team1)

static int MpPlayers() { return g_mpMode == MP_TEAMS ? 4 : g_mpMode == MP_FFA ? g_mpFfaN : g_mpCoopN; }

static int MpTeamOf(int slot)
{
    const int t1 = kSplitTeam1[g_mpSplit];
    return slot == t1 / 10 - 1 || slot == t1 % 10 - 1 ? 0 : 1;
}

// the rows of a box: 0 the game's, 1 who plays (the places)
static void MpRows(int box, std::vector<int>& rows)
{
    rows.clear();
    if (box == 1) { for (int r = 0; r < g_lineupN; ++r) rows.push_back(r); return; }
    rows.push_back(MR_MODE);
    rows.push_back(g_mpMode == MP_TEAMS ? MR_SPLIT : MR_PLAYERS);
    if (g_mpMode != MP_CAMPAIGN) {
        rows.push_back(MR_ENEMIES);
        rows.push_back(MR_RULE);
        rows.push_back(g_mpTimed ? MR_TIME : MR_LIVES);
        rows.push_back(MR_ROUNDS);
    }
}

static int g_mpBox;   // the box the cursor is in, or the last one before Start and Back

static int MpBoxOf(int id) { return id < 4 ? 1 : id < MR_START ? 0 : -1; }   // -1 Start, Back

// the shoulder buttons, Tab, Page Up / Page Down: the other box, on the row at the same height (or its last one)
static void MpSwitchBox()
{
    const int from = MpBoxOf(g_lineupRow);
    std::vector<int> cur, other;
    MpRows(from < 0 ? g_mpBox : from, cur);
    g_mpBox = (from < 0 ? g_mpBox : from) ^ 1;
    MpRows(g_mpBox, other);
    int at = 0;
    for (int i = 0; i < (int)cur.size(); ++i) if (cur[i] == g_lineupRow) at = i;
    if (!other.empty()) g_lineupRow = other[(std::min)(at, (int)other.size() - 1)];
    MenuIdleReset();
}

// the places follow the number of players; a new one gets the last line-up's name for it, if not taken
static void SetLineupN(int n)
{
    if (n == g_lineupN) return;
    const int old = g_lineupN;
    g_lineupN = n;
    if (n > old) FillLineup(old, n);
}

static int Wrap(int v, int lo, int hi) { return v > hi ? lo : v < lo ? hi : v; }

static void MpStep(int id, int dir)
{
    switch (id) {
    case MR_MODE: g_mpMode = Wrap(g_mpMode + dir, MP_CAMPAIGN, MP_TEAMS); break;
    case MR_PLAYERS: {
        int& n = g_mpMode == MP_FFA ? g_mpFfaN : g_mpCoopN;
        n = Wrap(n + dir, 2, 4);
        break;
    }
    case MR_SPLIT: g_mpSplit = Wrap(g_mpSplit + dir, 0, 2); break;
    case MR_ENEMIES: g_mpEnemies = !g_mpEnemies; break;
    case MR_LIVES: g_mpLives = Wrap(g_mpLives + dir, 1, 5); break;
    case MR_RULE: g_mpTimed = !g_mpTimed; break;
    case MR_TIME: g_mpMinutes = g_mpMinutes == 2 ? (dir > 0 ? 3 : 5) : g_mpMinutes == 3 ? (dir > 0 ? 5 : 2) : (dir > 0 ? 2 : 3); break;
    case MR_ROUNDS: g_mpRounds = g_mpRounds == 1 ? (dir > 0 ? 3 : 5) : g_mpRounds == 3 ? (dir > 0 ? 5 : 1) : (dir > 0 ? 1 : 3); break;
    default: return;
    }
    SetLineupN(MpPlayers());
}

static void MpLoad()
{
    g_mpMode = Wrap(IniInt("Multiplayer", "Mode", MP_CAMPAIGN), MP_CAMPAIGN, MP_TEAMS);
    const int coop = IniInt("Coop", "Players", 3), ffa = IniInt("PvP", "Players", 2), lives = IniInt("PvP", "Lives", 2);
    g_mpCoopN = coop < 2 || coop > 4 ? 3 : coop;
    g_mpFfaN = ffa < 2 || ffa > 4 ? 2 : ffa;
    g_mpLives = lives < 1 || lives > 5 ? 2 : lives;
    g_mpEnemies = IniInt("PvP", "Enemies", 1) != 0;
    g_mpTimed = IniInt("PvP", "Rule", 0) == 1;
    const int minutes = IniInt("PvP", "Time", 3), rounds = IniInt("PvP", "Rounds", 1);
    g_mpMinutes = minutes == 2 || minutes == 5 ? minutes : 3;
    g_mpRounds = rounds == 3 || rounds == 5 ? rounds : 1;
    const int t1 = IniInt("PvP", "Team1", 12);
    g_mpSplit = t1 == 13 ? 1 : t1 == 14 ? 2 : 0;
}

bool PlayersMultiEnabled() { return g_enabled && g_askWho && g_multiPage; }
std::string PlayersMultiLabel() { return Txt(T_MP_TITLE); }

bool PlayersOpenMulti()
{
    if (!PlayersMultiEnabled()) return false;
    MpLoad();
    if (!PlayersOpenLineup(MpPlayers())) return false;
    g_multi = true;
    g_lineupRow = MR_START;
    g_mpBox = 0;
    Log("players: multiplayer page, mode %d, %d players, enemies %d, lives %d, team 1 = %d", g_mpMode, MpPlayers(),
        g_mpEnemies, g_mpLives, kSplitTeam1[g_mpSplit]);
    return true;
}

// after Start on the Multiplayer page: what Screens.cpp starts
bool PlayersMultiChoice(MultiChoice& m)
{
    if (!g_multi) return false;
    m.players = MpPlayers();
    m.versus = g_mpMode != MP_CAMPAIGN;
    m.teams = g_mpMode == MP_TEAMS;
    m.enemies = g_mpEnemies;
    m.timed = g_mpTimed;
    m.lives = g_mpLives;
    m.minutes = g_mpMinutes;
    m.rounds = g_mpRounds;
    m.team1 = kSplitTeam1[g_mpSplit];
    return true;
}

static void LineupCycle(int row, int dir);
static void BeginEditLineup(int row);
static void LineupStart();

static void MultiAction(int action, int slot)
{
    if (slot >= 1 && slot <= 3) {   // another player's controller: only its own place
        if (slot < g_lineupN && (action == 4 || action == 5)) LineupCycle(slot, action == 4 ? 1 : -1);
        return;
    }
    const int id = g_lineupRow, box = MpBoxOf(id);
    if (box >= 0) g_mpBox = box;
    if (action == 0 || action == 2) {
        // one box and Start, Back as a loop: down from its last row to Start, up from its first row to Back
        std::vector<int> rows;
        MpRows(g_mpBox, rows);
        rows.push_back(MR_START);
        rows.push_back(MR_BACK);
        int at = -1;
        for (int i = 0; i < (int)rows.size(); ++i) if (rows[i] == id) at = i;
        const int n = (int)rows.size();
        g_lineupRow = at < 0 ? rows[0] : rows[(at + (action == 0 ? n - 1 : 1)) % n];
    } else if (action == 4 || action == 5) {
        if (box < 0) g_lineupRow = id == MR_START ? MR_BACK : MR_START;   // Start and Back side by side
        else if (id < 4) LineupCycle(id, action == 4 ? 1 : -1);
        else MpStep(id, action == 4 ? 1 : -1);
    } else if (action == 7) {
        if (id < 4 && g_lineup[id] == kNewName) BeginEditLineup(id);
        else if (id == MR_BACK) Close();
        else LineupStart();   // from any row: the help line says so
    } else if (action == 8) Close();
}

static void LineupCycle(int row, int dir)
{
    std::vector<std::string> list;
    for (const std::string& k : AllNames()) if (!LineupTaken(row, k)) list.push_back(k);
    list.push_back("");        // guest
    list.push_back(kNewName);
    const int n = (int)list.size();
    int cur = dir > 0 ? n - 1 : 0;   // not found: the first step lands on the first (or last) entry
    for (int i = 0; i < n; ++i) if (list[i] == g_lineup[row]) cur = i;
    g_lineup[row] = list[(cur + dir + n) % n];
}

static void BeginEditLineup(int row)
{
    BeginEdit(row, "");
    g_editLineup = true;
}

// the name typed for a row ("" or cancelled: a guest)
static void LineupTyped(const std::string& s, bool save)
{
    const int row = g_editSlot;
    if (row < 0 || row >= g_lineupN) return;
    if (!save || s.empty()) { g_lineup[row] = ""; return; }
    // known with other capitals: the same person, written the new way (as SetName)
    for (const std::string& k : AllNames())
        if (SameName(k, s)) {
            if (k != s) {
                RenameName(k, s);
                for (int r = 0; r < g_lineupN; ++r) if (RealName(g_lineup[r]) && SameName(g_lineup[r], s)) g_lineup[r] = s;
            }
            break;
        }
    // a name another row has: the two rows swap
    const std::string old = RealName(g_lineup[row]) ? g_lineup[row] : "";
    for (int r = 0; r < g_lineupN; ++r) if (r != row && RealName(g_lineup[r]) && SameName(g_lineup[r], s)) g_lineup[r] = old;
    g_lineup[row] = s;
    if (!HasName(g_known, s)) { g_known.push_back(s); SaveNames(); }
}

static void LineupStart()
{
    for (int r = 0; r < g_lineupN; ++r)
        if (g_lineup[r] == kNewName) { g_lineupRow = r; BeginEditLineup(r); return; }   // a new name still to type
    char ini[MAX_PATH], sec[16], key[8];
    PlayersIni(ini, sizeof ini);
    sprintf_s(sec, "Lineup%d", g_lineupN);
    for (int r = 0; r < g_lineupN; ++r) {
        strncpy_s(g_name[r], g_lineup[r].c_str(), _TRUNCATE);
        sprintf_s(key, "P%d", r + 1);
        WritePrivateProfileStringA(sec, key, g_lineup[r].c_str(), ini);
    }
    // the places that do not play this time keep their names, but never one of the players' (one name, one place)
    for (int s = g_lineupN; s < 4; ++s)
        for (int r = 0; r < g_lineupN; ++r)
            if (RealName(g_lineup[r]) && SameName(g_name[s], g_lineup[r])) g_name[s][0] = 0;
    SaveNames();
    for (int r = 2; r < g_lineupN; ++r) RememberPadName(r);
    Log("players: who is playing (%d): %s / %s / %s / %s", g_lineupN, PlayerName(0).c_str(),
        g_lineupN > 1 ? PlayerName(1).c_str() : "-", g_lineupN > 2 ? PlayerName(2).c_str() : "-",
        g_lineupN > 3 ? PlayerName(3).c_str() : "-");
    if (g_multi) {   // the Multiplayer page's own choices (the versus ones: Pvp.cpp, PvpConfigure)
        IniWriteInt("Multiplayer", "Mode", g_mpMode);
        IniWriteInt("Coop", "Players", g_mpCoopN);
        Log("players: multiplayer: mode %d, %d players, enemies %d, %s, %d round(s), team 1 = %d", g_mpMode, MpPlayers(),
            g_mpEnemies, g_mpTimed ? (std::to_string(g_mpMinutes) + " minutes").c_str() : (std::to_string(g_mpLives) + " lives").c_str(),
            g_mpRounds, kSplitTeam1[g_mpSplit]);
    }
    g_lineupDone = true;
    Close();
}

bool PlayersLineupDone()
{
    const bool d = g_lineupDone;
    g_lineupDone = false;
    return d;
}

bool PlayersAskWhoPlays() { return g_enabled && g_askWho; }

static void LineupAction(int action, int slot)
{
    if (g_lineupLogged < 16) {   // which controller drives which row (to check the slots from the log)
        ++g_lineupLogged;
        Log("players: who is playing? action %d from slot %d", action, slot);
    }
    if (g_multi) { MultiAction(action, slot); return; }
    if (slot >= 1 && slot <= 3) {   // another player's controller: only its own row
        if (slot < g_lineupN && (action == 4 || action == 5)) LineupCycle(slot, action == 4 ? 1 : -1);
        return;
    }
    const int rows = g_lineupN + 2;
    if (action == 0) g_lineupRow = (g_lineupRow + rows - 1) % rows;
    else if (action == 2) g_lineupRow = (g_lineupRow + 1) % rows;
    else if ((action == 4 || action == 5) && g_lineupRow < g_lineupN) LineupCycle(g_lineupRow, action == 4 ? 1 : -1);
    else if (action == 7) {
        if (g_lineupRow < g_lineupN) {
            if (g_lineup[g_lineupRow] == kNewName) BeginEditLineup(g_lineupRow);
            else ++g_lineupRow;
        } else if (g_lineupRow == g_lineupN) LineupStart();
        else Close();
    } else if (action == 8) Close();
}

static void EndEdit(bool save)
{
    std::string s = g_edit;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    // a capital letter at the start of each word, as the on-screen letters do ("alex" -> "Alex")
    for (size_t i = 0; i < s.size(); ++i)
        if (i == 0 || s[i - 1] == ' ') {
            char c[2] = { s[i], 0 };
            CharUpperBuffA(c, 1);   // Windows' own table, accented letters included
            s[i] = c[0];
        }
    if (g_editLineup) {
        g_editLineup = false;
        LineupTyped(s, save);
        g_page = P_LINEUP;
        return;
    }
    if (g_editSlot == kEditAdd) {   // a new name from Manage names: known from now on, selected in the list
        std::string added;
        if (save && !s.empty()) {
            for (const std::string& k : AllNames()) if (SameName(k, s)) added = k;   // already there (any capitals): that one
            if (added.empty()) {
                g_known.push_back(s);
                SaveNames();
                added = s;
                Log("players: name \"%s\" added", s.c_str());
            }
        }
        OpenManage();
        for (size_t i = 0; i < g_list.size(); ++i) if (g_list[i] == added) g_listSel = (int)i;
        return;
    }
    if (g_editSlot >= 0) {
        if (save) SetName(g_editSlot, s);
        g_page = P_MAIN;
    } else {
        if (save && !s.empty() && s != g_actName) {
            // the name of another player: the rename would merge the two without asking (a rename is a merge into a new
            // name); the merge's own question is asked instead
            for (const std::string& k : AllNames())
                if (k != g_actName && SameName(k, s)) { g_mergeTo = k; g_confirm = 2; g_yes = 1; g_page = P_CONFIRM; return; }
            RenameName(g_actName, s);
        }
        OpenManage();
    }
}

static bool Allowed(unsigned char c)
{
    if (c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xC0)) return false;
    return !strchr("[]=;|\"@\\", c);
}

static void AddChar(char c)
{
    const size_t n = strlen(g_edit);
    if (n >= (size_t)kMaxName || !Allowed((unsigned char)c)) return;
    if (c == ' ' && n == 0) return;
    g_edit[n] = c;
    g_edit[n + 1] = 0;
}

static void DeleteChar() { const size_t n = strlen(g_edit); if (n) g_edit[n - 1] = 0; }

// left/right on a player's row: the next or previous name among "no name" and the names used before, skipping the
// names of the other players
// (02/11/2026: and "New name...", as on "Who is playing?", now that confirm on a place opens the player's card; it only
// shows, the place keeps its name until one is typed)
static void CycleName(int slot, int dir)
{
    std::vector<std::string> list = { "" };
    for (const std::string& k : g_known) list.push_back(k);
    list.push_back(kNewName);
    const int n = (int)list.size();
    int cur = 0;
    if (g_slotNew[slot]) cur = n - 1;
    else for (int i = 0; i < n; ++i) if (SameName(list[i], g_name[slot])) cur = i;
    for (int step = 0; step < n; ++step) {
        cur = (cur + dir + n) % n;
        bool taken = false;
        for (int s = 0; s < 4; ++s)
            if (s != slot && RealName(list[cur]) && SameName(list[cur], g_name[s])) taken = true;
        if (!taken) break;
    }
    g_slotNew[slot] = list[cur] == kNewName;
    if (!g_slotNew[slot]) SetName(slot, list[cur]);
}

// a player's card from the Fellowship page: the place's name (one without statistics yet gets an empty card)
static void OpenCardOf(const std::string& name)
{
    LoadTable();
    int at = -1;
    for (int i = 0; i < (int)g_table.size(); ++i) if (SameName(g_table[i].name, name)) at = i;
    if (at < 0 && !name.empty()) { g_table.push_back(Totals{ name, Vals() }); at = (int)g_table.size() - 1; }
    if (at < 0) return;
    g_statsSel = at;
    g_cardBack = P_MAIN;
    g_page = P_CARD;
}

static void GridPress()
{
    if (g_gridR < 4) {
        char c = kGrid[g_gridR][g_gridC];
        if (c >= 'A' && c <= 'Z' && g_edit[0] && g_edit[strlen(g_edit) - 1] != ' ') c = (char)(c - 'A' + 'a');   // capital only at the start of a word
        AddChar(c);
        return;
    }
    if (g_gridC == 0) AddChar(' ');
    else if (g_gridC == 1) DeleteChar();
    else EndEdit(true);
}

static void ListMove(int action, int n)
{
    if (n <= 0) return;
    if (action == 0) g_listSel = (g_listSel + n - 1) % n;
    else if (action == 2) g_listSel = (g_listSel + 1) % n;
    else if (action == 4) g_listSel = (std::min)(n - 1, g_listSel + 8);
    else if (action == 5) g_listSel = (std::max)(0, g_listSel - 8);
}

static void PageAction(int action, int slot);
static bool FromLeftClick();
static bool FromRightClick();
static void LeftClick();
static unsigned LeftPressedAgo();   // ms since the left button was last pressed (0: never)

// the menu's actions while the page is open (Screens.cpp); onEntry = the "Players" entry is the selected one; slot = the
// controller's slot the action comes from. true = the action was used here
bool PlayersMenuAction(int action, bool onEntry, int slot)
{
    if (!g_enabled) return false;
    if (g_page == P_CLOSED) {
        // the action that comes right after the page was closed with a key or a click is taken here and dropped: the
        // same click also reaches the menu, on the entry under the pointer (test of 30/10/2026: Back opened the page
        // again on Fellowship, or confirmed Versus on Who is playing?)
        if (g_typedAt && GetTickCount() - g_typedAt < 300) return true;
        if (onEntry && action == 7) { Open(); return true; }
        return false;
    }
    const DWORD now = GetTickCount();
    // a key typed on the keyboard can also reach the menu as an action (Enter = confirm, Esc = back, letters bound to
    // the simulated pads...): the actions right after typing are ignored, on every page (the Enter that closes the
    // editor must not open it again, the Esc that cancels it must not close the page); so are those right after a
    // mouse click the page used itself
    if (g_typedAt && now - g_typedAt < 300) return true;
    // The mouse. One click brings two confirms: one from the pointer (slot 3) and, about 15 ms later, one from slot 0
    // (the mouse button bound as a key; log of 30/10/2026). The first is the click: the place under the pointer is
    // used; any other confirm in the next quarter of a second is dropped. The back of the right button is dropped too
    // (MouseFrame does it).
    // (the twin is dropped only among the clicks: two players' confirms, or two Enters, within 250 ms are not clicks.
    // The pointer's own confirm comes from slot 3, which is also the fourth controller's: the button decides)
    static DWORD lastClick;
    // a confirm with Enter or Space held is the keyboard's in any case; and a left button that is only down (see
    // FromLeftClick) is noted, to see how often Windows keeps one down
    const bool keyConfirm = KeyDown(VK_RETURN) || KeyDown(VK_SPACE);
    if (action == 7 && LogDetail()) {   // what a confirm on the page looks like ([Log] Detail=1, the first 12)
        static int logged;
        if (logged < 12) {
            ++logged;
            Log("players: confirm on the page (slot %d): left button %s, pressed %u ms ago, Enter or Space %s -> %s", slot,
                KeyDown(VK_LBUTTON) ? "down" : "up", LeftPressedAgo(), keyConfirm ? "held" : "not held",
                !keyConfirm && FromLeftClick() ? "a click" : "the keys");
        }
    }
    if (action == 7 && KeyDown(VK_LBUTTON) && !FromLeftClick()) {
        static int logged;
        if (logged < 5) {
            ++logged;
            Log("players: confirm while the left button looks held (pressed %u ms ago, slot %d, %s): not a click",
                LeftPressedAgo(), slot, keyConfirm ? "Enter or Space held" : "no key held");
        }
    }
    if (action == 7 && !keyConfirm && FromLeftClick()) {
        if (lastClick && now - lastClick < 250) return true;
        lastClick = now | 1;
        LeftClick();
        g_typedAt = now;
        return true;
    }
    if (action == 8 && FromRightClick()) return true;
    PageAction(action, slot);
    return true;
}

// what an action does on the open page (from the menu's actions and from the mouse)
static void PageAction(int action, int slot)
{
    switch (g_page) {
    case P_LINEUP:
        LineupAction(action, slot);
        break;
    case P_MAIN:
        if (action == 0 || action == 2) {
            g_row = (g_row + (action == 0 ? MAIN_ROWS - 1 : 1)) % MAIN_ROWS;
            for (bool& b : g_slotNew) b = false;   // "New name..." was not confirmed: the place shows its name again
        }
        else if ((action == 4 || action == 5) && g_row < 4) CycleName(g_row, action == 4 ? 1 : -1);
        else if (action == 7) {
            // a place: its player's card; a guest or "New name...": the editor
            if (g_row < 4) {
                if (g_slotNew[g_row] || !g_name[g_row][0]) { g_slotNew[g_row] = false; BeginEdit(g_row, ""); }
                else OpenCardOf(g_name[g_row]);
            }
            else if (g_row == ROW_STATS) { LoadTable(); g_page = P_STATS; }
            else if (g_row == ROW_CARDS) {   // the first place's player first (the others with left/right)
                OpenCardOf(g_name[0]);
                if (g_page != P_CARD) { LoadTable(); g_statsSel = 0; g_cardBack = P_MAIN; g_page = P_CARD; }
            }
            else if (g_row == ROW_RECORDS) { LoadTable(); g_page = P_RECORDS; }
            else if (g_row == ROW_LAST) { LoadSummary(); g_page = P_LAST; }
            else if (g_row == ROW_MANAGE) { g_listSel = 0; OpenManage(); }
            else Close();
        } else if (action == 8) Close();
        break;
    case P_EDIT:
        if (action == 0) g_gridR = (g_gridR + kGridRows - 1) % kGridRows;
        else if (action == 2) g_gridR = (g_gridR + 1) % kGridRows;
        else if (action == 4) g_gridC = (g_gridC + 1) % (g_gridR < 4 ? 10 : 3);
        else if (action == 5) g_gridC = (g_gridC + (g_gridR < 4 ? 9 : 2)) % (g_gridR < 4 ? 10 : 3);
        else if (action == 7) GridPress();
        else if (action == 8) EndEdit(false);   // back = cancel, as on every page (test of 31/10/2026); letters go with Delete
        if (g_gridR == 4 && g_gridC > 2) g_gridC = 2;
        break;
    case P_STATS:
    case P_CARD:
    case P_DETAIL: {
        const int n = (int)g_table.size();
        // the statistics (test of 02/11/2026 with the keyboard: the tabs are side by side, so left/right change them):
        // left/right the tab, up/down scroll it (only the enemies' is longer than the page), the shoulder buttons and
        // Page Up / Page Down the player (ShoulderPlayers, PlayersFrame); the card and the heroes: left/right the player
        if (g_page == P_STATS && action == 0) g_statsFirst = (std::max)(0, g_statsFirst - 1);
        else if (g_page == P_STATS && action == 2) g_statsFirst = (std::min)(g_statsFirstMax, g_statsFirst + 1);
        else if (g_page == P_STATS && (action == 4 || action == 5)) TabStep(action == 4 ? 1 : -1);
        else if (n && (action == 4 || action == 2)) g_statsSel = (g_statsSel + 1) % n;
        else if (n && (action == 5 || action == 0)) g_statsSel = (g_statsSel + n - 1) % n;
        // statistics -> the player's card -> their heroes, and back the same way
        else if (action == 7 && g_page == P_STATS && n) { g_cardBack = P_STATS; g_page = P_CARD; }
        else if (action == 7 && g_page == P_CARD) g_page = P_DETAIL;
        else if (action == 7 || action == 8) g_page = g_page == P_DETAIL ? P_CARD : g_page == P_CARD ? g_cardBack : P_MAIN;
        break;
    }
    case P_RECORDS:
    case P_LAST:
        if (action == 7 || action == 8) g_page = P_MAIN;
        break;
    case P_MANAGE: {   // the names, then "Add a name" (index n)
        const int n = (int)g_list.size();
        ListMove(action, n + 1);
        if (action == 7 && g_listSel == n) BeginEdit(kEditAdd, "");
        else if (action == 7) { g_actName = g_list[g_listSel]; g_actRow = 0; g_page = P_ACTION; }
        else if (action == 8) g_page = P_MAIN;
        break;
    }
    case P_ACTION:
        if (action == 0) g_actRow = (g_actRow + 3) % 4;
        else if (action == 2) g_actRow = (g_actRow + 1) % 4;
        else if (action == 7) {
            if (g_actRow == 0) BeginEdit(-1, g_actName);
            else if (g_actRow == 1) {
                g_list = AllNames();
                g_list.erase(std::remove(g_list.begin(), g_list.end(), g_actName), g_list.end());
                g_listSel = 0;
                if (!g_list.empty()) g_page = P_PICK;
            } else if (g_actRow == 2) { g_confirm = 1; g_yes = 1; g_page = P_CONFIRM; }
            else OpenManage();
        } else if (action == 8) OpenManage();
        break;
    case P_PICK:
        ListMove(action, (int)g_list.size());
        if (action == 7 && !g_list.empty()) { g_mergeTo = g_list[g_listSel]; g_confirm = 2; g_yes = 1; g_page = P_CONFIRM; }
        else if (action == 8) g_page = P_ACTION;
        break;
    case P_CONFIRM:
        if (action == 0 || action == 2 || action == 4 || action == 5) g_yes ^= 1;
        else if (action == 7) {
            if (g_yes == 0) { if (g_confirm == 1) RemoveName(g_actName); else MergeNames(g_actName, g_mergeTo); }
            g_listSel = 0;
            OpenManage();
        } else if (action == 8) g_page = P_ACTION;
        break;
    default: break;
    }
}

// --- keyboard in the editor -------------------------------------------------------------------------------------
static bool GameHasFocus()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// the character of a key in the active layout (cp1252), 0 if none
static char KeyChar(int vk)
{
    if (KeyDown(VK_CONTROL) && !KeyDown(VK_MENU)) return 0;   // Ctrl+key: no text (AltGr = Ctrl+Alt is allowed)
    BYTE ks[256] = {};
    for (int k : { VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU })
        if (KeyDown(k)) ks[k] = 0x80;
    if (GetKeyState(VK_CAPITAL) & 1) ks[VK_CAPITAL] = 1;
    const HKL hkl = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
    const UINT sc = MapVirtualKeyExW((UINT)vk, MAPVK_VK_TO_VSC, hkl);
    wchar_t w[4] = {};
    if (ToUnicodeEx((UINT)vk, sc, ks, w, 4, 4, hkl) != 1) return 0;   // flag 4: the keyboard state is not changed
    char c = 0;
    BOOL lossy = FALSE;   // a character that the game's text (cp1252) cannot show: none
    if (WideCharToMultiByte(1252, 0, w, 1, &c, 1, nullptr, &lossy) != 1 || lossy) return 0;
    return c;
}

// --- the mouse ---------------------------------------------------------------------------------------------------
// Test of 30/10/2026: the pages' entries could not be clicked (they are our own texts, not the menu's elements). The
// game's pointer of the menus is drawn by 0x501850; 0x501800(&x, &y) gives its position (fractions of the screen, like
// the elements: confirmed by the log) and the byte 0x8E38BC says whether it is shown (ROTK Fix hides it while a
// controller is used). The pages register what can be clicked while they are drawn (AddHit). Every frame the pointer
// moving over an entry selects it. A left click is done once, by the game's own action of that click: the menu turns
// every click into a "confirm" (action 7, from slot 3), and when it reaches the page (PlayersMenuAction) with the left
// button down or just pressed, the place under the pointer is used. A first version also acted on the button by
// itself, and since the two came in either order one click did two things (log of 30/10/2026: the click on Single
// opened Who is playing? and, 15 ms later, pressed Start). The right button goes back (MouseFrame), and a "back" of
// the menu that comes with it is dropped.
enum HitKind { H_SELECT, H_LEFT, H_RIGHT, H_PICK, H_TAB, H_UP, H_DOWN };
struct ClickArea { float x0, y0, x1, y1; int kind, idx; };
static ClickArea g_areas[96];
static int g_nAreas;
static DWORD g_leftAt, g_rightAt;   // when the buttons were last pressed (MouseFrame)
static bool  g_leftWas, g_rightWas; // the buttons at the last frame (MouseFrame)

static void AddHit(float x0, float y0, float x1, float y1, int kind, int idx)
{
    if (g_nAreas < (int)(sizeof g_areas / sizeof g_areas[0])) g_areas[g_nAreas++] = ClickArea{ x0, y0, x1, y1, kind, idx };
}

static void SelectHit(int idx)
{
    switch (g_page) {
    case P_MAIN:
        if (idx != g_row) for (bool& b : g_slotNew) b = false;
        g_row = idx;
        break;
    case P_LINEUP: g_lineupRow = idx; break;
    case P_EDIT: g_gridR = idx / 10; g_gridC = idx % 10; break;
    case P_MANAGE: case P_PICK: g_listSel = idx; break;
    case P_ACTION: g_actRow = idx; break;
    case P_CONFIRM: g_yes = idx; break;
    default: break;
    }
}

static bool PointerPos(float& x, float& y)
{
    if (!GameHasFocus() || !*(const uint8_t*)0x8E38BC || !*(const uint32_t*)0x8E38C4) return false;
    return Guard([&] { ((void(__cdecl*)(float*, float*))0x501800)(&x, &y); });
}

static const ClickArea* AreaAt(float x, float y)
{
    for (int i = 0; i < g_nAreas; ++i)
        if (x >= g_areas[i].x0 && x < g_areas[i].x1 && y >= g_areas[i].y0 && y < g_areas[i].y1) return &g_areas[i];
    return nullptr;
}

// Is this action of the menu the left click (or the right one)? The button was pressed a moment ago: it went down at
// this frame (not seen by MouseFrame yet) or less than half a second ago. A button that is only down is not a click:
// after Alt+Tab Windows may keep a button down for good (test of 03/11/2026: the game takes the mouse back before the
// button is released, and the release never reaches Windows), and then every Enter was taken for a click on the place
// under the pointer, where nothing was.
static bool FromLeftClick()
{
    return (KeyDown(VK_LBUTTON) && !g_leftWas) || (g_leftAt && GetTickCount() - g_leftAt < 500);
}
static bool FromRightClick()
{
    return (KeyDown(VK_RBUTTON) && !g_rightWas) || (g_rightAt && GetTickCount() - g_rightAt < 500);
}
static unsigned LeftPressedAgo() { return g_leftAt ? (unsigned)(GetTickCount() - g_leftAt) : 0u; }

// the left click, from the menu's action: the place under the pointer (nothing if the click fell on nothing)
static void LeftClick()
{
    float x = 0.0f, y = 0.0f;
    const ClickArea* over = PointerPos(x, y) ? AreaAt(x, y) : nullptr;
    static int logged;
    if (logged < 3) { ++logged; Log("players: click at %.3f %.3f (%s)", x, y, over ? "on an entry" : "nothing there"); }
    MenuIdleReset();
    if (!over) return;
    const ClickArea h = *over;   // the action may change the page, and with it the list
    switch (h.kind) {
    case H_SELECT: SelectHit(h.idx); PageAction(7, 0); break;
    case H_LEFT: case H_RIGHT:
        if (g_page == P_LINEUP) g_lineupRow = h.idx;
        if (g_page == P_MAIN) g_row = h.idx;   // the arrows of a place
        PageAction(h.kind == H_LEFT ? 5 : 4, 0);
        break;
    case H_PICK:
        if (g_statsSel == h.idx) PageAction(7, 0);   // the selected player again: their card
        else g_statsSel = h.idx;
        break;
    case H_TAB: g_statsTab = h.idx; g_statsFirst = 0; break;
    case H_UP: g_statsFirst = (std::max)(0, g_statsFirst - h.idx); break;   // the scroll bar: a page up or down
    case H_DOWN: g_statsFirst = (std::min)(g_statsFirstMax, g_statsFirst + h.idx); break;
    }
}

// every frame: the buttons' times, the pointer over an entry selects it, the right button goes back
static void MouseFrame()
{
    static float lastX = -1.0f, lastY = -1.0f;
    const bool left = KeyDown(VK_LBUTTON), right = KeyDown(VK_RBUTTON);
    if (left && !g_leftWas) g_leftAt = GetTickCount() | 1;
    const bool rightClick = right && !g_rightWas;
    if (rightClick) g_rightAt = GetTickCount() | 1;
    g_leftWas = left; g_rightWas = right;
    float x = 0.0f, y = 0.0f;
    if (g_page == P_CLOSED || !PointerPos(x, y)) return;
    const bool moved = fabsf(x - lastX) > 0.002f || fabsf(y - lastY) > 0.002f;
    lastX = x; lastY = y;
    const ClickArea* over = AreaAt(x, y);
    if (over && moved && over->kind == H_SELECT) SelectHit(over->idx);
    if (rightClick) { g_typedAt = GetTickCount(); MenuIdleReset(); PageAction(8, 0); }
}

// L1/R1 change the player on the statistics page. The menus have no action for them (the loop 0x4EAA60 makes up, down,
// left, right, confirm, back, and cross and R1 pressed and released, 9-12), so the input records are read (Pads.cpp:
// buttons now at +0x40, the bit of each id at 0x99CB60): the left shoulders (ids 8 and 9: LT and LB of an Xbox-style pad)
// go back, the right ones (10 and 11: RB and RT) forward. The buttons seen are kept here, because this runs more than
// once a frame.
// On SteamOS (test of 04/11/2026) those records had no shoulder buttons in the menus: they are read from SDL,
// on every open controller (Pads.cpp, PadsShoulder), and the records only without SDL.
static void ShoulderPlayers()
{
    bool sdl = false;
    const int dir = PadsShoulder(sdl);
    if (sdl) {
        if (!dir) return;
        if (g_page == P_LINEUP && g_multi) MpSwitchBox();
        else if (g_page == P_STATS) PlayerStep(dir);
        return;
    }
    static uint16_t was[4];
    const uint16_t* bit = (const uint16_t*)0x99CB60;
    const uint16_t back = bit[8] | bit[9], next = bit[10] | bit[11];
    for (int p = 0; p < 4; ++p) {
        const uint16_t now = *(const uint16_t*)(0x99CBA0 + p * 0x58 + 0x40);
        const uint16_t pressed = now & ~was[p];
        was[p] = now;
        // the Multiplayer page: player 1's shoulder buttons go to the other box
        if (g_page == P_LINEUP && g_multi && p == 0 && (pressed & (back | next))) { MpSwitchBox(); continue; }
        if (g_page != P_STATS) continue;
        if (pressed & back) PlayerStep(-1);
        else if (pressed & next) PlayerStep(1);
    }
}

// every frame from ScreensFrame and when the page is drawn
void PlayersFrame()
{
    if (!g_enabled) return;
    const int mode = ScreenMode();
    // the Last game over the map must be seen to take the menu's actions: not drawn in 1.5 s, it closes (the map as before)
    if (g_recap && !g_recapDrawn && GetTickCount() - g_recapAt > 1500) {
        Log("players: the Last game was NOT drawn over the map within 1.5 s: closed, the map as it is");
        PlayersCloseRecap();
    }
    {
        static bool wasIn;
        IconsFrame(g_inLevel && !wasIn);   // the portraits taken while playing (Icons.cpp)
        wasIn = g_inLevel;
    }
    // The end of a level is also seen here, every frame: at the end of a level the game writes the mode directly (999,
    // or 998 towards the menu, in 0x480830, without 0x47FA20) and only then changes to the score screen, so SetMode
    // (Screens.cpp) saw 999 -> 2 instead of 0 -> 2 and the level was never counted (test of 29/10/2026). Once the level
    // has been played (mode 0 seen), the score screen (a victory), the hero selection, the map or the main menu close
    // it. Not the shop (3), the save (6) or the co-op statistics (16): they come after the score screen, and if one of
    // them could open from inside a level (the pause?) the rest of the level would be lost. Not the loading (14) either:
    // a level loaded again directly is "Retry" (PlayersLevelStart).
    if (g_inLevel) {
        if (mode == 0) {
            g_seenPlay = true;
            NotePlayers();
            NoteCombos();
            if (!g_sumTplOk) g_sumTplOk = HudTextTemplate(g_sumTpl);   // for the summary on the score screen
        }
        else if (g_seenPlay && (mode == 1 || mode == 2 || mode == 4 || mode == 17)) PlayersLevelLeave(mode == 2);
    }
    if (g_sumShow && mode != 0 && mode != 2 && mode != 998 && mode != 999) g_sumShow = false;   // the score screens are over
    const bool editing = g_page == P_EDIT && GameHasFocus() && mode == 17;
    for (int vk = 8; vk < 0xE0; ++vk) {   // from Backspace (the mouse buttons are below 8)
        const bool down = KeyDown(vk);
        const bool pressed = down && !g_keyWas[vk];
        g_keyWas[vk] = down;
        if (pressed && g_page == P_STATS && mode == 17 && GameHasFocus() && (vk == VK_PRIOR || vk == VK_NEXT)) {
            PlayerStep(vk == VK_PRIOR ? -1 : 1);   // Page Up / Page Down: the player from the keyboard
            continue;
        }
        if (pressed && g_page == P_LINEUP && g_multi && mode == 17 && GameHasFocus() &&
            (vk == VK_TAB || vk == VK_PRIOR || vk == VK_NEXT)) {
            MpSwitchBox();   // the Multiplayer page: the other box
            continue;
        }
        if (!pressed || !editing) continue;
        MenuIdleReset();   // typing a name is using the menu too
        if (vk == VK_RETURN) { g_typedAt = GetTickCount(); EndEdit(true); return; }
        if (vk == VK_ESCAPE) { g_typedAt = GetTickCount(); EndEdit(false); return; }
        if (vk == VK_BACK)   { g_typedAt = GetTickCount(); DeleteChar(); continue; }
        if (vk >= VK_F1 && vk <= VK_F24) continue;
        if (const char c = KeyChar(vk)) { g_typedAt = GetTickCount(); AddChar(c); }
    }
    if (mode == 17) { MouseFrame(); ShoulderPlayers(); }
    GlintFrame(mode);
    // the menu was left (the demo after a minute without the menu's own actions, as in the game; holding it back left
    // the menu without its background after the demo, test of 30/10/2026): the page closes
    // (not the Last game over the map after a versus game: it lives on the map, test of 05/11/2026: closed here at once)
    if (g_page != P_CLOSED && mode != 17 && !(g_recap && mode == 4)) {
        Log("players: page closed by the game (screen mode %d)", mode);
        g_page = P_CLOSED;
        g_recap = false;
    }
    // The game's mouse gives the pointer to the entry pressed until the button is released (0x570474; while it is set,
    // every other entry ignores the pointer, 0x4FFE80). A click on Fellowship opened the page and hid the entries before
    // the release reached it, so the entry kept the pointer and, back in the menu, the pointer could not select anything
    // else (test of 30/10/2026). When a page opens or closes, the hold is let go once the buttons are up.
    static bool wasOpen, release;
    if ((g_page != P_CLOSED) != wasOpen) { wasOpen = g_page != P_CLOSED; release = true; }
    if (release && !KeyDown(VK_LBUTTON) && !KeyDown(VK_RBUTTON)) {
        release = false;
        uint32_t& held = *(uint32_t*)0x570474;
        if (held) { Log("players: the menu's mouse hold of element %08X let go", held); held = 0; }
    }
}

// --- drawing --------------------------------------------------------------------------------------------------
// Our texts are copies of a label of the main menu (MenuTextTemplate: the Co-op entry's normal and highlighted texts).
// A text element (drawn by 0x4FB800): +0x40/+0x44 position (fractions of the screen below 2), +0x68/+0x6C scale, +0x8C
// the text ("@" = literal), +0x90 the font (one of six, 0x4DF010), colour at +0x78, +0x80, +0x7C, +0x84 (floats 0..1,
// handed to 0x4E94D0 in that order, alpha last). The styles keep the menu's two colours and only darken them, so the
// order of the colour channels does not matter. The game's own measure gives the width of a text (TextWidth): numbers
// are right-aligned in their column, and what does not fit is made smaller, then cut.
enum Style { ST_VALUE, ST_LABEL, ST_HEAD, ST_SEL, ST_TITLE, ST_HELP, ST_MOON };   // ST_MOON: silver-blue (the Moon's team)
static uint8_t g_txt[0x200];
static char    g_txtBuf[192];

static void PrepareText(const uint8_t* tpl, const std::string& s, float x, float y, float scale)
{
    memcpy(g_txt, tpl, 0xB0);
    *(uint32_t*)(g_txt + 4) = 0;
    *(uint16_t*)(g_txt + 0x74) = 1;
    *(float*)(g_txt + 0x88) = 0.0f;
    _snprintf_s(g_txtBuf, sizeof g_txtBuf, _TRUNCATE, "@%s", s.c_str());   // a long text is cut, never an error
    *(const char**)(g_txt + 0x8C) = g_txtBuf;
    *(float*)(g_txt + 0x40) = x;
    *(float*)(g_txt + 0x44) = y;
    const float sx = *(const float*)(tpl + 0x68), sy = *(const float*)(tpl + 0x6C);
    *(float*)(g_txt + 0x68) = (sx ? sx : 1.0f) * scale;
    *(float*)(g_txt + 0x6C) = (sy ? sy : 1.0f) * scale;
}

static void TextWith(uint8_t* tpl, const std::string& s, float x, float y, float scale)
{
    if (!tpl) return;
    PrepareText(tpl, s, x, y, scale);
    DrawGameElement(g_txt);
}

// The labels exist only on the main menu: copies of them (0xB0 bytes: font index, colours, scale, box; no pointer the
// drawing follows, as HudTextTemplate's) are kept for the Last game drawn over the map after a versus game (g_recap)
static uint8_t g_tplCopy[2][0xB0];
static bool    g_tplCopied[2];

static uint8_t* StyleTemplate(int style)
{
    const int hi = style == ST_SEL || style == ST_HEAD || style == ST_TITLE ? 1 : 0;
    uint8_t* tpl = MenuTextTemplate(hi != 0);
    if (!tpl) tpl = MenuTextTemplate(false);
    if (tpl) {
        if (uint8_t* t = MenuTextTemplate(hi != 0); t && !IsBadReadPtr(t, 0xB0)) { memcpy(g_tplCopy[hi], t, 0xB0); g_tplCopied[hi] = true; }
        return tpl;
    }
    if (!g_recap) return nullptr;
    return g_tplCopied[hi] ? g_tplCopy[hi] : g_tplCopied[0] ? g_tplCopy[0] : nullptr;
}

// Width of a text in the units of the elements' x. 0x4DF010(font) and 0x4DF1D0(scale x, y) set what 0x4E94D0 sets
// for a draw (the scale as 0x4FB800 computes it: the global factors at 0x8E2F7C / 0x8E2F80 twice, +0x68 / +0x6C,
// +0x9C), then 0x4DF200(text) measures it with the font's glyphs (0x49C110). The text manager's font and scale
// (*0x56D628: +0xF0, +0xCC, +0xD0) are put back.
static float TextWidthRaw(const uint8_t* tpl, const char* s, float scale)
{
    uint8_t* mgr = *(uint8_t* const*)0x56D628;
    if (!mgr || !tpl || !*s) return 0.0f;
    float sx = *(const float*)(tpl + 0x68), sy = *(const float*)(tpl + 0x6C), k = *(const float*)(tpl + 0x9C);
    if (sx == 0.0f) sx = 1.0f;
    if (sy == 0.0f) sy = 1.0f;
    if (k == 0.0f) k = 1.0f;
    const float gx = *(const float*)0x8E2F7C, gy = *(const float*)0x8E2F80;
    const int fontWas = *(const int*)(mgr + 0xF0);
    const float sxWas = *(const float*)(mgr + 0xCC), syWas = *(const float*)(mgr + 0xD0);
    float w = 0.0f;
    if (!Guard([&] {
            if (((int(__cdecl*)(int))0x4DF010)(*(const int16_t*)(tpl + 0x90))) {
                ((void(__cdecl*)(float, float))0x4DF1D0)(gx * gx * sx * scale * k, gy * gy * sy * scale * k);
                w = ((float(__cdecl*)(const char*))0x4DF200)(s);
            }
        }))
        w = 0.0f;
    *(int*)(mgr + 0xF0) = fontWas;
    *(float*)(mgr + 0xCC) = sxWas;
    *(float*)(mgr + 0xD0) = syWas;
    return w;
}

static float TextWidth(int style, const std::string& s, float scale)
{
    float w = TextWidthRaw(StyleTemplate(style), s.c_str(), scale);
    if (w > 2.0f) w /= 512.0f;                                  // (the 512-wide units of an element's x above 2)
    if (w <= 0.0f && !s.empty()) w = 0.0115f * scale * (float)s.size();   // no measure: an estimate
    static bool logged;
    if (!logged && s.size() >= 5) { logged = true; Log("players: width of \"%s\" at scale %.2f: %.4f", s.c_str(), scale, w); }
    return w;
}

// align: 0 left (x = left edge), 1 right (x = right edge), 2 centred on x. maxW > 0: a wider text is drawn smaller
// (down to 60%), then cut with a dot
static void Put(const std::string& text, float x, float y, float scale, int style, int align = 0, float maxW = 0.0f)
{
    uint8_t* tpl = StyleTemplate(style);
    if (!tpl || text.empty()) return;
    std::string s = text;
    float w = TextWidth(style, s, scale);
    if (maxW > 0.0f && w > maxW) {
        const float f = (std::max)(0.6f, maxW / w);
        scale *= f;
        w *= f;
        while (w > maxW && s.size() > 2) {
            s.resize(s.size() - (s.back() == '.' ? 2 : 1));
            s += '.';
            w = TextWidth(style, s, scale);
        }
    }
    if (align == 1) x -= w;
    else if (align == 2) x -= w * 0.5f;
    PrepareText(tpl, s, x, y, scale);
    // Labels, headings and help lines dimmer. The menu label's colour is 0.8 0.8 0.8 (log of 04/11/2026) and 0.5 is the
    // font's own colour: anything above it is drawn as bright as 0.5, so multiplying 0.8 by these factors changed
    // nothing (the pages looked the same from 30/10 to 04/11/2026). From 0.5, then.
    static const float kShade[] = { 1.0f, 0.82f, 0.85f, 1.0f, 1.0f, 0.62f, 1.0f };   // by Style
    if (kShade[style] != 1.0f)
        for (int off : { 0x78, 0x7C, 0x80 }) {
            float& c = *(float*)(g_txt + off);
            if (c >= 0.0f && c <= 1.01f) c = (std::min)(c, 0.5f) * kShade[style];
        }
    // The Moon's pale blue. A text's colour is +0x78 red, +0x80 green, +0x7C blue (0x4FB800 passes them to 0x4E94D0 in
    // that order), and 0.5 is the font's own colour, as for the HUD's images: a first version multiplied the menu
    // label's values (0.8) and the Moon stayed white (test of 04/11/2026). So at most 0.5, red and green lower than blue
    // (the second test: pale blue); the label's own values go to the log once.
    if (style == ST_MOON) {
        static bool logged;
        if (!logged) {
            logged = true;
            Log("players: colour of the menu label: %.3f %.3f %.3f %.3f (+0x78 +0x7C +0x80 +0x84)", *(const float*)(g_txt + 0x78),
                *(const float*)(g_txt + 0x7C), *(const float*)(g_txt + 0x80), *(const float*)(g_txt + 0x84));
        }
        static const struct { int off; float k; } kMoon[3] = { { 0x78, 0.70f }, { 0x80, 0.82f }, { 0x7C, 1.0f } };
        for (const auto& m : kMoon) {
            float& c = *(float*)(g_txt + m.off);
            c = (c >= 0.0f && c < 0.5f ? c : 0.5f) * m.k;
        }
    }
    DrawGameElement(g_txt);
}

static void Text(const std::string& s, float x, float y, float scale, bool hi) { Put(s, x, y, scale, hi ? ST_SEL : ST_VALUE); }
static void Title(const std::string& s) { Put(s, 0.5f, 0.07f, g_scale * 1.1f, ST_TITLE, 2, 0.9f); }

// a title with "<" and ">" on its sides that can be clicked (another player)
static void Arrow(float x, float y, float h, bool right, int style);   // (with the rectangles, below)

static void TitleArrows(const std::string& s)
{
    Title(s);
    const float half = (std::min)(0.45f, TextWidth(ST_TITLE, s, g_scale * 1.1f)) * 0.5f;
    const float xl = 0.5f - half - 0.045f, xr = 0.5f + half + 0.03f;
    Arrow(xl, 0.077f, 0.032f, false, ST_SEL);
    Arrow(xr, 0.077f, 0.032f, true, ST_SEL);
    AddHit(xl - 0.02f, 0.05f, xl + 0.05f, 0.14f, H_LEFT, 0);
    AddHit(xr - 0.02f, 0.05f, xr + 0.05f, 0.14f, H_RIGHT, 0);
}
static void Help(const std::string& text, float y = 0.93f);   // the help line (with the rectangles, below)

// A dark see-through rectangle behind the page (test of 30/10/2026: the texts were hard to read on the bright sky of the
// menu's picture). The game's own "drkblk" images (0x4FA360) cover the screen with a black quad: the states 0x569C18
// +0x38 = 0x40 and 0x569DF0 +0x90 = 0 (the 2D camera), 0x4A2E20, then 0x4AE0C0(5, corners, colours, 0, 4), which copies
// them into its vertex buffer and draws at once. The same call with our own corners (x, y, 0.1, 1 in fractions of the
// screen) and colours (r, g, b, a; without texture coordinates the alpha counts double, so 0.5 is opaque). Drawn before
// the page's texts, after the menu's picture. [Players] Shade: 0 = no rectangle, 1 = black.
static float g_shade = 0.55f;

// four corners as a strip (two triangles: 0 1 2 and 1 2 3), one colour
static void Quad(const float xs[4], const float ys[4], float alpha, float r, float g, float b)
{
    if (alpha <= 0.0f) return;
    static float v[16], c[16];
    for (int i = 0; i < 4; ++i) {
        v[i * 4] = xs[i]; v[i * 4 + 1] = ys[i]; v[i * 4 + 2] = 0.1f; v[i * 4 + 3] = 1.0f;
        c[i * 4] = r; c[i * 4 + 1] = g; c[i * 4 + 2] = b;
        c[i * 4 + 3] = (std::min)(alpha, 1.0f) * 0.5f;
    }
    Guard([&] {
        *(uint32_t*)(*(uint8_t* const*)0x569C18 + 0x38) = 0x40;
        (*(uint8_t* const*)0x569DF0)[0x90] = 0;
        ((void(__cdecl*)())0x4A2E20)();
        ((int(__cdecl*)(int, const float*, const float*, const void*, int))0x4AE0C0)(5, v, c, nullptr, 4);
    });
}

static void ColorRect(float x0, float y0, float x1, float y1, float alpha, float r, float g, float b)
{
    const float xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 };
    Quad(xs, ys, alpha, r, g, b);
}

// A triangle pointing left or right, h high, its box's top left at x, y (02/11/2026: the menu's font has no "<" or ">",
// so the arrows of the titles and of the players' places were never seen). Two of the strip's corners are on the tip, so
// one of its triangles is empty; the other has the same corners' order as the rectangle's (no culling surprise). The
// colour follows the style: gold when highlighted, the text's otherwise, dimmed for ST_HELP.
static void Arrow(float x, float y, float h, bool right, int style)
{
    const float w = h * 0.48f, ym = y + h * 0.5f, xb = right ? x : x + w, xt = right ? x + w : x;
    const float xr[4] = { xb, xt, xb, xt }, yr[4] = { y, ym, y + h, ym };   // base top, tip, base bottom, tip
    const float xl[4] = { xt, xb, xt, xb }, yl[4] = { ym, y, ym, y + h };   // tip, base top, tip, base bottom
    const float c = style == ST_SEL ? 1.0f : style == ST_HELP ? 0.6f : 0.85f;
    Quad(right ? xr : xl, right ? yr : yl, 1.0f, c, c * 0.86f, c * (style == ST_SEL ? 0.5f : 0.62f));
}

static void DarkRect(float x0, float y0, float x1, float y1, float alpha, float grey = 0.0f)
{
    ColorRect(x0, y0, x1, y1, alpha, grey, grey, grey);
}

// A gold cup, cx its middle, top its top, h its height (fractions of the screen): bowl, handles, stem with a knot, foot
// and base as quads (Quad, as the pages' rectangles), drawn above the banner of a tournament's winner (Hud.cpp). First
// the whole cup a little larger in dark bronze (its outline), then the gold: the bowl's left half lit, the right half in
// shade, a light stripe on the left, a darker inside (test of 04/11/2026: one flat gold looked like a sticker).
void PlayersDrawCup(float cx, float top, float h)
{
    const float k = 9.0f / 16.0f, w = h * k;   // as wide as tall on the screen
    struct Col { float r, g, b; };
    const Col lit = { 1.0f, 0.84f, 0.40f }, shade = { 0.80f, 0.60f, 0.22f }, deep = { 0.55f, 0.38f, 0.12f },
              shine = { 1.0f, 0.96f, 0.72f }, edge = { 0.28f, 0.17f, 0.05f };
    float m = 0.0f;   // the outline's margin, in heights (its width in widths)
    auto quad = [&](float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, const Col& c) {
        // a strip: 0 1 2, 1 2 3 (0 and 2 on the left, 1 and 3 on the right), widened by the margin
        const float mx = m * h * k, my = m * h;
        const float xs[4] = { x0 - mx, x1 + mx, x2 - mx, x3 + mx }, ys[4] = { y0 - my, y1 - my, y2 + my, y3 + my };
        Quad(xs, ys, 1.0f, c.r, c.g, c.b);
    };
    auto rect = [&](float x0, float y0, float x1, float y1, const Col& c) { quad(x0, y0, x1, y0, x0, y1, x1, y1, c); };
    const float bt = top, bb = top + h * 0.48f;   // the bowl
    for (int pass = 0; pass < 2; ++pass) {
        const bool outline = pass == 0;
        m = outline ? 0.025f : 0.0f;
        auto c = [&](const Col& x) -> const Col& { return outline ? edge : x; };
        // the bowl: two halves
        quad(cx - w * 0.46f, bt, cx, bt, cx - w * 0.22f, bb, cx, bb, c(lit));
        quad(cx, bt, cx + w * 0.46f, bt, cx, bb, cx + w * 0.22f, bb, c(shade));
        for (int side = -1; side <= 1; side += 2) {   // the handles: a bar out, down, and back in
            const Col& hc = side < 0 ? lit : shade;
            const float x0 = cx + side * w * 0.44f, x1 = cx + side * w * 0.62f;
            rect((std::min)(x0, x1), bt + h * 0.06f, (std::max)(x0, x1), bt + h * 0.10f, c(hc));
            const float xa = cx + side * w * 0.58f, xb = cx + side * w * 0.62f;
            rect((std::min)(xa, xb), bt + h * 0.06f, (std::max)(xa, xb), bt + h * 0.30f, c(hc));
            const float xc = cx + side * w * 0.30f;
            rect((std::min)(xc, xb), bt + h * 0.26f, (std::max)(xc, xb), bt + h * 0.30f, c(hc));
        }
        rect(cx - w * 0.05f, bb, cx + w * 0.05f, top + h * 0.78f, c(shade));                // the stem
        rect(cx - w * 0.10f, top + h * 0.58f, cx + w * 0.10f, top + h * 0.64f, c(lit));     // its knot
        quad(cx - w * 0.16f, top + h * 0.78f, cx + w * 0.16f, top + h * 0.78f, cx - w * 0.30f, top + h * 0.92f, cx + w * 0.30f,
             top + h * 0.92f, c(lit));                                                       // the foot
        rect(cx - w * 0.34f, top + h * 0.92f, cx + w * 0.34f, top + h * 0.97f, c(shade));   // the base
    }
    m = 0.0f;
    // the inside, the rim, the light on the left
    quad(cx - w * 0.34f, bt + h * 0.05f, cx + w * 0.34f, bt + h * 0.05f, cx - w * 0.16f, bb - h * 0.08f, cx + w * 0.16f,
         bb - h * 0.08f, deep);
    rect(cx - w * 0.46f, bt, cx + w * 0.46f, bt + h * 0.045f, lit);
    quad(cx - w * 0.36f, bt + h * 0.07f, cx - w * 0.30f, bt + h * 0.07f, cx - w * 0.20f, bb - h * 0.04f, cx - w * 0.15f,
         bb - h * 0.04f, shine);
}

// The help line at the bottom of a page (layout chosen on 04/11/2026): each "key: action" of the text (the pieces are
// separated by three spaces) becomes the key in a dark box with a thin gold edge, followed by the action; up/down and
// left/right are drawn as the pages' own arrows, and confirm and back are left out (every page has them, and the text
// ran together: test of 04/11/2026). A piece in another form, or a key not recognised (ROTKPlus.lang.ini), goes in the
// box as written. Smaller if the line is wider than the screen.
enum HelpKey { HK_TEXT, HK_UPDOWN, HK_LEFTRIGHT, HK_SKIP };

static HelpKey HelpKind(std::string k)
{
    for (char& c : k) c = (char)tolower((unsigned char)c);
    static const char* const ud[] = { "up/down", "su/giu'", "su/giu", "haut/bas", "hoch/runter", "arriba/abajo" };
    static const char* const lr[] = { "left/right", "sinistra/destra", "gauche/droite", "links/rechts", "izquierda/derecha" };
    static const char* const skip[] = { "confirm", "conferma", "valider", "bestaetigen", "confirmar",
                                        "back", "indietro", "retour", "zurueck", "volver" };
    for (const char* w : ud) if (k == w) return HK_UPDOWN;
    for (const char* w : lr) if (k == w) return HK_LEFTRIGHT;
    for (const char* w : skip) if (k == w) return HK_SKIP;
    return HK_TEXT;
}

// a gold triangle pointing up or down, w wide and h high, its box's top left at x, y (the corners in the order of
// Arrow's, so that no culling hides it; the strip's second triangle is empty)
static void ArrowV(float x, float y, float w, float h, bool down)
{
    const float xm = x + w * 0.5f;
    const float xu[4] = { xm, x + w, x, x }, yu[4] = { y, y + h, y + h, y + h };   // tip, base right, base left
    const float xd[4] = { x, x + w, xm, xm }, yd[4] = { y, y, y + h, y + h };      // base left, base right, tip
    Quad(down ? xd : xu, down ? yd : yu, 1.0f, 1.0f, 0.86f, 0.5f);
}

static std::string Trimmed(std::string t)
{
    while (!t.empty() && t.front() == ' ') t.erase(0, 1);
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}

static void Help(const std::string& text, float y)
{
    struct Item { HelpKey kind; std::string key, action; float kw, aw; };
    std::vector<Item> items;
    for (size_t at = 0; at < text.size();) {
        const size_t end = text.find("   ", at);
        const std::string piece = Trimmed(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? text.size() : end + 3;
        if (piece.empty()) continue;
        const size_t colon = piece.find(':');
        Item it{ HK_TEXT, piece, "", 0.0f, 0.0f };
        if (colon != std::string::npos) {
            it.key = Trimmed(piece.substr(0, colon));
            it.action = Trimmed(piece.substr(colon + 1));
            it.kind = HelpKind(it.key);
        }
        if (it.kind != HK_SKIP) items.push_back(it);
    }
    if (items.empty()) return;
    const float pad = 0.008f, gapIn = 0.008f, gapOut = 0.03f, lrW = 0.0125f, udW = 0.014f;
    float ts = g_tableScale * 0.85f;
    auto measure = [&](float sc) {
        float total = gapOut * (float)(items.size() - 1);
        for (Item& it : items) {
            it.kw = it.kind == HK_TEXT ? TextWidth(ST_SEL, it.key, sc) : it.kind == HK_LEFTRIGHT ? lrW * 2 + 0.006f : udW * 2 + 0.006f;
            it.aw = it.action.empty() ? 0.0f : TextWidth(ST_LABEL, it.action, sc);
            total += pad * 2 + it.kw + (it.aw > 0.0f ? gapIn + it.aw : 0.0f);
        }
        return total;
    };
    float total = measure(ts);
    if (total > 0.92f) { ts *= (std::max)(0.7f, 0.92f / total); total = measure(ts); }
    float x = 0.5f - total * 0.5f;
    const float top = y - 0.006f, bottom = y + 0.036f, ex = 0.0012f, ey = 0.002f;
    for (const Item& it : items) {
        const float x1 = x + pad * 2 + it.kw;
        DarkRect(x, top, x1, bottom, 0.85f, 0.03f);
        ColorRect(x, top, x1, top + ey, 0.8f, 0.72f, 0.6f, 0.36f);          // the gold edge
        ColorRect(x, bottom - ey, x1, bottom, 0.8f, 0.72f, 0.6f, 0.36f);
        ColorRect(x, top, x + ex, bottom, 0.8f, 0.72f, 0.6f, 0.36f);
        ColorRect(x1 - ex, top, x1, bottom, 0.8f, 0.72f, 0.6f, 0.36f);
        const float cx = x + pad;
        if (it.kind == HK_TEXT) Put(it.key, cx, y, ts, ST_SEL);
        else if (it.kind == HK_LEFTRIGHT) {
            Arrow(cx, y + 0.002f, 0.026f, false, ST_SEL);
            Arrow(cx + lrW + 0.006f, y + 0.002f, 0.026f, true, ST_SEL);
        } else {
            ArrowV(cx, y + 0.004f, udW, 0.022f, false);
            ArrowV(cx + udW + 0.006f, y + 0.004f, udW, 0.022f, true);
        }
        x = x1;
        if (it.aw > 0.0f) { Put(it.action, x + gapIn, y, ts, ST_LABEL); x += gapIn + it.aw; }
        x += gapOut;
    }
}

// --- icons next to the rows (test of 01/11/2026: the enemies' portraits beside their names) ------------------------
// An icon is a file ROTKPlus_icons\<text key>.raw in the game folder: "RPI1", width and height (32-bit each), then the
// pixels as B, G, R, A. It becomes a texture of the game's device (*0x5C2364: CreateTexture, managed pool, kept for the
// session) and is drawn like the dark rectangle (0x4AE0C0), this time with texture coordinates: stage 0 takes the
// texture times the colour for the duration of the draw. The game keeps its texture stage states in a cache (0x569C68),
// so the ones changed here are read before and written back after; the texture is unbound after (the game binds its
// own before each of its draws, e.g. 0x49C031 for the text).
struct Icon { void* tex; bool tried; };
static std::map<std::string, Icon> g_icons;

// Icons.cpp has just written this icon: the next drawing loads it
void PlayersIconReady(const char* key) { g_icons[key].tried = false; }

static void* IconTexture(const char* key)
{
    Icon& ic = g_icons[key];
    if (ic.tried) return ic.tex;
    ic.tried = true;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\ROTKPlus_icons\\%s.raw", GameDir(), key);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") || !f) return nullptr;
    char tag[4] = {};
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    if (fread(tag, 1, 4, f) == 4 && !memcmp(tag, "RPI1", 4) && fread(&w, 4, 1, f) == 1 && fread(&h, 4, 1, f) == 1 &&
        w && h && w <= 1024 && h <= 1024) {
        px.resize((size_t)w * h * 4);
        if (fread(px.data(), 1, px.size(), f) != px.size()) px.clear();
    }
    fclose(f);
    if (px.empty()) { Log("players: icon %s not readable", key); return nullptr; }
    void* dev = *(void* const*)0x5C2364;
    if (!dev) return nullptr;
    void** vt = *(void***)dev;
    void* tex = nullptr;
    typedef long(__stdcall* CreateTex)(void*, unsigned, unsigned, unsigned, unsigned long, int, int, void**);
    if (((CreateTex)vt[20])(dev, w, h, 1, 0, 21 /* A8R8G8B8 */, 1 /* managed */, &tex) < 0 || !tex) return nullptr;
    struct { int pitch; void* bits; } lr = {};
    void** tv = *(void***)tex;
    typedef long(__stdcall* LockRect)(void*, unsigned, void*, const RECT*, unsigned long);
    typedef long(__stdcall* UnlockRect)(void*, unsigned);
    if (((LockRect)tv[16])(tex, 0, &lr, nullptr, 0) >= 0 && lr.bits) {
        for (uint32_t y = 0; y < h; ++y) memcpy((uint8_t*)lr.bits + (size_t)y * lr.pitch, px.data() + (size_t)y * w * 4, w * 4);
        ((UnlockRect)tv[17])(tex, 0);
    }
    ic.tex = tex;
    Log("players: icon %s loaded (%ux%u)", key, w, h);
    return tex;
}

// shade < 1 darkens it (the heroes never played, the bosses never beaten on the player's card)
static void DrawIcon(void* tex, float x0, float y0, float x1, float y1, float shade = 1.0f)
{
    static float v[16], c[16], uv[16];
    const float xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 }, us[4] = { 0, 1, 0, 1 }, vs[4] = { 0, 0, 1, 1 };
    for (int i = 0; i < 4; ++i) {
        v[i * 4] = xs[i]; v[i * 4 + 1] = ys[i]; v[i * 4 + 2] = 0.1f; v[i * 4 + 3] = 1.0f;
        c[i * 4] = c[i * 4 + 1] = c[i * 4 + 2] = shade;
        c[i * 4 + 3] = 1.0f;
        uv[i * 4] = us[i]; uv[i * 4 + 1] = vs[i]; uv[i * 4 + 2] = uv[i * 4 + 3] = 0.0f;
    }
    Guard([&] {
        *(uint32_t*)(*(uint8_t* const*)0x569C18 + 0x38) = 0x40;
        (*(uint8_t* const*)0x569DF0)[0x90] = 0;
        ((void(__cdecl*)())0x4A2E20)();
        void* dev = *(void* const*)0x5C2364;
        void** vt = *(void***)dev;
        typedef long(__stdcall* GetTSS)(void*, unsigned long, int, unsigned long*);
        typedef long(__stdcall* SetTSS)(void*, unsigned long, int, unsigned long);
        typedef long(__stdcall* SetTex)(void*, unsigned long, void*);
        static const int states[6] = { 1, 2, 3, 4, 5, 6 };   // colour op, arg 1, arg 2; alpha op, arg 1, arg 2
        static const unsigned long ours[6] = { 4, 2, 0, 4, 2, 0 };   // modulate texture x diffuse, for both
        unsigned long saved[6] = {};
        for (int i = 0; i < 6; ++i) ((GetTSS)vt[62])(dev, 0, states[i], &saved[i]);
        ((SetTex)vt[61])(dev, 0, tex);
        for (int i = 0; i < 6; ++i) ((SetTSS)vt[63])(dev, 0, states[i], ours[i]);
        ((int(__cdecl*)(int, const float*, const float*, const float*, int))0x4AE0C0)(5, v, c, uv, 4);
        for (int i = 0; i < 6; ++i) ((SetTSS)vt[63])(dev, 0, states[i], saved[i]);
        ((SetTex)vt[61])(dev, 0, nullptr);
    });
}

// every other row of a table a little darker, to follow a row across the columns
static void Band(int row, float y, float dy) { if (row & 1) DarkRect(0.035f, y - 0.006f, 0.965f, y + dy - 0.006f, g_shade * 0.45f); }

static std::string Time(int secs)
{
    char b[32];
    if (secs >= 3600) sprintf_s(b, "%dh %02dm", secs / 3600, secs / 60 % 60);
    else sprintf_s(b, "%dm %02ds", secs / 60, secs % 60);
    return b;
}

// a number with the language's separator between the thousands (12.345 / 12,345 / 12 345)
static std::string Num(int v)
{
    const std::string d = std::to_string(v < 0 ? -(long long)v : (long long)v);
    std::string out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += Txt(T_THOUSANDS);
        out += d[i];
    }
    return v < 0 ? "-" + out : out;
}


static const std::string& HeroName(int h);   // (further down, with the statistics)
static int FavouriteHeroIndex(const Vals& v);
static std::string StatValue(const Vals& v, int row);

// the records (the page Record and the preview of the Fellowship page): the best value and who holds it (ties: every
// name), false if nobody has one yet. group: the page's box (0 in a level, 1 overall, 2 versus games), with a short
// label under the group's heading
struct RecordDef { int label; const char* key; bool lowest; int group; int brief; };
static const RecordDef kRecords[] = { { R_KILLS, "BestKills", false, 0, S_KILLS }, { R_PERFECT, "BestPerfect", false, 0, S_PERFECT },
                                      { R_ASSISTS, "BestAssists", false, 0, S_ASSISTS }, { R_PVP_FAST, "FastestPvpWin", true, 2, T_REC_FAST },
                                      { R_TOT_KILLS, "Kills", false, 1, S_KILLS }, { R_TOT_WINS, "Wins", false, 1, S_WINS },
                                      { R_PVP_WINS, "PvpWins", false, 2, S_PVP_WINS } };

static bool RecordHolder(const RecordDef& r, int& best, std::string& who, std::vector<std::string>* names = nullptr)
{
    best = 0; who.clear();
    if (names) names->clear();
    for (const Totals& t : g_table) {
        const int x = Get(t.v, r.key);
        if (x <= 0) continue;
        if (who.empty() || (r.lowest ? x < best : x > best)) { best = x; who = t.name; if (names) names->assign(1, t.name); }
        else if (x == best) { who += ", " + t.name; if (names) names->push_back(t.name); }
    }
    return !who.empty();
}

static const Totals* TotalsOf(const std::string& name)
{
    if (name.empty()) return nullptr;
    for (const Totals& t : g_table) if (SameName(t.name, name)) return &t;
    return nullptr;
}

static void* HeroIcon(int h)
{
    if (h < 0 || h > 8) return nullptr;
    char key[8];
    sprintf_s(key, "Hero%d", h);
    return IconTexture(key);
}

static int FavouriteOf(const std::string& name)
{
    const Totals* t = RealName(name) ? TotalsOf(name) : nullptr;
    return t ? FavouriteHeroIndex(t->v) : -1;
}

static std::string NoColon(std::string t)
{
    while (!t.empty() && (t.back() == ':' || t.back() == ' ')) t.pop_back();
    return t;
}

// the box behind the selected entry, and a group's heading: small, gold, with a thin gold line after it to the edge of
// the entries (test of 02/11/2026: grey and small, the headings looked like entries)
static void SelBox(float x0, float y, float x1, float rowH) { DarkRect(x0, y - 0.006f, x1, y + rowH - 0.006f, g_shade * 1.3f); }

static void Heading(const std::string& t, float x, float y, float right)
{
    const float hs = g_tableScale * 0.8f;
    Put(t, x, y, hs, ST_HEAD, 0, right - x);
    const float w = (std::min)(TextWidth(ST_HEAD, t, hs), right - x);
    ColorRect(x + w + 0.012f, y + 0.0115f, right, y + 0.0145f, 0.75f, 0.72f, 0.6f, 0.36f);
}

// A player's place (the Fellowship page and Who is playing?): "Player N", the favourite hero's portrait, the name between
// two arrows (a guest and "New name..." dimmed). The arrows can be clicked (left/right on the place), the rest selects it.
// value: a name, "" (guest) or kNewName.
static void DrawPlace(int idx, float x, float y, float rowH, float right, bool sel, const std::string& label,
                      const std::string& value, float labelW = 0.222f)
{
    const float ts = g_scale * 0.85f, k = 9.0f / 16.0f;
    const bool named = RealName(value);
    Put(label, x, y, ts, sel ? ST_SEL : ST_VALUE, 0, labelW - 0.007f);
    const float px = x + labelW;
    if (void* icon = named ? HeroIcon(FavouriteOf(value)) : nullptr) DrawIcon(icon, px, y - 0.003f, px + 0.044f * k, y + 0.041f);
    const std::string shown = value == kNewName ? Txt(T_WHO_NEW) : named ? value : Txt(T_WHO_GUEST);
    const float ah = 0.03f, aw = ah * 0.48f, ax = px + 0.033f, nx = ax + aw + 0.008f;
    const float maxW = right - 0.012f - aw - 0.008f - nx, nw = (std::min)(TextWidth(ST_VALUE, shown, ts), maxW);
    Arrow(ax, y + 0.006f, ah, false, sel ? ST_SEL : ST_VALUE);
    Put(shown, nx, y, ts, sel ? ST_SEL : named ? ST_VALUE : ST_HELP, 0, maxW);
    Arrow(nx + nw + 0.008f, y + 0.006f, ah, true, sel ? ST_SEL : ST_VALUE);
    AddHit(ax - 0.012f, y - 0.006f, ax + aw + 0.006f, y + rowH - 0.006f, H_LEFT, idx);   // (before the place: found first)
    AddHit(nx + nw + 0.002f, y - 0.006f, nx + nw + 0.03f, y + rowH - 0.006f, H_RIGHT, idx);
    AddHit(x - 0.02f, y - 0.006f, right, y + rowH - 0.006f, H_SELECT, idx);
}

// the preview on the right of the two pages
static const float kPanelX = 0.595f, kPanelR = 0.935f;
static void PanelFrame() { DarkRect(kPanelX - 0.015f, 0.185f, kPanelR + 0.015f, 0.745f, g_shade); }

// a place's player: the favourite hero, the name and four numbers; a guest or "New name..." what it means. hint: the
// line at the bottom (-1 none)
static void PlayerPreview(const std::string& label, const std::string& value, int hint)
{
    const float s = g_tableScale, px = kPanelX, pr = kPanelR, pw = pr - px, k = 9.0f / 16.0f;
    PanelFrame();
    Put(label, px, 0.2f, s * 0.8f, ST_LABEL, 0, pw);
    if (hint >= 0) Put(Txt(hint), px, 0.69f, s * 0.8f, ST_HELP, 0, pw);
    if (!RealName(value)) {
        Put(Txt(value == kNewName ? T_WHO_NEW : T_WHO_GUEST), px, 0.245f, s * 1.3f, ST_SEL, 0, pw);
        if (value.empty()) Put(Txt(T_PANEL_GUEST), px, 0.34f, s * 0.85f, ST_VALUE, 0, pw);
        return;
    }
    const Totals* t = TotalsOf(value);
    const int fav = t ? FavouriteHeroIndex(t->v) : -1;
    const float ph = 0.15f;
    float tx = px;
    if (void* icon = HeroIcon(fav)) { DrawIcon(icon, px, 0.24f, px + ph * k, 0.24f + ph); tx = px + ph * k + 0.02f; }
    Put(value, tx, 0.255f, s * 1.3f, ST_SEL, 0, pr - tx);
    if (fav >= 0) Put(Txt(S_HERO) + ": " + HeroName(fav), tx, 0.32f, s * 0.7f, ST_LABEL, 0, pr - tx);
    if (!t) { Put(Txt(T_NONE), px, 0.45f, s * 0.85f, ST_VALUE, 0, pw); return; }
    const int nLevels = (int)(sizeof kLevels / sizeof kLevels[0]);
    int done = 0;
    for (int i = 0; i < nLevels; ++i) if (Get(t->v, std::string("Won_") + kLevels[i]) > 0) ++done;
    const std::pair<int, std::string> nums[4] = {
        { S_WINS, Num(Get(t->v, "Wins")) + " / " + Num(Get(t->v, "Levels")) }, { S_TIME, Time(Get(t->v, "Seconds")) },
        { S_KILLS, StatValue(t->v, S_KILLS) }, { S_LEVELS_WON, std::to_string(done) + "/" + std::to_string(nLevels) } };
    for (int i = 0; i < 4; ++i) {
        const float x = px + (i % 2) * (pw * 0.5f), y = 0.43f + (i / 2) * 0.1f;
        Put(NoColon(Txt(nums[i].first)), x, y, s * 0.7f, ST_LABEL, 0, pw * 0.5f - 0.01f);
        Put(nums[i].second, x, y + 0.035f, s * 1.05f, ST_SEL, 0, pw * 0.5f - 0.01f);
    }
}

// The preview on the right of the Fellowship page: what the selected entry holds (02/11/2026). A place
// shows its player (PlayerPreview); the other entries what they open.
static void MainPanel(int row)
{
    if (row == ROW_BACK) return;
    if (row < 4) {
        const std::string v = g_slotNew[row] ? std::string(kNewName) : std::string(g_name[row]);
        PlayerPreview(g_playerLabel[row], v, RealName(v) ? T_PANEL_OPEN : T_PANEL_TYPE);
        return;
    }
    const float s = g_tableScale, px = kPanelX, pr = kPanelR, pw = pr - px, k = 9.0f / 16.0f;
    PanelFrame();
    static const int heads[] = { T_STATS, T_CARDS, T_RECORDS, T_LAST, T_MANAGE };
    Put(Txt(heads[row - ROW_STATS]), px, 0.2f, s * 1.1f, ST_HEAD, 0, pw);
    float y = 0.28f;
    switch (row) {
    case ROW_STATS:   // the tabs it opens
        for (int t = 0; t < TAB_COUNT; ++t, y += 0.06f) Put(Txt(TB_GAMES + t), px + 0.01f, y, s * 0.9f, ST_VALUE, 0, pw);
        break;
    case ROW_CARDS:   // who has one, with the favourite hero and the time played
        for (size_t i = 0; i < g_table.size() && i < 7; ++i, y += 0.06f) {
            if (void* icon = HeroIcon(FavouriteHeroIndex(g_table[i].v)))
                DrawIcon(icon, px, y - 0.004f, px + 0.045f * k, y - 0.004f + 0.045f);
            Put(g_table[i].name, px + 0.035f, y, s * 0.85f, ST_VALUE, 0, pw * 0.6f);
            Put(Time(Get(g_table[i].v, "Seconds")), pr, y, s * 0.75f, ST_LABEL, 1, pw * 0.35f);
        }
        if (g_table.empty()) Put(Txt(T_NONE), px, y, s * 0.85f, ST_VALUE, 0, pw);
        break;
    case ROW_RECORDS:   // the first four
        for (int i = 0; i < 4; ++i, y += 0.105f) {
            int best = 0; std::string who;
            Put(Txt(kRecords[i].label), px, y, s * 0.7f, ST_LABEL, 0, pw);
            if (!RecordHolder(kRecords[i], best, who)) { Put("-", px, y + 0.035f, s * 0.9f, ST_VALUE); continue; }
            Put((kRecords[i].lowest ? Time(best) : Num(best)) + "   " + who, px, y + 0.035f, s * 0.9f, ST_SEL, 0, pw);
        }
        break;
    case ROW_LAST: {   // the summary of the last game
        if (g_sum.empty()) { Put(Txt(T_LAST_NONE), px, y, s * 0.85f, ST_VALUE, 0, pw); break; }
        std::string kind = Txt(g_sumKind == 3 ? T_LAST_PVP : g_sumKind == 1 ? T_LAST_COOP : T_LAST_LOST);
        const std::string level = g_sumLevel.empty() ? std::string() : LangGameText(g_sumLevel.c_str());
        if (!level.empty() && level != g_sumLevel) kind += ": " + level;
        Put(kind, px, y, s * 0.8f, ST_LABEL, 0, pw);
        for (size_t i = 0; i < g_sum.size() && i < 4; ++i) Put(SumLine(g_sum[i]), px, y + 0.06f + i * 0.06f, s * 0.7f, ST_VALUE, 0, pw);
        break;
    }
    case ROW_MANAGE:
        Put(Txt(T_PANEL_MANAGE), px, y, s * 0.85f, ST_VALUE, 0, pw);
        break;
    }
}

// The Fellowship page (02/11/2026): three groups under a heading (who plays, statistics, names), the
// entries a step further in; the four places (DrawPlace); the selected entry on a darker box; on the right the preview
// (MainPanel); a help line at the bottom.
static void DrawMain()
{
    const float s = g_scale, ts = s * 0.85f, x0 = 0.10f, ex = 0.125f, rowH = 0.055f, right = 0.575f;
    Put(Txt(T_TITLE), x0, 0.09f, s * 1.2f, ST_TITLE);   // (Txt also loads g_playerLabel)
    Heading(Txt(T_SEC_WHO), x0, 0.17f, right);
    for (int i = 0; i < 4; ++i) {
        const float y = 0.205f + i * rowH;
        if (g_row == i) SelBox(x0, y, right, rowH);
        DrawPlace(i, ex, y, rowH, right, g_row == i, g_playerLabel[i],
                  g_slotNew[i] ? std::string(kNewName) : std::string(g_name[i]));
    }
    Heading(Txt(T_SEC_STATS), x0, 0.445f, right);
    Heading(Txt(T_SEC_NAMES), x0, 0.715f, right);
    static const int labels[] = { T_STATS, T_CARDS, T_RECORDS, T_LAST, T_MANAGE, T_BACK };
    for (int e = 0; e < 6; ++e) {
        const int row = ROW_STATS + e;
        const float y = e < 4 ? 0.48f + e * rowH : 0.75f + (e - 4) * rowH;
        if (g_row == row) SelBox(x0, y, right, rowH);
        AddHit(x0 - 0.02f, y - 0.006f, right, y + rowH - 0.006f, H_SELECT, row);
        Put(Txt(labels[e]), ex, y, ts, g_row == row ? ST_SEL : ST_VALUE, 0, right - ex - 0.01f);
    }
    MainPanel(g_row);
    Help(Txt(T_MAIN_HELP));
}

// a place's name for the teams' lines: the name, or "Player N"
static std::string PlaceName(int r)
{
    return RealName(g_lineup[r]) ? g_lineup[r] : g_playerLabel[r];
}

// The Multiplayer page, layout "one table" (chosen on 04/11/2026): two dark boxes side by side, the game on the left
// (each rule a row with its value between two arrows) and who plays on the right (the places); the box with the cursor
// a little darker; Start and Back side by side under the boxes. The teams are the Sun's (gold) and the Moon's
// (silver-blue).
static void DrawMulti()
{
    std::vector<int> rows;
    MpRows(0, rows);
    // up to seven rows on the left (a tournament with teams): a little closer then, and the boxes as tall as the longer
    const float s = g_scale, ts = s * 0.85f, vs = s * 0.75f, rowH = rows.size() > 5 ? 0.052f : 0.06f;
    const float lx = 0.05f, lr = 0.47f, rx = 0.53f, rr = 0.95f, top = 0.165f;
    const float leftEnd = top + 0.075f + rowH * (float)rows.size() + (g_mpMode == MP_TEAMS ? 0.175f : 0.07f);
    const float rightEnd = top + 0.075f + rowH * (float)g_lineupN + 0.03f;
    const float bottom = (std::min)(0.80f, (std::max)(0.665f, (std::max)(leftEnd, rightEnd)));
    const int box = MpBoxOf(g_lineupRow);
    Put(Txt(T_MP_TITLE), 0.06f, 0.075f, s * 1.2f, ST_TITLE);   // (Txt also loads g_playerLabel)
    DarkRect(lx, top, lr, bottom, g_shade * (box == 0 ? 1.3f : 0.85f));
    DarkRect(rx, top, rr, bottom, g_shade * (box == 1 ? 1.3f : 0.85f));
    // the game
    Heading(Txt(T_MP_GAME), lx + 0.02f, top + 0.02f, lr - 0.02f);
    float y = top + 0.075f;
    for (int id : rows) {
        const bool sel = g_lineupRow == id;
        int label = T_MP_MODE;
        std::string value;
        switch (id) {
        case MR_MODE: value = Txt(g_mpMode == MP_CAMPAIGN ? T_MP_CAMPAIGN : g_mpMode == MP_FFA ? T_MP_FFA : T_MP_TEAMS); break;
        case MR_PLAYERS: label = T_MP_PLAYERS; value = std::to_string(MpPlayers()); break;
        case MR_SPLIT: {
            label = T_MP_SPLIT;
            int a[2] = { 0, 0 }, b[2] = { 0, 0 }, na = 0, nb = 0;
            for (int k = 0; k < 4; ++k) {
                if (MpTeamOf(k) == 0 && na < 2) a[na++] = k + 1;
                else if (MpTeamOf(k) == 1 && nb < 2) b[nb++] = k + 1;
            }
            value = Replace(Replace(Replace(Replace(Txt(T_MP_SPLIT_VAL), "{a}", std::to_string(a[0])), "{b}", std::to_string(a[1])),
                                    "{c}", std::to_string(b[0])), "{d}", std::to_string(b[1]));
            break;
        }
        case MR_ENEMIES: label = T_MP_ENEMIES; value = Txt(g_mpEnemies ? T_MP_ENEMIES_ON : T_MP_HEROES); break;
        case MR_LIVES: label = T_MP_LIVES; value = std::to_string(g_mpLives); break;
        case MR_RULE: label = T_MP_RULE; value = Txt(g_mpTimed ? T_MP_RULE_TIME : T_MP_RULE_LAST); break;
        case MR_TIME: label = T_MP_TIME; value = Replace(Txt(T_MP_MINUTES), "{n}", std::to_string(g_mpMinutes)); break;
        case MR_ROUNDS:
            label = T_MP_ROUNDS;
            value = g_mpRounds == 1 ? Txt(T_MP_SINGLE) : Replace(Txt(T_MP_BEST), "{n}", std::to_string(g_mpRounds));
            break;
        }
        if (sel) SelBox(lx + 0.015f, y, lr - 0.015f, rowH);
        Put(Txt(label), lx + 0.03f, y, vs, sel ? ST_SEL : ST_VALUE, 0, 0.125f);
        const float ax = lx + 0.165f, ah = 0.03f, aw = ah * 0.48f, nx = ax + aw + 0.008f;
        const float maxW = lr - 0.03f - aw - 0.008f - nx, nw = (std::min)(TextWidth(ST_VALUE, value, vs), maxW);
        Arrow(ax, y + 0.006f, ah, false, sel ? ST_SEL : ST_VALUE);
        Put(value, nx, y, vs, sel ? ST_SEL : ST_VALUE, 0, maxW);
        Arrow(nx + nw + 0.008f, y + 0.006f, ah, true, sel ? ST_SEL : ST_VALUE);
        AddHit(ax - 0.012f, y - 0.006f, ax + aw + 0.006f, y + rowH - 0.006f, H_LEFT, id);   // (before the row: found first)
        AddHit(nx + nw + 0.002f, y - 0.006f, nx + nw + 0.03f, y + rowH - 0.006f, H_RIGHT, id);
        AddHit(lx, y - 0.006f, lr, y + rowH - 0.006f, H_SELECT, id);
        y += rowH;
    }
    // under the rules: the two teams, or where the level is chosen
    y += 0.03f;
    if (g_mpMode == MP_TEAMS) {
        Heading(Txt(T_MP_TEAMS_HEAD), lx + 0.02f, y, lr - 0.02f);
        for (int t = 0; t < 2; ++t) {
            std::string names;
            for (int k = 0; k < 4; ++k)
                if (MpTeamOf(k) == t) names = names.empty() ? PlaceName(k) : LangText(10, 0, PlaceName(k).c_str(), names.c_str());
            Put(LangText(9, 0, names.c_str(), LangText(11 + t).c_str()), lx + 0.03f, y + 0.045f + t * 0.05f,
                g_tableScale * 0.85f, t == 0 ? ST_SEL : ST_MOON, 0, lr - lx - 0.05f);
        }
    } else Put(Txt(g_mpMode == MP_FFA ? T_MP_NOTE_ARENA : T_MP_NOTE_MAP), lx + 0.03f, y, g_tableScale * 0.8f, ST_HELP, 0,
               lr - lx - 0.05f);
    // who plays
    Heading(Txt(T_SEC_WHO), rx + 0.02f, top + 0.02f, rr - 0.02f);
    y = top + 0.075f;
    const float tagW = g_mpMode == MP_TEAMS ? 0.08f : 0.0f;
    for (int r = 0; r < g_lineupN; ++r, y += rowH) {
        const bool sel = g_lineupRow == r;
        if (sel) SelBox(rx + 0.015f, y, rr - 0.015f, rowH);
        DrawPlace(r, rx + 0.03f, y, rowH, rr - 0.02f - tagW, sel, std::to_string(r + 1), g_lineup[r], 0.035f);
        if (tagW > 0.0f)
            Put(Txt(MpTeamOf(r) == 0 ? T_MP_SUN : T_MP_MOON), rr - 0.03f, y + 0.006f, g_tableScale * 0.8f,
                MpTeamOf(r) == 0 ? ST_SEL : ST_MOON, 1, tagW);
    }
    // Start and Back, side by side under the boxes
    const float bs = ts * 1.1f, gap = 0.12f, by = bottom + 0.035f;
    const float w0 = TextWidth(ST_SEL, Txt(T_WHO_START), bs), w1 = TextWidth(ST_VALUE, Txt(T_BACK), bs);
    const float x0 = 0.5f - (w0 + gap + w1) * 0.5f, x1 = x0 + w0 + gap;
    for (int e = 0; e < 2; ++e) {
        const int id = e ? MR_BACK : MR_START;
        const float x = e ? x1 : x0, w = e ? w1 : w0;
        const bool sel = g_lineupRow == id;
        if (sel) SelBox(x - 0.025f, by, x + w + 0.025f, rowH + 0.01f);
        AddHit(x - 0.025f, by - 0.006f, x + w + 0.025f, by + rowH, H_SELECT, id);
        Put(Txt(e ? T_BACK : T_WHO_START), x, by, bs, sel ? ST_SEL : ST_VALUE);
    }
    Help(Txt(T_MP_HELP));
}

// Who is playing? in the same style (02/11/2026): the places of the game about to start, then Start and Back; on the
// right the selected player (or, on Start, the whole line-up); the help line at the bottom.
static void DrawLineup()
{
    if (g_multi) { DrawMulti(); return; }
    const float s = g_scale, ts = s * 0.85f, x0 = 0.10f, ex = 0.125f, rowH = 0.06f, right = 0.575f;
    Put(Txt(T_WHO_TITLE), x0, 0.09f, s * 1.2f, ST_TITLE);   // (Txt also loads g_playerLabel)
    for (int r = 0; r < g_lineupN; ++r) {
        const float y = 0.205f + r * rowH;
        if (g_lineupRow == r) SelBox(x0, y, right, rowH);
        DrawPlace(r, ex, y, rowH, right, g_lineupRow == r, g_playerLabel[r], g_lineup[r]);
    }
    const float y0 = 0.205f + g_lineupN * rowH + 0.04f;
    for (int e = 0; e < 2; ++e) {
        const int row = g_lineupN + e;
        const float y = y0 + e * rowH;
        if (g_lineupRow == row) SelBox(x0, y, right, rowH);
        AddHit(x0 - 0.02f, y - 0.006f, right, y + rowH - 0.006f, H_SELECT, row);
        Put(Txt(e ? T_BACK : T_WHO_START), ex, y, ts, g_lineupRow == row ? ST_SEL : ST_VALUE, 0, right - ex - 0.01f);
    }
    if (g_lineupRow < g_lineupN) {
        const std::string& v = g_lineup[g_lineupRow];
        PlayerPreview(g_playerLabel[g_lineupRow], v, v == kNewName ? T_PANEL_TYPE : -1);
    } else if (g_lineupRow == g_lineupN) {   // Start: who plays, with their portraits
        const float px = kPanelX, pw = kPanelR - kPanelX, k = 9.0f / 16.0f;
        PanelFrame();
        Put(Txt(T_WHO_START), px, 0.2f, g_tableScale * 1.1f, ST_HEAD, 0, pw);
        for (int r = 0; r < g_lineupN; ++r) {
            const float y = 0.28f + r * 0.07f;
            const std::string& v = g_lineup[r];
            if (void* icon = RealName(v) ? HeroIcon(FavouriteOf(v)) : nullptr)
                DrawIcon(icon, px, y - 0.004f, px + 0.05f * k, y - 0.004f + 0.05f);
            Put(g_playerLabel[r] + ": " + (v == kNewName ? Txt(T_WHO_NEW) : RealName(v) ? v : Txt(T_WHO_GUEST)),
                px + 0.04f, y, g_tableScale * 0.85f, RealName(v) ? ST_VALUE : ST_HELP, 0, pw - 0.04f);
        }
    }
    Help(Txt(T_WHO_HELP));
}

static void DrawEdit()
{
    const float s = g_scale;
    const std::string title = g_editLineup ? Replace(Txt(T_WHO_EDIT), "{player}", g_playerLabel[g_editSlot])
                            : g_editSlot >= 0 ? Replace(Txt(T_EDIT), "{player}", g_playerLabel[g_editSlot])
                            : g_editSlot == kEditAdd ? Txt(T_ADD_NAME)
                                              : Replace(Txt(T_RENAME_TITLE), "{name}", g_actName);
    Put(title, 0.10f, 0.14f, s, ST_TITLE, 0, 0.85f);
    Text(std::string(g_edit) + "_", 0.10f, 0.24f, s * 1.2f, true);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 10; ++c) {
            const bool sel = g_gridR == r && g_gridC == c;
            const char ch[2] = { kGrid[r][c], 0 };
            AddHit(0.09f + c * 0.07f, 0.38f + r * 0.08f, 0.16f + c * 0.07f, 0.46f + r * 0.08f, H_SELECT, r * 10 + c);
            Text(sel ? std::string("[") + ch + "]" : std::string(" ") + ch, 0.10f + c * 0.07f, 0.38f + r * 0.08f, s, sel);
        }
    const int sp[3] = { T_SPACE, T_DELETE, T_DONE };
    for (int c = 0; c < 3; ++c) {
        const bool sel = g_gridR == 4 && g_gridC == c;
        AddHit(0.09f + c * 0.24f, 0.72f, 0.31f + c * 0.24f, 0.80f, H_SELECT, 40 + c);
        Text((sel ? "[" : " ") + Txt(sp[c]) + (sel ? "]" : ""), 0.10f + c * 0.24f, 0.72f, s, sel);
    }
    Put(Txt(T_EDIT_HELP), 0.10f, 0.84f, g_tableScale, ST_HELP, 0, 0.85f);
}

static const std::string& HeroName(int h) { return Txt(T_HERO0 + h); }

static std::string FavouriteHero(const Vals& v)
{
    int best = -1, n = 0;   // levels and versus games with each hero
    for (int h = 0; h < 9; ++h) {
        const int x = Get(v, HeroKey(h, "")) + Get(v, HeroKey(h, "Pvp"));
        if (x > n) { n = x; best = h; }
    }
    return best >= 0 ? HeroName(best) : "-";
}

// The game's character types (+0x78) of each kind of enemy, from the Palantirs' waves (log of 01/11/2026: each new type
// with its wave and its life, the counts matching the waves of the guide). Orcs: 3 (50 life), 0x1A archer (30). Uruks of
// Mordor: 0x15 (70), 0x26 archer (40), 0x25 shield (100), 0x28 champion (210). Uruk-hai: 0x14 (60, Helm's Deep), 0x29 (60, with a shield or not),
// 0x2A archer (30). Berserkers 0x2B (400). The Dead: 0x2C (60), 0x2E archer (40), 0x2D shield (50), 0x2F champion
// (210). Easterlings: 0x34 (100), 0x35 shield (70), 0x37 champion (210). Spiders 0x4A (100, the big ones too). Bosses:
// Gollum 0x4E (600). Southern Gate (log of 01/11/2026): trolls 0x4C (400), the mumak 0x32. Still to be found: the
// other bosses; they are "other enemies" until then.
static const struct { int row; int types[5]; } kFoeKinds[] = {
    // 0x23 (50 of life, Minas Tirith, Shelob's lair, Cirith Ungol) an orc; 0x24 (210, Osgiliath, Cirith Ungol) an Uruk
    { F_ORC, { 0x03, 0x1A, 0x23 } }, { F_URUK, { 0x15, 0x25, 0x26, 0x28, 0x24 } }, { F_URUKHAI, { 0x14, 0x29, 0x2A } },
    { F_BERSERKER, { 0x2B } }, { F_DEAD, { 0x2C, 0x2D, 0x2E, 0x2F } }, { F_EAST, { 0x34, 0x35, 0x37 } },
    { F_SPIDER, { 0x4A } }, { F_TROLL, { 0x4C } }, { F_MUMAK, { 0x32 } }, { F_GOLLUM, { 0x4E } },
    { F_SHELOB, { 0x49 } }, { F_GORBAG, { 0x3D } }, { F_KINGDEAD, { 0x4D } }, { F_NAZGUL, { 0x52 } }, { F_MOUTH, { 0x53 } },
};

// Under a people's row its kinds, from the same types (rows ROW_FOE + i). The captains are the ones with their own life
// bar (210 of life): at the Black Gate the "0/6" of the objective went up with the kills of 0x24 and 0x28 (02/11/2026).
// The Uruk-hai of Saruman have one type with and without a shield (0x29).
enum { ROW_FOE = 4000 };
enum { ROW_VS = 5000 };   // the Versus tab's rows ROW_VS + i: the knock-downs of the i-th player shown
static const struct { int people; int label; int types[2]; } kFoeSubs[] = {
    { F_ORC, V_SOLDIERS, { 0x03, 0x23 } }, { F_ORC, V_ARCHERS, { 0x1A } },
    { F_URUK, V_SOLDIERS, { 0x15 } }, { F_URUK, V_SHIELD, { 0x25 } }, { F_URUK, V_ARCHERS, { 0x26 } },
    { F_URUK, V_CAPTAINS, { 0x24, 0x28 } },
    { F_URUKHAI, V_SOLDIERS, { 0x14, 0x29 } }, { F_URUKHAI, V_ARCHERS, { 0x2A } },
    { F_DEAD, V_SOLDIERS, { 0x2C } }, { F_DEAD, V_SHIELD, { 0x2D } }, { F_DEAD, V_ARCHERS, { 0x2E } }, { F_DEAD, V_CAPTAINS, { 0x2F } },
    { F_EAST, V_SOLDIERS, { 0x34 } }, { F_EAST, V_SHIELD, { 0x35 } }, { F_EAST, V_CAPTAINS, { 0x37 } },
};
static const int kFoeSubCount = sizeof kFoeSubs / sizeof kFoeSubs[0];
static bool IsLevelRow(int r) { return r >= ROW_LEVEL && r < ROW_FOE; }
static bool IsFoeSub(int r) { return r >= ROW_FOE && r < ROW_FOE + kFoeSubCount; }

static int FoeTypeKills(const Vals& v, int type)
{
    char key[16];
    sprintf_s(key, "Foe%02X", type);
    return Get(v, key);
}

static int FoeRowOf(int type)
{
    for (const auto& k : kFoeKinds)
        for (int t : k.types) if (t && t == type) return k.row;
    return F_OTHER;
}

// the kills of a kind of enemy: the Foe<type> counters of the types of that row (F_OTHER: every type not in the table)
static int FoeKills(const Vals& v, int row)
{
    int n = 0;
    for (const auto& kv : v)
        if (!kv.first.compare(0, 3, "Foe") && kv.first.size() > 3 &&
            FoeRowOf((int)strtol(kv.first.c_str() + 3, nullptr, 16)) == row) n += kv.second;
    return n;
}

// the hero played in most levels and versus games, -1 if none (as FavouriteHero)
static int FavouriteHeroIndex(const Vals& v)
{
    int best = -1, n = 0;
    for (int h = 0; h < 9; ++h) {
        const int x = Get(v, HeroKey(h, "")) + Get(v, HeroKey(h, "Pvp"));
        if (x > n) { n = x; best = h; }
    }
    return best;
}

static std::string StatValue(const Vals& v, int row)
{
    if (IsLevelRow(row)) return Num(Get(v, std::string("Won_") + kLevels[row - ROW_LEVEL]));
    if (IsFoeSub(row)) {
        int n = 0;
        for (int t : kFoeSubs[row - ROW_FOE].types) if (t) n += FoeTypeKills(v, t);
        return Num(n);
    }
    switch (row) {
    case S_LEVELS: return Num(Get(v, "Levels"));
    case S_WINS: return Num(Get(v, "Wins"));
    case S_TIME: return Time(Get(v, "Seconds"));
    case S_KILLS: return Num(Get(v, "Kills"));
    case S_RANGED: return Num(Get(v, "RangedKills"));
    case S_FINISH: return Num(Get(v, "KillingMoves"));
    case S_SOLO: return Num(Get(v, "SoloKills"));
    case S_ASSISTS: return Num(Get(v, "Assists"));
    case S_DEATHS: return Num(Get(v, "Deaths"));
    case S_PERFECT: return Num(Get(v, "Perfect"));
    case S_EXCELLENT: return Num(Get(v, "Excellent"));
    case S_GOOD: return Num(Get(v, "Good"));
    case S_FAIR: return Num(Get(v, "Fair"));
    case S_EXP: return Num(Get(v, "Experience"));
    case S_HERO: return FavouriteHero(v);
    case S_PVP_GAMES: return Num(Get(v, "PvpGames"));
    case S_PVP_WINS: return Num(Get(v, "PvpWins"));
    case S_PVP_KILLS: return Num(Get(v, "PvpKills"));
    case S_PVP_FALLS: return Num(Get(v, "PvpFalls"));
    case S_PVP_ELIMS: return Num(Get(v, "PvpElims"));
    case S_PVP_ROUNDS: return Num(Get(v, "PvpRounds"));
    case S_PVP_ROUND_WINS: return Num(Get(v, "PvpRoundWins"));
    case S_PVP_TOURNEYS: return Num(Get(v, "PvpTourneys"));
    case S_PVP_CUPS: return Num(Get(v, "PvpCups"));
    case S_PVP_TEAM: return Num(Get(v, "PvpTeamWins")) + " / " + Num(Get(v, "PvpTeamGames"));
    case S_PVP_TIMED: return Num(Get(v, "PvpTimedWins")) + " / " + Num(Get(v, "PvpTimedGames"));
    case S_PVP_BEST_TIMED: return Num(Get(v, "BestTimedKills"));
    case F_ORC: case F_URUK: case F_URUKHAI: case F_BERSERKER: case F_DEAD: case F_EAST: case F_SPIDER: case F_TROLL:
    case F_MUMAK: case F_GOLLUM: case F_SHELOB: case F_GORBAG: case F_KINGDEAD: case F_NAZGUL: case F_MOUTH: case F_OTHER:
        return Num(FoeKills(v, row));
    case S_MULTI: return Num(Get(v, "BestMulti"));
    case C_RISING: return Num(Get(v, "Combo2C"));
    case S_SPEAR: return Num(Get(v, "SpearKills"));
    case S_PARRY_HITS: return Num(Get(v, "ParriedHits"));
    case S_PARRY_ARROWS: return Num(Get(v, "ParriedArrows"));
    case S_LADDERS: return Num(Get(v, "Ladders"));
    case P_HEAL30: return Num(Get(v, "Pick1"));
    case P_HEAL60: return Num(Get(v, "Pick2"));
    case S_BURNED: return Num(Get(v, "Burned"));
    case S_KICKS: return Num(Get(v, "KicksHit")) + " / " + Num(Get(v, "Kicks"));
    case S_SPEED_HITS: return Num(Get(v, "SpeedHits"));
    case S_FIERCE_HITS: return Num(Get(v, "FierceHits"));
    case S_SHIELDS: return Num(Get(v, "ShieldsBroken"));
    case P_ARROWS: return Num(Get(v, "Pick4") + Get(v, "Pick5") + Get(v, "Pick6") + Get(v, "Pick8"));
    case P_EXP: return Num(Get(v, "Pick7"));
    case C_HEWER: case C_JUDGMENT: case C_BALROG: case C_DARK: case C_CLEAVER: case C_LIGHTNING: case C_HAMMER:
    case C_SWIFT: case C_BANE: {
        // the state each combo ends in (the trail of the log of 01/11/2026, Aragorn and Gandalf): a combo is counted
        // on its last part, the parts before it are shared (combo_1 begins both Final Judgment and Balrog's Gambit)
        static const char* const key[] = { "Combo19", "Combo1C", "Combo1A", "Combo18", "Combo12", "Combo16", "Combo14",
                                           "Combo13", "Combo48" };
        return Num(Get(v, key[row - C_HEWER]));
    }
    }
    return "";
}

// The statistics in six tabs (02/11/2026: tabs rather than one long page; the things picked up went with the games).
// Every tab fits the page but the enemies' (26 rows with their kinds, one tab rather than two): that one scrolls.
// Groups inside a tab have half a row between them (-1).
static const std::vector<int> kTabRows[TAB_COUNT] = {
    { S_LEVELS, S_WINS, S_TIME, S_HERO, S_PERFECT, S_EXCELLENT, S_GOOD, S_FAIR, S_EXP, -1, P_HEAL30, P_HEAL60, P_ARROWS, P_EXP },
    { S_LEVELS_WON, ROW_LEVEL + 0, ROW_LEVEL + 1, ROW_LEVEL + 2, ROW_LEVEL + 3, ROW_LEVEL + 4, ROW_LEVEL + 5, ROW_LEVEL + 6,
      ROW_LEVEL + 7, ROW_LEVEL + 8, ROW_LEVEL + 9, ROW_LEVEL + 10, ROW_LEVEL + 11, ROW_LEVEL + 12, ROW_LEVEL + 13,
      ROW_LEVEL + 14 },
    { S_KILLS, S_RANGED, S_SPEAR, S_FINISH, S_SOLO, S_MULTI, S_ASSISTS, S_DEATHS, -1, S_SPEED_HITS, S_FIERCE_HITS, S_KICKS,
      S_SHIELDS, S_PARRY_HITS, -1, S_LADDERS, S_BURNED },
    { F_ORC, ROW_FOE + 0, ROW_FOE + 1, F_URUK, ROW_FOE + 2, ROW_FOE + 3, ROW_FOE + 4, ROW_FOE + 5, F_URUKHAI, ROW_FOE + 6,
      ROW_FOE + 7, F_BERSERKER, F_DEAD, ROW_FOE + 8, ROW_FOE + 9, ROW_FOE + 10, ROW_FOE + 11, F_EAST, ROW_FOE + 12,
      ROW_FOE + 13, ROW_FOE + 14, F_SPIDER, F_TROLL, F_MUMAK, F_NAZGUL, F_OTHER },
    { F_GOLLUM, F_SHELOB, F_GORBAG, F_KINGDEAD, F_MOUTH, -1, C_HEWER, C_JUDGMENT, C_BALROG, C_DARK, C_CLEAVER, C_LIGHTNING,
      C_HAMMER, C_SWIFT, C_RISING, C_BANE },
    { S_PVP_GAMES, S_PVP_WINS, S_PVP_KILLS, S_PVP_FALLS, S_PVP_ELIMS, -1, S_PVP_ROUNDS, S_PVP_ROUND_WINS, S_PVP_TOURNEYS,
      S_PVP_CUPS, -1, S_PVP_TEAM, S_PVP_TIMED, S_PVP_BEST_TIMED },
};

// the parts of a row above ("di cui..." under the kills, the kinds under a people): further in and a little smaller
static bool KillPart(int r) { return r == S_RANGED || r == S_SPEAR || r == S_FINISH || r == S_SOLO; }

// four players at a time, one column each: the tabs on top (in place of the title), the names under them, the labels on
// the left (as wide as the longest of every tab, so that the columns stay put from a tab to the next), the numbers
// right-aligned; the selected player's column in the highlighted colour
static void DrawStats()
{
    const float s = g_tableScale;
    // the tabs: the open one highlighted on a darker box, the others dimmed; together at most 90% of the width
    {
        const float ty = 0.075f, gap = 0.035f;
        float ts = s * 1.1f, w[TAB_COUNT], sum = 0.0f;
        for (int t = 0; t < TAB_COUNT; ++t) sum += w[t] = TextWidth(ST_SEL, Txt(TB_GAMES + t), ts);
        const float gaps = gap * (TAB_COUNT - 1);
        if (sum + gaps > 0.9f) {
            const float f = (0.9f - gaps) / sum;
            ts *= f; sum *= f;
            for (float& x : w) x *= f;
        }
        float x = 0.5f - (sum + gaps) * 0.5f;
        for (int t = 0; t < TAB_COUNT; ++t) {
            if (t == g_statsTab) DarkRect(x - 0.012f, ty - 0.008f, x + w[t] + 0.012f, ty + 0.045f * ts + 0.012f, g_shade);
            Put(Txt(TB_GAMES + t), x, ty, ts, t == g_statsTab ? ST_SEL : ST_HELP);
            AddHit(x - gap * 0.5f, ty - 0.01f, x + w[t] + gap * 0.5f, ty + 0.06f, H_TAB, t);
            x += w[t] + gap;
        }
    }
    if (g_table.empty()) { Put(Txt(T_NONE), 0.5f, 0.30f, s, ST_LABEL, 2); Help(Txt(T_STATS_HELP)); return; }
    const int first = g_statsSel / 4 * 4, shown = (std::min)(4, (int)g_table.size() - first);
    // the Versus tab ends with who downed whom: a row for each player shown ("Downed Alex"), in each column how many
    // times that column's player did it (05/11/2026)
    std::vector<int> rows = kTabRows[(std::max)(0, (std::min)(g_statsTab, TAB_COUNT - 1))];
    if (g_statsTab == 5 && shown > 1) { rows.push_back(-1); for (int i = 0; i < shown; ++i) rows.push_back(ROW_VS + i); }
    // the rows of the kinds of enemy have a portrait in front: their labels start after it, all of them, so that the
    // names stay in a column (test of 01/11/2026: icons out of the dark panel, names not aligned). The labels' column
    // does not count the levels' names: "Minas Tirith - sommita' delle mura" made it 0.42 and left the players' columns
    // too narrow for "Aragorn" next to his portrait (cut to "aragor.", test of 02/11/2026); a level's name gets smaller.
    const float top = 0.20f, bottom = 0.80f, sub = 0.9f;
    // where each row starts, in rows (the gaps between groups are half a row); a tab a little too long has its rows a
    // little closer (down to 0.036), a much longer one scrolls, a row at a time, with a bar on the right
    std::vector<float> at;
    float units = 0.0f;
    for (int r : rows) { if (r < 0) units += 0.5f; else { at.push_back(units); units += 1.0f; } }
    const float dy = (std::max)(0.036f, (std::min)(0.04f, (bottom - top) / units)), room = (bottom - top) / dy;
    g_statsFirstMax = 0;
    while (g_statsFirstMax + 1 < (int)at.size() && units - at[g_statsFirstMax] > room + 0.001f) ++g_statsFirstMax;
    g_statsFirst = (std::max)(0, (std::min)(g_statsFirst, g_statsFirstMax));
    const float from = at[g_statsFirst];
    const float iconH = (std::min)(0.04f, dy) * 0.95f, iconW = iconH * 9.0f / 16.0f, iconIndent = 0.04f * 0.95f * 9.0f / 16.0f + 0.008f;
    auto withIcon = [](int r) { return (r >= F_ORC && r <= F_OTHER) || r == F_NAZGUL || r == F_MOUTH; };
    auto isSub = [](int r) { return IsFoeSub(r) || KillPart(r); };
    auto indent = [&](int r) { return IsFoeSub(r) ? iconIndent + 0.025f : KillPart(r) || IsLevelRow(r) ? 0.025f : withIcon(r) ? iconIndent : 0.0f; };
    auto RowLabel = [&](int r) {
        if (r >= ROW_VS) return Replace(Txt(S_VS_DOWNS), "{name}", g_table[first + r - ROW_VS].name);
        return IsLevelRow(r) ? LevelName(r - ROW_LEVEL) : IsFoeSub(r) ? std::string(Txt(kFoeSubs[r - ROW_FOE].label))
                                                                     : std::string(Txt(r));
    };
    float labelW = 0.0f;
    for (const auto& tab : kTabRows)
        for (int r : tab)
            if (r >= 0 && !IsLevelRow(r))
                labelW = (std::max)(labelW, TextWidth(ST_LABEL, RowLabel(r), isSub(r) ? s * sub : s) + indent(r));
    const float x0 = 0.05f, maxLabel = (std::min)(labelW, 0.42f);
    const float labelScale = labelW > maxLabel ? s * (std::max)(0.6f, maxLabel / labelW) : s;
    const float colsL = x0 + maxLabel + 0.03f, colW = (0.95f - colsL) / 4.0f;
    for (int i = 0; i < shown; ++i) {
        AddHit(colsL + i * colW, 0.13f, colsL + (i + 1) * colW, 0.86f, H_PICK, first + i);
        Put(g_table[first + i].name, colsL + (i + 1) * colW - 0.01f, 0.145f, s, first + i == g_statsSel ? ST_SEL : ST_HEAD, 1,
            colW - 0.015f);
    }
    if (g_table.size() > 4)   // more than four players: which four (left/right goes on to the next ones)
        Put(std::to_string(g_statsSel / 4 + 1) + "/" + std::to_string(((int)g_table.size() + 3) / 4), x0, 0.145f, s, ST_HELP);
    // the favourite heroes' names, all at one size: the largest at which each fits next to its portrait (never cut)
    float heroScale = s;
    for (int i = 0; i < shown; ++i) {
        const int h = FavouriteHeroIndex(g_table[first + i].v);
        if (h < 0) continue;
        const float w = TextWidth(ST_VALUE, HeroName(h), s), room = colW - 0.015f - iconIndent;
        if (w > room) heroScale = (std::min)(heroScale, s * room / w);
    }
    int row = 0;
    for (int r : rows) {
        if (r < 0) continue;
        const float u = at[row] - from;
        if (u < -0.001f || u + 1.0f > room + 0.001f) { ++row; continue; }   // above or below the page
        const float y = top + u * dy;
        Band(row++, y, dy);
        const float lx = x0 + indent(r), ls = isSub(r) ? labelScale * sub : labelScale;
        // (a smaller text is lowered to stand on the row's line: the glyphs are about 0.045 x scale high). A level's name
        // may use the room up to the first player's number (test of 03/11/2026: "Minas Tirith - Sommita' delle mura" was
        // drawn smaller than the others to fit the width of the other tabs' labels)
        const float labelRoom = IsLevelRow(r) ? colsL + colW - 0.07f - lx : maxLabel - (lx - x0);
        Put(RowLabel(r), lx, y + 0.045f * (labelScale - ls), ls, ST_LABEL, 0, labelRoom);
        // the portrait in front of the label, inside the panel (a square as high as the row)
        if (void* icon = withIcon(r) ? IconTexture(kTxt[r].key) : nullptr)
            DrawIcon(icon, x0, y - 0.004f, x0 + iconW, y - 0.004f + iconH);
        for (int i = 0; i < shown; ++i) {
            const std::string val = r < ROW_VS ? StatValue(g_table[first + i].v, r)
                                  : first + i == first + r - ROW_VS ? std::string("-")
                                  : Num(Get(g_table[first + i].v, DownsKey(g_table[first + r - ROW_VS].name)));
            const float right = colsL + (i + 1) * colW - 0.01f;
            // the favourite hero: the portrait before the name, both inside the column (the name gets smaller if needed:
            // test of 01/11/2026, "legolas" ran into the next column's portrait)
            void* hero = nullptr;
            if (r == S_HERO && FavouriteHeroIndex(g_table[first + i].v) >= 0) {
                char key[8];
                sprintf_s(key, "Hero%d", FavouriteHeroIndex(g_table[first + i].v));
                hero = IconTexture(key);
            }
            const float maxW = colW - 0.015f - (hero ? iconIndent : 0.0f);
            const float vs = r == S_HERO ? heroScale : isSub(r) ? s * sub : s;
            Put(val, right, y + 0.045f * (s - vs), vs, first + i == g_statsSel ? ST_SEL : ST_VALUE, 1, maxW);
            if (hero) {
                const float tx = right - (std::min)(TextWidth(ST_VALUE, val, vs), maxW);
                DrawIcon(hero, tx - iconIndent, y - 0.004f, tx - iconIndent + iconW, y - 0.004f + iconH);
            }
        }
    }
    // the scroll bar of a long tab: the part shown, light, on a dark track; a click above or below it moves a page
    if (g_statsFirstMax > 0) {
        const float bx0 = 0.953f, bx1 = 0.962f, by0 = top - 0.006f, by1 = top + room * dy - 0.006f, h = by1 - by0;
        const float t0 = by0 + h * from / units, t1 = by0 + h * (from + room) / units;
        DarkRect(bx0, by0, bx1, by1, g_shade);
        DarkRect(bx0, t0, bx1, (std::min)(t1, by1), 0.8f, 0.85f);
        const int pageRows = (std::max)(1, (int)room - 1);
        AddHit(bx0 - 0.002f, by0, bx1 + 0.01f, t0, H_UP, pageRows);   // (the columns' own areas end at 0.95)
        AddHit(bx0 - 0.002f, (std::min)(t1, by1), bx1 + 0.01f, by1, H_DOWN, pageRows);
    }
    Help(Txt(T_STATS_HELP));
}

// --- one player's card (02/11/2026: proposal C, between the statistics and the heroes) -------------------------------
// On the left the favourite hero's portrait, the main numbers in boxes and the nine heroes with their levels (the ones
// never played dimmed); on the right the peoples of enemies with their portraits, totals and captains, the five bosses
// (dimmed until beaten) and the fifteen levels as marks (gold = completed at least once). Left/right another player,
// confirm (or a click) the heroes' table, back the statistics.
static void Box(float x, float y, float w, float h, const std::string& label, const std::string& value, float s)
{
    DarkRect(x, y, x + w, y + h, g_shade * 0.9f);
    Put(label, x + 0.008f, y + 0.006f, s * 0.75f, ST_LABEL, 0, w - 0.016f);
    Put(value, x + w - 0.008f, y + 0.03f, s * 1.05f, ST_SEL, 1, w - 0.016f);
}

static void DrawCard()
{
    const float s = g_tableScale, k = 9.0f / 16.0f;   // k: the width of a square for its height
    if (g_table.empty()) { g_page = P_STATS; return; }
    const Totals& t = g_table[g_statsSel];
    const Vals& v = t.v;
    const std::string title = Replace(Txt(T_CARD), "{name}", t.name);
    if (g_table.size() > 1) TitleArrows(title); else Title(title);
    AddHit(0.0f, 0.15f, 1.0f, 1.0f, H_SELECT, 0);   // a click under the title: the heroes (the title's arrows come first)
    // left: the favourite hero, the time
    const float lx = 0.06f, lw = 0.34f;
    const int fav = FavouriteHeroIndex(v);
    if (fav >= 0) {
        char key[8];
        sprintf_s(key, "Hero%d", fav);
        if (void* icon = IconTexture(key)) DrawIcon(icon, lx, 0.17f, lx + 0.22f * k, 0.39f);
    }
    const float tx = lx + 0.22f * k + 0.025f, tw = lx + lw - tx;
    Put(Txt(S_HERO), tx, 0.185f, s * 0.8f, ST_LABEL, 0, tw);
    Put(fav >= 0 ? HeroName(fav) : "-", tx, 0.215f, s * 1.3f, ST_SEL, 0, tw);
    Put(Txt(S_TIME), tx, 0.30f, s * 0.8f, ST_LABEL, 0, tw);
    Put(Time(Get(v, "Seconds")), tx, 0.33f, s * 1.1f, ST_SEL, 0, tw);
    // the main numbers, two boxes a row
    const float bw = (lw - 0.01f) / 2.0f, bh = 0.07f;
    const std::pair<int, std::string> boxes[6] = {
        { S_WINS, Num(Get(v, "Wins")) + " / " + Num(Get(v, "Levels")) }, { S_KILLS, StatValue(v, S_KILLS) },
        { S_EXP, StatValue(v, S_EXP) }, { S_PERFECT, StatValue(v, S_PERFECT) },
        { T_CARD_BURNED, StatValue(v, S_BURNED) }, { S_DEATHS, StatValue(v, S_DEATHS) } };
    for (int i = 0; i < 6; ++i)
        Box(lx + (i % 2) * (bw + 0.01f), 0.43f + (i / 2) * (bh + 0.01f), bw, bh, Txt(boxes[i].first), boxes[i].second, s);
    // the nine heroes, with the levels played
    Put(Txt(T_CARD_HEROES), lx, 0.685f, s * 0.85f, ST_HEAD, 0, lw);
    const float hh = 0.06f, hw = hh * k, hg = (lw - 9 * hw) / 8.0f;
    for (int h = 0; h < 9; ++h) {
        const int levels = Get(v, HeroKey(h, "")), any = levels + Get(v, HeroKey(h, "Pvp"));
        const float x = lx + h * (hw + hg);
        char key[8];
        sprintf_s(key, "Hero%d", h);
        if (void* icon = IconTexture(key)) DrawIcon(icon, x, 0.725f, x + hw, 0.725f + hh, any ? 1.0f : 0.3f);
        Put(levels ? Num(levels) : "-", x + hw * 0.5f, 0.79f, s * 0.75f, levels ? ST_VALUE : ST_HELP, 2, hw + hg);
    }
    // right: the peoples of enemies, two a row (the portrait, the name, the kills, the captains under the name)
    const float rx = 0.45f, rw = 0.50f, cw = (rw - 0.02f) / 2.0f;
    Put(Txt(T_CARD_FOES), rx, 0.165f, s * 0.9f, ST_HEAD, 0, rw);
    static const int peoples[10] = { F_ORC, F_URUK, F_URUKHAI, F_BERSERKER, F_DEAD, F_EAST, F_SPIDER, F_TROLL, F_MUMAK, F_NAZGUL };
    for (int i = 0; i < 10; ++i) {
        const int r = peoples[i];
        const float x = rx + (i % 2) * (cw + 0.02f), y = 0.21f + (i / 2) * 0.068f, ih = 0.055f;
        DarkRect(x - 0.004f, y - 0.004f, x + cw, y + 0.062f, g_shade * 0.6f);
        if (void* icon = IconTexture(kTxt[r].key)) DrawIcon(icon, x, y, x + ih * k, y + ih);
        const float nx = x + ih * k + 0.008f;
        Put(Txt(r), nx, y + 0.002f, s * 0.85f, ST_LABEL, 0, cw - (nx - x) - 0.07f);
        Put(StatValue(v, r), x + cw - 0.006f, y + 0.002f, s * 0.95f, ST_SEL, 1, 0.07f);
        for (int j = 0; j < kFoeSubCount; ++j)
            if (kFoeSubs[j].people == r && kFoeSubs[j].label == V_CAPTAINS)
                Put(StatValue(v, ROW_FOE + j) + " " + Txt(V_CAPTAINS), nx, y + 0.033f, s * 0.65f, ST_HELP, 0, cw - (nx - x));
    }
    // the bosses: the portrait (the name where the game has none), the times beaten under it
    Put(Txt(T_CARD_BOSSES), rx, 0.57f, s * 0.9f, ST_HEAD, 0, rw);
    static const int bosses[5] = { F_GOLLUM, F_SHELOB, F_GORBAG, F_KINGDEAD, F_MOUTH };
    for (int i = 0; i < 5; ++i) {
        const int r = bosses[i], n = FoeKills(v, r);
        const float cx = rx + rw * (i + 0.5f) / 5.0f, bh2 = 0.075f;
        std::string name = Txt(r);
        const size_t c = name.find(": ");
        if (c != std::string::npos) name = name.substr(c + 2);   // "Boss: Shelob" -> "Shelob"
        if (void* icon = IconTexture(kTxt[r].key))
            DrawIcon(icon, cx - bh2 * k * 0.5f, 0.605f, cx + bh2 * k * 0.5f, 0.605f + bh2, n ? 1.0f : 0.3f);
        else Put(name, cx, 0.625f, s * 0.7f, n ? ST_LABEL : ST_HELP, 2, rw / 5.0f - 0.005f);
        Put(n ? Num(n) : "-", cx, 0.69f, s * 0.8f, n ? ST_SEL : ST_HELP, 2, rw / 5.0f - 0.005f);
    }
    // the levels, in the campaign's order: a gold mark for each completed at least once
    const int nLevels = (int)(sizeof kLevels / sizeof kLevels[0]);
    int done = 0;
    for (int i = 0; i < nLevels; ++i) if (Get(v, std::string("Won_") + kLevels[i]) > 0) ++done;
    Put(Txt(S_LEVELS_WON), rx, 0.74f, s * 0.9f, ST_HEAD, 0, rw - 0.08f);
    Put(std::to_string(done) + "/" + std::to_string(nLevels), rx + rw, 0.74f, s * 0.9f, ST_SEL, 1);
    const float mg = 0.007f, mw = (rw - (nLevels - 1) * mg) / nLevels;
    for (int i = 0; i < nLevels; ++i) {
        const float x = rx + i * (mw + mg);
        if (Get(v, std::string("Won_") + kLevels[i]) > 0) ColorRect(x, 0.785f, x + mw, 0.815f, 1.0f, 0.86f, 0.72f, 0.40f);
        else DarkRect(x, 0.785f, x + mw, 0.815f, g_shade * 1.2f);
    }
    // under everything, a band (04/11/2026): the versus games (won of played, Cups, opponents knocked down), each its
    // label and its number (the kicks are in the Fights tab only, 05/11/2026)
    const float by = 0.845f, bw3 = (rx + rw - lx) / 3.0f;
    DarkRect(lx - 0.004f, by - 0.006f, rx + rw + 0.004f, by + 0.05f, g_shade * 0.6f);
    const std::pair<int, std::string> band[3] = {
        { S_PVP_WINS, Num(Get(v, "PvpWins")) + " / " + Num(Get(v, "PvpGames")) }, { S_PVP_CUPS, Num(Get(v, "PvpCups")) },
        { T_CARD_DOWNS, Num(Get(v, "PvpKills")) } };
    for (int i = 0; i < 3; ++i) {
        const float x = lx + i * bw3;
        Put(Txt(band[i].first), x + 0.008f, by + 0.008f, s * 0.8f, ST_LABEL, 0, bw3 - 0.11f);
        Put(band[i].second, x + bw3 - 0.02f, by + 0.004f, s * 0.95f, ST_SEL, 1, 0.08f);
    }
    Help(Txt(T_CARD_HELP));
}

// the headings of a table's columns: the parts of one text, split at two spaces
static std::vector<std::string> Columns(const std::string& heads)
{
    std::vector<std::string> cols;
    for (size_t at = 0; at < heads.size();) {
        const size_t sp = heads.find("  ", at);
        cols.push_back(heads.substr(at, sp == std::string::npos ? std::string::npos : sp - at));
        if (sp == std::string::npos) break;
        at = heads.find_first_not_of(' ', sp);
    }
    return cols;
}

// one player's heroes: levels, levels won, versus games, kills, assists, deaths
static void DrawDetail()
{
    const float s = g_tableScale;
    if (g_table.empty()) { g_page = P_STATS; return; }
    const Totals& t = g_table[g_statsSel];
    if (g_table.size() > 1) TitleArrows(Replace(Txt(T_DETAIL), "{name}", t.name));
    else Title(Replace(Txt(T_DETAIL), "{name}", t.name));
    // one column per value, each heading (the parts of PlayersDetailCols) right-aligned above its numbers (test of
    // 30/10/2026: in one line of text the numbers drifted away from their headings)
    const std::vector<std::string> cols = Columns(Txt(T_DETAIL_COLS));
    const float x0 = 0.05f, colsL = 0.27f, colW = (0.95f - colsL) / 6.0f;
    for (size_t c = 0; c < cols.size() && c < 6; ++c)
        Put(cols[c], colsL + (c + 1) * colW - 0.01f, 0.18f, s, ST_HEAD, 1, colW - 0.012f);
    int row = 0;
    for (int h = 0; h < 9; ++h) {
        const int levels = Get(t.v, HeroKey(h, ""));
        if (!levels && !Get(t.v, HeroKey(h, "Pvp"))) continue;   // a hero used only in versus games: kills still count
        const float y = 0.26f + row * 0.06f;
        Band(row++, y, 0.06f);
        // the hero's portrait in front of the name, inside the panel (ROTKPlus_icons\Hero<n>.raw)
        const float ih = 0.055f, iw = ih * 9.0f / 16.0f;
        char key[8];
        sprintf_s(key, "Hero%d", h);
        Put(HeroName(h), x0 + iw + 0.01f, y, s, ST_LABEL, 0, colsL - x0 - iw - 0.02f);
        if (void* icon = IconTexture(key)) DrawIcon(icon, x0, y - 0.01f, x0 + iw, y - 0.01f + ih);
        // in the hero's kills and deaths also the versus knock-downs and falls (test of 30/10/2026: a fallen hero in a
        // versus game showed 0 deaths)
        const int vals[6] = { levels, Get(t.v, HeroKey(h, "Wins")), Get(t.v, HeroKey(h, "Pvp")),
                              Get(t.v, HeroKey(h, "Kills")) + Get(t.v, HeroKey(h, "PvpKills")), Get(t.v, HeroKey(h, "Assists")),
                              Get(t.v, HeroKey(h, "Deaths")) + Get(t.v, HeroKey(h, "PvpFalls")) };
        for (int c = 0; c < 6; ++c) Put(Num(vals[c]), colsL + (c + 1) * colW - 0.01f, y, s, ST_VALUE, 1, colW - 0.012f);
    }
    if (!row) Put("-", x0, 0.26f, s, ST_LABEL);
    Help(Txt(T_DETAIL_HELP));
}

// --- Records, Last game, Manage names (03/11/2026: in the style of the other pages) ----------------------------------
static const float kGold[3] = { 0.86f, 0.72f, 0.40f };   // the colour of the headings' lines and of the levels' marks

// a record in its box: what it is, the value in big, who holds it with the first holder's favourite hero (ties: every
// name); a gold line on top when somebody holds it
static void RecordBox(const RecordDef& r, float x, float y, float w, float h)
{
    const float s = g_tableScale, k = 9.0f / 16.0f, ih = 0.05f;
    int best = 0; std::string who;
    std::vector<std::string> names;
    const bool held = RecordHolder(r, best, who, &names);
    DarkRect(x, y, x + w, y + h, g_shade * 0.9f);
    if (held) ColorRect(x, y, x + w, y + 0.004f, 0.9f, kGold[0], kGold[1], kGold[2]);
    Put(Txt(r.brief), x + 0.012f, y + 0.016f, s * 0.8f, ST_LABEL, 0, w - 0.024f);
    if (!held) { Put("-", x + 0.012f, y + 0.055f, s * 1.6f, ST_HELP); return; }
    Put(r.lowest ? Time(best) : Num(best), x + 0.012f, y + 0.055f, s * 1.6f, ST_SEL, 0, w - 0.024f);
    const float iy = y + h - 0.065f;
    float nx = x + 0.012f;
    if (void* icon = HeroIcon(FavouriteOf(names[0]))) { DrawIcon(icon, nx, iy, nx + ih * k, iy + ih); nx += ih * k + 0.01f; }
    Put(who, nx, iy + 0.01f, s * 0.95f, ST_VALUE, 0, x + w - 0.012f - nx);
}

// the records in three groups of boxes (in a level, overall, versus games), and who holds the most of them
static void DrawRecords()
{
    const float s = g_tableScale, k = 9.0f / 16.0f;
    Title(Txt(T_RECORDS));
    AddHit(0.0f, 0.0f, 1.0f, 1.0f, H_SELECT, 0);   // a click goes back
    const float x0 = 0.05f, x1 = 0.95f, g = 0.02f, h = 0.2f, mid = 0.5f;
    int inLevel = 0;   // the boxes of the first row (in a level) share its width
    for (const RecordDef& r : kRecords) inLevel += r.group == 0;
    const float w3 = (x1 - x0 - (inLevel - 1) * g) / (float)(inLevel > 0 ? inLevel : 1), w2 = (mid - g * 0.5f - x0 - g) / 2.0f;
    Heading(Txt(T_REC_LEVEL), x0, 0.165f, x1);
    Heading(Txt(T_REC_TOTAL), x0, 0.455f, mid - g * 0.5f);
    Heading(Txt(TB_PVP), mid + g * 0.5f, 0.455f, x1);
    int inGroup[3] = {};
    std::map<std::string, int> held;
    for (const RecordDef& r : kRecords) {
        const int i = inGroup[r.group]++;
        if (r.group == 0) RecordBox(r, x0 + i * (w3 + g), 0.205f, w3, h);
        else RecordBox(r, (r.group == 1 ? x0 : mid + g * 0.5f) + i * (w2 + g), 0.495f, w2, h);
        int best = 0; std::string who;
        std::vector<std::string> names;
        if (RecordHolder(r, best, who, &names)) for (const std::string& n : names) ++held[n];
    }
    int most = 0;
    std::string top, first;
    for (const auto& kv : held) {
        if (kv.second > most) { most = kv.second; top = first = kv.first; }
        else if (kv.second == most) top += ", " + kv.first;
    }
    if (most > 0) {
        const std::string line = Replace(Replace(Txt(T_REC_MOST), "{name}", top), "{n}", std::to_string(most));
        const float ts = s * 1.05f, lw = (std::min)(TextWidth(ST_HEAD, line, ts), 0.8f), ih = 0.05f;
        void* icon = HeroIcon(FavouriteOf(first));
        const float lx = 0.5f - (lw + (icon ? ih * k + 0.012f : 0.0f)) * 0.5f;
        if (icon) DrawIcon(icon, lx, 0.765f, lx + ih * k, 0.765f + ih);
        Put(line, lx + (icon ? ih * k + 0.012f : 0.0f), 0.772f, ts, ST_HEAD, 0, 0.8f);
    }
    Help(Txt(T_BACK_HELP));
}

// the last game: the level, how it ended, then one row per player (the hero played, the name, the numbers under their
// columns), the one with the most kills marked in gold, and the total of a co-op game
static void DrawLast()
{
    const float s = g_tableScale, k = 9.0f / 16.0f;
    Title(Txt(T_LAST));
    if (g_recap) {   // after a versus game, over the map: one button, Back to the map
        const float y = kRecapY, rowH = 0.06f;
        DarkRect(kRecapBx, y - 0.006f, kRecapBx + kRecapBw, y + rowH - 0.006f, 0.95f);
        ColorRect(kRecapBx, y + rowH - 0.010f, kRecapBx + kRecapBw, y + rowH - 0.006f, 0.9f, kGold[0], kGold[1], kGold[2]);
        Put(Txt(T_TO_MAP), kRecapBx + kRecapBw * 0.5f, y, g_scale * 0.85f, ST_SEL, 2, kRecapBw - 0.02f);
    } else {
        AddHit(0.0f, 0.0f, 1.0f, 1.0f, H_SELECT, 0);   // a click goes back
        Help(Txt(T_BACK_HELP));
    }
    if (g_sum.empty()) { Put(Txt(T_LAST_NONE), 0.5f, 0.30f, s, ST_LABEL, 2); return; }
    const bool pvp = g_sumKind == 3;
    const float x0 = 0.08f, x1 = 0.92f, colW = 0.13f, rowH = 0.085f;
    const std::string level = g_sumLevel.empty() ? std::string() : LangGameText(g_sumLevel.c_str());
    float y = 0.16f;
    if (!level.empty() && level != g_sumLevel) { Put(level, x0, y, s * 1.4f, ST_SEL, 0, x1 - x0); y += 0.065f; }   // its name in the game's language
    // a versus game (04/11/2026): how it ended under the level's name, its kind in the heading ("Versus: Teams, heroes
    // only, timed 2 minutes, best of 3"); the winners marked in gold, the teams by the names, the rounds won
    if (pvp && !g_sumResult.empty()) { Put(g_sumResult, x0, y, s * 1.05f, ST_VALUE, 0, x1 - x0); y += 0.055f; }
    Heading(pvp && !g_sumMode.empty() ? Txt(T_LAST_PVP) + ": " + g_sumMode : Txt(pvp ? T_LAST_PVP : g_sumKind == 1 ? T_LAST_COOP : T_LAST_LOST),
            x0, y, x1);
    const bool rich = pvp && !g_sumMode.empty();   // (a Last game saved before 1.4.0 has two columns)
    const int nc = !pvp ? 3 : rich && g_sumTourney ? 3 : 2;
    auto colX = [&](int c) { return x1 - (nc - 1 - c) * colW; };   // right edge of a column
    const std::vector<std::string> cols = Columns(Txt(pvp ? T_LAST_PVP_COLS : T_LAST_COLS));
    y += 0.05f;
    for (int c = 0; c < nc && c < (int)cols.size(); ++c) Put(cols[c], colX(c), y, s * 0.8f, ST_LABEL, 1, colW - 0.01f);
    y += 0.04f;
    const SumRow* top = nullptr;
    for (const SumRow& r : g_sum) if (!top || r.k > top->k) top = &r;
    if (g_sum.size() < 2 || !top || top->k <= 0) top = nullptr;
    const float ih = 0.07f, nx = x0 + ih * k + 0.015f, nw = colX(0) - colW - nx;
    int tot[4] = {};
    for (size_t i = 0; i < g_sum.size() && i < 4; ++i, y += rowH) {
        const SumRow& r = g_sum[i];
        const bool gold = rich ? r.win : &r == top;   // a versus game: its winners; a level: the most kills
        Band((int)i, y, rowH);
        if (gold) ColorRect(0.035f, y - 0.006f, 0.041f, y + rowH - 0.006f, 1.0f, kGold[0], kGold[1], kGold[2]);
        if (void* icon = HeroIcon(r.hero)) DrawIcon(icon, x0, y, x0 + ih * k, y + ih);
        Put(r.name, nx, y + 0.004f, s * 1.1f, gold ? ST_SEL : ST_VALUE, 0, nw);
        std::string under = r.hero >= 0 && r.hero <= 8 ? HeroName(r.hero) : std::string("?");
        Put(under, nx, y + 0.042f, s * 0.75f, ST_LABEL, 0, nw);
        if (rich && (r.team == 0 || r.team == 1)) {   // the team, after the hero: "Aragorn  Sun"
            const float hx = nx + (std::min)(TextWidth(ST_LABEL, under, s * 0.75f), nw * 0.6f) + 0.02f;
            Put(Txt(r.team == 0 ? T_MP_SUN : T_MP_MOON), hx, y + 0.042f, s * 0.75f, r.team == 0 ? ST_SEL : ST_MOON, 0, nw * 0.3f);
        }
        const int vals[3] = { r.k, pvp ? r.d : r.a, pvp ? r.rounds : r.d };
        for (int c = 0; c < nc; ++c) {
            Put(Num(vals[c]), colX(c), y + 0.018f, s * 1.1f, gold && c == 0 ? ST_SEL : ST_VALUE, 1, colW - 0.01f);
            tot[c] += vals[c];
        }
    }
    if (!pvp && g_sum.size() > 1) {   // the whole fellowship
        ColorRect(x0, y + 0.004f, x1, y + 0.006f, 0.6f, kGold[0], kGold[1], kGold[2]);
        Put(Txt(T_LAST_TOTAL), nx, y + 0.016f, s, ST_HEAD, 0, nw);
        for (int c = 0; c < nc; ++c) Put(Num(tot[c]), colX(c), y + 0.016f, s, ST_HEAD, 1, colW - 0.01f);
        y += 0.06f;
    }
    if (top) Put(Replace(Txt(pvp ? T_SUM_BEST_PVP : T_SUM_BEST), "{name}", top->name), x0, y + 0.03f, s * 0.9f, ST_LABEL, 0, x1 - x0);
}

// --- after a versus game: the Last game over the map (05/11/2026) -------------------------------------------------
// Screens.cpp opens it when it sends the game straight to the map ([PvP] SkipScoreScreens and LastGame) and gives it
// every menu action while it is open (the map's own keys are held back meanwhile); confirm or back (or a click on its
// button) close it, and the map is there as usual. (A rematch from here, the same arena and heroes again, was tried on
// 05/11/2026 and set aside: see the project's notes.)
void PlayersOpenRecap()
{
    if (!g_enabled) return;
    LoadSummary();
    g_page = P_LAST;
    g_recap = true;
    g_recapAt = GetTickCount();
    g_recapDrawn = false;
    Log("players: the Last game over the map, until confirm or back");
}

bool PlayersRecapOpen() { return g_recap && g_page == P_LAST; }

void PlayersCloseRecap()
{
    if (!g_recap) return;
    g_recap = false;
    if (g_page == P_LAST) g_page = P_CLOSED;
}

int PlayersRecapAction(int action, int slot)
{
    if (!g_recap) return 0;
    if (slot == 3) {   // the pointer: only a click on the button (test of 05/11/2026: a click anywhere chose)
        float x, y;
        if (action != 7 || !PointerPos(x, y)) return 0;
        if (x < kRecapBx || x >= kRecapBx + kRecapBw || y < kRecapY - 0.006f || y >= kRecapY + 0.054f) return 0;
    }
    // (the buttons pressed in the last moments of the fight must not close it at once: a second to look first)
    if (GetTickCount() - g_recapAt < 1000) return 0;
    if (action != 7 && action != 8) return 0;
    PlayersCloseRecap();
    Log("players: the Last game closed, the map");
    return 2;
}

// The names (Manage names, and the names to merge with), in the Fellowship page's style: eight a page with the favourite
// hero and the time played, the selected entry on a darker box, its player on the right (PlayerPreview). Manage names
// has "Add a name" under them (selected as the index after the last name).
static void DrawList(const std::string& title)
{
    const float s = g_scale, ts = s * 0.85f, x0 = 0.10f, ex = 0.125f, rowH = 0.055f, right = 0.575f, k = 9.0f / 16.0f;
    const int n = (int)g_list.size();
    Put(title, x0, 0.09f, s * 1.2f, ST_TITLE, 0, 0.85f);
    Help(Txt(T_LIST_HELP));
    if (g_page == P_MANAGE) {
        const float y = 0.205f + 8 * rowH + 0.025f;
        const bool sel = g_listSel == n;
        if (sel) SelBox(x0, y, right, rowH);
        AddHit(x0 - 0.02f, y - 0.006f, right, y + rowH - 0.006f, H_SELECT, n);
        Put(Txt(T_ADD_NAME), ex, y, ts, sel ? ST_SEL : ST_VALUE, 0, right - ex - 0.01f);
        if (sel) PlayerPreview("", kNewName, T_PANEL_TYPE);
    }
    if (g_list.empty()) { Put(Txt(T_NO_NAMES), ex, 0.21f, ts, ST_LABEL, 0, right - ex); return; }
    const int first = (std::min)(g_listSel, n - 1) / 8 * 8;   // on "Add a name": the last page
    std::string head = Txt(T_SEC_NAMES);
    if (n > 8) head += "   " + std::to_string(first + 1) + "-" + std::to_string((std::min)(n, first + 8)) + " / " + std::to_string(n);
    Heading(head, x0, 0.17f, right);
    for (int i = 0; i < 8 && first + i < n; ++i) {
        const int idx = first + i;
        const float y = 0.205f + i * rowH, iw = 0.044f * k;
        const bool sel = idx == g_listSel;
        const std::string& name = g_list[idx];
        if (sel) SelBox(x0, y, right, rowH);
        AddHit(x0 - 0.02f, y - 0.006f, right, y + rowH - 0.006f, H_SELECT, idx);
        if (void* icon = HeroIcon(FavouriteOf(name))) DrawIcon(icon, ex, y - 0.003f, ex + iw, y + 0.041f);
        Put(name, ex + iw + 0.012f, y, ts, sel ? ST_SEL : ST_VALUE, 0, right - ex - iw - 0.15f);
        if (const Totals* t = TotalsOf(name)) Put(Time(Get(t->v, "Seconds")), right - 0.012f, y + 0.008f, g_tableScale * 0.8f, ST_LABEL, 1, 0.12f);
    }
    if (g_listSel < n) PlayerPreview("", g_list[g_listSel], g_page == P_MANAGE ? T_PANEL_MANAGE : -1);
}

// what to do with a name: the same list's style, Delete on a red box, the name's player on the right
static void DrawAction()
{
    const float s = g_scale, ts = s * 0.85f, x0 = 0.10f, ex = 0.125f, rowH = 0.055f, right = 0.575f;
    Put(g_actName, x0, 0.09f, s * 1.2f, ST_TITLE, 0, right - x0);
    Heading(Txt(T_MANAGE), x0, 0.17f, right);
    const int labels[4] = { T_RENAME, T_MERGE, T_REMOVE, T_BACK };
    for (int i = 0; i < 4; ++i) {
        const float y = 0.205f + i * rowH + (i == 3 ? 0.025f : 0.0f);   // Back a little apart
        const bool sel = g_actRow == i;
        if (sel && i == 2) ColorRect(x0, y - 0.006f, right, y + rowH - 0.006f, 0.8f, 0.42f, 0.07f, 0.05f);
        else if (sel) SelBox(x0, y, right, rowH);
        AddHit(x0 - 0.02f, y - 0.006f, right, y + rowH - 0.006f, H_SELECT, i);
        Put(Txt(labels[i]), ex, y, ts, sel ? ST_SEL : ST_VALUE, 0, right - ex - 0.01f);
    }
    PlayerPreview("", g_actName, -1);
    Help(Txt(T_LIST_HELP));
}

// a name in the confirmation: the favourite hero above it, the time played under it, centred on cx
static void ConfirmName(const std::string& name, float cx)
{
    const float s = g_tableScale, k = 9.0f / 16.0f, ih = 0.08f;
    if (void* icon = HeroIcon(FavouriteOf(name))) DrawIcon(icon, cx - ih * k * 0.5f, 0.35f, cx + ih * k * 0.5f, 0.35f + ih);
    Put(name, cx, 0.44f, s * 1.2f, ST_SEL, 2, 0.3f);
    if (const Totals* t = TotalsOf(name)) Put(Time(Get(t->v, "Seconds")), cx, 0.49f, s * 0.8f, ST_LABEL, 2, 0.3f);
}

// the question, who it is about (a merge: the name that goes, an arrow, the one that stays), Yes and No side by side
static void DrawConfirm()
{
    const float s = g_scale, ts = g_tableScale;
    const bool remove = g_confirm == 1;
    Put(g_actName, 0.10f, 0.09f, s * 1.2f, ST_TITLE, 0, 0.85f);
    DarkRect(0.15f, 0.24f, 0.85f, 0.68f, g_shade);
    std::string q = Txt(remove ? T_ASK_REMOVE : T_ASK_MERGE);
    q = Replace(Replace(q, "{name}", g_actName), "{other}", g_mergeTo);
    Put(q, 0.5f, 0.27f, ts, ST_HEAD, 2, 0.66f);
    if (remove) ConfirmName(g_actName, 0.5f);
    else {
        ConfirmName(g_actName, 0.33f);
        Arrow(0.488f, 0.37f, 0.045f, true, ST_SEL);
        ConfirmName(g_mergeTo, 0.67f);
    }
    for (int i = 0; i < 2; ++i) {
        const float bx = i ? 0.52f : 0.32f, bw = 0.16f, y = 0.575f, rowH = 0.06f;
        const bool sel = g_yes == i;
        if (sel && i == 0 && remove) ColorRect(bx, y - 0.006f, bx + bw, y + rowH - 0.006f, 0.8f, 0.42f, 0.07f, 0.05f);
        else DarkRect(bx, y - 0.006f, bx + bw, y + rowH - 0.006f, sel ? g_shade * 1.3f : g_shade * 0.5f);
        AddHit(bx, y - 0.006f, bx + bw, y + rowH - 0.006f, H_SELECT, i);
        Put(Txt(i ? T_NO : T_YES), bx + bw * 0.5f, y, s * 0.85f, sel ? ST_SEL : ST_VALUE, 2, bw - 0.02f);
    }
    Help(Txt(T_CONFIRM_HELP));
}

// --- the summary on the score screen -------------------------------------------------------------------------------
// Drawn with a copy of the HUD's text element (the "kills" text, the font of the names next to the HUD boxes), taken
// while the level is played (g_sumTpl). The first version copied the score screen's first text: a big font in a narrow
// box, so every line wrapped and the lines covered each other (test of 30/10/2026). At the top of the screen
// ([Players] SummaryY, SummaryScale).
static void DrawSummaryOverlay()
{
    if (g_sum.empty() || !g_sumTplOk) return;
    uint8_t* tpl = g_sumTpl;
    float y = g_sumY;
    const SumRow* top = nullptr;
    for (const SumRow& row : g_sum) if (!top || row.k > top->k) top = &row;
    std::string head = Txt(T_SUM_TITLE);
    if (top && g_sum.size() > 1 && top->k > 0) head += "   " + Replace(Txt(g_sumKind == 3 ? T_SUM_BEST_PVP : T_SUM_BEST), "{name}", top->name);
    TextWith(tpl, head, 0.03f, y, g_sumScale);
    for (const SumRow& row : g_sum) { y += 0.045f * g_sumScale / 0.8f; TextWith(tpl, SumLine(row), 0.03f, y, g_sumScale); }
}

// after an element is drawn (Hud.cpp): our page goes on top of the main menu's last root, the summary on top of the
// score screen's
// --- the menu's glint on our pages ------------------------------------------------------------------------------------
// The main menu marks its selected entry with a glint: the 2D effect "FE_TextSelect", which 0x4E9920 puts on the
// selected entry's first label with 0x4F9D70(name, {x, y, 1, 1}), keeping the handle at 0x8E5D60 (the effect clears it
// when it ends, through the pointer at its +0xC) and stopping the one before with 0x401720. While a page is open the
// menu's cursor does not move, so the glint stayed on the menu's entry, over the page (tests of 03/11 and 04/11/2026; a
// version that moved it onto the page's selected row was found awkward). Now it is stopped while a page is open, and
// when the page closes 0x4E9920 puts it back on the menu's entry.
static bool g_glintOff;   // we stopped it

static void GlintFrame(int mode)
{
    uint32_t* const h = (uint32_t*)0x8E5D60;
    if (g_recap) return;   // (the recap over the map: the map's own glint stays)
    if (g_page != P_CLOSED) {
        if (*h) {   // the menu's own (made when the page opened, or by the game meanwhile)
            Guard([&] { ((void(__cdecl*)(uint32_t))0x401720)(*h); });
            *h = 0;
            g_glintOff = true;
        }
        return;
    }
    if (!g_glintOff) return;
    g_glintOff = false;
    if (mode == 17) Guard([&] { ((void(__cdecl*)(char))0x4E9920)(0); });   // back on the menu's entry
}

void PlayersAfterDraw(uint32_t e)
{
    // (0x4F6690 draws *0x8E2F68, then *0x8E2F74, also when it is 0: the pages go after the last one, 0 included; test of
    // 05/11/2026: skipping a null one left every page of the main menu undrawn)
    if (!g_enabled || e != *(const uint32_t*)0x8E2F74) return;
    const int mode = ScreenMode();
    if (mode == 2 && g_sumShow) { if (g_sumOn) DrawSummaryOverlay(); return; }
    if (g_page == P_CLOSED) return;
    if (mode != 17 && !(g_recap && mode == 4)) return;
    if (mode == 17) PlayersFrame();
    if (g_recap) DarkRect(0.0f, 0.0f, 1.0f, 1.0f, 0.55f);   // the map's own texts and glint further down behind it
    if (g_recap && !g_recapDrawn) {
        g_recapDrawn = true;
        Log("players: the Last game drawn over the map (texts from the menu's labels: %s)", g_tplCopied[0] ? "kept" : "MISSING");
    }
    g_nAreas = 0;
    // down to the bottom, around the menu's own "F1 Help" and our hint line (test of 30/10/2026: the edge cut the "F")
    if (g_page != P_CLOSED) DarkRect(0.025f, 0.03f, 0.975f, 0.975f, g_shade);
    switch (g_page) {
    case P_MAIN: DrawMain(); break;
    case P_EDIT: DrawEdit(); break;
    case P_STATS: DrawStats(); break;
    case P_CARD: DrawCard(); break;
    case P_DETAIL: DrawDetail(); break;
    case P_RECORDS: DrawRecords(); break;
    case P_LAST: DrawLast(); break;
    case P_MANAGE: DrawList(Txt(T_MANAGE)); break;
    case P_PICK: DrawList(Replace(Txt(T_PICK), "{name}", g_actName)); break;
    case P_ACTION: DrawAction(); break;
    case P_CONFIRM: DrawConfirm(); break;
    case P_LINEUP: DrawLineup(); break;
    default: break;
    }
}

// label of the main menu entry
std::string PlayersMenuLabel() { return Txt(T_MENU); }
std::string PlayersChooseText() { return Txt(T_CHOOSE); }

// winner of a versus game with the player's own name; "" without a name (Pvp.cpp keeps its "Player N wins")
std::string PlayersWinnerText(int slot, const char* hero)
{
    if (!PlayerHasName(slot)) return "";
    return Replace(Replace(Txt(T_WINNER), "{name}", g_name[slot]), "{hero}", hero ? hero : "");
}

bool  PlayersEnabled() { return g_enabled; }
bool  PlayersHeadNames() { return g_enabled && g_headNames; }
bool  PlayersHudNames() { return g_enabled && g_hudNames; }
float PlayersNameScale() { return g_nameScale; }

// --- installation ------------------------------------------------------------------------------------------------
static float IniFloat(const char* ini, const char* key, const char* def, float lo, float hi)
{
    char buf[32];
    GetPrivateProfileStringA("Players", key, def, buf, sizeof buf, ini);
    const float v = (float)atof(buf);
    return v < lo || v > hi ? (float)atof(def) : v;
}

// ROTKPlus_add.ini next to the game, if there is one: its [Stats.<name>] sections are added to those statistics once
// (counters added up, records kept as MergeVals does), then the file is renamed ROTKPlus_add.ini.done. For what the logs
// show but the statistics did not count yet (02/11/2026: the levels completed before the rows by name existed).
static void ApplyAdditions()
{
    char path[MAX_PATH], done[MAX_PATH];
    sprintf_s(path, "%s\\ROTKPlus_add.ini", GameDir());
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
    static char names[4096], buf[16384];
    const DWORD n = GetPrivateProfileSectionNamesA(names, sizeof names, path);
    for (const char* p = names; p < names + n && *p; p += strlen(p) + 1) {
        if (_strnicmp(p, "Stats.", 6) || !p[6]) continue;
        Vals add;
        const DWORD m = GetPrivateProfileSectionA(p, buf, sizeof buf, path);
        for (const char* q = buf; q < buf + m && *q; q += strlen(q) + 1) {
            const char* eq = strchr(q, '=');
            if (eq && eq > q) add[std::string(q, eq - q)] = atoi(eq + 1);
        }
        const std::string name = p + 6;
        Vals v = ReadVals(name);
        MergeVals(v, add);
        WriteVals(name, v);
        Log("players: ROTKPlus_add.ini: %d values added to %s", (int)add.size(), name.c_str());
    }
    sprintf_s(done, "%s.done", path);
    MoveFileExA(path, done, MOVEFILE_REPLACE_EXISTING);
}

void PlayersInstall()
{
    g_enabled = IniInt("Players", "Enabled", 1) != 0;
    if (!g_enabled) { Log("player names and statistics: off ([Players] Enabled=0)"); return; }
    ApplyAdditions();
    char ini[MAX_PATH];
    sprintf_s(ini, "%s\\ROTKPlus.ini", GameDir());
    g_scale = IniFloat(ini, "TextScale", "0.8", 0.2f, 3.0f);
    g_tableScale = IniFloat(ini, "TableScale", "0.6", 0.2f, 3.0f);
    g_nameScale = IniFloat(ini, "NameScale", "0.8", 0.2f, 3.0f);
    g_sumScale = IniFloat(ini, "SummaryScale", "0.8", 0.2f, 3.0f);
    g_sumY = IniFloat(ini, "SummaryY", "0.02", 0.0f, 0.95f);
    g_shade = IniFloat(ini, "Shade", "0.55", 0.0f, 1.0f);   // the dark rectangle behind the pages (0 = none)
    g_headNames = IniInt("Players", "HeadNames", 1) != 0;   // the names in place of the letters g1-g4 above the heads
    g_hudNames = IniInt("Players", "HudNames", 0) != 0;     // off by default: the names above the heads are enough
    g_sumOn = IniInt("Players", "ScoreSummary", 0) != 0;   // off by default: it covered the score screen ("Last game" has it)
    g_askWho = IniInt("Players", "AskWhoPlays", 1) != 0;
    g_multiPage = IniInt("Players", "MultiplayerPage", 1) != 0;
    LoadNames();
    LoadSummary();
    bool ok = DrawHookInstall();
    // the hit handlers' calls of 0x40CF80
    for (uintptr_t site : { (uintptr_t)0x4031BC, (uintptr_t)0x43A1A9 }) ok &= PatchCall(site, 0x40CF80, (const void*)&Damage);
    for (uintptr_t site : { (uintptr_t)0x404D12, (uintptr_t)0x404D5B })
        ok &= PatchCall(site, 0x40CF80, (const void*)&DamageWithData);
    // the heroes' event handler (parries and pick-ups): 0x43BA2D "mov dword ptr [esi+384h], 43A080h"
    const bool events = PatchDword(0x43BA33, 0x43A080, (uint32_t)(uintptr_t)&HeroEvent);
    Log("players: kicks, speed and fierce attacks %s", PatchCall(0x418758, 0x401780, (const void*)&MeleeHit) ? "counted" :
        "NOT counted (code not as expected)");
    const bool shields = PatchCall(0x41C941, 0x401650, (const void*)&ShieldBreakA) & PatchCall(0x421A0C, 0x4016B0, (const void*)&ShieldBreakB);
    Log("players: shields broken %s", shields ? "counted" : "NOT all counted");
    // 0x45D910: mov eax,[esp+4]; mov eax,[eax+0B8h] (replaced as a whole by LadderKillOn)
    static const uint8_t ladderOld[] = { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x80, 0xB8, 0x00, 0x00, 0x00 };
    const bool ladders = PatchJmp(0x45D910, ladderOld, sizeof ladderOld, (const void*)&LadderKillOn);
    Log("players: pick-ups %s, ladders %s", events ? "counted" : "NOT installed", ladders ? "counted" : "NOT installed");
    bool fire = true;
    for (uint32_t at : { 0x403111u, 0x405755u, 0x405DA2u, 0x41BDA2u }) fire &= PatchCall(at, 0x41A640, (const void*)&CharFire);
    fire &= PatchCall(0x41FE01, 0x401650, (const void*)&TrollFire);
    Log("players: enemies set on fire %s", fire ? "counted" : "NOT all counted");
    if (LogDetail()) {   // the traces of a level (nothing else needs them: the siege engines are not counted)
        static const uint8_t tagOld[] = { 0xA0, 0xB0, 0x5B, 0x5A, 0x00 };   // mov al,[5A5BB0h]
        Log("trace: animation tags %s", PatchJmp(0x45E280, tagOld, sizeof tagOld, (const void*)&AnimTag) ? "written" : "NOT installed");
        static const uint8_t fxOld[] = { 0x83, 0xEC, 0x10, 0x53, 0x8B, 0x5C, 0x24, 0x20 };   // sub esp,10h; push ebx; mov ebx,[esp+20h]
        Log("trace: effects %s", PatchJmp(0x401650, fxOld, sizeof fxOld, (const void*)&Effect) ? "written" : "NOT installed");
    }
    Log("player names and statistics: %s (names %s / %s / %s / %s, who is playing? %s)", ok ? "active" : "NOT completely installed",
        g_name[0][0] ? g_name[0] : "-", g_name[1][0] ? g_name[1] : "-", g_name[2][0] ? g_name[2] : "-",
        g_name[3][0] ? g_name[3] : "-", g_askWho ? "before every game" : "off");
}
