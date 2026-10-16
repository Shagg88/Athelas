#!/bin/bash
# compile the generated partitions once (cached by the harness build via -G)
GEN=/home/claude/tt/game/generated/game; RT=/home/claude/tt/game/sdk/runtime; OUT=/home/claude/tt/hbuild/gen
BASE="-std=c++20 -march=x86-64-v2 -fno-strict-aliasing"
export GEN RT OUT BASE
ls "$GEN"/x86_recomp.*.cpp "$GEN"/x86_func_mapping.cpp | xargs -P 2 -I{} sh -c '
  o="$OUT/$(basename {} .cpp).o"; [ "$o" -nt {} ] && exit 0
  start=$(date +%s); clang++ $BASE -O1 -g0 -w -I"$RT" -I"$GEN" -c {} -o "$o.tmp" && mv "$o.tmp" "$o" && echo "done $(basename {}) $(( $(date +%s)-start ))s" || echo "FAIL {}"'
echo ALL_DONE
