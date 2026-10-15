import tex,dxt,numpy as np,glob
from PIL import Image
s=open('/home/claude/rotkx/work/fonts/pc_fro.bin','rb').read()
T={t['name']:t for t in tex.tables(s) if t['table']==0x811e8}
def pc(n):
    t=T[n]; st=t['blob']+0x18+t['off']; return dxt.dxt1(s[st:st+t['w']*t['h']//2],1024,1024)
def xb(f):
    a=np.frombuffer(open(f,'rb').read(),np.uint8).reshape(448,1024).astype(float)
    Y=a[:,0::2]; U=a[:,1::4].repeat(2,1); V=a[:,3::4].repeat(2,1)
    return np.clip(np.stack([Y+1.402*(V-128),Y-0.344*(U-128)-0.714*(V-128),Y+1.772*(U-128)],-1),0,255)
def sample(img,X,Y):
    h,w=img.shape[:2]; X=np.clip(X,0,w-1.001); Y=np.clip(Y,0,h-1.001)
    x0=X.astype(int); y0=Y.astype(int); fx=(X-x0)[...,None]; fy=(Y-y0)[...,None]
    return (img[y0,x0]*(1-fx)+img[y0,x0+1]*fx)*(1-fy)+(img[y0+1,x0]*(1-fx)+img[y0+1,x0+1]*fx)*fy
def err(x,p,a,b,c,d,step=8):
    ys,xs=np.mgrid[4:448:step,4:512:step].astype(float)
    q=sample(p,a*xs+b,c*ys+d); return np.abs(q-x[ys.astype(int),xs.astype(int)]).mean()
def register(x,p):
    best=(1e9,)
    for a in np.arange(1.6,2.6,0.05):
        for c in np.arange(1.8,2.8,0.05):
            for b in np.arange(-100,160,20):
                for d in np.arange(-100,160,20):
                    e=err(x,p,a,b,c,d,16)
                    if e<best[0]: best=(e,a,b,c,d)
    e,a,b,c,d=best
    for st in (5,2,1,0.5):
        for _ in range(3):
            for da,db,dc,dd in [(0.01*st,0,0,0),(-0.01*st,0,0,0),(0,st,0,0),(0,-st,0,0),(0,0,0.01*st,0),(0,0,-0.01*st,0),(0,0,0,st),(0,0,0,-st)]:
                e2=err(x,p,a+da,b+db,c+dc,d+dd,4)
                if e2<e: e,a,b,c,d=e2,a+da,b+db,c+dc,d+dd
    return e,a,b,c,d
