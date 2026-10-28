#!/bin/bash
# sweep.sh: every Two Towers level as each hero (play_as), headless, 150 s each, two at a time.
# Results in runs/<level>_<hero>/ and summary.txt: crash, frames reached, kills (kill points lines), the level's file
W=/home/claude/work/heroes; X=/home/claude/work/h_tt2/tt2_headless; G=/mnt/user-data/uploads/TT--extracted
H=/home/claude/tt-port/game/sdk/harness/run.sh
cd $W; mkdir -p runs
cp $W/pad2.txt pad.txt
one() { L=$1; P=$2; D=runs/${L}_$P; rm -rf $D; mkdir -p $D/1
  printf '[gameplay]\nplay_as = "%s"\nkill_points = true\n[graphics]\nskip_all_videos = true\n' $P > $D/1/settings.toml
  TT_PLAY_AS_ANY=1 TT_LEVEL=$L REF=1500 REF_FROM=6000 bash $H -x $X -g $G -o $D -t 330 -s $W/pad.txt > /dev/null 2>&1
  local log=$(ls $D/1/logs/*.log 2>/dev/null | head -1)
  local segv=$(grep -c Segmentation $D/1/run.out 2>/dev/null)
  local fr=$(grep -o "renderer: frame [0-9]*" $log | tail -1 | awk '{print $3}')
  local kills=$(grep -c "kill points" $log); local f=$(grep -o "worlds\\\\[0-9A-Za-z]*\.bf." $log | tail -1)
  local trap=$(grep -c -i "trap\|unhandled\|fatal" $log)
  echo "$L $P: segv $segv, frames $fr, killpoint-lines $kills, traps $trap, file $f" >> summary.txt; }
: > summary.txt
for L in ${LEVELS:-00ISUL1 01WEAT1 02GATE2 04BALIN1 13AMON1 14FANG2 16ROHAN1 16ROHAN2 16ROHAN3 17TOP1 18HOLE1 20YARD1 21ORTH1 21ORTH2}; do
  one $L aragorn & one $L legolas & wait
  one $L gimli & one $L isildur & wait
done
echo DONE >> summary.txt
