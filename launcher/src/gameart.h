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

// The Return of the King (PC): the menu backgrounds in Data\Game\fro\fro03.scw
// (film stills and paintings without any text or logos). Slow (reads ~100 MB);
// call from a worker thread. Returns false if the file isn't there or can't be read.
bool rotk_menu_art_load(const std::string &gameDir, std::vector<ArtPic> &out);

// DXT1 -> RGBA (malloc'd; free with free()). w and h are multiples of 4.
unsigned char *dxt1_to_rgba(const uint8_t *src, size_t srcLen, int w, int h);
