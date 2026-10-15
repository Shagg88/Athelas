import struct,re
def tables(s):
    """TXHE tables: (name, w, h, fmt, levels byte, data offset abs) — pixel blob assumed right after the table"""
    out=[]
    for m in re.finditer(b'TXHE',s):
        o=m.start(); size=struct.unpack_from('<I',s,o+4)[0]; n=(size-8)//0x50
        blob=o+size
        for k in range(n):
            r=o+8+k*0x50; name=s[r:r+8].split(b'\0')[0].decode('latin1')
            off=struct.unpack_from('<I',s,r+8)[0]; w,h=struct.unpack_from('<HH',s,r+0x38)
            out.append(dict(name=name,w=w,h=h,fmt=s[r+0x3c],b=s[r+0x3d:r+0x40].hex(),off=off,table=o,blob=blob,rec=r))
    return out
