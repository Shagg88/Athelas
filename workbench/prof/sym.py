# sym.py OUT BIN [top]: samples per function (nm symbols of BIN), grouped
import sys,subprocess,bisect,collections,re
out,binp=sys.argv[1],sys.argv[2]; top=int(sys.argv[3]) if len(sys.argv)>3 else 40
pcs=[];maps=[]
for l in open(out):
    if l.startswith('MAP '):
        p=l.split(); a,b=[int(x,16) for x in p[1].split('-')]; off=int(p[3],16); maps.append((a,b,off,p[-1] if len(p)>6 else ''))
    else: pc,c=l.split(); pcs.append((int(pc,16),int(c)))
syms=[]
for l in subprocess.run(['nm','-C','--defined-only','-n',binp],capture_output=True,text=True).stdout.splitlines():
    p=l.split(' ',2)
    if len(p)==3 and p[1] in 'tTwW': syms.append((int(p[0],16),p[2]))
addrs=[s[0] for s in syms]
tot=sum(c for _,c in pcs); agg=collections.Counter(); lib=collections.Counter()
for pc,c in pcs:
    m=[x for x in maps if x[0]<=pc<x[1]]
    if not m: agg['?']+=c; continue
    a,b,off,path=m[0]
    if not path.endswith(binp.split('/')[-1]): lib[path.split('/')[-1]]+=c; agg['[lib] '+path.split('/')[-1]]+=c; continue
    rel=pc-a+off
    # PIE: nm addresses are file vaddrs; text segment mapped with offset
    i=bisect.bisect_right(addrs,rel)-1
    name=syms[i][1] if i>=0 else '?'
    agg[name]+=c
print('samples',tot)
grp=collections.Counter()
for k,c in agg.items():
    g='game code (recompiled)' if re.match(r'sub_[0-9A-F]{8}|__imp__sub_',k) else k
    grp[g]+=c
for k,c in grp.most_common(top): print(f'{100*c/tot:6.2f}%  {k[:150]}')
