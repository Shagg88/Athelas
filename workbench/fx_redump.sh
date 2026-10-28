#!/bin/bash
cd /home/claude/work
fx() { S=$1; D=runs/fxtex/$S; rm -rf $D; mkdir -p $D/1
  TEXPACK=1 TEXPACK_SCAN=7000 FPS=0 FOTR_SCENE=$S bash /home/claude/fotr-port/fotrxbox/sdk/harness/run.sh -x h_fotr/fotr -g /mnt/user-data/uploads/FOTR-extracted -o $D -t 50 -s /home/claude/work/fotr/pad_scene.txt > $D/run.txt 2>&1
  rm -f $D/1/cap/*.tga
  P=$D/1/cap/pack; G=$(dirname $(dirname $D))/$(basename $(dirname $D))_pool; mkdir -p $G; for f in $P/*.png; do [ -e "$f" ] && mv -n "$f" $G/ 2>/dev/null; done; rm -f $P/*.png $D/1/hdd -r
  echo "$S: $(grep -c '^tex' $D/1/cap/pack/index.txt 2>/dev/null) textures, traps $(grep -hc 'guest trap' $D/1/logs/*.log | paste -sd+ | bc)" >> runs/fxtex/summary.txt; }
mkdir -p runs/fxtex; rm -f runs/fxtex/summary.txt
set -- $(cat fotr/scenes.txt)
while [ $# -gt 0 ]; do fx $1 & [ -n "$2" ] && fx $2 & wait; shift 2 2>/dev/null || shift; done
rm -rf fxpack_lv; python3 texmerge.py runs/fxtex fxpack_lv runs/fxtex_pool >> runs/fxtex/summary.txt 2>&1
echo DONE >> runs/fxtex/summary.txt
