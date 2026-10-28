#!/bin/bash
# sweep.sh: every ROTK Xbox level as each hero (ROTK_PLAY_AS), headless, 110 s each, two at a time
W=/home/claude/work/rheroes; X=/home/claude/work/h_rotkx/rotkx_headless; G=/mnt/user-data/uploads/ROTKX--extracted
H=/home/claude/rotkx-port/rotkxbox/sdk/harness/run.sh; PAD=/home/claude/rotkx-port/work/pad_sweep.txt
cd $W; mkdir -p runs
one() { L=$1; C=$2; D=runs/${L}_$C; rm -rf $D; mkdir -p $D/1
  printf '[graphics]\nskip_all_videos = true\n' > $D/1/settings.toml
  ROTK_LEVEL=$L ROTK_PLAY_AS=$C REF=1500 bash $H -x $X -g $G -o $D -t 110 -s $PAD > /dev/null 2>&1
  local log=$(ls $D/1/logs/*.log 2>/dev/null | head -1)
  local fr=$(grep -o "renderer: frame [0-9]*" $log | tail -1 | awk '{print $3}')
  local pa=$(grep -c "played as" $log); local err=$(grep -c -i "\[error\]\|unhandled\|trap" $log)
  echo "$L $C: frames $fr, playas-lines $pa, errors $err" >> summary.txt; }
[ -n "$KEEP" ] && cp "$KEEP" summary.txt || : > summary.txt
for L in ${LEVELS:-Hel01 Pat01 Pat03 Min02 Pel02 Bla01 Ise01 Min00 Min04 Osg01 She01 Cir00 Cra01 Fro03 Cir01 Pat02 Pal01 Pal02}; do
  for C in 0 1 2 3 4 5 6 7 8; do one $L $C & [ $((C % 2)) = 1 ] && wait; done; wait
done
echo DONE >> summary.txt
