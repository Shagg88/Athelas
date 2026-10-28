# fcallers.py ADDR [depth]: Fellowship's call tree upward from a function (direct calls), with each caller's float compares
import pickle,bisect,sys,re
ins=pickle.load(open('/home/claude/work/fins.pkl','rb')); fs=pickle.load(open('/home/claude/work/ffs.pkl','rb'))
def fn(a): return fs[bisect.bisect_right(fs,a)-1]
calls={}
for a,t in ins.items():
    m=re.match(r'call 0x([0-9A-F]{8})$',t)
    if m: calls.setdefault(int(m.group(1),16),[]).append(a)
def fcmp(f):
    i=bisect.bisect_right(fs,f); end=fs[i] if i<len(fs) else f+0x1000
    return [(hex(a),ins[a]) for a in sorted(k for k in ins if f<=k<end) if ins[a].startswith(('fcom','fucom'))]
def up(f,d,seen,ind=''):
    if d<0 or f in seen: return
    seen.add(f); cs=sorted(set(fn(a) for a in calls.get(f,[])))
    c=fcmp(f)
    print(f"{ind}{hex(f)}  callers {len(cs)}  fcmp {len(c)}" + (f"  e.g. {c[:3]}" if c else ''))
    for g in cs[:8]: up(g,d-1,seen,ind+'  ')
up(int(sys.argv[1],16),int(sys.argv[2]) if len(sys.argv)>2 else 3,set())
