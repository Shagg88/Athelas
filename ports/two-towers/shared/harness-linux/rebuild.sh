#!/bin/bash
# regenerate + compile changed partitions + relink the headless harness
set -e
cd /home/claude/tt/game
/home/claude/tt/build_sym/b/xbe_recomp config/game.toml --xdk sdk/xdk 2>&1 | grep -E "^total:|hook|error" || true
/home/claude/tt/hbuild/compile_gen.sh 2>&1 | grep -E "FAIL|done" | tail -3
TOML_INCLUDE=/home/claude/tt/build_sym/b/_deps/tomlplusplus-src/include JOBS=2 bash sdk/harness/build.sh -r . -g generated/game -o /home/claude/tt/hbuild -n tt_headless -G /home/claude/tt/hbuild/gen 2>&1 | grep -vE "^compile " | grep -E "error|built" | head -20
