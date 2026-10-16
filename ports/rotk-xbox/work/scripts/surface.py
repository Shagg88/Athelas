# Library call surface: E8 calls from game code into library code, named from config + XbSymbolDatabase.
import sys,struct,re,json,collections
sys.path.insert(0,'/home/claude/rotkx/tools')
from xbe_info import XBE
xbe,cfg,syms,starts=sys.argv[1:5]
x=XBE(xbe); d=x.d
names={}
for l in open(syms):
    m=re.match(r"(\w+?)__(\w+) = 0x([0-9a-f]+)",l.strip())
    if m: names[int(m.group(3),16)]=m.group(2)
sec=None
for l in open(cfg):
    m=re.match(r"^\[(\w+)\]",l)
    if m: sec=m.group(1)
    m=re.match(r"^(\w+)\s*=\s*(0x[0-9A-Fa-f]+)",l)
    if m and sec=="symbols" and int(m.group(2),16): names.setdefault(int(m.group(2),16),m.group(1))
fs=set(json.load(open(starts)))
glo,ghi=int(sys.argv[5],16),int(sys.argv[6],16)   # game code range
libs={}
for s in x.sections:
    if s["name"] in ("D3D","DSOUND","XGRPH","XPP","D3DX"): libs[s["name"]]=(s["va"],s["va"]+s["vsize"])
libs["XAPI/LIBC"]=(ghi,0x12a2a0)
def lib(t):
    for n,(a,b) in libs.items():
        if a<=t<b: return n
c=collections.defaultdict(list)
for va in range(glo,ghi-5):
    o=x.va2off(va)
    if o is not None and d[o]==0xE8:
        t=(va+5+struct.unpack('<i',d[o+1:o+5])[0])&0xffffffff
        L=lib(t)
        if L and t in fs: c[t].append(va)
rows=sorted(c.items(),key=lambda kv:(lib(kv[0]),kv[0]))
for t,v in rows: print(f"{lib(t):10} {t:#08x} {names.get(t,'?'):45} {len(v):4}  {' '.join(hex(a) for a in v[:3])}")
