#!/bin/sh
set -e
CC="${CC:-i686-w64-mingw32-gcc}"
echo "Building ValarBoons.dll using $CC..."
$CC -O2 -shared -o ValarBoons.dll valar_main.c valar_config.c valar_logic.c valar_render.c -s -luser32 -lgdi32 -ld3d9
echo "ValarBoons.dll built successfully."
