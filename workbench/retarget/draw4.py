import numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
from skel import skel
fig,axs=plt.subplots(2,2,figsize=(26,30))
S={}
for r,o in enumerate((0x91a000,0xc71000)):
    n,M,par=skel(o); P=np.array([M[i][1][3,:3] for i in range(n)]); S[o]=(n,par,P,[M[i][1] for i in range(n)])
    for c,(a,b) in enumerate(((0,2),(1,2))):
        ax=axs[r][c]
        for i in range(n):
            p=par[i]-1
            if p>=0: ax.plot([P[i,a],P[p,a]],[P[i,b],P[p,b]],'b-')
            ax.text(P[i,a],P[i,b],str(i),fontsize=11,color='r')
        ax.set_aspect('equal')
plt.savefig('bind.png',dpi=40)
np.save('bind.npy',{k:(v[0],v[1],v[2],v[3]) for k,v in S.items()},allow_pickle=True)
