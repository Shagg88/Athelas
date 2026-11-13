#!/bin/sh
# Athelas - builds and runs the launcher's tests (tests/tests.cpp). Run after build.sh (it links the launcher's own
# object files). Needs wine (the 32-bit one) and 7z on Linux; exits non-zero if a check fails.
set -e
cd "$(dirname "$0")"
SDL=${SDL_DIR:-/tmp/claude-0/sdl/SDL3-3.4.20}/i686-w64-mingw32
T=build/tests
rm -rf "$T"; mkdir -p "$T/src7z/folder"
OBJS=$(ls build/obj/*.o | grep -v '/main\.o$' | grep -v 'launcher_res\.o$')
i686-w64-mingw32-g++ -O1 -std=c++17 -Wall -Wextra -c tests/tests.cpp -o "$T/tests.o"
i686-w64-mingw32-g++ -mconsole -static-libgcc -static-libstdc++ -o "$T/tests.exe" "$T/tests.o" $OBJS \
  "$SDL/lib/libSDL3.dll.a" -lshell32 -lole32 -luuid -lwinhttp -lbcrypt -ladvapi32
cp "$SDL/bin/SDL3.dll" "$T/"
# a small archive made the way package.sh makes the release (solid blocks, the BCJ filter)
printf 'Athelas test file\r\n' > "$T/src7z/Athelas.txt"
head -c 300000 /dev/urandom > "$T/src7z/folder/inner.bin"
: > "$T/src7z/folder/empty.txt"
(cd "$T/src7z" && 7z a -t7z -mx=9 -ms=64m -mf=BCJ -bd -bso0 ../test.7z .)
if command -v wine >/dev/null 2>&1; then RUN="wine"; else RUN=""; fi
(cd "$T" && WINEDEBUG=${WINEDEBUG:--all} $RUN ./tests.exe .)
