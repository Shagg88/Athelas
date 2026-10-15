#!/bin/bash
# regenerate config + code, compile changed partitions, relink the headless harness
set -e
cd /home/claude/rotkx/rotkxbox
config/make_config.sh >/dev/null
/home/claude/rotkx/work/tools/b/xbe_recomp config/game.toml --xdk sdk/xdk 2>&1 | grep -E "^total:|hook|error" || true
/home/claude/rotkx/work/compile_gen.sh 2>&1 | grep -E "FAIL" | tail -3
TOML_INCLUDE=/home/claude/rotkx/work/tools/b/_deps/tomlplusplus-src/include JOBS=2 bash sdk/harness/build.sh -r . -g generated/game -o /home/claude/rotkx/work/hbuild -n rotkx_headless -G /home/claude/rotkx/work/hbuild/gen 2>&1 | grep -vE "^compile " | grep -E "error|built" | head -20
