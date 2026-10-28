#!/bin/bash
# build_port.sh <repo game dir> <name> : recompile + headless harness build with the merged tools
set -e
G=$1; N=$2; T=/home/claude/work/tools/b
cd "$G"
[ -f config/game.toml ] || exit 3
$T/xbe_recomp config/game.toml --xdk sdk/xdk 2>&1 | grep -E "^total:|error" | tail -5
TOML_INCLUDE=$T/_deps/tomlplusplus-src/include JOBS=2 bash sdk/harness/build.sh -r . -g generated/game -o /home/claude/work/h_$N -n ${N}_headless 2>&1 | grep -vE "^compile " | grep -E "error|warning: unused|built|Error" | head -40
ls -la /home/claude/work/h_$N/${N}_headless
