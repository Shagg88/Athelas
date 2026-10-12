import sys, pefile, capstone
pe=pefile.PE(sys.argv[1]); base=pe.OPTIONAL_HEADER.ImageBase
start=int(sys.argv[2],16); n=int(sys.argv[3],0)
data=pe.get_memory_mapped_image()[start-base:start-base+n]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for i in md.disasm(data,start): print("%08x  %-7s %s"%(i.address,i.mnemonic,i.op_str))
