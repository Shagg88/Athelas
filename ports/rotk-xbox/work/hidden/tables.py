#!/usr/bin/env python3
"""tables.py <stream.bin> [tag...]: dump EA data tables (header: u32 2, tag, nRec, nFld,
nFld x (name4, type4 'it32'/'flot', size u32), then nRec x nFld values).
Registered handlers in default.xbe (0x113d60): abil, npc , plyr, upgd, weap."""
import sys,struct,re
d=open(sys.argv[1],'rb').read()
want=[t.encode().ljust(4) for t in sys.argv[2:]] or [b'abil',b'npc ',b'plyr',b'upgd',b'weap']
def tag(b): return b.decode('latin1')
for m in re.finditer(rb'\x02\x00\x00\x00(....)',d):
    t=m.group(1)
    if t not in want: continue
    o=m.start()+8; nrec,nfld=struct.unpack_from('<II',d,o); o+=8
    if not(0<nfld<200 and 0<nrec<2000): continue
    flds=[]
    ok=True
    for i in range(nfld):
        n,ty,sz=d[o:o+4],d[o+4:o+8],struct.unpack_from('<I',d,o+8)[0]; o+=12
        if not all(32<=c<127 for c in ty) or sz not in (4,8,16,32,64): ok=False;break
        flds.append((tag(n),tag(ty),sz))
    if not ok: continue
    print(f'### table {tag(t)} @0x{m.start():x}: {nrec} records x {nfld} fields')
    print('fields: '+' '.join(f'{n}:{ty}' for n,ty,sz in flds))
    for r in range(nrec):
        vals=[]
        for n,ty,sz in flds:
            raw=d[o:o+sz]; o+=sz
            if ty.startswith('st'): vals.append(raw.split(b'\0')[0].decode('latin1'))
            elif ty=='id  ' or n=='id  ': vals.append(tag(raw) if all(32<=c<127 or c==0 for c in raw) else raw.hex())
            elif ty=='flot': vals.append(f'{struct.unpack("<f",raw)[0]:g}')
            else: vals.append(str(struct.unpack('<i',raw)[0]))
        print(' | '.join(vals))
    print()
