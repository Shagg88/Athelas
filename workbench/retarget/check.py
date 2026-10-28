import numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
P=np.load('pose.npy',allow_pickle=True).item(); B=np.load('bind.npy',allow_pickle=True).item()
fig,axs=plt.subplots(1,4,figsize=(36,14))
for k,(w,o) in enumerate((('player',0x91a000),('gandalf',0xc71000))):
    n,par,Pb,Bm=B[o]
    J=[]
    for i in range(n):
        v=P[w][i+1]; t=v[:3]; R=v[3:].reshape(3,3)  # rows m0..m2, m4..m6, m8..m10
        J.append(Pb[i]@R+t)
    J=np.array(J)
    for c,(a,b) in enumerate(((0,2),(1,2))):
        ax=axs[k*2+c]
        for i in range(n):
            p=par[i]-1
            if p>=0: ax.plot([J[i,a],J[p,a]],[J[i,b],J[p,b]],'b-')
            ax.text(J[i,a],J[i,b],str(i),fontsize=11,color='r')
        ax.set_aspect('equal'); ax.set_title(w)
plt.savefig('check.png',dpi=35)
