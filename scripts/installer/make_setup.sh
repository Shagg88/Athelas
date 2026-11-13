#!/bin/sh
# Athelas-Setup-<ver>.exe from a release archive: make_setup.sh <ver> <Athelas-<ver>.7z> <out dir>  (needs makensis, 7z)
set -e
VER="$1"; ARC=$(realpath "$2"); OUTDIR=$(realpath "$3")
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d)
mkdir "$W/files"
7z x -bd -bso0 -o"$W/files" "$ARC"
# the uninstaller's list: only the files the download has (not the mods' .ini settings), deepest folders last
( cd "$W/files" && find . -type f ! -iname '*.ini' | sed 's#^\./##' | LC_ALL=C sort | while IFS= read -r f; do
    printf 'Delete "$INSTDIR\\%s"\n' "$(echo "$f" | tr / '\\')"; done
  find . -mindepth 1 -type d | sed 's#^\./##' | awk '{ print length($0) "\t" $0 }' | sort -rn | cut -f2- | while IFS= read -r d; do
    printf 'RMDir "$INSTDIR\\%s"\n' "$(echo "$d" | tr / '\\')"; done ) > "$W/files.nsh"
# Windows' file version is four numbers: 1.8.4-rc1 -> 1.8.4.0
VERNUM=$(printf '%s' "$VER" | sed 's/^[vV]//; s/[^0-9.].*//; s/\.\.*/./g; s/^\.//; s/\.$//' | cut -d. -f1-4)
[ -n "$VERNUM" ] || VERNUM=0
while [ "$(printf '%s' "$VERNUM" | tr -cd . | wc -c)" -lt 3 ]; do VERNUM="$VERNUM.0"; done
makensis -V2 -DVER="$VER" -DVERNUM="$VERNUM" -DSRC="$W/files" -DOUT="$OUTDIR/Athelas-Setup-$VER.exe" -DICON="$HERE/../../launcher/art/rotkfix.ico" \
    -DFILES_INC="$W/files.nsh" "$HERE/athelas.nsi"
(cd "$OUTDIR" && sha256sum "Athelas-Setup-$VER.exe" > "Athelas-Setup-$VER.exe.sha256")
rm -rf "$W"
ls -l "$OUTDIR/Athelas-Setup-$VER.exe"
