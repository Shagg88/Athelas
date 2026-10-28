#!/bin/bash
# menu_hz.sh <tag>: ROTK Xbox's Windows test build under Wine at the main menu for 70 s, sharp text on: how often the
# sharp menu picture is made again
N=$1; D=/tmp/claude-0/menuhz_$N; rm -rf $D; mkdir -p $D; cd $D
cp /home/claude/work/mingw/obj_ROTKXbox/ROTKXbox.exe .; cp /tmp/claude-0/sdl/SDL3-3.4.18/x86_64-w64-mingw32/bin/SDL3.dll .
mkdir -p saves; cp -r /mnt/user-data/uploads/Athelas/ROTK-Xbox/saves/E saves/
printf '[display]\nmode = "windowed"\nwidth = 640\nheight = 480\n[graphics]\nsharp_text = true\nskip_all_videos = true\nskip_logos = true\n' > settings.toml
export WINEDEBUG=-all WINEPREFIX=/tmp/claude-0/wpfx
cat > go.sh <<E
#!/bin/bash
wine ROTKXbox.exe --game 'Z:\\mnt\\user-data\\uploads\\ROTKX--extracted' --ini 'Z:\\tmp\\claude-0\\rotkpc\\ROTKFix.ini' --no-dialogs > run.out 2>&1 &
P=\$!; sleep 150; import -window root shot.png; kill \$P; wineserver -k; sleep 2
E
chmod +x go.sh; timeout 300 xvfb-run -a -s "-screen 0 1024x768x24" ./go.sh
L=$(ls logs/*.log | head -1); echo "== $N: $(grep -c 'picture drawn from ROTK PC' $L) pictures made, $(grep -c '^.\{12\}hitch: frame' $L) hitches"; grep -E "sharp menus|error" $L | head -5 | cut -c1-160
