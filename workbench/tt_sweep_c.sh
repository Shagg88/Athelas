#!/bin/bash
# tt_sweep.sh <harness exe> <tag> : the TT level sweep (lvsweep.sh's settings), 13 levels (no Fangorn: its bank can't be copied)
X=$1; TAG=$2; W=/home/claude/work; GAME=/mnt/user-data/uploads/TT--extracted; PAD=/home/claude/tt-port/shared/harness-linux/pad_lv.txt
cd $W
run() { L=$1; D=runs/$TAG/lv_$L; rm -rf $D; mkdir -p $D/1
  printf '[graphics]\nskip_all_videos = true\nwidescreen = true\nfps = "60"\nsmooth_2d = true\nbutton_pictures = "playstation"\nalpha_edges = true\n[gameplay]\ndamage_taken = 150\nunlock_all = true\n' > $D/1/settings.toml
  TT_ASPECT=2.3703 ASPECT=2.3703 TT_LEVEL=$L REF=1100 bash /home/claude/tt-port/game/sdk/harness/run.sh -x $X -g $GAME -o $D -t 125 -s $PAD > $D.txt 2>&1; ls $D/1/cap/*.tga 2>/dev/null | sort -V | head -n -2 | xargs -r rm -f; rm -rf $D/1/hdd; }
set -- 00ISUL1 01WEAT1 02GATE2 04BALIN1 13AMON1 16ROHAN2 16ROHAN1 16ROHAN3 17TOP1 18HOLE1 20YARD1 21ORTH1 21ORTH2
while [ $# -gt 0 ]; do a=$1; shift; b=$1; [ $# -gt 0 ] && shift; run $a & [ -n "$b" ] && run $b & wait; done
echo SWEEP DONE
