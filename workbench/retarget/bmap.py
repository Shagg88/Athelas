import numpy as np, matplotlib, sys
matplotlib.use('Agg'); import matplotlib.pyplot as plt
from skel import skel
def bind(o):
    n,M,par=skel(o); return n,[p-1 for p in par],np.array([M[i][1][3,:3] for i in range(n)])
nh,ph,Ph=bind(0x91a000); nb,pb,Pb=bind(0xae6000)
fig,axs=plt.subplots(2,2,figsize=(26,30))
for r,(n,par,P) in enumerate(((nh,ph,Ph),(nb,pb,Pb))):
    for c,(a,b) in enumerate(((0,2),(1,2))):
        ax=axs[r][c]
        for i in range(n):
            if par[i]>=0: ax.plot([P[i,a],P[par[i],a]],[P[i,b],P[par[i],b]],'b-')
            ax.text(P[i,a],P[i,b],str(i),fontsize=11,color='r')
        ax.set_aspect('equal')
plt.savefig('boromir.png',dpi=40)
for g in range(nb):
    d=np.linalg.norm(Ph-Pb[g],axis=1); o=np.argsort(d)
    print('b%2d par%3d %-22s near h%2d(par%2d) %.2f  h%2d %.2f'%(g,pb[g],str(Pb[g].round(2)),o[0],ph[o[0]],d[o[0]],o[1],d[o[1]]))
