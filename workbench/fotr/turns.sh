#!/bin/bash
# turns.sh <tag> <seconds> scenes...: each scene at 144 fps, two at a time
cd /home/claude/work; TAG=$1; T=$2; shift 2
t() { S=$1; O=fotr/turns/$TAG/$S; rm -rf $O; mkdir -p $O/1; printf '[graphics]\nskip_logos = true\nfps = "144"\n' > $O/1/settings.toml
  FPS=144 FOTR_SCENE=$S bash /home/claude/fotr-port/fotrxbox/sdk/harness/run.sh -x h_fotr/fotr_headless -g /mnt/user-data/uploads/FOTR-extracted -o $O -t $T -s /home/claude/work/fotr/pad_scene.txt > $O.out 2>&1
  log=$(ls $O/1/logs/*.log | head -1)
  echo "$S: crash $(grep -c -i 'segmentation\|aborted\|trap' $O/1/run.out), errors $(grep -c '\[error\]' $log), $(grep -o 'renderer: frame [0-9]*' $log | tail -1), late ticks $(grep -o 'ran without its turn.*' $log | tail -1), turns $(grep -c 'takes turns' $log), hitches $(grep -c 'hitch: frame' $log)" >> fotr/turns/$TAG.txt
  rm -rf $O/1/hdd $O/1/cap; }
mkdir -p fotr/turns; rm -f fotr/turns/$TAG.txt
while [ $# -gt 0 ]; do t $1 & [ -n "$2" ] && t $2 & wait; shift 2 2>/dev/null || shift; done
