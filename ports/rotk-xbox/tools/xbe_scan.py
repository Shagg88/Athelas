#!/usr/bin/env python3
"""Cross-reference scan of an XBE.
 - names kernel imports and finds every call site
 - loads XbSymbolDatabase output and finds where game code calls / touches each XDK symbol
 - looks for direct hardware access (NV2A, APU, AC97, other MMIO), port I/O, privileged instructions
Usage: xbe_scan.py Default.xbe symbols_raw.txt kernel_names.json outdir"""
import sys, json, re, collections
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from xbe_info import XBE

xbe_path, sym_path, kn_path, outdir = sys.argv[1:5]
x = XBE(xbe_path)
kn = {int(k): v for k, v in json.load(open(kn_path)).items()}

# ---- symbols -----------------------------------------------------------------
syms = {}
for line in open(sym_path):
    m = re.match(r"(\w+?)__(\w+) = 0x([0-9a-f]+)", line.strip())
    if m: syms[int(m.group(3), 16)] = (m.group(1), m.group(2))

# ---- kernel thunk slots ---------------------------------------------------------
thunk_slots = {}
for va, ordn in x.kernel_imports():
    thunk_slots[va] = (ordn, kn.get(ordn, {}).get("name", f"ord{ordn}"), kn.get(ordn, {}).get("kind", "?"))

HW = [  # (lo, hi, name)
    (0xFD000000, 0xFE000000, "NV2A registers (GPU MMIO)"),
    (0xFE800000, 0xFE820000, "APU (MCPX audio)"),
    (0xFEC00000, 0xFEC01000, "AC97"),
    (0xFEF00000, 0xFEF00400, "NIC"),
    (0xFED00000, 0xFED01000, "USB0 OHCI"),
    (0xFED08000, 0xFED09000, "USB1 OHCI"),
    (0xFF000000, 0x100000000, "Flash ROM"),
    (0xF0000000, 0xF8000000, "GPU framebuffer aperture"),
]
def hw(v):
    for lo, hi, n in HW:
        if lo <= v < hi: return n
PRIV = {"in", "out", "insb", "insw", "insd", "outsb", "outsw", "outsd", "cli", "sti", "hlt",
        "wbinvd", "invd", "rdmsr", "wrmsr", "lgdt", "lidt", "invlpg", "rdtsc", "cpuid", "rdpmc"}

md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True

code_secs = [s for s in x.sections if s["executable"] and not s["inserted"] and s["rsize"]]
kcalls = collections.defaultdict(list)            # ordinal -> [(site, section)]
scalls = collections.defaultdict(list)            # sym addr -> [(site, section, how)]
hwrefs, privs = [], collections.defaultdict(list)
contig = []                                       # 0x80000000-0x83FFFFFF (contiguous/physical) refs
for s in code_secs:
    data = x.d[s["raw"]:s["raw"] + s["rsize"]]
    off = 0
    while off < len(data):
        progressed = False
        for ins in md.disasm(data[off:], s["va"] + off):
            progressed = True
            off = ins.address - s["va"] + ins.size
            mn = ins.mnemonic
            if mn in PRIV or (mn == "mov" and ("cr" in ins.op_str.split(",")[0] or ", cr" in ins.op_str)):
                privs[(mn, s["name"])].append(ins.address)
            for op in ins.operands:
                vals = []
                if op.type == X86_OP_IMM: vals.append(("imm", op.imm & 0xFFFFFFFF))
                elif op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                    vals.append(("mem", op.mem.disp & 0xFFFFFFFF))
                elif op.type == X86_OP_MEM and op.mem.disp:
                    vals.append(("memdisp", op.mem.disp & 0xFFFFFFFF))
                for how, v in vals:
                    if v in thunk_slots and how == "mem":
                        kcalls[thunk_slots[v][0]].append((ins.address, s["name"], mn))
                    if v in syms and how in ("imm", "mem"):
                        scalls[v].append((ins.address, s["name"], mn))
                    h = hw(v)
                    # skip small negative constants / masks that happen to land in the high ranges
                    if h and v < 0xFFFF0000:
                        hwrefs.append((ins.address, s["name"], h, how, v, f"{mn} {ins.op_str}"))
                    if 0x80000000 <= v < 0x84000000 and how != "imm":
                        contig.append((ins.address, s["name"], v, f"{mn} {ins.op_str}"))
        # capstone stops on an undecodable byte; step over it
        if off < len(data):
            try: next(md.disasm(data[off:off + 15], s["va"] + off))
            except StopIteration: off += 1

# ---- reports ---------------------------------------------------------------------
lib_of_sec = {}
def owner(addr):
    sec = x.section_of(addr) or "?"
    return sec

R = {}
R["kernel_imports"] = [dict(ordinal=o, name=n, kind=k, slot=hex(va),
                            call_sites=len(kcalls[o]),
                            sections=sorted(set(c[1] for c in kcalls[o])))
                       for va, (o, n, k) in sorted(thunk_slots.items())]

R["symbols"] = []
for a, (lib, name) in sorted(syms.items()):
    refs = scalls.get(a, [])
    R["symbols"].append(dict(lib=lib, name=name, addr=hex(a), section=owner(a),
                             refs_total=len(refs),
                             refs_from_text=sum(1 for r in refs if r[1] == ".text"),
                             ref_kinds=sorted(set(r[2] for r in refs))))
R["hardware_refs"] = [dict(site=hex(a), section=s, target=h, how=how, value=hex(v), ins=i) for a, s, h, how, v, i in hwrefs]
R["privileged"] = [dict(ins=m, section=s, count=len(v), first=[hex(z) for z in v[:6]]) for (m, s), v in sorted(privs.items())]
R["contiguous_mem_refs"] = [dict(site=hex(a), section=s, value=hex(v), ins=i) for a, s, v, i in contig]
json.dump(R, open(f"{outdir}/scan.json", "w"), indent=1)

# human summary
print("== kernel imports by area")
area = collections.Counter(re.match(r"[A-Z][a-z]+|[A-Z]+(?=[A-Z][a-z])|\w{2}", e["name"]).group(0) for e in R["kernel_imports"])
print(dict(area.most_common()))
print("== symbols per library:", dict(collections.Counter(e["lib"] for e in R["symbols"])))
print("== symbols per section:", dict(collections.Counter(e["section"] for e in R["symbols"])))
print("== hardware refs per section/target:",
      dict(collections.Counter((e["section"], e["target"]) for e in R["hardware_refs"])))
print("== privileged:", [(e["ins"], e["section"], e["count"]) for e in R["privileged"]])
print("== contiguous-memory refs per section:", dict(collections.Counter(e["section"] for e in R["contiguous_mem_refs"])))
