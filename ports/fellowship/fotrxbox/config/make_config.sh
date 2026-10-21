#!/bin/bash
# rebuild config/game.toml: xbe_symbols output + XbSymbolDatabase (with LTCG conventions) + hand-proven addresses + hooks
# inputs in ../analysis are made from the player's Default.xbe and stay out of the repository
set -e
cd "$(dirname "$0")/.."
SDK_DIR=sdk python3 ../tools/merge_ltcg.py ../analysis/game_try5849.toml ../analysis/symbols_raw.txt config/game.toml ../xdk_4831/manual.toml
# the CRT's setjmp (_setjmp3, "VC20"): Lua's protected calls (0x895D9) longjmp back there on a script error
sed -i 's/^partitions = .*/&\nsetjmp = [0x0007A4C8]/' config/game.toml
cat config/hooks.toml >> config/game.toml
