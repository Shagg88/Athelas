import struct, numpy as np
D=open('/mnt/user-data/uploads/TT--extracted/Worlds/02gate2.bfa','rb').read()
def skel(o):
    h=o+0x40
    n,mo,po,bo,co=struct.unpack_from('<5I',D,h+8)
    M=[]
    for i in range(n):
        a=np.array(struct.unpack_from('<16f',D,h+mo+i*0x80)).reshape(4,4)
        b=np.array(struct.unpack_from('<16f',D,h+mo+i*0x80+0x40)).reshape(4,4)
        M.append((a,b))
    par=list(D[h+po:h+po+n])
    return n,M,par
if __name__=='__main__':
    for o in (0x91a000,0xc71000):
        n,M,par=skel(o)
        print(hex(o),n,par)
        for i in range(n):
            a,b=M[i]
            try: ib=np.linalg.inv(b)
            except: ib=b
            print(i,par[i],'A',np.round(a[3,:3],3),'B^-1',np.round(ib[3,:3],3), 'Bdiag',np.round(np.diag(b),2))
