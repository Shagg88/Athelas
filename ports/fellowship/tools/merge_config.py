#!/usr/bin/env python3
"""Merge XbSymbolDatabase results into an XboxXBE-Recomp config written by xbe_symbols.

usage: merge_config.py <xbe_symbols config.toml> <XbSymbolDatabaseCLI output> <out.toml> [manual.toml]

- Fills names the toolkit left at 0 from XbSymbolDatabase (with name aliases).
- Where both have an address and they differ, XbSymbolDatabase wins (it checks callees)
  and the conflict is reported so it can be checked by hand.
- manual.toml ([symbols]/[data] name = 0xADDR) is applied last and wins over both.
Only addresses are written: no game bytes."""
import re, sys

cfg_path, xsdb_path, out_path = sys.argv[1:4]
manual_path = sys.argv[4] if len(sys.argv) > 4 else None

# toolkit name -> XbSymbolDatabase name, where they differ
ALIAS = {
    "D3D_pDevice": "D3D_g_pDevice",
    "D3D_RenderState": "D3D_g_RenderState",
    "D3D_DeferredTextureState": "D3D_g_DeferredTextureState",
    "D3D_Streams": "D3D_g_Stream",
    "XPP_GamepadType": "g_DeviceType_Gamepad",
    "XPP_MemoryUnitType": "g_DeviceType_MU",
    "D3D_MakeRequestedSpace": "D3D_MakeRequestedSpace_8",
    "IDirectSoundBuffer_SetMixBinVolumes": "IDirectSoundBuffer_SetMixBinVolumes_8",
    "CDirectSoundStream_SetMixBinVolumes": "CDirectSoundStream_SetMixBinVolumes_8",
    "IDirectSoundStream_SetMixBinVolumes": "IDirectSoundStream_SetMixBinVolumes_8",
}

xs = {}
for line in open(xsdb_path):
    m = re.match(r"(\w+?)__(\w+) = 0x([0-9a-f]+)", line.strip())
    if m: xs[m.group(2)] = int(m.group(3), 16)

def read_manual(p):
    out = {}
    if not p: return out
    for line in open(p):
        m = re.match(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", line)
        if m: out[m.group(1)] = int(m.group(2), 0)
    return out
manual = read_manual(manual_path)

lines = open(cfg_path).read().splitlines()
sec = None; out = []; stats = dict(kept=0, filled=0, conflict=0, manual=0, zero=0); zeros = []
for line in lines:
    m = re.match(r"^\[(\w+)\]", line)
    if m: sec = m.group(1)
    m = re.match(r"^(\w+)(\s*=\s*)(0x[0-9A-Fa-f]+|\d+)(.*)$", line)
    if m and sec in ("symbols", "data"):
        name, eq, val, rest = m.groups(); a = int(val, 0)
        x = xs.get(ALIAS.get(name, name))
        src = "toolkit"
        if name in manual:
            new = manual[name]; src = "manual"; stats["manual"] += 1
        elif x is not None and a == 0:
            new = x; src = "xbsymdb"; stats["filled"] += 1
        elif x is not None and x != a:
            print(f"conflict {name}: toolkit {a:#x}, XbSymbolDatabase {x:#x} -> using XbSymbolDatabase")
            new = x; src = "xbsymdb"; stats["conflict"] += 1
        else:
            new = a; stats["kept"] += 1 if a else 0
        if new == 0: stats["zero"] += 1; zeros.append(name)
        tag = "" if src == "toolkit" else f"  # {src}"
        line = f"{name}{eq}{new:#08X}".replace("0X", "0x") + rest + tag if new else f"{name}{eq}0{rest}"
    out.append(line)
# manual names the toolkit's list lacks go at the end of their section
seen = set()
for line in out:
    m = re.match(r"^(\w+)\s*=", line)
    if m: seen.add(m.group(1))
def read_manual_sections(p):
    secs = {}; cur = None
    if not p: return secs
    for line in open(p):
        m = re.match(r"^\[(\w+)\]", line)
        if m: cur = m.group(1); continue
        m = re.match(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", line)
        if m and cur: secs.setdefault(cur, []).append((m.group(1), int(m.group(2), 0)))
    return secs
extra = read_manual_sections(manual_path)
for secname, items in extra.items():
    new = [f"{n:<28} = {v:#08X}  # manual (added)".replace("0X", "0x") for n, v in items if n not in seen]
    if not new: continue
    idx = None
    for k, line in enumerate(out):
        if line.strip() == f"[{secname}]": idx = k
    if idx is None: out += ["", f"[{secname}]"] + new; continue
    end = idx + 1
    while end < len(out) and not out[end].startswith("["): end += 1
    while end > idx + 1 and not out[end - 1].strip(): end -= 1
    out[end:end] = new
    stats["manual"] += len(new)
open(out_path, "w").write("\n".join(out) + "\n")
print(stats)
print("still 0:", " ".join(zeros))
