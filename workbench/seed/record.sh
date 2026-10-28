#!/bin/bash
# record.sh GAME EXE_DIR_NAME EXE LEVELVAR "levels..." [seconds]: each level under Wine, shaders recorded into seed/GAME.shaders,
# pipelines merged into seed/GAME.pipelines
G=$1; EXEN=$3; VAR=$4; LEVELS=$5; T=${6:-130}
S=/home/claude/work/seed; W=/tmp/claude-0/seedrun/$G; mkdir -p $W
cp /home/claude/work/mingw/$2/$EXEN.exe /tmp/claude-0/sdl/SDL3-3.4.18/x86_64-w64-mingw32/bin/SDL3.dll $W/
case $G in tt) GAME='Z:\mnt\user-data\uploads\TT--extracted';; rx) GAME='Z:\mnt\user-data\uploads\ROTKX--extracted';; fx) GAME='Z:\mnt\user-data\uploads\FOTR-extracted';; esac
python3 $S/mkpad.py $G $T > $W/pad.txt
export WINEDEBUG=-all WINEPREFIX=/tmp/claude-0/wpfx
[ -d $WINEPREFIX ] || timeout 200 xvfb-run -a wineboot -i > /dev/null 2>&1
first=1
for L in $LEVELS; do
  cd $W; rm -rf logs hdd; rm -f shadercache/pipelines.bin
  if [ $first = 1 ]; then printf '' > settings.toml; else case $G in fx) printf '[graphics]\nskip_logos = true\n' > settings.toml;; *) printf '[graphics]\nskip_all_videos = true\n' > settings.toml;; esac; fi
  cat > go.sh <<EOF
#!/bin/bash
XBE_SHADER_RECORD='Z:\\home\\claude\\work\\seed\\$G.shaders' TEST_PAD=pad.txt $VAR=$L wine $EXEN.exe --game '$GAME' --no-dialogs > run.out 2>&1 &
P=\$!; sleep $T; import -window root shot_$L.png; kill \$P; wineserver -k; sleep 2
EOF
  chmod +x go.sh; timeout $((T+60)) xvfb-run -a -s "-screen 0 1280x720x24" ./go.sh
  python3 $S/merge.py $S/$G.pipelines $W/shadercache/pipelines.bin
  L2=$(ls logs/*.log 2>/dev/null | head -1)
  n=$(python3 $S/count.py $S/$G.shaders 2>/dev/null)
  echo "$G $L: errors $(grep -c '\[error\]' $L2 2>/dev/null) frames $(grep -o 'frame [0-9]*' $L2 | tail -1) seed shaders $n pipelines $(python3 $S/merge.py $S/$G.pipelines)" >> $S/record.log
  first=0
done
