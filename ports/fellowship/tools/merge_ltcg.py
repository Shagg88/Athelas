#!/usr/bin/env python3
"""Merge XbSymbolDatabase results into an xbe_symbols config for a title built with LTCG libraries.

usage: merge_ltcg.py <xbe_symbols config.toml> <XbSymbolDatabaseCLI output> <out.toml> [manual.toml]

XbSymbolDatabase names an LTCG function with its convention: Name_<stack bytes popped>__LTCG_<reg><arg>...,
e.g. D3DDevice_SetTexture_4__LTCG_eax1 (argument 1 in eax, argument 2 on the stack, 4 bytes popped). The name is
matched without the suffix, and an [abi] line is written giving each argument's place in order. Only addresses and
conventions are written: no game bytes."""
import re, sys
sys.path.insert(0, __import__("os").path.dirname(__file__))

cfg_path, xsdb_path, out_path = sys.argv[1:4]
manual_path = sys.argv[4] if len(sys.argv) > 4 else None
ALIAS = {
    "D3D_pDevice": "D3D_g_pDevice", "D3D_RenderState": "D3D_g_RenderState",
    "D3D_DeferredTextureState": "D3D_g_DeferredTextureState", "D3D_Streams": "D3D_g_Stream",
    "XPP_GamepadType": "g_DeviceType_Gamepad", "XPP_MemoryUnitType": "g_DeviceType_MU",
}
xs, abi = {}, {}
for line in open(xsdb_path):
    m = re.match(r"(\w+?)__(\w+) = 0x([0-9a-f]+)", line.strip())
    if not m: continue
    name, addr = m.group(2), int(m.group(3), 16)
    lt = re.match(r"(\w+?)_(\d+)__LTCG_((?:[a-z]{3}\d+_?)+)$", name)
    if lt:
        base, pops, regs = lt.group(1), int(lt.group(2)), re.findall(r"([a-z]{3})(\d+)", lt.group(3))
        n = pops // 4 + len(regs)
        place = ["stack"] * n
        for r, i in regs:
            if int(i) <= n: place[int(i) - 1] = r
        xs[base] = addr
        abi[base] = place
        continue
    st = re.match(r"(\w+?)_(\d+)$", name)  # Name_<bytes>: stdcall size, same convention
    if st and st.group(1) not in xs: xs.setdefault(st.group(1), addr)
    xs.setdefault(name, addr)
    # "__r2" and similar revisions
    rv = re.match(r"(\w+?)__r\d+$", name)
    if rv: xs.setdefault(rv.group(1), addr)

def read_manual(p):
    out = {}
    if not p: return out
    for line in open(p):
        m = re.match(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", line)
        if m: out[m.group(1)] = int(m.group(2), 0)
    return out
manual = read_manual(manual_path)
sec = None; out = []; used_abi = {}; st = dict(kept=0, filled=0, conflict=0, manual=0, zero=0); zeros = []
for line in open(cfg_path).read().splitlines():
    m = re.match(r"^\[(\w+)\]", line)
    if m: sec = m.group(1)
    if sec == "abi": continue  # xbe_symbols' guesses ("?"): replaced by XbSymbolDatabase's conventions below
    m = re.match(r"^(\w+)(\s*=\s*)(0x[0-9A-Fa-f]+|\d+)(.*)$", line)
    if m and sec in ("symbols", "data"):
        name, eq, val, rest = m.groups(); a = int(val, 0)
        key = ALIAS.get(name, name); x = xs.get(key)
        src = "toolkit"
        if name in manual: new = manual[name]; src = "manual"; st["manual"] += 1
        elif x is not None and a == 0: new = x; src = "xbsymdb"; st["filled"] += 1
        elif x is not None and x != a:
            print(f"conflict {name}: toolkit {a:#x}, XbSymbolDatabase {x:#x} -> XbSymbolDatabase"); new = x; src = "xbsymdb"; st["conflict"] += 1
        else: new = a; st["kept"] += 1 if a else 0
        if new == 0: st["zero"] += 1; zeros.append(name)
        if key in abi and new == xs.get(key): used_abi[name] = abi[key]
        tag = "" if src == "toolkit" else f"  # {src}"
        line = (f"{name}{eq}{new:#08X}".replace("0X", "0x") + rest + tag) if new else f"{name}{eq}0{rest}"
    out.append(line)
# manual names the lists lack: at the end of their section
def manual_sections(p):
    secs = {}; cur = None
    if not p: return secs
    for line in open(p):
        m = re.match(r"^\[(\w+)\]", line)
        if m: cur = m.group(1); continue
        m = re.match(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", line)
        if m and cur: secs.setdefault(cur, []).append((m.group(1), int(m.group(2), 0)))
    return secs
seen = {re.match(r"^(\w+)\s*=", l).group(1) for l in out if re.match(r"^(\w+)\s*=", l)}
for sname, items in manual_sections(manual_path).items():
    extra = [f"{k} = {v:#08X}".replace("0X", "0x") + "  # manual" for k, v in items if k not in seen]
    if not extra: continue
    idx = next(i for i, l in enumerate(out) if l.strip() == f"[{sname}]")
    end = idx + 1
    while end < len(out) and not out[end].startswith("["): end += 1
    while end > idx + 1 and out[end - 1].strip() == "": end -= 1
    out[end:end] = extra
# names the SDK's host code knows beyond xbe_symbols' list (its "#ifndef SYM_<name>" defaults), filled from
# XbSymbolDatabase when it has them
import glob, os
sdk_dir = os.environ.get("SDK_DIR")
if sdk_dir:
    known = set()
    for f in glob.glob(os.path.join(sdk_dir, "runtime", "*", "*.cpp")):
        known.update(re.findall(r"^#ifndef SYM_(\w+)", open(f).read(), re.M))
    seen = {re.match(r"^(\w+)\s*=", l).group(1) for l in out if re.match(r"^(\w+)\s*=", l)}
    extra = [f"{k} = {xs[k]:#08X}".replace("0X", "0x") + "  # xbsymdb (a name the SDK knows)" for k in sorted(known)
             if k in xs and k not in seen]
    if extra:
        idx = next(i for i, l in enumerate(out) if l.strip() == "[symbols]")
        end = idx + 1
        while end < len(out) and not out[end].startswith("["): end += 1
        while end > idx + 1 and out[end - 1].strip() == "": end -= 1
        out[end:end] = extra
        for k in sorted(known):
            if k in xs and k in abi and k not in seen: used_abi[k] = abi[k]
# manual [abi] lines (name = ["reg", "stack", ...]) win
if manual_path:
    cur = None
    for line in open(manual_path):
        m = re.match(r"^\[(\w+)\]", line)
        if m: cur = m.group(1); continue
        m = re.match(r'^\s*(\w+)\s*=\s*\[(.*?)\]', line)
        if m and cur == "abi": used_abi[m.group(1)] = [x.strip().strip('"') for x in m.group(2).split(",")]
out.append("")
out.append("# where LTCG library functions take their arguments (XbSymbolDatabase's names), each argument in order")
out.append("[abi]")
for k, v in sorted(used_abi.items()):
    out.append(f"{k:<50} = [{', '.join(chr(34) + p + chr(34) for p in v)}]")
open(out_path, "w").write("\n".join(out) + "\n")
print(st, "abi", len(used_abi))
print("still 0:", " ".join(zeros))
