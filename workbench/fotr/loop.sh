#!/bin/bash
# loop.sh NAME SECONDS [pad] : regenerate, rebuild the FOTR harness, run once (env passed through)
set -e
R=/home/claude/fotr-port/fotrxbox
cd $R && /home/claude/work/ftools/b/xbe_recomp config/game.toml --xdk sdk/xdk 2>&1 | grep -E "^total|error" || true
cd /home/claude/work && JOBS=2 TOML_INCLUDE=/home/claude/work/tools/b/_deps/tomlplusplus-src/include bash $R/sdk/harness/build.sh -r $R -g $R/generated/game -o /home/claude/work/h_fotr -n fotr -G /home/claude/work/h_fotr/gen 2>&1 | grep -E "error|built" | head
cd /home/claude/work/fotr; O=runs/$1; rm -rf $O; mkdir -p $O
FPS=${FPS:-0} REF=${REF:-60} bash $R/sdk/harness/run.sh -x /home/claude/work/h_fotr/fotr -g /mnt/user-data/uploads/FOTR-extracted -o $O -t ${2:-60} -s ${3:-$PWD/pad1.txt} > $O.out 2>&1 || true
tail -2 $O.out
