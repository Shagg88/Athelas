#!/bin/bash
# Boots every level in the headless harness for about two minutes each, two at a time, with the settings below.
# TT_LEVEL loads the named level in place of the opening battle (src/game/tt_harness_level.cpp). Results go to
# runs/lv2_<level>/ and runs/lv2_<level>.txt; lvreport.py summarises them.
# Edit the paths: WORK holds hbuild/tt_headless and the game folder, as rebuild.sh lays them out.
WORK=${WORK:-/home/claude/tt}
GAME=${GAME:-gamedata_sweep}   # the game's files (Fangorn needs its stream bank S_WB_14FANG2.xwb)
PAD=${PAD:-$(dirname "$0")/pad_lv.txt}
cd "$WORK"
run() { L=$1; rm -rf runs/lv2_$L; mkdir -p runs/lv2_$L/1
  printf '[graphics]\nskip_all_videos = true\nwidescreen = true\nfps = "60"\nsmooth_2d = true\n[gameplay]\ndamage_taken = 150\n' > runs/lv2_$L/1/settings.toml
  TT_ASPECT=2.3703 TT_LEVEL=$L REF=1100 bash game/sdk/harness/run.sh -x hbuild/tt_headless -g "$GAME" -o runs/lv2_$L -t 125 -s "$PAD" > runs/lv2_$L.txt 2>&1; }
set -- 00ISUL1 01WEAT1 02GATE2 04BALIN1 13AMON1 14FANG2 16ROHAN2 16ROHAN1 16ROHAN3 17TOP1 18HOLE1 20YARD1 21ORTH1 21ORTH2
while [ $# -gt 0 ]; do a=$1; shift; b=$1; [ $# -gt 0 ] && shift; run $a & [ -n "$b" ] && run $b & wait; done
echo SWEEP DONE
