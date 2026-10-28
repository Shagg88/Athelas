#!/bin/bash
# TT and ROTK levels, the current build (base) and the x87-cache build (new) side by side
cd /home/claude/work; mkdir -p runs; 
rx() { V=$1; X=$2; L=$3; D=runs/x87_rx_$V/$L; rm -rf $D; mkdir -p $D/1; printf '[graphics]\nskip_all_videos = true\n' > $D/1/settings.toml
  ROTK_LEVEL=$L REF=1200 bash /home/claude/rotkx-port/rotkxbox/sdk/harness/run.sh -x $X -g /mnt/user-data/uploads/ROTKX--extracted -o $D -t 110 -s /home/claude/rotkx-port/work/pad_sweep.txt > $D/run.txt 2>&1; rm -rf $D/1/hdd; ls $D/1/cap/*.tga 2>/dev/null | sort -V | head -n -1 | xargs -r rm -f
  log=$(ls $D/1/logs/*.log 2>/dev/null | head -1); echo "rotk $L $V: crash $(grep -c -i 'segmentation\|aborted\|trap' $D/1/run.out) errors $(grep -c '\[error\]' $log) $(grep -o 'frame [0-9]*' $log | tail -1)" >> runs/x87_summary.txt; }
tt() { V=$1; X=$2; L=$3; D=runs/x87_tt_$V/lv_$L; rm -rf $D; mkdir -p $D/1
  printf '[graphics]\nskip_all_videos = true\nwidescreen = true\nfps = "60"\nsmooth_2d = true\n[gameplay]\ndamage_taken = 150\n' > $D/1/settings.toml
  TT_ASPECT=2.3703 ASPECT=2.3703 TT_LEVEL=$L REF=1100 bash /home/claude/tt-port/game/sdk/harness/run.sh -x $X -g /mnt/user-data/uploads/TT--extracted -o $D -t 125 -s /home/claude/tt-port/shared/harness-linux/pad_lv.txt > $D.txt 2>&1; rm -rf $D/1/hdd; ls $D/1/cap/*.tga 2>/dev/null | sort -V | head -n -1 | xargs -r rm -f
  log=$(ls $D/1/logs/*.log 2>/dev/null | head -1); echo "tt $L $V: crash $(grep -c -i 'segmentation\|aborted\|trap' $D/1/run.out) errors $(grep -c '\[error\]' $log) $(grep -o 'frame [0-9]*' $log | tail -1)" >> runs/x87_summary.txt; }


for L in Bla01 Ise01 Min00 Min04 Osg01 She01 Cir00 Cra01 Cir01 Pat02 Pal01 Pal02 Hel01 Pat01; do rx base h_rx_fx/rx_fx_headless $L & rx new h_rx_x87/rx $L & wait; done
echo "ALL DONE $(date +%T)"
