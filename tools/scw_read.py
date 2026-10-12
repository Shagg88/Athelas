"""Reads ROTK .scw (PC) / .scx (Xbox) level files: SHOC chunks, SHDR resource headers,
SDAT (stored) and Rdat (compressed) data. unpack() is a port of the game's own
decompressor (rotk.exe 1.0 at 0x4dfe10). Research tool; reads the player's own files."""
import struct
def unpack(src, size):
    out=bytearray(); i=0
    while len(out)<size:
        w=(src[i]<<8)|src[i+1]; i+=2
        if (w&0x8800)==0x8800:
            n=(w>>12)&7
            if n==0:
                c=w&0x7ff; out+=src[i:i+c]; i+=c
            else:
                dist=((w>>5)&0x38)|n; ln=(w&0xff)+3
                out+=bytes([out[-dist]])*ln
        else:
            ln=(w>>12)&7; off=w&0xfff
            if ln==7: ln=src[i]+7; i+=1
            ln+=3
            p=len(out)-off
            if w&0x8000:
                p+=2
                for k in range(ln): out.append(out[p-k])
            else:
                out+=out[p:p+ln]
    return bytes(out[:size])
def resources(path):
    d=open(path,"rb").read(); o=0; res=[]; cur=None
    while o+16<=len(d):
        if d[o:o+4]!=b"COHS": o+=4; continue
        sz=struct.unpack_from("<I",d,o+4)[0]; sub=d[o+16:o+20]
        if sub==b"RDHS":
            typ=d[o+24:o+28][::-1].decode("latin1"); cur=[typ,bytes(d[o+16:o+sz]),bytearray()]; res.append(cur)
        elif sub==b"TADS" and cur is not None:
            cur[2]+=d[o+0x40:o+sz]
        elif sub==b"tadR" and cur is not None:
            n=struct.unpack_from("<I",d,o+0x40)[0]
            cur[2]+=unpack(d[o+0x44:o+sz]+b"\0\0\0\0",n)
        o+=sz if sz>=16 else 16
    return res
