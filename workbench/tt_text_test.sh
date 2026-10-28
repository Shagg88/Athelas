#!/bin/bash
# tt_text_test.sh <tag> <true|false>: TT's Windows test build under Wine, windowed 1280x960, the front end; shots at 40, 60, 80 s
N=$1; ON=$2; D=/tmp/claude-0/tttext_$N; rm -rf $D; mkdir -p $D; cd $D
cp /home/claude/work/mingw/obj_TwoTowers/TwoTowers.exe .; cp /tmp/claude-0/sdl/SDL3-3.4.18/x86_64-w64-mingw32/bin/SDL3.dll .
printf '[display]\nmode = "windowed"\nwidth = 1280\nheight = 960\n[graphics]\nsharp_text = %s\nskip_all_videos = true\nskip_logos = true\n' $ON > settings.toml
printf '25 START\n32 START\n38 A\n80 START\n' > pad.txt
export WINEDEBUG=-all WINEPREFIX=/tmp/claude-0/wpfx
cat > go.sh <<E
#!/bin/bash
TEST_PAD=\$PWD/pad.txt wine TwoTowers.exe --game 'Z:\\mnt\\user-data\\uploads\\TT--extracted' --no-dialogs > run.out 2>&1 &
P=\$!; sleep 88; import -window root s1.png; sleep 8; import -window root s2.png; kill \$P; wineserver -k; sleep 2
E
chmod +x go.sh; timeout 260 xvfb-run -a -s "-screen 0 1400x1050x24" ./go.sh
L=$(ls logs/*.log | head -1); echo "== $N"; grep -E "sharper text|error" $L | cut -c1-160 | head
