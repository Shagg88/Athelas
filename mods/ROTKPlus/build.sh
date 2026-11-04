#!/bin/sh
# Builds ROTKPlus.dll: ROTKPlus as a mod of Athelas. Copied with its ROTKPlus.ini (athelas/ROTKPlus.ini, the entry of
# the launcher's Mods tab) into <game folder>/ROTKFix/mods/, it is started by ROTK Fix (ModInit).
# Needs the 32-bit mingw-w64 g++ (Debian and Ubuntu: g++-mingw-w64-i686); CXX=... picks another one.
# The same sources build the DivxMediaLib.dll proxy with MSVC (src/ROTKPlus.vcxproj).
#
# -fno-strict-aliasing, -fwrapv and -fno-delete-null-pointer-checks: the code reads the game's memory through casts
# and may touch an object that is gone (inside Guard), as MSVC compiles it; libstdc++ and libgcc are linked in (-static),
# so the DLL needs nothing but Windows' own.
set -e
cd "$(dirname "$0")"
CXX="${CXX:-i686-w64-mingw32-g++}"
SRC="src/Audio.cpp src/Config.cpp src/Coop.cpp src/Guard.cpp src/HeroIndex.cpp src/Hud.cpp src/Icons.cpp src/Lang.cpp
     src/Main.cpp src/Pads.cpp src/Players.cpp src/Pvp.cpp src/Screens.cpp src/Sim.cpp src/Support.cpp"
echo "Building ROTKPlus.dll with $CXX..."
$CXX -std=gnu++17 -O2 -Wall -Wno-multichar -msse2 -mfpmath=sse -fno-strict-aliasing -fwrapv \
     -fno-delete-null-pointer-checks -DROTKPLUS_MOD -shared -static -s -o ROTKPlus.dll $SRC
echo "ROTKPlus.dll built: copy it with its ROTKPlus.ini into <game folder>/ROTKFix/mods/"
