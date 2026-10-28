import numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
import match
fig,axs=plt.subplots(2,2,figsize=(22,22))
for row,(n,par,P) in enumerate(((match.nh,match.ph,match.Ph),(match.ng,match.pg,match.Pg))):
    for col,(a,b) in enumerate(((0,2),(1,2))):
        ax=axs[row][col]
        for i in range(n):
            p=par[i]-1
            if p>=0: ax.plot([P[i,a],P[p,a]],[P[i,b],P[p,b]],'b-')
            ax.text(P[i,a],P[i,b],str(i),fontsize=9,color='r')
        ax.set_aspect('equal'); ax.set_title(('hero','gandalf')[row]+' axes %d,%d'%(a,b))
plt.savefig('skels.png',dpi=60)
