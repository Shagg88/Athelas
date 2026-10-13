#!/usr/bin/env python3
"""xbe_fn.py <xbe> <config.toml> <name-or-hexVA> [max_insns]
Disassemble one function (follows straight-line code to the first ret/jmp at the
end, up to max_insns) and annotate known symbol addresses from the config."""
import sys, re
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from xbe_info import XBE
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

x = XBE(sys.argv[1])
names = {}
for line in open(sys.argv[2]):
    m = re.match(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", line)
    if m and int(m.group(2), 16): names[int(m.group(2), 16)] = m.group(1)
rev = {v: k for k, v in names.items()}
t = sys.argv[3]
va = rev[t] if t in rev else int(t, 16)
n = int(sys.argv[4]) if len(sys.argv) > 4 else 200
md = Cs(CS_ARCH_X86, CS_MODE_32)
o = x.va2off(va)
print(f"== {names.get(va, hex(va))}")
seen_branch_targets = set()
for i, ins in enumerate(md.disasm(x.d[o:o + n * 15], va)):
    if i >= n: break
    note = []
    for h in re.findall(r"0x[0-9a-f]+", ins.op_str):
        v = int(h, 16)
        if v in names: note.append(names[v])
    print(f"  {ins.address:08x}  {ins.mnemonic:6s} {ins.op_str}" + (f"   ; {', '.join(note)}" if note else ""))
    if ins.mnemonic.startswith("j") and ins.op_str.startswith("0x"):
        seen_branch_targets.add(int(ins.op_str, 16))
    if ins.mnemonic in ("ret", "jmp") and not any(b > ins.address for b in seen_branch_targets):
        break
