import sys, pefile, capstone
pe=pefile.PE(sys.argv[1]); base=pe.OPTIONAL_HEADER.ImageBase
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.skipdata=True
pats=sys.argv[2:]
for i in md.disasm(text.get_data(),base+text.VirtualAddress):
    t=i.mnemonic+" "+i.op_str
    if any(p in t for p in pats): print("%08x  %s"%(i.address,t))
