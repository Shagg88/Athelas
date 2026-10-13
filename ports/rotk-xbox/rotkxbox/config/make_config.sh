#!/bin/bash
# rebuild config/game.toml: xbe_symbols output + XbSymbolDatabase + hand-proven addresses + hooks
# inputs in ../analysis are made from the player's default.xbe and stay out of the repository
set -e
cd "$(dirname "$0")/.."
python3 ../tools/merge_config.py ../analysis/game_try5849.toml ../analysis/symbols.txt config/game.toml ../sdk_5558/manual.toml
cat config/hooks.toml >> config/game.toml
