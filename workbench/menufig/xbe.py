import struct
class XBE:
    def __init__(s,p):
        d=open(p,'rb').read(); s.d=d
        base,=struct.unpack_from('<I',d,0x104)
        n,hdr=struct.unpack_from('<II',d,0x11C)
        s.secs=[]
        for i in range(n):
            o=hdr-base+i*0x38
            fl,va,vs,ra,rs=struct.unpack_from('<5I',d,o)
            s.secs.append((va,vs,ra,rs))
    def read(s,a,n):
        for va,vs,ra,rs in s.secs:
            if va<=a<va+vs:
                off=a-va; b=s.d[ra+off:ra+min(off+n,rs)]
                return b+bytes(n-len(b))
        raise KeyError(hex(a))
    def u32(s,a): return struct.unpack('<I',s.read(a,4))[0]
