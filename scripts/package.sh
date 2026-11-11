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
#   TA_DIR    = The Third Age Xbox port's release folder (TAXbox.exe and its licence files)
#   KITS_DIR  = the ports' build kits (BUILD_RELEASE.bat's release-out\\kits): since 1.7.3 the port folders come without
#               their programs and the launcher builds them on the player's PC
#   TOOLS_X64 = the x64 zip's build tools (make_toolchain.ps1 -Target x86_64)
#   KITS_ARM_DIR, TOOLS_ARM = the ARM64 zip's kits (<folder>-arm64) and build tools (-Target aarch64)
#   SEED_DIR  = shader seeds recorded from every level (tt|rx|fx|ta.shaders and .pipelines): each port's folder gets
#               shaders.seed and pipelines.seed, compiled in the background at a first start (no first-play stutter)
#   UP_DIR    = the upscalers' files for the Xbox ports, one shared copy in upscalers\: AMD FSR SDK 2.4
#               (amd_fidelityfx_loader_dx12.dll, amd_fidelityfx_upscaler_dx12.dll, amd_fidelityfx_framegeneration_dx12.dll), NVIDIA Streamline 2.14 with
#               DLSS and DLSS frame generation (sl.interposer.dll, sl.common.dll, sl.dlss.dll, sl.dlss_g.dll, sl.reflex.dll,
#               sl.pcl.dll, nvngx_dlss.dll, nvngx_dlssg.dll) and Intel XeSS 3.0 (libxess.dll, libxess_fg.dll, libxell.dll),
#               their licences and README.txt (Intel's licence from sdk/third_party/xess).
#               x64 only: left out of the ARM64 zip
#   TT_ARM, RX_ARM = the ports' ARM64 builds (TwoTowers-arm64.exe, ROTKXbox-arm64.exe): with both, a second zip,
#               (FX_ARM, FOTRXbox-arm64.exe, goes in it too when given)
#               Athelas-<ver>-arm64.zip, the same files plus those two and ARM64.txt (the launcher's sign that its
#               ARM64 builds are this release's; its updater fetches this zip on Windows on ARM)
#
# Layout (the launcher lives in a folder of its own; see README.txt):
#   Athelas.exe, SDL3.dll, README.txt, lang\ (the launcher in other languages)
#   ROTK-PC\pcfix\   what "Set up ROTK Fix" copies into a PC game folder:
#                    ROTKFix.dll, d3d8.dll, dinput8.dll, SDL3.dll, ROTKFix\dxvk, \sound, \licenses
#   TT-Xbox\, ROTK-Xbox\, FOTR-Xbox\, TA-Xbox\   the Xbox ports (they keep their settings, saves and logs there too)
#   upscalers\       FSR, DLSS and XeSS for the Xbox ports (UP_DIR), under AMD's, NVIDIA's and Intel's own terms
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
rm -rf "$OUT" "$OUT.zip" "$OUT.7z"
mkdir -p "$FIX/ROTKFix/dxvk" "$FIX/ROTKFix/licenses" "$FIX/ROTKFix/mods" "$OUT/licenses"
if [ -d mods ]; then
    for mod_dir in mods/*; do
        if [ -d "$mod_dir" ]; then
            mod_name=$(basename "$mod_dir")
            if [ -f "$mod_dir/build.sh" ]; then
                echo "Building mod: $mod_name..."
                (cd "$mod_dir" && sh build.sh)
            fi
            for f in "$mod_dir"/*.dll "$mod_dir"/*.ini "$mod_dir"/*.ini.example; do
                [ -f "$f" ] && cp "$f" "$FIX/ROTKFix/mods/"
            done
            if [ -f "$mod_dir/README.md" ]; then
                cp "$mod_dir/README.md" "$FIX/ROTKFix/mods/${mod_name}-README.md"
            elif [ -f "$mod_dir/README.txt" ]; then
                cp "$mod_dir/README.txt" "$FIX/ROTKFix/mods/${mod_name}-README.txt"
            fi
            # a mod's own licence ships with its DLL (ROTKPlus is MIT: its notice must go with the binary)
            [ -f "$mod_dir/LICENSE" ] && cp "$mod_dir/LICENSE" "$FIX/ROTKFix/licenses/${mod_name}-LICENSE.txt"
        fi
    done
fi
cp launcher/build/Athelas.exe dist-files/README.txt "$OUT/"
# What's new (GitHub #51): every release's notes, newest first, without their checksum lists, read by the launcher
for f in $(ls docs/release-notes-*.md | sed 's/.*release-notes-//; s/\.md$//' | sort -t. -k1,1nr -k2,2nr -k3,3nr); do
    sed '/^## Checksums/,$d' "docs/release-notes-$f.md"; printf '\n'
done > "$OUT/WHATS-NEW.txt"
# the launcher's translations (lang\<code>.txt, read by Athelas.exe)
mkdir -p "$OUT/lang"; cp launcher/lang/*.txt "$OUT/lang/"
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
if [ -n "$TA_DIR" ]; then mkdir -p "$OUT/TA-Xbox"; cp -r "$TA_DIR"/. "$OUT/TA-Xbox/"; fi
# the build kits (release-out\kits): the launcher builds each port's program from them and the player's own game
if [ -n "$KITS_DIR" ]; then mkdir -p "$OUT/kits"; cp -r "$KITS_DIR"/. "$OUT/kits/"; fi
# the build tools those kits are built with (TOOLS_X64: make_toolchain.ps1 -Target x86_64), in tools\llvm-mingw
if [ -n "$TOOLS_X64" ]; then mkdir -p "$OUT/tools/llvm-mingw"; cp -r "$TOOLS_X64"/. "$OUT/tools/llvm-mingw/"; cp "$TOOLS_X64"/licenses/* "$OUT/licenses/" 2>/dev/null || true; fi
# no game file may be in the zip: an .xbe, an .iso, recompiled source
# (since 1.7.5 the ports' programs ship again, Shagg's call; the game's files and recompiled source still never do)
bad=$(cd "$OUT" && find . \( -iname '*.xbe' -o -iname '*.iso' -o -iname 'x86_recomp*' -o -iname 'x86_func_mapping*' \) -print)
if [ -n "$bad" ]; then echo "STOPPED: these are made from a game and can't ship:"; echo "$bad"; exit 1; fi
if [ -n "$SEED_DIR" ]; then
    for p in TT-Xbox:tt ROTK-Xbox:rx FOTR-Xbox:fx TA-Xbox:ta; do
        d=$OUT/${p%%:*}; g=${p#*:}
        [ -d "$d" ] && [ -f "$SEED_DIR/$g.shaders" ] && cp "$SEED_DIR/$g.shaders" "$d/shaders.seed"
        [ -d "$d" ] && [ -f "$SEED_DIR/$g.pipelines" ] && cp "$SEED_DIR/$g.pipelines" "$d/pipelines.seed"
    done
fi
if [ -n "$UP_DIR" ]; then
    mkdir -p "$OUT/upscalers"
    for f in amd_fidelityfx_loader_dx12.dll amd_fidelityfx_upscaler_dx12.dll amd_fidelityfx_framegeneration_dx12.dll sl.interposer.dll sl.common.dll sl.dlss.dll \
             sl.dlss_g.dll sl.reflex.dll sl.pcl.dll nvngx_dlss.dll nvngx_dlssg.dll libxess.dll libxess_fg.dll libxell.dll \
             AMD-FSR-LICENSE.md NVIDIA-DLSS-LICENSE.txt NVIDIA-Streamline-LICENSE.txt README.txt; do
        cp "$UP_DIR/$f" "$OUT/upscalers/"
    done
    cp sdk/third_party/xess/LICENSE.txt "$OUT/upscalers/INTEL-XeSS-LICENSE.txt"
    # the notices for what Intel's and NVIDIA's DLLs carry inside them (their licences ask for these with the binaries)
    cp sdk/third_party/xess/third-party-programs.txt "$OUT/upscalers/INTEL-XeSS-third-party-programs.txt"
    cp sdk/third_party/streamline/3rd-party-licenses.md "$OUT/upscalers/NVIDIA-Streamline-3rd-party-licenses.md"
    cp "$OUT/upscalers/INTEL-XeSS-third-party-programs.txt" "$OUT/upscalers/NVIDIA-Streamline-3rd-party-licenses.md" "$OUT/licenses/"
    cp "$UP_DIR/AMD-FSR-LICENSE.md" "$UP_DIR/NVIDIA-DLSS-LICENSE.txt" "$UP_DIR/NVIDIA-Streamline-LICENSE.txt" "$OUT/upscalers/INTEL-XeSS-LICENSE.txt" "$OUT/licenses/"
fi
# the Xbox ports are built on XboxXBE-Recomp (MIT): one copy of its licence with the others
for f in "$OUT"/TT-Xbox/LICENSE-XboxXBE-Recomp.txt "$OUT"/ROTK-Xbox/LICENSE.txt "$OUT"/FOTR-Xbox/LICENSE.txt "$OUT"/TA-Xbox/LICENSE.txt; do [ -f "$f" ] && cp "$f" "$OUT/licenses/XboxXBE-Recomp-LICENSE.txt" && break; done
manifest() {   # every file the download has, with its SHA-256: the launcher removes what a newer version no longer ships
    (cd "$1" && rm -f athelas-files.txt && cp "$OLDPWD/dist-files/athelas-shipped-before.txt" . \
        && find . -type f ! -name athelas-files.txt -exec sha256sum {} + | sed 's#  \./#  #' | LC_ALL=C sort -k2 > athelas-files.txt)
}
# every licence must be in the download, or nothing is made (Shagg, 8 Nov 2026)
missing=""
for f in licenses/*; do [ -f "$OUT/licenses/$(basename "$f")" ] || missing="$missing licenses/$(basename "$f")"; done
[ -f "$OUT/licenses/XboxXBE-Recomp-LICENSE.txt" ] || { [ -z "$TT_DIR$RX_DIR$FX_DIR$TA_DIR" ] || missing="$missing licenses/XboxXBE-Recomp-LICENSE.txt"; }
for f in licenses/*; do [ -f "$FIX/ROTKFix/licenses/$(basename "$f")" ] || missing="$missing ROTK-PC/pcfix/ROTKFix/licenses/$(basename "$f")"; done
if [ -n "$DSOAL_DIR" ] && [ -d third_party/dsoal/source ]; then ls "$OUT"/licenses/source/*.zip >/dev/null 2>&1 || missing="$missing licenses/source (DSOAL/OpenAL Soft source)"; fi
if [ -d "$OUT/upscalers" ]; then
    for f in AMD-FSR-LICENSE.md NVIDIA-DLSS-LICENSE.txt NVIDIA-Streamline-LICENSE.txt NVIDIA-Streamline-3rd-party-licenses.md \
             INTEL-XeSS-LICENSE.txt INTEL-XeSS-third-party-programs.txt; do
        [ -f "$OUT/upscalers/$f" ] || missing="$missing upscalers/$f"
        [ -f "$OUT/licenses/$f" ] || missing="$missing licenses/$f"
    done
fi
for d in FOTR-Xbox TA-Xbox; do
    [ -d "$OUT/$d/fonts" ] && for f in CormorantUnicase-OFL.txt PTSans-OFL.txt UncialAntiqua-OFL.txt; do [ -f "$OUT/$d/fonts/$f" ] || missing="$missing $d/fonts/$f"; done
done
if [ -n "$missing" ]; then echo "STOPPED: licences missing from the download:$missing"; exit 1; fi
# the ports' ARM64 builds in the one download: ARM64.txt (this version) is the launcher's sign they belong to it
if ls "$OUT"/*-Xbox/*-arm64.exe >/dev/null 2>&1; then printf '%s\r\n' "$VER" > "$OUT/ARM64.txt"; fi
manifest "$OUT"
# a .7z since 1.8.3 (Shagg): smaller; files at its top, in 64 MB blocks (the 32-bit launcher unpacks updates a block at a time)
# -mf=BCJ: only the classic x86 filter, which every extractor knows (7-Zip's automatic ARM64 filter made older 7-Zip and
# WinRAR call the ARM64 programs corrupted, Reddit, Nov 2026)
(cd "$OUT" && 7z a -t7z -mx=9 -ms=64m -mf=BCJ -bd -bso0 "../Athelas-$VER.7z" .)
(cd dist && sha256sum "Athelas-$VER.7z" > "Athelas-$VER.7z.sha256")
echo "built dist/Athelas-$VER.7z"
cat "dist/Athelas-$VER.7z.sha256"
# the ARM64 zip: the same, with the ARM64 kits (KITS_ARM_DIR: <folder>-arm64) and ARM64 build tools (TOOLS_ARM)
if [ -n "$KITS_DIR" ] && [ -n "$KITS_ARM_DIR" ] && [ -n "$TOOLS_ARM" ]; then
    ARM=dist/Athelas-$VER-arm64
    rm -rf "$ARM" "$ARM.zip"
    cp -r "$OUT" "$ARM"
    rm -rf "$ARM/upscalers" "$ARM/kits" "$ARM/tools"   # (AMD's and NVIDIA's files are x64 only)
    mkdir -p "$ARM/kits" "$ARM/tools/llvm-mingw"
    cp -r "$KITS_ARM_DIR"/. "$ARM/kits/"
    cp -r "$TOOLS_ARM"/. "$ARM/tools/llvm-mingw/"
    printf '%s\r\n' "$VER" > "$ARM/ARM64.txt"
    manifest "$ARM"
    (cd "$ARM" && zip -qr9 "../Athelas-$VER-arm64.zip" .)
    (cd dist && sha256sum "Athelas-$VER-arm64.zip" > "Athelas-$VER-arm64.zip.sha256")
    echo "built dist/Athelas-$VER-arm64.zip"
    cat "dist/Athelas-$VER-arm64.zip.sha256"
fi
if [ -z "$KITS_DIR" ] && [ -n "$TT_ARM" ] && [ -n "$RX_ARM" ]; then
    ARM=dist/Athelas-$VER-arm64
    rm -rf "$ARM" "$ARM.zip"
    cp -r "$OUT" "$ARM"
    rm -rf "$ARM/upscalers"  # AMD's and NVIDIA's files are x64 only
    cp "$TT_ARM" "$ARM/TT-Xbox/TwoTowers-arm64.exe"
    cp "$RX_ARM" "$ARM/ROTK-Xbox/ROTKXbox-arm64.exe"
    (cd "$ARM/TT-Xbox" && sha256sum TwoTowers-arm64.exe > TwoTowers-arm64.exe.sha256)
    (cd "$ARM/ROTK-Xbox" && sha256sum ROTKXbox-arm64.exe > ROTKXbox-arm64.exe.sha256)
    if [ -n "$FX_ARM" ] && [ -d "$ARM/FOTR-Xbox" ]; then
        cp "$FX_ARM" "$ARM/FOTR-Xbox/FOTRXbox-arm64.exe"
        (cd "$ARM/FOTR-Xbox" && sha256sum FOTRXbox-arm64.exe > FOTRXbox-arm64.exe.sha256)
    fi
    printf '%s\r\n' "$VER" > "$ARM/ARM64.txt"
    (cd "$ARM" && zip -qr9 "../Athelas-$VER-arm64.zip" .)
    (cd dist && sha256sum "Athelas-$VER-arm64.zip" > "Athelas-$VER-arm64.zip.sha256")
    echo "built dist/Athelas-$VER-arm64.zip"
    cat "dist/Athelas-$VER-arm64.zip.sha256"
fi
