import sys,struct
sys.path.insert(0,'/home/claude/rotkx/tools')
from xbe_info import XBE
x=XBE(sys.argv[1]); targets={int(t,16) for t in sys.argv[2:]}
d=x.d
# scan all executable bytes for E8 rel32
lo,hi=0x12000,0x170000
for va in range(lo,hi-5):
    o=x.va2off(va)
    if o is None: continue
    if d[o]==0xE8:
        t=(va+5+struct.unpack('<i',d[o+1:o+5])[0])&0xffffffff
        if t in targets: print(hex(t),'<-',hex(va))
