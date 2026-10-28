import numpy as np
from skel import skel
def glob(o):
    n,M,par=skel(o)
    G=[None]*n
    for i in range(n):
        p=par[i]-1
        G[i]=M[i][0] if p<0 else M[i][0]@G[p]
    return n,par,np.array([g[3,:3] for g in G]),G
nh,ph,Ph,Gh=glob(0x91a000)
ng,pg,Pg,Gg=glob(0xc71000)
print('hero extent',Ph.min(0).round(2),Ph.max(0).round(2))
print('gand extent',Pg.min(0).round(2),Pg.max(0).round(2))
for i in range(nh):
    d=np.linalg.norm(Pg-Ph[i],axis=1); j=d.argmin()
    print('h%2d(p%2d) %s -> g%2d(p%2d) %s d=%.2f'%(i,ph[i]-1,Ph[i].round(1),j,pg[j]-1,Pg[j].round(1),d[j]))
