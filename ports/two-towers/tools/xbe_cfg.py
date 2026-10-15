#!/usr/bin/env python3
"""Recursive-descent disassembly of an XBE: only follows real control flow, so data
mixed into code sections is not mistaken for instructions.
Seeds: entry point, TLS callbacks, every XbSymbolDatabase function, and code pointers
found in data sections (vtables, callback tables). Writes out/cfg.json with
functions, instructions-per-section, call graph edges, and the hardware/kernel facts.
Usage: xbe_cfg.py Default.xbe symbols_raw.txt kernel_names.json outdir"""
import sys, json, re, struct, collections
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_GRP_JUMP, CS_GRP_CALL, CS_GRP_RET, CS_GRP_INT
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_INVALID
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from xbe_info import XBE

xbe_path, sym_path, kn_path, outdir = sys.argv[1:5]
x = XBE(xbe_path)
kn = {int(k): v for k, v in json.load(open(kn_path)).items()}

CODE = {".text", "D3D", "XGRPH", "DSOUND", "XACTENG", "WMADEC", "XPP"} | \
       {s["name"] for s in x.sections if s["name"].startswith("BINK") and s["name"] != "BINKDATA"}
code_secs = [s for s in x.sections if s["name"] in CODE]
def in_code(va):
    for s in code_secs:
        if s["va"] <= va < s["va"] + s["rsize"]: return s["name"]
def sec(va): return x.section_of(va) or "?"

syms = {}
for line in open(sym_path):
    m = re.match(r"(\w+?)__(\w+) = 0x([0-9a-f]+)", line.strip())
    if m: syms[int(m.group(3), 16)] = (m.group(1), m.group(2))
thunk = {va: o for va, o in x.kernel_imports()}
kname = lambda o: kn.get(o, {}).get("name", f"ord{o}")

md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True

def code_ptrs_in_data():
    """Aligned dwords in data sections that point at a plausible function start in code."""
    out = set()
    for s in x.sections:
        if s["name"] in CODE or s["inserted"] or not s["rsize"]: continue
        if s["name"] in ("DOLBY", "DSPImage", "BINKDATA"): continue
        d = x.d[s["raw"]:s["raw"] + s["rsize"]]
        for i in range(0, len(d) - 3, 4):
            v = struct.unpack_from("<I", d, i)[0]
            if in_code(v): out.add(v)
    return out

VARLIKE = re.compile(r"^(g_|D3DRS_|D3D_g_|.*Table|.*TableBegin|.*TableEnd)")
seeds = {x.entry} | {a for a, (l, n) in syms.items() if in_code(a) and not VARLIKE.match(n)}
dptrs = code_ptrs_in_data()

visited = {}            # addr -> insn info
covered = set()         # every byte that belongs to a decoded instruction
func_starts = set(seeds)
work = list(seeds)
data_seed_pending = sorted(dptrs - seeds)
edges = collections.defaultdict(set)   # caller func? (we record site->target)
call_sites = []         # (site, target or None, kind)
kcalls = collections.defaultdict(list)
jt_count = 0

def read32(va):
    o = x.va2off(va)
    return None if o is None else struct.unpack_from("<I", x.d, o)[0]

def decode_at(va):
    o = x.va2off(va)
    if o is None: return None
    for ins in md.disasm(x.d[o:o + 16], va):
        return ins
    return None

def walk(start, strict=False):
    """Follow one function body. strict=True for data-pointer seeds: abort (and roll back)
    if we hit undecodable bytes or privileged junk, since the pointer may not be code."""
    global jt_count
    local = {}; stack = [start]; new_funcs = set(); new_calls = []; new_k = []
    while stack:
        va = stack.pop()
        while True:
            if va in visited or va in local: break
            if not in_code(va):
                if strict: return False
                break
            ins = decode_at(va)
            if ins is None:
                if strict: return False
                break
            if strict and (ins.mnemonic in ("in", "out", "insb", "insd", "outsb", "outsd", "hlt", "cli", "sti",
                                            "retf", "int", "into", "arpl", "bound", "les", "lds", "aaa", "aas", "daa", "das")
                           or any(b in covered for b in range(va, va + ins.size))):
                return False   # pointer was not really code (or lands mid-instruction)
            local[va] = ins
            grp = set(ins.groups)
            op0 = ins.operands[0] if ins.operands else None
            nxt = va + ins.size
            if CS_GRP_CALL in grp:
                if op0.type == X86_OP_IMM:
                    t = op0.imm & 0xFFFFFFFF
                    if in_code(t): new_funcs.add(t)
                    new_calls.append((va, t, "direct"))
                elif op0.type == X86_OP_MEM and op0.mem.base == X86_REG_INVALID and op0.mem.index == X86_REG_INVALID:
                    slot = op0.mem.disp & 0xFFFFFFFF
                    if slot in thunk: new_k.append((thunk[slot], va))
                    new_calls.append((va, slot, "kernel" if slot in thunk else "memptr"))
                else:
                    new_calls.append((va, None, "indirect"))
                va = nxt; continue
            if CS_GRP_RET in grp: break
            if ins.mnemonic == "int3": break
            if CS_GRP_JUMP in grp:
                if op0.type == X86_OP_IMM:
                    t = op0.imm & 0xFFFFFFFF
                    stack.append(t)
                    if ins.mnemonic == "jmp":
                        if t in syms or t in thunk: new_funcs.add(t)
                        break
                    va = nxt; continue
                # jmp [table + reg*4]  -> switch table
                if op0.type == X86_OP_MEM and op0.mem.scale == 4 and op0.mem.disp:
                    tbl = op0.mem.disp & 0xFFFFFFFF
                    for i in range(1024):
                        t = read32(tbl + 4 * i)
                        if t is None or not in_code(t): break
                        stack.append(t); jt_count += 1
                    break
                if op0.type == X86_OP_MEM and op0.mem.base == X86_REG_INVALID and op0.mem.index == X86_REG_INVALID:
                    slot = op0.mem.disp & 0xFFFFFFFF
                    if slot in thunk: new_k.append((thunk[slot], va))   # tail-call into kernel
                break
            va = nxt
    visited.update(local)
    for a, i in local.items(): covered.update(range(a, a + i.size))
    for f in new_funcs:
        if f not in func_starts:
            func_starts.add(f); work.append(f)
    call_sites.extend(new_calls)
    for o, va in new_k: kcalls[o].append(va)
    return True

def drain():
    while work:
        walk(work.pop())

drain()
accepted_data_seeds = 0
for p in data_seed_pending:
    if p in visited: continue
    if walk(p, strict=True):
        func_starts.add(p); accepted_data_seeds += 1
        drain()

# ---- facts from reachable instructions only ------------------------------------------
HWR = [(0xFD000000, 0xFE000000, "NV2A"), (0xFE800000, 0xFE820000, "APU"),
       (0xFEC00000, 0xFEC01000, "AC97"), (0xFEF00000, 0xFEF00400, "NIC"),
       (0xFED00000, 0xFED01000, "USB0"), (0xFED08000, 0xFED09000, "USB1")]
def hw(v):
    for lo, hi, n in HWR:
        if lo <= v < hi: return n
PRIV = {"in", "out", "insb", "insw", "insd", "outsb", "outsw", "outsd", "cli", "sti", "hlt",
        "wbinvd", "invd", "rdmsr", "wrmsr", "lgdt", "lidt", "invlpg", "rdtsc", "cpuid", "rdpmc"}
insn_per_sec = collections.Counter()
hwrefs, privs, symrefs, contig = [], [], collections.defaultdict(list), []
for va, ins in visited.items():
    s = in_code(va); insn_per_sec[s] += 1
    mn = ins.mnemonic
    if mn in PRIV or (mn == "mov" and re.search(r"\b[cd]r\d\b", ins.op_str)):
        privs.append((va, s, mn, ins.op_str))
    for op in ins.operands:
        if op.type == X86_OP_IMM: v, how = op.imm & 0xFFFFFFFF, "imm"
        elif op.type == X86_OP_MEM: v, how = op.mem.disp & 0xFFFFFFFF, ("abs" if op.mem.base == X86_REG_INVALID and op.mem.index == X86_REG_INVALID else "disp")
        else: continue
        h = hw(v)
        if h: hwrefs.append((va, s, h, how, v, f"{mn} {ins.op_str}"))
        if v in syms and how in ("imm", "abs"): symrefs[v].append((va, s, mn))
        if 0x80000000 <= v < 0x84000000 and how in ("imm", "abs"):
            contig.append((va, s, v, f"{mn} {ins.op_str}"))

# which function contains an address (nearest preceding start)
starts_sorted = sorted(func_starts)
import bisect
def func_of(va):
    i = bisect.bisect_right(starts_sorted, va) - 1
    return starts_sorted[i] if i >= 0 else None
def fname(a):
    if a in syms: return f"{syms[a][0]}::{syms[a][1]}"
    return f"sub_{a:08x}"

R = dict(
    functions=len(func_starts), data_pointer_seeds=accepted_data_seeds, jump_table_targets=jt_count,
    insn_per_section=dict(insn_per_sec),
    kernel=[dict(ordinal=o, name=kname(o), sites=len(kcalls[o]),
                 callers=sorted({fname(func_of(v)) for v in kcalls[o]})[:12],
                 sections=sorted({in_code(v) for v in kcalls[o]}))
            for o in sorted(set(thunk.values()))],
    symbols=[dict(lib=l, name=n, addr=hex(a), section=sec(a),
                  reached=a in visited,
                  refs_from_text=sum(1 for r in symrefs[a] if r[1] == ".text"),
                  refs_total=len(symrefs[a]))
             for a, (l, n) in sorted(syms.items())],
    hardware=[dict(site=hex(va), func=fname(func_of(va)), section=s, dev=h, how=how, value=hex(v), ins=i)
              for va, s, h, how, v, i in sorted(hwrefs)],
    privileged=[dict(site=hex(va), func=fname(func_of(va)), section=s, ins=f"{m} {o}") for va, s, m, o in sorted(privs)],
    contiguous=[dict(site=hex(va), func=fname(func_of(va)), section=s, value=hex(v), ins=i) for va, s, v, i in sorted(contig)],
    indirect_calls=sum(1 for c in call_sites if c[2] in ("indirect", "memptr")),
    direct_calls=sum(1 for c in call_sites if c[2] == "direct"),
)
json.dump(R, open(f"{outdir}/cfg.json", "w"), indent=1)
json.dump(sorted(func_starts), open(f"{outdir}/func_starts.json", "w"))

# fs:-segment accesses (Xbox fs -> KPCR, Windows fs -> TEB; layouts differ past offset 0x1C)
fs = collections.Counter(); fs_ex = {}
for va, ins in visited.items():
    if "fs:" in ins.op_str:
        m = re.search(r"fs:\[([^\]]+)\]", ins.op_str)
        key = (in_code(va), m.group(1) if m else "?")
        fs[key] += 1; fs_ex.setdefault(key, f"{va:#x} {ins.mnemonic} {ins.op_str} in {fname(func_of(va))}")
R["fs_accesses"] = [dict(section=k[0], operand=k[1], count=n, example=fs_ex[k]) for k, n in fs.most_common()]
json.dump(R, open(f"{outdir}/cfg.json", "w"), indent=1)
print("fs: accesses:")
for e in R["fs_accesses"]: print("  ", e)

print("functions:", R["functions"], "(from data pointers:", accepted_data_seeds, ") jump-table targets:", jt_count)
print("reachable instructions per section:", R["insn_per_section"])
print("calls: direct", R["direct_calls"], "indirect", R["indirect_calls"])
print("kernel imports never reached:", [k["name"] for k in R["kernel"] if not k["sites"]])
print("hardware refs:", dict(collections.Counter((h["section"], h["dev"]) for h in R["hardware"])))
print("privileged:", dict(collections.Counter((p["section"], p["ins"].split()[0]) for p in R["privileged"])))
print("contiguous-memory constants:", dict(collections.Counter(c["section"] for c in R["contiguous"])))
