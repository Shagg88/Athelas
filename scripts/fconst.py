import sys,pefile,struct,capstone
pe=pefile.PE(sys.argv[1]);m=pe.get_memory_mapped_image();b=0x400000
vals={float(v):v for v in sys.argv[2:]}
rd=[s for s in pe.sections if s.Name.rstrip(b'\0') in (b'.rdata',b'.data')]
locs={}
for s in rd:
    d=s.get_data()
    for off in range(0,len(d)-3,4):
        f=struct.unpack_from('<f',d,off)[0]
        for v in vals:
            if abs(f-v)<=abs(v)*1e-5: locs[b+s.VirtualAddress+off]=v
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.skipdata=True
cnt={}
for i in md.disasm(text.get_data(),b+text.VirtualAddress):
    for a,v in locs.items():
        if "0x%x]"%a in i.op_str:
            cnt.setdefault(v,[]).append("%08x %s %s"%(i.address,i.mnemonic,i.op_str))
for v,l in cnt.items():
    print("== %g : %d refs"%(v,len(l)))
    for x in l[:12]: print("  ",x)
