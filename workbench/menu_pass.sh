#!/bin/bash
# menu pass: random menu walks (front end and in-level pause menus) in all three games, plus The Two Towers' levels
# again with the memory scan on; merged into menu/<game>.csv sets, then added to rxpack, ttpack, fxpack
cd /home/claude/work
run() { # game name env seconds script
  G=$1; N=$2; E=$3; T=$4; S=$5; D=runs/menu_$G/$N; rm -rf $D; mkdir -p $D/1
  printf '[graphics]\nskip_all_videos = true\n' > $D/1/settings.toml
  case $G in
    rx) X=h_rx_fx/rx_fx_headless; R=/home/claude/rotkx-port/rotkxbox; GD=/mnt/user-data/uploads/ROTKX--extracted ;;
    tt) X=h_tt_fx/tt_fx_headless; R=/home/claude/tt-port/game; GD=/mnt/user-data/uploads/TT--extracted ;;
    fx) X=h_fotr/fotr; R=/home/claude/fotr-port/fotrxbox; GD=/mnt/user-data/uploads/FOTR-extracted ;;
  esac
  env $E TEXPACK=1 TEXPACK_SCAN_EVERY=300 FPS=0 bash $R/sdk/harness/run.sh -x $X -g $GD -o $D -t $T -s $S > $D/run.txt 2>&1
  rm -f $D/1/cap/*.tga; rm -rf $D/1/hdd
  P=$D/1/cap/pack; mkdir -p runs/menu_${G}_pool; for f in $P/*.png; do [ -e "$f" ] && mv -n "$f" runs/menu_${G}_pool/; done; rm -f $P/*.png
  echo "$G $N: $(grep -c '^tex' $P/index.txt 2>/dev/null) textures, traps $(grep -hc 'guest trap' $D/1/logs/*.log | paste -sd+ | bc)" >> runs/menu_summary.txt; }
pair() { "$@" & }
mkdir -p menu; rm -f runs/menu_summary.txt
for s in 1 2 3 4 5 6; do python3 menuwalk.py $s 14000 > menu/fe$s.txt; python3 menuwalk.py $((s+100)) 14000 pause > menu/pz$s.txt; done
jobs=()
for G in fx rx tt; do for s in 1 2 3 4 5 6; do jobs+=("$G fe$s NONE=1 90 menu/fe$s.txt"); done; done
set -- Village_of_Bree Gates_of_Moria Weathertop_newest House_of_elrond Bag_End Anduin_aragorn
k=1; for S in "$@"; do jobs+=("fx pz_$S FOTR_SCENE=$S 90 menu/pz$k.txt"); k=$((k+1)); done
k=1; for L in Pat01 Hel01 Min02 Pel02 Cra01 Pal02; do jobs+=("rx pz_$L ROTK_LEVEL=$L 90 menu/pz$k.txt"); k=$((k+1)); done
k=1; for L in 01WEAT1 04BALIN1 13AMON1 16ROHAN2 18HOLE1 21ORTH1; do jobs+=("tt pz_$L TT_LEVEL=$L 90 menu/pz$k.txt"); k=$((k+1)); done
for L in 00ISUL1 01WEAT1 02GATE2 04BALIN1 13AMON1 14FANG2 16ROHAN1 16ROHAN2 16ROHAN3 17TOP1 18HOLE1 20YARD1 21ORTH1 21ORTH2; do
  jobs+=("tt lv_$L TT_LEVEL=$L 100 /home/claude/tt-port/shared/harness-linux/pad_lv.txt"); done
i=0; while [ $i -lt ${#jobs[@]} ]; do run ${jobs[$i]} & [ $((i+1)) -lt ${#jobs[@]} ] && run ${jobs[$((i+1))]} & wait; i=$((i+2)); done
for G in fx rx tt; do python3 texmerge.py runs/menu_$G menu/$G runs/menu_${G}_pool >> runs/menu_summary.txt 2>&1; done
echo DONE >> runs/menu_summary.txt
