#!/bin/bash
cd /home/claude/work/fotr
while read s; do bash sweep.sh $s 45 & bash sweepw.sh $s 45 & wait; done < scenes.txt
echo DONE >> sweep/summary.txt
