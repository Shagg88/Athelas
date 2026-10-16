import struct
def unpack(src, pos, size):
    """the game's Rdat decompressor (Xbox 0xF2CE0): big-endian 16-bit tokens"""
    out=bytearray(); end=size
    while len(out)<end:
        t=src[pos]<<8|src[pos+1]; pos+=2
        if (t&0x8800)==0x8800:
            n=(t>>12)&7
            if n==0:
                k=t&0x7ff; out+=src[pos:pos+k]; pos+=k
            else:
                dist=((t>>5)&0x38)|n; ln=(t&0xff)+3; b=out[-dist]; out+=bytes([b])*ln
        else:
            ln=(t>>12)&7
            if ln==7: ln=src[pos]+7; pos+=1
            ln+=3; dist=t&0xfff
            if t&0x8000:
                s=len(out)-dist+2
                for i in range(ln): out.append(out[s-i])
            else:
                s=len(out)-dist
                for i in range(ln): out.append(out[s+i])
    return bytes(out[:end])
def stream(d):
    out=bytearray(); o=0
    # find first SHOC
    o=d.find(b'COHS')
    while 0<=o and o+8<=len(d):
        tag=d[o:o+4]; size=struct.unpack_from('<I',d,o+4)[0]
        if tag==b'LLIF':  # FILL: to the next 64 KB block
            o=(o//0x10000+1)*0x10000; continue
        if size<8 or o+size>len(d):  # padding: on to the next chunk
            n=d.find(b'COHS',o+4)
            if n<0: break
            o=n; continue
        sub=d[o+0x10:o+0x14]
        if tag in (b'COHS',b'ONOS'):
            if sub==b'TADS': out+=d[o+0x40:o+size]
            elif sub==b'tadR':
                n=struct.unpack_from('<I',d,o+0x40)[0]; out+=unpack(d,o+0x44,n)
        o+=size
    return bytes(out)
