#!/bin/sh
# Athelas, the launcher (32-bit, uses the SDL3.dll shipped with ROTK Fix)
set -e
cd "$(dirname "$0")"
SDL=${SDL_DIR:-/tmp/claude-0/sdl/SDL3-3.4.18}/i686-w64-mingw32   # SDL3 mingw development package
IM=third_party/imgui
mkdir -p build/obj
CXX="i686-w64-mingw32-g++ -O2 -std=c++17 -I$IM -I$SDL/include -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS"
for f in imgui imgui_draw imgui_tables imgui_widgets imgui_impl_sdl3 imgui_impl_sdlrenderer3; do
  [ build/obj/$f.o -nt $IM/$f.cpp ] || $CXX -c $IM/$f.cpp -o build/obj/$f.o
done
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/config.cpp -o build/obj/config.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/library.cpp -o build/obj/library.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/port.cpp -o build/obj/port.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/ornament.cpp -o build/obj/ornament.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/gpu.cpp -o build/obj/gpu.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/gameart.cpp -o build/obj/gameart.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/main.cpp -o build/obj/main.o
i686-w64-mingw32-windres src/launcher.rc -O coff -o build/obj/launcher_res.o
i686-w64-mingw32-g++ -mwindows -static-libgcc -static-libstdc++ -s -o build/Athelas.exe build/obj/*.o \
  $SDL/lib/libSDL3.dll.a -lshell32 -lole32 -luuid
