#!/bin/bash
# sweep_all.sh: the bug sweep after the #14/#15 changes: test builds rebuilt, then every level of the three ports
cd /home/claude/work
T=/home/claude/work/tools/b/_deps/tomlplusplus-src/include


echo "builds done $(date +%T)"
# Fellowship: 64 scenes, logos skipped (the scripted presses no longer depend on when the last logo starts)
fx() { S=$1; O=fotr/sweep/$S; rm -rf $O; mkdir -p $O/1; printf '[graphics]\nskip_logos = true\n' > $O/1/settings.toml
  FPS=0 FOTR_SCENE=$S XBE_PUSH_LOG=1 REF=3000 bash /home/claude/fotr-port/fotrxbox/sdk/harness/run.sh -x h_fotr_x87/fotr -g /mnt/user-data/uploads/FOTR-extracted -o $O -t 60 -s /home/claude/work/fotr/pad_scene.txt > $O.out 2>&1
  log=$(ls $O/1/logs/*.log | head -1)
  crash=$(grep -c -i "segmentation\|aborted\|trap" $O/1/run.out)
  fr=$(grep -o "renderer: frame [0-9]*, [0-9]* draws" $log | tail -1)
  tex=$(grep -c "open D:\\\\scenes\\\\[A-Za-z_]*\\\\$S\\\\Media\\\\Textures.xpr" $log)
  err=$(grep -c "\[error\]" $log)
  echo "$S: crash $crash, errors $err, $fr, scene textures opened $tex" >> fotr/sweep/summary.txt; rm -rf $O/1/hdd; ls $O/1/cap/*.tga 2>/dev/null | sort -V | head -n -2 | xargs -r rm -f; }
rm -f fotr/sweep/summary.txt
set -- $(cat fotr/scenes.txt)
while [ $# -gt 0 ]; do fx $1 & [ -n "$2" ] && fx $2 & wait; shift 2 2>/dev/null || shift; done
echo "fotr done $(date +%T)"
