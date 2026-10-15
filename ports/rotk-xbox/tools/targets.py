import sys,struct,re,collections
sys.path.insert(0,'/home/claude/tt/tools')
from xbe_info import XBE
x=XBE(sys.argv[1]); d=x.d
names={}
for l in open('/home/claude/tt/game/generated/game/image_symbols.h'):
    m=re.match(r'#define SYM_(\w+) ([0-9A-F]{8})',l)
    if m and not m.group(2).startswith('FFFF'): names[int(m.group(2),16)]=m.group(1)
lo,hi=int(sys.argv[2],16),int(sys.argv[3],16); tlo,thi=int(sys.argv[4],16),int(sys.argv[5],16)
c=collections.defaultdict(list)
for va in range(lo,hi-5):
    o=x.va2off(va)
    if o is not None and d[o]==0xE8:
        t=(va+5+struct.unpack('<i',d[o+1:o+5])[0])&0xffffffff
        if tlo<=t<thi: c[t].append(va)
for t in sorted(c): print(hex(t), names.get(t,'?'), len(c[t]), ' '.join(hex(v) for v in c[t][:4]))
