#!/usr/bin/env python3
"""pcdis.py <hexStart> <hexEnd>: disassemble ROTK PC rotk.exe range."""
import sys; sys.path.insert(0,'/home/claude/rotkx/work/hidden')
from pcx import d,va2off,md
md.skipdata=True
a=int(sys.argv[1],16); b=int(sys.argv[2],16); o=va2off(a)
for ins in md.disasm(d[o:o+b-a],a): print(f'  {ins.address:08x}  {ins.mnemonic:6s} {ins.op_str}')
