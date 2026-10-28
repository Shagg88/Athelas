import struct,sys
f='/mnt/user-data/uploads/FOTR-extracted/Default.xbe'
d=open(f,'rb').read()
base=struct.unpack_from('<I',d,0x104)[0]
n=struct.unpack_from('<I',d,0x11C)[0]; sh=struct.unpack_from('<I',d,0x120)[0]-base
secs=[struct.unpack_from('<IIIII',d,sh+i*56) for i in range(n)]
def va2off(va):
    for fl,va0,vs,ra,rs in secs:
        if va0<=va<va0+rs: return ra+va-va0
def off2va(o):
    for fl,va0,vs,ra,rs in secs:
        if ra<=o<ra+rs: return va0+o-ra
def find(s):
    out=[]; b=s.encode()+b'\0'; i=d.find(b)
    while i>=0:
        if i==0 or d[i-1]==0: out.append(off2va(i))
        i=d.find(b,i+1)
    return out
def cstr(va):
    o=va2off(va); return d[o:d.index(b'\0',o)].decode('latin1')
def f32(va): return struct.unpack_from('<f',d,va2off(va))[0]
def u32(va): return struct.unpack_from('<I',d,va2off(va))[0]
