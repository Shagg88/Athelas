import numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
B=np.load('bind.npy',allow_pickle=True).item(); P=np.load('pose.npy',allow_pickle=True).item()
nh,ph,Ph,_=B[0x91a000]; ng,pg,Pg,_=B[0xc71000]
MAP={0:0,1:1,5:2,6:3,7:6,8:4,9:5,14:9,15:10,20:11,21:12,22:13,27:14,28:8,29:15,30:7,32:16,33:17,34:18,
 35:21,36:22,38:24,39:23,41:25,42:26,43:27,44:28,45:29,46:30,47:31,49:33,50:32,52:34,53:35,54:36,55:37,56:38,57:39,59:40}
R={};T={}
for h in range(nh):
    v=P['player'][h+1]; T[h]=v[:3]; R[h]=v[3:].reshape(3,3)
Jh={h:Ph[h]@R[h]+T[h] for h in range(nh)}
Rg={};Jg={}
for g in range(ng):
    p=pg[g]-1
    if g in MAP: Rg[g]=R[MAP[g]]
    else: Rg[g]=Rg[p] if p>=0 else np.eye(3)
    if p<0 or g in (0,1): Jg[g]=Jh[MAP.get(g,0)]
    else: Jg[g]=Jg[p]+(Pg[g]-Pg[p])@Rg[p]
fig,axs=plt.subplots(1,4,figsize=(36,14))
for k,(J,par,n) in enumerate(((Jh,ph,nh),(Jg,pg,ng))):
    for c,(a,b) in enumerate(((0,2),(1,2))):
        ax=axs[k*2+c]
        for i in range(n):
            p=par[i]-1
            if p>=0: ax.plot([J[i][a],J[p][a]],[J[i][b],J[p][b]],'b-')
            ax.text(J[i][a],J[i][b],str(i),fontsize=11,color='r')
        ax.set_aspect('equal')
plt.savefig('rt.png',dpi=35)
