#!/bin/bash
# sweep.sh SCENE [secs]: a new game started in SCENE (FOTR_SCENE), walked about; summary line into sweep/summary.txt
S=$1; T=${2:-60}; O=/home/claude/work/fotr/sweep/$S; rm -rf $O; mkdir -p $O
FPS=${FPS:-0} FOTR_SCENE=$S XBE_PUSH_LOG=1 REF=3000 bash /home/claude/fotr-port/fotrxbox/sdk/harness/run.sh -x /home/claude/work/h_fotr/fotr -g /mnt/user-data/uploads/FOTR-extracted -o $O -t $T -s /home/claude/work/fotr/pad_scene.txt > $O.out 2>&1
log=$(ls $O/1/logs/*.log | head -1)
crash=$(grep -c -i "segmentation\|aborted\|trap" $O/1/run.out)
fr=$(grep -o "renderer: frame [0-9]*, [0-9]* draws" $log | tail -1)
tex=$(grep -c "open D:\\\\scenes\\\\[A-Za-z_]*\\\\$S\\\\Media\\\\Textures.xpr" $log)
drop=$(grep -c "dropped\|not run" $log)
echo "$S: crash $crash, $fr, scene textures opened $tex, push notes $drop" >> /home/claude/work/fotr/sweep/summary.txt
