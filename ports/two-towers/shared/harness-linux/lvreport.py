# Summarises lvsweep.sh runs: level loaded, frames, movies, stalls, warnings and crashes per level.
import re,glob,os,sys
rows=[]
for d in sorted(glob.glob(os.environ.get("WORK","/home/claude/tt")+"/runs/lv2_*/")):
    L=d.rstrip("/").split("lv2_")[1]
    logs=sorted(glob.glob(d+"1/logs/*.log"))
    if not logs: rows.append((L,"no log")); continue
    t=open(logs[-1],errors="replace").read()
    scene = re.search(r"open d:\\worlds\\%s\.bfa"%L, t, re.I) is not None
    swapped = ("-> %s"%L) in t
    missing=[m for m in re.findall(r"missing (\S+)",t) if "mont.bik" not in m]
    movie=re.findall(r"tt: movie (\S+)",t)
    ended=re.findall(r"tt: movie (\S+) ended",t)
    fr=[(float(a),int(b),int(c)) for a,b,c in re.findall(r"\[\s*([\d.]+)\] \[info\] \[\w+\] renderer: frame (\d+), (\d+) draws",t)]
    last=fr[-1] if fr else (0,0,0)
    # stalls: per-second frame delta 0, or draws per frame <= 2 over a 1 s window after gameplay start
    stall=[]; low=[]
    for (t0,f0,d0),(t1,f1,d1) in zip(fr,fr[1:]):
        if f1==f0: stall.append(t1)
        elif t1>20 and (d1-d0)/(f1-f0)<=2: low.append(t1)
    warns=[l for l in t.splitlines() if "[warn]" in l or "[error]" in l]
    crash = "Exit (" in t and "Exit (0)" not in t or "exception" in t.lower()
    rows.append((L,scene,swapped,missing,movie,ended,last,stall,low,warns,crash))
for r in rows:
    if len(r)==2: print(r); continue
    L,scene,sw,miss,mov,end,last,stall,low,warns,crash=r
    print(f"{L}: scene={scene} jump={sw} last frame {last[1]} draws {last[2]} at {last[0]:.0f}s | movies {mov} ended {end} | missing {miss[:3]} | stall secs {stall[:5]} ({len(stall)}) low-draw secs {low[:5]} ({len(low)}) | warn/err {len(warns)} crash {crash}")
    for w in sorted(set(re.sub(r'^\[\s*[\d.]+\] ','',x) for x in warns))[:6]: print("    ",w[:160])
