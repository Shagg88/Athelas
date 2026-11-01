// Athelas - what is left of building the ports on the player's PC (1.7.3/1.7.4 shipped build kits and the launcher
// built each port's program from the player's own game). Since 1.7.5 the ports ship as programs again; their kits,
// build tools and build folders are removed once every game has its program.
#pragma once
#include <string>
#include "library.h"

// 1.7.5: removes the leftover build kits, tools and build folders once every game has its program (builder.cpp)
void builder_cleanup_old();
// the game's default.xbe as SHA-1 hex ("" if it can't be read)
std::string xbe_sha1(const LibGame &g);
