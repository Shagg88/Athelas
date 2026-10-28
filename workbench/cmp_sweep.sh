#!/bin/bash
# cmp_sweep.sh : ROTK and TT levels, the shared-sdk build and the FOTR-SDK test build side by side
W=/home/claude/work; cd $W
RXG=/mnt/user-data/uploads/ROTKX--extracted; RXP=/home/claude/rotkx-port/work/pad_sweep.txt; RXH=/home/claude/rotkx-port/rotkxbox/sdk/harness/run.sh
TTG=/mnt/user-data/uploads/TT--extracted; TTP=/home/claude/tt-port/shared/harness-linux/pad_lv.txt; TTH=/home/claude/tt-port/game/sdk/harness/run.sh
rx() { V=$1; X=$2; L=$3; D=runs/cmp_rx_$V/$L; rm -rf $D; mkdir -p $D/1
  printf '[graphics]\nskip_all_videos = true\n' > $D/1/settings.toml
  FPS=0 ROTK_LEVEL=$L REF=6000 bash $RXH -x $X -g $RXG -o $D -t 110 -s $RXP > $D/run.txt 2>&1; }
tt() { V=$1; X=$2; L=$3; D=runs/cmp_tt_$V/$L; rm -rf $D; mkdir -p $D/1
  printf '[graphics]\nskip_all_videos = true\nwidescreen = true\nfps = "60"\nsmooth_2d = true\n[gameplay]\ndamage_taken = 150\n' > $D/1/settings.toml
  FPS=0 TT_ASPECT=2.3703 ASPECT=2.3703 TT_LEVEL=$L REF=6000 bash $TTH -x $X -g $TTG -o $D -t 125 -s $TTP > $D/run.txt 2>&1; }
for L in Pat03 Min02 Pel02 Bla01 Ise01 Min00 Min04 Osg01 She01 Cir00 Cra01 Cir01 Pat02 Pal01 Pal02 Hel01 Pat01; do
  rx base h_rx_base/rotkx_headless $L & rx fx h_rx_fx/rotkx_headless $L & wait
done
for L in 00ISUL1 01WEAT1 02GATE2 04BALIN1 13AMON1 16ROHAN2 16ROHAN1 16ROHAN3 17TOP1 18HOLE1 20YARD1 21ORTH1 21ORTH2; do
  tt base h_tt_base/tt_headless $L & tt fx h_tt_fx/tt_headless $L & wait
done
echo SWEEP DONE > runs/cmp_done
