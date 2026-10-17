#!/bin/bash
# rebuild config/game.toml: xbe_symbols output + XbSymbolDatabase + hand-proven addresses + hooks
set -e
cd "$(dirname "$0")/.."
python3 ../tools/merge_config.py ../../analysis/game_try5849.toml ../../analysis/symbols_raw.txt config/game.toml ../sdk_4928/manual.toml
sed -i 's#path = "assets/Default.xbe"#path = "assets/default.xbe"#' config/game.toml
cat config/hooks.toml >> config/game.toml
# setjmp/longjmp (notes/10-setjmp.md): the CRT's setjmp, so longjmp's resume point is caught
sed -i 's|^code_sections = \[".text".*\]$|&\nsetjmp = [0x00100564]|' config/game.toml
