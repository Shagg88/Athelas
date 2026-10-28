# merge.py OUT [IN]: pipelines.bin keys from IN added to OUT (deduped); prints OUT's count
import sys,os
out=sys.argv[1]; HDR=8
def keys(p):
    if not os.path.exists(p): return None,[]
    d=open(p,'rb').read()
    if len(d)<HDR or d[:4]!=b'APL1': return None,[]
    ks=int.from_bytes(d[4:8],'little'); return d[:HDR],[d[i:i+ks] for i in range(HDR,len(d)-ks+1,ks)]
h,have=keys(out)
if len(sys.argv)>2:
    h2,new=keys(sys.argv[2])
    if h2:
        h=h or h2; s=set(have)
        for k in new:
            if k not in s: s.add(k); have.append(k)
        open(out,'wb').write(h+b''.join(have))
print(len(have))
