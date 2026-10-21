#!/bin/sh
# Build everything and assemble the release zip:  scripts/package.sh <version>
# Needs: i686-w64-mingw32 toolchain, zip, and
#   SDL_DIR  = unpacked SDL3 mingw development package (SDL3-devel-<v>-mingw.tar.gz)
#   DXVK_DIR = unpacked DXVK release (dxvk-<v>.tar.gz)
# Optional:
#   DSOAL_DIR = unpacked DSOAL release (its Win32 folder): 3D sound / EAX
#   TT_DIR    = The Two Towers port's release folder (TwoTowers.exe and its licence files)
#   RX_DIR    = The Return of the King Xbox port's release folder (ROTKXbox.exe and its licence files)
#   FX_DIR    = The Fellowship of the Ring Xbox port's release folder (FOTRXbox.exe and its licence files)
#   TT_ARM, RX_ARM = the ports' ARM64 builds (TwoTowers-arm64.exe, ROTKXbox-arm64.exe): with both, a second zip,
#               Athelas-<ver>-arm64.zip, the same files plus those two and ARM64.txt (the launcher's sign that its
#               ARM64 builds are this release's; its updater fetches this zip on Windows on ARM)
#
# Layout (the launcher lives in a folder of its own; see README.txt):
#   Athelas.exe, SDL3.dll, README.txt
#   ROTK-PC\pcfix\   what "Set up ROTK Fix" copies into a PC game folder:
#                    ROTKFix.dll, d3d8.dll, dinput8.dll, SDL3.dll, ROTKFix\dxvk, \sound, \licenses
#   TT-Xbox\, ROTK-Xbox\, FOTR-Xbox\   the Xbox ports (they keep their settings, saves and logs there too)
#   licenses\        every third-party licence
set -e
cd "$(dirname "$0")/.."
VERSION_FILE=VERSION . scripts/version.sh
VER=${1:-$ROTKFIX_VERSION}
: "${SDL_DIR:?set SDL_DIR}" "${DXVK_DIR:?set DXVK_DIR}"
export SDL_DIR
sh build.sh
sh launcher/build.sh
OUT=dist/Athelas-$VER
FIX=$OUT/ROTK-PC/pcfix
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$FIX/ROTKFix/dxvk" "$FIX/ROTKFix/licenses" "$OUT/licenses"
cp launcher/build/Athelas.exe dist-files/README.txt "$OUT/"
cp "$SDL_DIR/i686-w64-mingw32/bin/SDL3.dll" "$OUT/"
cp build/ROTKFix.dll build/d3d8.dll build/dinput8.dll "$SDL_DIR/i686-w64-mingw32/bin/SDL3.dll" "$FIX/"
cp "$DXVK_DIR/x32/d3d8.dll" "$DXVK_DIR/x32/d3d9.dll" "$FIX/ROTKFix/dxvk/"
cp licenses/* "$FIX/ROTKFix/licenses/"
cp licenses/* "$OUT/licenses/"
if [ -n "$DSOAL_DIR" ]; then mkdir -p "$FIX/ROTKFix/sound"; cp "$DSOAL_DIR/dsound.dll" "$DSOAL_DIR/dsoal-aldrv.dll" "$FIX/ROTKFix/sound/"; fi
# their source at the exact commits shipped (LGPL), once, in the zip's own licenses folder
if [ -d third_party/dsoal/source ]; then mkdir -p "$OUT/licenses/source"; cp third_party/dsoal/source/*.zip "$OUT/licenses/source/"; fi
if [ -n "$TT_DIR" ]; then mkdir -p "$OUT/TT-Xbox"; cp -r "$TT_DIR"/. "$OUT/TT-Xbox/"; fi
if [ -n "$RX_DIR" ]; then mkdir -p "$OUT/ROTK-Xbox"; cp -r "$RX_DIR"/. "$OUT/ROTK-Xbox/"; fi
if [ -n "$FX_DIR" ]; then mkdir -p "$OUT/FOTR-Xbox"; cp -r "$FX_DIR"/. "$OUT/FOTR-Xbox/"; fi
# the Xbox ports are built on XboxXBE-Recomp (MIT): one copy of its licence with the others
for f in "$OUT"/TT-Xbox/LICENSE-XboxXBE-Recomp.txt "$OUT"/ROTK-Xbox/LICENSE.txt "$OUT"/FOTR-Xbox/LICENSE.txt; do [ -f "$f" ] && cp "$f" "$OUT/licenses/XboxXBE-Recomp-LICENSE.txt" && break; done
(cd "$OUT" && zip -qr9 "../Athelas-$VER.zip" .)
(cd dist && sha256sum "Athelas-$VER.zip" > "Athelas-$VER.zip.sha256")
echo "built dist/Athelas-$VER.zip"
cat "dist/Athelas-$VER.zip.sha256"
if [ -n "$TT_ARM" ] && [ -n "$RX_ARM" ]; then
    ARM=dist/Athelas-$VER-arm64
    rm -rf "$ARM" "$ARM.zip"
    cp -r "$OUT" "$ARM"
    cp "$TT_ARM" "$ARM/TT-Xbox/TwoTowers-arm64.exe"
    cp "$RX_ARM" "$ARM/ROTK-Xbox/ROTKXbox-arm64.exe"
    (cd "$ARM/TT-Xbox" && sha256sum TwoTowers-arm64.exe > TwoTowers-arm64.exe.sha256)
    (cd "$ARM/ROTK-Xbox" && sha256sum ROTKXbox-arm64.exe > ROTKXbox-arm64.exe.sha256)
    printf '%s\r\n' "$VER" > "$ARM/ARM64.txt"
    (cd "$ARM" && zip -qr9 "../Athelas-$VER-arm64.zip" .)
    (cd dist && sha256sum "Athelas-$VER-arm64.zip" > "Athelas-$VER-arm64.zip.sha256")
    echo "built dist/Athelas-$VER-arm64.zip"
    cat "dist/Athelas-$VER-arm64.zip.sha256"
fi
