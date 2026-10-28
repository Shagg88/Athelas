#!/bin/bash
# asm.sh START END : disassembly comments from the TT recompiled code
cd /home/claude/rotkx-port/rotkxbox/generated/game
grep -h "^\s*// 000[0-9A-F]\{5\} " x86_recomp.*.cpp | python3 -c "
import sys
a0=int('$1',16);a1=int('$2',16);seen={}
for l in sys.stdin:
    p=l.split(None,2); a=int(p[1],16)
    if a0<=a<=a1 and a not in seen: seen[a]=p[2].rstrip()
for a in sorted(seen): print(hex(a),seen[a])
"
