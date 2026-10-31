// Athelas - building a port's program on the player's PC from its build kit and the player's own copy of the game.
// No game code is shipped: a release carries each Xbox port's kit (kits\<folder>\kit.json: the recompiler, the port's
// config and our own code already compiled) and the launcher turns the player's default.xbe into <folder>\<port>.exe
// beside Athelas.exe: recompile, compile (clang, most of the processor's threads), link. A stamp beside the program
// (<port>.exe.build) records the kit and game it was built from; when either changes it is built again.
#pragma once
#include <string>
#include "library.h"

enum BuildPhase { BP_IDLE, BP_TOOLS, BP_READ, BP_COMPILE, BP_LINK, BP_DONE, BP_FAILED, BP_CANCELLED };

struct BuildStatus {
    BuildPhase phase = BP_IDLE;
    int done = 0, total = 0;          // files compiled / to compile
    double seconds = 0, left = -1;    // time so far, estimated time left (-1: not known yet)
    std::string game;                 // "The Two Towers"
    std::string folder;               // the port's folder beside Athelas.exe ("TT-Xbox")
    std::string error;                // plain English, when it failed
    std::string log;                  // the build's log file
};

// The kit for a port's folder ("TT-Xbox"): kits\TT-Xbox beside Athelas.exe, "" if this release has none
std::string builder_kit(const char *folder);
// The program built for this PC: <exe>, or <name>-arm64.exe on Windows on ARM
std::string out_name(const char *folder, const char *exe);
// Does <folder>\<exe> need building (missing, or made from another kit or game file)?
bool builder_needed(const char *folder, const char *exe, const LibGame &g);
// Starts building in the background (one build at a time); false if one is already running
bool builder_start(const char *folder, const char *exe, const char *game, const LibGame &g, const std::string &logPath);
// 1.7.5: removes the leftover build kits, tools and build folders once every game has its program (builder.cpp)
void builder_cleanup_old();
// the game's default.xbe as SHA-1 hex ("" if it can't be read)
std::string xbe_sha1(const LibGame &g);
void builder_cancel();
BuildStatus builder_status();
bool builder_busy();
// After a finished build has been noticed: back to idle
void builder_clear();
