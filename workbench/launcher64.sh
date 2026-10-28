#!/bin/sh
# launcher64.sh: a 64-bit build of the launcher, for testing under this machine's Wine (it has no 32-bit Wine)
cd /home/claude/rotk/launcher
sed -e 's/i686-w64-mingw32/x86_64-w64-mingw32/g' -e 's#build/obj#build64/obj#g' -e 's#build/Athelas.exe#build64/Athelas.exe#' build.sh | sed "s|^cd .*dirname.*|cd /home/claude/rotk/launcher|" > /tmp/claude-0/b64.sh
sh /tmp/claude-0/b64.sh
