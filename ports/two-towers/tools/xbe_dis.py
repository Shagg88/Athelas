#!/usr/bin/env python3
"""dis.py Default.xbe VA [count]  -- quick linear disassembly at a virtual address"""
import sys; sys.path.insert(0, __file__.rsplit("/",1)[0])
from xbe_info import XBE
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
x = XBE(sys.argv[1]); va = int(sys.argv[2], 16); n = int(sys.argv[3]) if len(sys.argv) > 3 else 20
o = x.va2off(va); md = Cs(CS_ARCH_X86, CS_MODE_32)
for i, ins in enumerate(md.disasm(x.d[o:o+n*15], va)):
    if i >= n: break
    print(f"  {ins.address:08x}  {ins.bytes.hex():20s} {ins.mnemonic} {ins.op_str}")
