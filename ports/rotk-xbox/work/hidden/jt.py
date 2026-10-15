#!/usr/bin/env python3
"""jt.py <tableVA> <count> [maxins]: for each jump-table case, list globals/strings/calls touched until ret."""
import sys,struct,re; sys.path.insert(0,'/home/claude/rotkx/work/hidden'); from xref import x,d,md
strs={}
for l in open('/home/claude/rotkx/work/hidden/xbe_strings.tsv'):
    va,k,sec,s=l.rstrip('\n').split('\t',3); strs[int(va,16)]=s
tb=int(sys.argv[1],16); n=int(sys.argv[2]); mx=int(sys.argv[3]) if len(sys.argv)>3 else 40
for i in range(n):
    t=struct.unpack_from('<I',d,x.va2off(tb+4*i))[0]
    o=x.va2off(t); items=[]
    for k,ins in enumerate(md.disasm(d[o:o+mx*15],t)):
        if k>=mx: break
        s=f'{ins.mnemonic} {ins.op_str}'
        for h in re.findall(r'0x[0-9a-f]+',ins.op_str):
            v=int(h,16)
            if v in strs: s+=f' "{strs[v][:40]}"'
        if re.search(r'\[0x1[0-9a-f]{5}\]|call|0x1[5-9a-f][0-9a-f]{4}',s): items.append(s)
        if ins.mnemonic in('ret',) : break
    print(f'case {i:2d} ({i:#x}) @{t:08x}: '+' | '.join(items[:12]))
