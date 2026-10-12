#!/bin/sh
# Build everything and assemble the release zip:  scripts/package.sh <version>
# Needs: i686-w64-mingw32 toolchain, zip, and
#   SDL_DIR  = unpacked SDL3 mingw development package (SDL3-devel-<v>-mingw.tar.gz)
#   DXVK_DIR = unpacked DXVK release (dxvk-<v>.tar.gz)
set -e
cd "$(dirname "$0")/.."
VER=${1:-dev}
: "${SDL_DIR:?set SDL_DIR}" "${DXVK_DIR:?set DXVK_DIR}"
export SDL_DIR
sh build.sh
sh launcher/build.sh
OUT=dist/ROTKFix-$VER
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/ROTKFix/dxvk" "$OUT/ROTKFix/licenses"
cp build/ROTKFix.dll build/d3d8.dll build/dinput8.dll launcher/build/ROTKFixLauncher.exe "$OUT/"
cp "$SDL_DIR/i686-w64-mingw32/bin/SDL3.dll" "$OUT/"
cp "$DXVK_DIR/x32/d3d8.dll" "$DXVK_DIR/x32/d3d9.dll" "$OUT/ROTKFix/dxvk/"
cp licenses/* "$OUT/ROTKFix/licenses/"
# DSOAL + OpenAL Soft (3D sound / EAX): DSOAL_DIR = unpacked DSOAL release (the DSOAL/Win32 folder)
if [ -n "$DSOAL_DIR" ]; then mkdir -p "$OUT/ROTKFix/sound"; cp "$DSOAL_DIR/dsound.dll" "$DSOAL_DIR/dsoal-aldrv.dll" "$OUT/ROTKFix/sound/"; fi
cp dist-files/README.txt "$OUT/"
if [ -d dist-files/backgrounds ]; then mkdir -p "$OUT/ROTKFix/backgrounds"; cp dist-files/backgrounds/* "$OUT/ROTKFix/backgrounds/"; fi
(cd "$OUT" && zip -qr9 "../ROTKFix-$VER.zip" .)
(cd dist && sha256sum "ROTKFix-$VER.zip" > "ROTKFix-$VER.zip.sha256")
echo "built dist/ROTKFix-$VER.zip"
cat "dist/ROTKFix-$VER.zip.sha256"
