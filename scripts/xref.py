import sys, pefile, struct, capstone
pe=pefile.PE(sys.argv[1]); base=pe.OPTIONAL_HEADER.ImageBase
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
code=text.get_data(); tva=base+text.VirtualAddress
iat={}
for e in pe.DIRECTORY_ENTRY_IMPORT:
    for i in e.imports:
        if i.name: iat[i.address]=i.name.decode()
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
md.skipdata=True
targets=set(sys.argv[2].split(','))
want={a for a,n in iat.items() if n in targets}
consts=[int(x,16) for x in sys.argv[3].split(',')] if len(sys.argv)>3 else []
for ins in md.disasm(code,tva):
    s=ins.op_str
    for a in want:
        if ("0x%x"%a) in s: print("%08x  %-6s %-40s ; %s"%(ins.address,ins.mnemonic,s,iat[a]))
    for c in consts:
        if ("0x%x"%c) in s and ins.mnemonic not in('call','jmp') : print("%08x  %-6s %s ; const %#x"%(ins.address,ins.mnemonic,s,c))
