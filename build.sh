#!/bin/sh
set -e
cd "$(dirname "$0")"
SDL_DIR=${SDL_DIR:-/tmp/claude-0/sdl/SDL3-3.4.18}   # SDL3 mingw development package
SDLINC=$SDL_DIR/i686-w64-mingw32/include
CC="i686-w64-mingw32-gcc -O2 -Wall -Wextra -std=gnu11 -static -static-libgcc -s"
VERSION_FILE=VERSION . ./scripts/version.sh
CC="$CC $VDEF"
mkdir -p build
$CC -I$SDLINC -shared -o build/ROTKFix.dll src/rotkfix.c src/input.c src/graphics.c src/post.c src/shot.c src/sound.c src/icons.c src/textures.c src/gameplay.c src/rotkfix.def -ldxguid -lshell32 -Wl,--kill-at -Wl,--enable-stdcall-fixup
$CC -shared -o build/dinput8.dll src/proxy.c src/dinput8.def
$CC -shared -o build/d3d8.dll src/proxy.c src/d3d8.def
