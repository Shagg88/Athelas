import numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
from skel import skel
P={'player':{},'gandalf':{}}
for l in open('pose.txt'):
    w,rest=l.split(' ',1); i,v=rest.split(':',1)
    nums=[float(x) for x in v.replace('|',' ').split()]
    P[w][int(i)]=np.array(nums)
par={'player':skel(0x91a000)[2],'gandalf':skel(0xc71000)[2]}
np.save('pose.npy',P,allow_pickle=True)
fig,axs=plt.subplots(2,3,figsize=(30,20))
for r,w in enumerate(('player','gandalf')):
    pts=np.array([P[w][i][:3] for i in range(len(P[w]))])
    for c,(a,b) in enumerate(((0,2),(1,2),(0,1))):
        ax=axs[r][c]
        for i in range(1,len(pts)):
            p=par[w][i-1]
            ax.plot([pts[i,a],pts[p,a]],[pts[i,b],pts[p,b]],'b-')
            ax.text(pts[i,a],pts[i,b],str(i-1),fontsize=10,color='r')
        ax.set_aspect('equal'); ax.set_title(w+str((a,b)))
plt.savefig('pose.png',dpi=40)
