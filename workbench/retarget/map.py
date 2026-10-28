import numpy as np
B=np.load('bind.npy',allow_pickle=True).item()
nh,ph,Ph,_=B[0x91a000]; ng,pg,Pg,_=B[0xc71000]
for g in range(ng):
    d=np.linalg.norm(Ph-Pg[g],axis=1); o=np.argsort(d)
    print('g%2d par%3d %-20s near h%2d %.2f  h%2d %.2f  h%2d %.2f'%(g,pg[g]-1,str(Pg[g].round(2)),o[0],d[o[0]],o[1],d[o[1]],o[2],d[o[2]]))
print()
for h in range(nh): print('h%2d par%3d %s'%(h,ph[h]-1,Ph[h].round(2)))
