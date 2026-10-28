#!/bin/bash
# sweep_160q.sh: quick 1.6.0 sweep, a spread of levels per port (harness builds h_f160/h_r160/h_t160)
cd /home/claude/work; R=runs/sw160q; mkdir -p $R
fx() { S=$1; O=$R/fx_$S; rm -rf $O; mkdir -p $O/1; printf '[graphics]\nskip_logos = true\nbutton_pictures = "playstation"\n' > $O/1/settings.toml
  FPS=0 FOTR_SCENE=$S XBE_PUSH_LOG=1 REF=3000 bash /home/claude/fotr-port/fotrxbox/sdk/harness/run.sh -x h_f160/f160_headless -g /mnt/user-data/uploads/FOTR-extracted -o $O -t 60 -s /home/claude/work/fotr/pad_scene.txt > $O.out 2>&1
  rm -rf $O/1/hdd; ls $O/1/cap/*.tga 2>/dev/null | sort -V | head -n -1 | xargs -r rm -f; }
rx() { L=$1; D=$R/rx_$L; rm -rf $D; mkdir -p $D/1; printf '[graphics]\nskip_all_videos = true\nskip_logos = true\n' > $D/1/settings.toml
  ROTK_LEVEL=$L REF=1200 bash /home/claude/rotkx-port/rotkxbox/sdk/harness/run.sh -x h_r160/r160_headless -g /mnt/user-data/uploads/ROTKX--extracted -o $D -t 110 -s /home/claude/rotkx-port/work/pad_sweep.txt > $D.out 2>&1
  rm -rf $D/1/hdd; ls $D/1/cap/*.tga 2>/dev/null | sort -V | head -n -1 | xargs -r rm -f; }
tt() { L=$1; D=$R/tt_$L; rm -rf $D; mkdir -p $D/1
  printf '[graphics]\nskip_all_videos = true\nwidescreen = true\nfps = "60"\nsmooth_2d = true\nbutton_pictures = "playstation"\nalpha_edges = true\n[gameplay]\ndamage_taken = 150\nunlock_all = true\n' > $D/1/settings.toml
  TT_ASPECT=2.3703 ASPECT=2.3703 TT_LEVEL=$L REF=1100 bash /home/claude/tt-port/game/sdk/harness/run.sh -x h_t160/t160_headless -g /mnt/user-data/uploads/TT--extracted -o $D -t 125 -s /home/claude/tt-port/shared/harness-linux/pad_lv.txt > $D.out 2>&1
  rm -rf $D/1/hdd; ls $D/1/cap/*.tga 2>/dev/null | sort -V | head -n -1 | xargs -r rm -f; }
pair() { F=$1; shift; while [ $# -gt 0 ]; do a=$1; shift; b=$1; [ $# -gt 0 ] && shift; $F $a & [ -n "$b" ] && $F $b & wait; done; }
pair fx Bag_End Village_of_Bree Weathertop_newest House_of_elrond Balins_Tomb Hall_of_statues Anduin_1 CombatRange
pair rx Pat03 Min02 Hel01 Pal01 Osg01 Cir01
pair tt 00ISUL1 02GATE2 04BALIN1 16ROHAN1 18HOLE1 21ORTH1
echo QUICK SWEEP DONE
