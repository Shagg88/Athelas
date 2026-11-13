#!/bin/sh
# Athelas, the launcher (32-bit, uses the SDL3.dll shipped with ROTK Fix)
set -e
cd "$(dirname "$0")"
SDL=${SDL_DIR:-/tmp/claude-0/sdl/SDL3-3.4.20}/i686-w64-mingw32   # SDL3 mingw development package
IM=third_party/imgui
VERSION_FILE=../VERSION . ../scripts/version.sh
mkdir -p build/obj
# the launcher's own objects are all rebuilt below: old ones (from a source file since removed or split) would
# otherwise be linked in by build/obj/*.o; Dear ImGui's and miniz's are kept
find build/obj -name '*.o' ! -name 'imgui*' ! -name 'miniz.o' ! -name 'lzma_*' -delete
CXX="i686-w64-mingw32-g++ -O2 -std=c++17 -isystem $IM -isystem $SDL/include -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS"
# the launcher's own code (Dear ImGui's and SDL's headers are -isystem: their warnings aren't ours)
WARN="-Wall -Wextra -Wno-missing-field-initializers -Wshadow -Wconversion -Wformat=2 -Wno-format-nonliteral -Wduplicated-cond -Wlogical-op -Wredundant-decls -Wnull-dereference"
for f in imgui imgui_draw imgui_tables imgui_widgets imgui_impl_sdl3 imgui_impl_sdlrenderer3; do
  [ build/obj/$f.o -nt $IM/$f.cpp ] || $CXX -c $IM/$f.cpp -o build/obj/$f.o
done
$CXX $WARN -c src/config.cpp -o build/obj/config.o
$CXX $WARN -c src/library.cpp -o build/obj/library.o
$CXX $WARN -c src/port.cpp -o build/obj/port.o
$CXX $WARN -c src/ornament.cpp -o build/obj/ornament.o
$CXX $WARN -c src/gpu.cpp -o build/obj/gpu.o
$CXX $WARN -c src/gameart.cpp -o build/obj/gameart.o
[ build/obj/miniz.o -nt third_party/miniz/miniz.c ] || i686-w64-mingw32-gcc -O2 -c third_party/miniz/miniz.c -o build/obj/miniz.o
$CXX $WARN -c src/update.cpp -o build/obj/update.o
$CXX $WARN -c src/unpack7z.cpp -o build/obj/unpack7z.o
# LZMA SDK's 7z decoder (public domain): the release download is a .7z since 1.8.3
for f in 7zAlloc 7zArcIn 7zBuf 7zCrc 7zCrcOpt 7zDec 7zStream Bcj2 Bra Bra86 BraIA64 CpuArch Delta Lzma2Dec LzmaDec; do
  [ build/obj/lzma_$f.o -nt third_party/lzma/$f.c ] || i686-w64-mingw32-gcc -O2 -c third_party/lzma/$f.c -o build/obj/lzma_$f.o
done
$CXX $WARN -c src/report.cpp -o build/obj/report.o
$CXX $WARN -c src/music.cpp -o build/obj/music.o
$CXX $WARN -c src/userdata.cpp -o build/obj/userdata.o
$CXX $WARN -c src/i18n.cpp -o build/obj/i18n.o
$CXX $WARN -c src/hwrec.cpp -o build/obj/hwrec.o
$CXX $WARN -c src/builder.cpp -o build/obj/builder.o
# the launcher's pages (app.h: what they share)
$CXX $WARN $VDEF -c src/ui.cpp -o build/obj/ui.o
$CXX $WARN $VDEF -c src/backgrounds.cpp -o build/obj/backgrounds.o
$CXX $WARN $VDEF -c src/pc_settings.cpp -o build/obj/pc_settings.o
$CXX $WARN $VDEF -c src/shortcuts.cpp -o build/obj/shortcuts.o
$CXX $WARN $VDEF -c src/launcher_settings.cpp -o build/obj/launcher_settings.o
$CXX $WARN $VDEF -c src/updater_ui.cpp -o build/obj/updater_ui.o
$CXX $WARN $VDEF -c src/mods.cpp -o build/obj/mods.o
$CXX $WARN $VDEF -c src/saves.cpp -o build/obj/saves.o
$CXX $WARN $VDEF -c src/ports.cpp -o build/obj/ports.o
$CXX $WARN $VDEF -c src/release_report.cpp -o build/obj/release_report.o
$CXX $WARN $VDEF -c src/problem_report.cpp -o build/obj/problem_report.o
$CXX $WARN $VDEF -c src/xbox_pages.cpp -o build/obj/xbox_pages.o
$CXX $WARN $VDEF -c src/pages.cpp -o build/obj/pages.o
$CXX $WARN $VDEF -c src/main.cpp -o build/obj/main.o
printf '#define VERSION_RC %s\n#define VERSION_STR "%s"\n' "$VERSION_RC" "$ROTKFIX_VERSION" > build/obj/version_rc.h
i686-w64-mingw32-windres -Ibuild/obj src/launcher.rc -O coff -o build/obj/launcher_res.o
i686-w64-mingw32-g++ -mwindows -Wl,--large-address-aware -static-libgcc -static-libstdc++ -s -o build/Athelas.exe build/obj/*.o \
  $SDL/lib/libSDL3.dll.a -lshell32 -lole32 -luuid -lwinhttp -lbcrypt -ladvapi32 -luuid
