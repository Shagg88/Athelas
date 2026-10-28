import struct,sys,capstone
X='/mnt/user-data/uploads/project/game/assets/default.xbe'
d=open(X,'rb').read()
base=struct.unpack_from('<I',d,0x104)[0]
n=struct.unpack_from('<I',d,0x11C)[0]; sh=struct.unpack_from('<I',d,0x120)[0]-base
secs=[]
for i in range(n):
    fl,va,vs,ra,rs=struct.unpack_from('<5I',d,sh+i*56)
    secs.append((va,vs,ra,rs))
def rd(va,ln):
    for s in secs:
        if s[0]<=va<s[0]+s[3]: o=s[2]+va-s[0]; return d[o:o+ln]
    return b''
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
a=int(sys.argv[1],16); ln=int(sys.argv[2],16) if len(sys.argv)>2 else 0x100
for i in md.disasm(rd(a,ln),a): print('%x: %s %s'%(i.address,i.mnemonic,i.op_str))
