#!/bin/bash
# rx_sweep.sh : every ROTK Xbox level, 1.0.0 and merged builds side by side, 110 s each (work/sweep.sh's settings)
W=/home/claude/work; GAME=/mnt/user-data/uploads/ROTKX--extracted; PAD=/home/claude/rotkx-port/work/pad_sweep.txt
H=/home/claude/rotkx-port/rotkxbox/sdk/harness/run.sh
cd $W
run() { V=$1; X=$2; L=$3; D=runs/rxsweep_$V/$L; rm -rf $D; mkdir -p $D/1
  printf '[graphics]\nskip_all_videos = true\n' > $D/1/settings.toml
  ROTK_LEVEL=$L REF=1200 bash $H -x $X -g $GAME -o $D -t 110 -s $PAD > $D/run.txt 2>&1; }
for L in Pat03 Min02 Pel02 Bla01 Ise01 Min00 Min04 Osg01 She01 Cir00 Cra01 Cir01 Pat02 Pal01 Pal02 Hel01 Pat01; do
  run orig h_rotkx_orig/rotkx_headless $L & run merged h_rotkx/rotkx_headless $L & wait
done
echo SWEEP DONE
