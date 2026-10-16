#!/usr/bin/env python3
"""regscan.py: rank call targets by how many call sites push a distinct string pointer
in the 6 instructions before the call (finds registration/lookup functions)."""
import sys,struct,collections
sys.path.insert(0,'/home/claude/rotkx/work/hidden'); from xref import x,d,md,T0,TS,o0
strs={}
for l in open('/home/claude/rotkx/work/hidden/xbe_strings.tsv'):
    va,k,sec,s=l.rstrip('\n').split('\t',3)
    if sec in ('.rdata','.data'): strs[int(va,16)]=s
md.skipdata=True
code=d[o0:o0+TS]
ins_list=list(md.disasm(code,T0))
cnt=collections.defaultdict(list)
win=[]
for ins in ins_list:
    if ins.mnemonic=='push' and ins.op_str.startswith('0x'):
        v=int(ins.op_str,16)
        if v in strs: win.append((ins.address,v))
    if ins.mnemonic=='call' and ins.op_str.startswith('0x'):
        t=int(ins.op_str,16)
        for a,v in win:
            if ins.address-a<40: cnt[t].append((ins.address,v))
        win=[]
    elif ins.mnemonic in ('ret','jmp'): win=[]
rank=sorted(cnt.items(),key=lambda kv:-len(set(v for a,v in kv[1])))
for t,l in rank[:int(sys.argv[1]) if len(sys.argv)>1 else 40]:
    ex=[strs[v] for a,v in l[:6]]
    print(f'{t:08x} sites={len(l):4d} distinct={len(set(v for a,v in l)):4d}  e.g. {ex}')
