#!/bin/bash
# every ROTK Xbox level's textures under their texture pack names: runs/rxtex/<level>/1/cap/pack
cd /home/claude/work
one() { L=$1; D=runs/rxtex/$L; rm -rf $D; mkdir -p $D/1
  printf '[graphics]\nskip_all_videos = true\n' > $D/1/settings.toml
  TEXPACK=1 TEXPACK_SCAN=1500 FPS=0 ROTK_LEVEL=$L bash /home/claude/rotkx-port/rotkxbox/sdk/harness/run.sh -x h_rx_fx/rx_fx_headless -g /mnt/user-data/uploads/ROTKX--extracted -o $D -t 75 -s /home/claude/rotkx-port/work/pad_sweep.txt > $D/run.txt 2>&1
  rm -f $D/1/cap/*.tga
  echo "$L: $(grep -c '^tex' $D/1/cap/pack/index.txt 2>/dev/null) textures, $(grep -c '^tile' $D/1/cap/pack/index.txt 2>/dev/null) tiling, traps $(grep -hc 'guest trap' $D/1/logs/*.log | paste -sd+ | bc)" >> runs/rxtex/summary.txt; }
mkdir -p runs/rxtex; rm -f runs/rxtex/summary.txt
set -- Pat01 Pat02 Pat03 Hel01 Ise01 Osg01 Min00 Min02 Min04 Pel02 Bla01 She01 Cir00 Cir01 Cra01 Pal01 Pal02
while [ $# -gt 0 ]; do one $1 & [ -n "$2" ] && one $2 & wait; shift 2 2>/dev/null || shift; done
echo DONE >> runs/rxtex/summary.txt
