#!/bin/bash
cd /home/claude/work/seed
./record.sh tt obj_TwoTowers TwoTowers TT_LEVEL "16ROHAN1 00ISUL1 01WEAT1 02GATE2 04BALIN1 13AMON1 14FANG2 16ROHAN2 16ROHAN3 17TOP1 18HOLE1 20YARD1 21ORTH1 21ORTH2" 140
./record.sh rx obj_ROTKXbox ROTKXbox ROTK_LEVEL "Pel02 Pat03 Min02 Bla01 Ise01 Min00 Min04 Osg01 She01 Cir00 Cra01 Cir01 Pat02 Pal01 Pal02 Hel01 Pat01" 140
while [ ! -f /home/claude/work/mingw/obj_FOTRXbox/FOTRXbox.exe ] || pgrep -f build_port_win.sh > /dev/null; do sleep 30; done
./record.sh fx obj_FOTRXbox FOTRXbox FOTR_SCENE "Bag_End $(cat /home/claude/work/fotr/scenes.txt | tr ' ' '\n' | grep -v '^Bag_End$' | tr '\n' ' ')" 110
echo ALL RECORDED >> record.log
