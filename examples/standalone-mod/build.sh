#!/bin/sh
set -e
cd "$(dirname "$0")"

# Compiles standalone mod into 32-bit DLL for rotk.exe
i686-w64-mingw32-gcc -O2 -shared -o MyMod.dll mymod.c -s

echo "Built MyMod.dll successfully!"
echo "Copy MyMod.dll into <GameFolder>/ROTKFix/mods/ to run it!"
