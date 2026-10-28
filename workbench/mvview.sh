#!/bin/bash
# mvview.sh <obj name> <exe> <game path> <env...>: the test build under Wine, upscaler FSR (fails here, so it shows
# the picture left / motion right with XBE_UP_SHOW_MOTION), screenshots every 50 s into /tmp/claude-0/upt_<name>
N=$1; EXE=$2; GAME=$3; shift 3; ENVS="$*"
D=/tmp/claude-0/upt_$N; rm -rf $D; mkdir -p $D; cd $D
cp /home/claude/work/mingw/obj_$N/$EXE .; cp /tmp/claude-0/sdl/SDL3-3.4.18/x86_64-w64-mingw32/bin/SDL3.dll .
printf '[display]\nmode = "windowed"\nwidth = 640\nheight = 480\n[graphics]\nskip_all_videos = true\nskip_logos = true\nupscaler = "1"\nupscaler_quality = "3"\n' > settings.toml
printf "%b" "$PAD" > pad.txt
export WINEDEBUG=-all WINEPREFIX=/tmp/claude-0/wpfx
cat > go.sh <<E
#!/bin/bash
env $ENVS XBE_UP_SHOW_MOTION=1 TEST_PAD=\$PWD/pad.txt wine $EXE --game '$GAME' --no-dialogs > run.out 2>&1 &
P=\$!; for i in 1 2 3 4 5 6; do sleep 50; import -window root shot\$i.png; done; kill \$P; wineserver -k; sleep 2
E
chmod +x go.sh; timeout 400 xvfb-run -a -s "-screen 0 1024x768x24" ./go.sh
L=$(ls logs/*.log | head -1); echo "== $N"; grep -i "upscaler:" $L | tail -3 | cut -c1-200; grep "page fault" run.out | head -1
python3 -c "
from PIL import Image
ims=[Image.open(f'shot{i}.png').crop((0,0,660,520)) for i in (2,4,6)]
W=Image.new('RGB',(1980,520)); [W.paste(im,(i*660,0)) for i,im in enumerate(ims)]; W.save('row.png')"
