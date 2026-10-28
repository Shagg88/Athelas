#!/bin/bash
cd /home/claude/work
tt() { L=$1; D=runs/tttex/$L; rm -rf $D; mkdir -p $D/1
  printf '[graphics]\nskip_all_videos = true\n' > $D/1/settings.toml
  TEXPACK=1 TEXPACK_SCAN=3000 FPS=0 TT_LEVEL=$L bash /home/claude/tt-port/game/sdk/harness/run.sh -x h_tt_fx/tt_fx_headless -g /mnt/user-data/uploads/TT--extracted -o $D -t 75 -s /home/claude/tt-port/shared/harness-linux/pad_lv.txt > $D/run.txt 2>&1
  rm -f $D/1/cap/*.tga
  P=$D/1/cap/pack; G=$(dirname $(dirname $D))/$(basename $(dirname $D))_pool; mkdir -p $G; for f in $P/*.png; do [ -e "$f" ] && mv -n "$f" $G/ 2>/dev/null; done; rm -f $P/*.png $D/1/hdd -r
  echo "$L: $(grep -c '^tex' $D/1/cap/pack/index.txt 2>/dev/null) textures, traps $(grep -hc 'guest trap' $D/1/logs/*.log | paste -sd+ | bc)" >> runs/tttex/summary.txt; }
fx() { S=$1; D=runs/fxtex/$S; rm -rf $D; mkdir -p $D/1
  TEXPACK=1 TEXPACK_SCAN=7000 FPS=0 FOTR_SCENE=$S bash /home/claude/fotr-port/fotrxbox/sdk/harness/run.sh -x h_fotr/fotr -g /mnt/user-data/uploads/FOTR-extracted -o $D -t 50 -s /home/claude/work/fotr/pad_scene.txt > $D/run.txt 2>&1
  rm -f $D/1/cap/*.tga
  P=$D/1/cap/pack; G=$(dirname $(dirname $D))/$(basename $(dirname $D))_pool; mkdir -p $G; for f in $P/*.png; do [ -e "$f" ] && mv -n "$f" $G/ 2>/dev/null; done; rm -f $P/*.png $D/1/hdd -r
  echo "$S: $(grep -c '^tex' $D/1/cap/pack/index.txt 2>/dev/null) textures, traps $(grep -hc 'guest trap' $D/1/logs/*.log | paste -sd+ | bc)" >> runs/fxtex/summary.txt; }
mkdir -p runs/tttex runs/fxtex; rm -f runs/tttex/summary.txt runs/fxtex/summary.txt
set -- 00ISUL1 01WEAT1 02GATE2 04BALIN1 13AMON1 14FANG2 16ROHAN1 16ROHAN2 16ROHAN3 17TOP1 18HOLE1 20YARD1 21ORTH1 21ORTH2
while [ $# -gt 0 ]; do tt $1 & [ -n "$2" ] && tt $2 & wait; shift 2 2>/dev/null || shift; done
python3 texmerge.py runs/tttex ttpack runs/tttex_pool >> runs/tttex/summary.txt 2>&1 && rm -rf runs/tttex/*/ runs/tttex_pool
echo DONE >> runs/tttex/summary.txt
set -- $(cat fotr/scenes.txt)
while [ $# -gt 0 ]; do fx $1 & [ -n "$2" ] && fx $2 & wait; shift 2 2>/dev/null || shift; done
python3 texmerge.py runs/fxtex fxpack runs/fxtex_pool >> runs/fxtex/summary.txt 2>&1 && rm -rf runs/fxtex/*/ runs/fxtex_pool
echo DONE >> runs/fxtex/summary.txt
