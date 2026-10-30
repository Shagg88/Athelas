#!/bin/sh
# Athelas, the launcher (32-bit, uses the SDL3.dll shipped with ROTK Fix)
set -e
cd "$(dirname "$0")"
SDL=${SDL_DIR:-/tmp/claude-0/sdl/SDL3-3.4.18}/i686-w64-mingw32   # SDL3 mingw development package
IM=third_party/imgui
VERSION_FILE=../VERSION . ../scripts/version.sh
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
[ build/obj/miniz.o -nt third_party/miniz/miniz.c ] || i686-w64-mingw32-gcc -O2 -c third_party/miniz/miniz.c -o build/obj/miniz.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/update.cpp -o build/obj/update.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/report.cpp -o build/obj/report.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/music.cpp -o build/obj/music.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/userdata.cpp -o build/obj/userdata.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/i18n.cpp -o build/obj/i18n.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/hwrec.cpp -o build/obj/hwrec.o
$CXX -Wall -Wextra -Wno-missing-field-initializers -c src/builder.cpp -o build/obj/builder.o
$CXX -Wall -Wextra -Wno-missing-field-initializers $VDEF -c src/main.cpp -o build/obj/main.o
printf '#define VERSION_RC %s\n#define VERSION_STR "%s"\n' "$VERSION_RC" "$ROTKFIX_VERSION" > build/obj/version_rc.h
i686-w64-mingw32-windres -Ibuild/obj src/launcher.rc -O coff -o build/obj/launcher_res.o
i686-w64-mingw32-g++ -mwindows -static-libgcc -static-libstdc++ -s -o build/Athelas.exe build/obj/*.o \
  $SDL/lib/libSDL3.dll.a -lshell32 -lole32 -luuid -lwinhttp -lbcrypt -ladvapi32 -luuid
