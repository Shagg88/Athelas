#!/bin/bash
# sweep.sh: TT as Gandalf/Boromir (TT_PLAY_AS_COMPANION) in 02GATE2 and 04BALIN1, as Aragorn and Isildur
W=/home/claude/work/heroes2; X=/home/claude/work/h_tt/tt_headless; G=/mnt/user-data/uploads/TT--extracted
H=/home/claude/tt-port/game/sdk/harness/run.sh
cd $W; mkdir -p runs
one() { L=$1; P=$2; C=$3; D=runs/${L}_${P}_$C; rm -rf $D; mkdir -p $D/1
  printf '[gameplay]\nplay_as = "%s"\nkill_points = true\n[graphics]\nskip_all_videos = true\n' $P > $D/1/settings.toml
  TT_PLAY_AS_COMPANION=$C TT_GANDALF_AMMO=1 TT_PLAY_AS_ANY=1 TT_LEVEL=$L REF=900 REF_FROM=6600 bash $H -x $X -g $G -o $D -t ${T:-420} -s $W/pad.txt > /dev/null 2>&1
  local log=$(ls $D/1/logs/*.log 2>/dev/null | head -1)
  local segv=$(grep -c -i "Segmentation\|Aborted" $D/1/run.out 2>/dev/null)
  local fr=$(grep -o "renderer: frame [0-9]*" $log | tail -1 | awk '{print $3}')
  local trap=$(grep -c -i "trap\|unhandled\|fatal\|assert" $log); local warn=$(grep -c "\[warn\]\|\[error\]" $log)
  local wore=$(grep -c -i "wears\|worn\|gandalf:\|boromir" $log)
  echo "$L $P $C: segv $segv, frames $fr, traps $trap, warn/err $warn, hero-lines $wore" >> summary.txt; }
: > summary.txt
for L in 02GATE2 04BALIN1; do
  one $L aragorn gandalf & one $L isildur boromir & wait
  one $L aragorn boromir & one $L isildur gandalf & wait
done
echo DONE >> summary.txt
