#!/usr/bin/env python3
"""dis.py <hexStart> <hexEnd>: linear disassembly of a VA range with string annotations."""
import sys; sys.path.insert(0,'/home/claude/rotkx/work/hidden'); from xref import x,d,md
strs={}
for l in open('/home/claude/rotkx/work/hidden/xbe_strings.tsv'):
    va,k,sec,s=l.rstrip('\n').split('\t',3); strs[int(va,16)]=s
md.skipdata=True
a=int(sys.argv[1],16); b=int(sys.argv[2],16); o=x.va2off(a)
import re
for ins in md.disasm(d[o:o+(b-a)],a):
    n=[strs[int(h,16)] for h in re.findall(r'0x[0-9a-f]+',ins.op_str) if int(h,16) in strs]
    print(f'  {ins.address:08x}  {ins.mnemonic:6s} {ins.op_str}'+(f'   ; "{n[0][:70]}"' if n else ''))
