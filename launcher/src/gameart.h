// ROTK Fix Launcher - background art read from the player's own copy of the game.
// Nothing is copied or saved: the pictures are read from the game's files each
// time the launcher opens and only kept in memory.
#pragma once
#include <string>
#include <vector>
#include <stdint.h>

struct ArtPic {
    std::string name;
    int w = 0, h = 0;              // texture size (the menu pictures are square textures shown at 4:3)
    std::vector<uint8_t> dxt;      // DXT1 blocks, as stored in the game
};

// The Return of the King: the menu backgrounds in Data\Game\fro\fro03.scw (PC), or
// fro03.scx in an extracted Xbox copy (same format, but only 4 of the pictures).
// (film stills and paintings without any text or logos). Slow (reads ~100 MB);
// call from a worker thread. Returns false if the file isn't there or can't be read.
bool rotk_menu_art_load(const std::string &gameDir, std::vector<ArtPic> &out);

// DXT1 -> RGBA (malloc'd; free with free()). w and h are multiples of 4.
unsigned char *dxt1_to_rgba(const uint8_t *src, size_t srcLen, int w, int h);

// The Two Towers (Xbox): the bonus gallery (UI\stills1-4.BFF, 39 film stills and
// concept art) and two menu pictures plus the options background. Adds list
// entries "\002tt:<header offset>:<file>" for the files that exist (cheap).
void tt_art_list(const std::string &gameDir, std::vector<std::string> &out);
// One of those pictures as RGBA (malloc'd), checking its header first.
unsigned char *tt_picture_load(const std::string &entry, int &w, int &h);

// The Fellowship of the Ring (Xbox): the three loading-screen sketches (Aragorn, Frodo, Gandalf) and the load
// menu's night landscape, 640x480 linear pictures in XboxMedia\*.xpr. Adds "\002fx:<offset>:<file>" entries for
// the files that exist (cheap); the offset is where the file starts (inside a disc image, or 0).
void fotr_art_list(const std::string &gameDir, std::vector<std::string> &out);
unsigned char *fotr_picture_load(const std::string &entry, int &w, int &h);
