// ROTK Fix Launcher - the player's games: which versions they have and where.
// The launcher can start more than one game: the PC Return of the King (ROTK Fix
// itself) and, as their ports become ready, the Xbox Return of the King and The
// Two Towers (Xbox). Each game stays where the player keeps it; the list of
// places is remembered in ROTKFix\games.ini next to the launcher.
#pragma once
#include <string>
#include <vector>
#include <stdint.h>

enum GameKind { GK_NONE, GK_ROTK_PC, GK_ROTK_XBOX, GK_TT_XBOX, GK_COUNT };

struct LibGame {
    GameKind kind = GK_NONE;
    std::string path;          // folder, or a disc image (.iso)
    bool iso = false;
    uint32_t titleId = 0;      // Xbox games: from default.xbe
    std::string title;         // Xbox games: the title stored in default.xbe
    uint32_t regions = 0;      // Xbox games: game region flags (1 = North America, 2 = Japan, 4 = Europe and the rest)
};

const char *game_kind_name(GameKind k);       // "The Return of the King - PC" ...
const char *game_kind_short(GameKind k);      // "PC", "Xbox"
bool game_kind_playable(GameKind k);          // false while its port isn't ready
bool game_region_supported(const LibGame &g); // Xbox games: only US copies for now
const char *game_region_name(const LibGame &g);

// What is at 'path' (a game folder or an Xbox disc image)? GK_NONE if it isn't one of ours.
GameKind detect_game_at(const std::string &path, LibGame &out);

// The remembered list (paths that no longer exist are dropped) and saving it.
std::vector<LibGame> library_load(const std::string &launcherDir);
bool library_save(const std::string &launcherDir, const std::vector<LibGame> &games);
// Adds (or refreshes) a game; returns its index.
int library_add(std::vector<LibGame> &games, const LibGame &g);

bool pick_disc_image(std::string &out);      // Windows' file box, for .iso files
