# Workbench

Claude's test and research scripts, kept with the code they test. They ran on Linux against the headless test
builds of the ports (`ports/*/…/sdk/harness`): level sweeps (`sweep_*.sh`, `rx_sweep.sh`, `tt_sweep*.sh`), menu and
controller walks, texture dumps and packs (`build_pack.py`, `addpack.py`, `tex*.py`), shader seeds (`seed/`), the AI
texture upscale pipeline (`aiup/`), the translation tools (`i18n/`), profiling (`prof/`) and one-off investigations
(`heroes*`, `retarget/`, `csel/`, `gt/`, `gand/`).

Paths in them are those of Claude's Linux workspace (`/home/claude/...`). The data they read and wrote (the game's
own text, texture dumps, test runs) is not here: game data never goes in this repository. The texture library and
the AI packs are in `C:\Athelas\private`.
