# ttext.py: the text tables (types 0x384..0x386) of a Two Towers UI package (.BFF)
import struct,sys
def tables(path):
    d=open(path,'rb').read()
    v,dirp=struct.unpack_from('<2I',d,0)
    n=struct.unpack_from('<I',d,dirp)[0]
    out={}
    for i in range(n):
        t,off,sub=struct.unpack_from('<3I',d,dirp+4+i*12)
        if 0x384<=t<=0x386: out[t]=off
    res={}
    for t,base in out.items():
        cnt=struct.unpack_from('<I',d,base+0xC)[0]
        lines={}
        for k in range(cnt):
            tid,so=struct.unpack_from('<2I',d,base+0x10+k*8)
            s=base+so; e=s
            raw=d[s:s+2048]
            lines[tid]=raw
        res[t]=(base,cnt,lines)
    return d,res
if __name__=='__main__':
    d,res=tables(sys.argv[1])
    for t,(base,cnt,lines) in sorted(res.items()):
        print(hex(t),'at',hex(base),'lines',cnt)
        for tid in list(lines)[:6]:
            r=lines[tid]
            print('  ',tid, r[:40].hex(), repr(r[:40]))
