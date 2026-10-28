#!/bin/bash
# one.sh LEVEL HERO [gandalf true|false]: a harness run with play as Gandalf, results in /home/claude/work/gt/<level>_<hero>
L=$1; P=$2; G=${3:-true}; W=/home/claude/work/gt; D=$W/${L}_$P${TAG}; rm -rf $D; mkdir -p $D/1
printf '[gameplay]\nplay_as = "%s"\ngandalf_unused = %s\n[graphics]\nskip_all_videos = true\n' $P $G > $D/1/settings.toml
TT_PLAY_AS=$P TT_PLAY_AS_ANY=1 TT_LEVEL=$L REF=${REF:-600} REF_FROM=${REF_FROM:-6000} bash /home/claude/tt-port/game/sdk/harness/run.sh -x /home/claude/work/h_gan/gan -g /mnt/user-data/uploads/TT--extracted -o $D -t ${T:-330} -s ${PAD:-/home/claude/work/heroes/pad2.txt} > $D.out 2>&1
touch $D.done
