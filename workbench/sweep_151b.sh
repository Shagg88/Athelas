#!/bin/bash
# sweep_151.sh: 1.5.1's sweep (settings uniform, button pictures, unlock all): every level of the three ports
cd /home/claude/work
T=/home/claude/work/tools/b/_deps/tomlplusplus-src/include


# Fellowship: 64 scenes, logos skipped (the scripted presses no longer depend on when the last logo starts)
fx() { S=$1; O=fotr/sweep/$S; rm -rf $O; mkdir -p $O/1; printf '[graphics]\nskip_logos = true\nbutton_pictures = "playstation"\n' > $O/1/settings.toml
  FPS=0 FOTR_SCENE=$S XBE_PUSH_LOG=1 REF=3000 bash /home/claude/fotr-port/fotrxbox/sdk/harness/run.sh -x h_fotr_w/fotr -g /mnt/user-data/uploads/FOTR-extracted -o $O -t 60 -s /home/claude/work/fotr/pad_scene.txt > $O.out 2>&1
  log=$(ls $O/1/logs/*.log | head -1)
  crash=$(grep -c -i "segmentation\|aborted\|trap" $O/1/run.out)
  fr=$(grep -o "renderer: frame [0-9]*, [0-9]* draws" $log | tail -1)
  tex=$(grep -c "open D:\\\\scenes\\\\[A-Za-z_]*\\\\$S\\\\Media\\\\Textures.xpr" $log)
  err=$(grep -c "\[error\]" $log)
  echo "$S: crash $crash, errors $err, $fr, scene textures opened $tex" >> fotr/sweep/summary.txt; rm -rf $O/1/hdd; ls $O/1/cap/*.tga 2>/dev/null | sort -V | head -n -2 | xargs -r rm -f; }

set -- $(cat /tmp/claude-0/frest.txt)
while [ $# -gt 0 ]; do fx $1 & [ -n "$2" ] && fx $2 & wait; shift 2 2>/dev/null || shift; done
echo "fotr done $(date +%T)"
# ROTK Xbox: 17 levels
rx() { L=$1; D=runs/sw151_rx/$L; rm -rf $D; mkdir -p $D/1; printf '[graphics]\nskip_all_videos = true\nskip_logos = true\n' > $D/1/settings.toml
  ROTK_LEVEL=$L REF=1200 bash /home/claude/rotkx-port/rotkxbox/sdk/harness/run.sh -x h_rx_w/rx_w -g /mnt/user-data/uploads/ROTKX--extracted -o $D -t 110 -s /home/claude/rotkx-port/work/pad_sweep.txt > $D/run.txt 2>&1; rm -rf $D/1/hdd; ls $D/1/cap/*.tga 2>/dev/null | sort -V | head -n -2 | xargs -r rm -f; }
set -- Pat03 Min02 Pel02 Bla01 Ise01 Min00 Min04 Osg01 She01 Cir00 Cra01 Cir01 Pat02 Pal01 Pal02 Hel01 Pat01
while [ $# -gt 0 ]; do rx $1 & [ -n "$2" ] && rx $2 & wait; shift 2 2>/dev/null || shift; done
echo "rotk done $(date +%T)"
sed 's|> $D.txt 2>\&1; }|> $D.txt 2>\&1; ls $D/1/cap/*.tga 2>/dev/null \| sort -V \| head -n -2 \| xargs -r rm -f; rm -rf $D/1/hdd; }|' tt_sweep_151.sh > tt_sweep_c.sh; bash tt_sweep_c.sh h_tt_v/tt_v sw151_tt
echo "ALL DONE $(date +%T)"
