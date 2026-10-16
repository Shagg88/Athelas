#!/usr/bin/env python3
"""guest_reach.py <xbe> <config.toml> <XbSymbolDatabase out> [section ...]
Which XDK library code will still run as recompiled guest code?
Walks direct calls/jumps from the entry point and from code pointers that game code loads
as immediates, but does NOT descend into functions the config names in [symbols]
(the host replaces those). Reports reachable functions inside the given library sections,
with any hardware access (NV2A/APU/AC97/USB MMIO, port I/O) and push-buffer writes they contain."""
import sys, re, struct, collections
sys.path.insert(0, __file__.rsplit("/", 1)[0])
from xbe_info import XBE
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_GRP_JUMP, CS_GRP_CALL, CS_GRP_RET
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_INVALID

x = XBE(sys.argv[1])
host = {}
sec_ = None
for line in open(sys.argv[2]):
    m = re.match(r"^\[(\w+)\]", line)
    if m: sec_ = m.group(1)
    m = re.match(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", line)
    if m and sec_ == "symbols" and int(m.group(2), 16): host[int(m.group(2), 16)] = m.group(1)
xs = {}
for line in open(sys.argv[3]):
    m = re.match(r"(\w+?)__(\w+) = 0x([0-9a-f]+)", line.strip())
    if m: xs[int(m.group(3), 16)] = m.group(2)
LIBS = set(sys.argv[4:]) or {"D3D", "DSOUND", "XACTENG", "XGRPH", "XPP"}
CODE = {".text", "D3D", "XGRPH", "DSOUND", "XACTENG", "WMADEC", "XPP"} | \
       {s["name"] for s in x.sections if s["name"].startswith("BINK") and s["name"] != "BINKDATA"}
csecs = [s for s in x.sections if s["name"] in CODE]
def sec(va):
    for s in csecs:
        if s["va"] <= va < s["va"] + s["rsize"]: return s["name"]
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
def dec(va):
    o = x.va2off(va)
    for i in md.disasm(x.d[o:o + 16], va): return i

HW = [(0xFD000000, 0xFE000000, "NV2A"), (0xFE800000, 0xFE820000, "APU"), (0xFEC00000, 0xFEC01000, "AC97"),
      (0xFED00000, 0xFED10000, "USB")]
def hw(v):
    for lo, hi, n in HW:
        if lo <= v < hi: return n

funcs = {}           # start -> dict(insns, calls, hw, io, caller)
work = [(x.entry, None)]
seen = set()
while work:
    f, caller = work.pop()
    if f in seen or f in host or not sec(f): continue
    seen.add(f)
    if caller is not None and f not in xs:
        # pointer seeds that land in data decode as junk: zero runs, strings, port I/O
        o = x.va2off(f)
        first = [i for _, i in zip(range(6), md.disasm(x.d[o:o + 64], f))]
        if not first or x.d[o:o + 2] == b"\0\0" or any(i.mnemonic in ("in", "out", "insb", "insd", "outsb", "outsd", "arpl", "bound", "das", "aaa") for i in first):
            continue
    info = dict(calls=set(), hw=[], io=[], caller=caller, size=0, ptrs=set())
    stack, body = [f], set()
    while stack:
        va = stack.pop()
        while va not in body and sec(va):
            i = dec(va)
            if i is None: break
            body.add(va); info["size"] += 1
            g = set(i.groups); op0 = i.operands[0] if i.operands else None
            if i.mnemonic in ("in", "out", "insb", "insd", "outsb", "outsd", "wbinvd"):
                info["io"].append(f"{va:#x} {i.mnemonic} {i.op_str}")
            for op in i.operands:
                v = None
                if op.type == X86_OP_IMM: v = op.imm & 0xFFFFFFFF
                elif op.type == X86_OP_MEM: v = op.mem.disp & 0xFFFFFFFF
                if v is None: continue
                h = hw(v)
                if h and v < 0xFFFF0000: info["hw"].append(f"{va:#x} {h} {i.mnemonic} {i.op_str}")
                if op.type == X86_OP_IMM and not (CS_GRP_CALL in g or CS_GRP_JUMP in g) and sec(v) and v not in host:
                    info["ptrs"].add(v)      # code pointer loaded as data (callbacks, vtables built in code)
            nxt = va + i.size
            if CS_GRP_CALL in g:
                if op0.type == X86_OP_IMM: info["calls"].add(op0.imm & 0xFFFFFFFF)
                va = nxt; continue
            if CS_GRP_RET in g or i.mnemonic == "int3": break
            if CS_GRP_JUMP in g:
                if op0.type == X86_OP_IMM:
                    t = op0.imm & 0xFFFFFFFF
                    if i.mnemonic == "jmp":
                        if t in host or t in xs: info["calls"].add(t); break   # tail call
                        stack.append(t); break
                    stack.append(t); va = nxt; continue
                if op0.type == X86_OP_MEM and op0.mem.scale == 4 and op0.mem.disp:
                    tb = op0.mem.disp & 0xFFFFFFFF
                    for k in range(512):
                        o = x.va2off(tb + 4 * k)
                        if o is None: break
                        t = struct.unpack_from("<I", x.d, o)[0]
                        if not sec(t): break
                        stack.append(t)
                break
            va = nxt
    funcs[f] = info
    for c in info["calls"]: work.append((c, f))
    for p in info["ptrs"]:
        # only follow pointers that look like function starts we can decode
        if p not in seen: work.append((p, f))

name = lambda a: host.get(a) or xs.get(a) or f"sub_{a:08x}"
lib_funcs = {a: v for a, v in funcs.items() if sec(a) in LIBS}
print(f"reachable guest functions: {len(funcs)}, inside {sorted(LIBS)}: {len(lib_funcs)}")
by = collections.Counter(sec(a) for a in lib_funcs)
print("per section:", dict(by))
print("\n-- library functions that run as guest code and touch hardware or ports --")
for a, v in sorted(lib_funcs.items()):
    if v["hw"] or v["io"]:
        print(f"{sec(a):8s} {a:#x} {name(a)}  (called from {name(v['caller']) if v['caller'] else 'entry'})")
        for h in (v["hw"] + v["io"])[:6]: print("      ", h)
print("\n-- call paths from game-facing functions to the hardware users --")
callers = collections.defaultdict(set)
for a, v in funcs.items():
    for c in v["calls"]: callers[c].add(a)
for a, v in sorted(lib_funcs.items()):
    if not v["hw"]: continue
    # walk up until a function that is called from .text (the API the game uses)
    frontier, seen_up, tops = [a], set(), set()
    while frontier:
        f = frontier.pop()
        if f in seen_up: continue
        seen_up.add(f)
        cs = callers.get(f, set())
        if any(sec(c) == ".text" for c in cs): tops.add(f)
        frontier += [c for c in cs if sec(c) in LIBS]
    print(f"{name(a)}: reached through " + ", ".join(sorted(name(t) for t in tops)))
print("\n-- all reachable library guest functions, by section --")
for s in sorted(LIBS):
    names = [name(a) for a in sorted(lib_funcs) if sec(a) == s]
    print(f"[{s}] {len(names)}: " + " ".join(names))
