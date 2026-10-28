import numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
from skel import skel
def glob(o,variant):
    n,M,par=skel(o)
    G=[None]*n
    for i in range(n):
        p=par[i]-1
        L=M[i][variant[0]]
        if variant[1]: L=np.linalg.inv(L)
        G[i]=L if p<0 else (L@G[p] if variant[2]==0 else G[p]@L)
    return n,par,np.array([g[3,:3] if variant[2]==0 else g[3,:3] for g in G])
vs=[(0,0,1),(1,0,0),(1,0,1),(0,1,0)]
fig,axs=plt.subplots(2,4,figsize=(28,16))
for c,v in enumerate(vs):
  for r,o in enumerate((0x91a000,0xc71000)):
    n,par,P=glob(o,v); ax=axs[r][c]
    for i in range(n):
        p=par[i]-1
        if p>=0: ax.plot([P[i,0],P[p,0]],[P[i,2],P[p,2]],'b-')
        ax.text(P[i,0],P[i,2],str(i),fontsize=8,color='r')
    ax.set_aspect('equal'); ax.set_title(str(v))
plt.savefig('skels2.png',dpi=45)
