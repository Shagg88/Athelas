import struct, numpy as np
from PIL import Image
def fonts_from(s):
    out=[];i=0
    while True:
        i=s.find(b'FNTS',i)
        if i<0: break
        f=dict(line=s[i+0x13],count=struct.unpack_from('<H',s,i+0xa)[0])
        rec=i+struct.unpack_from('<I',s,i+0x14)[0]; t=i+struct.unpack_from('<I',s,i+0x1c)[0]
        W,H=struct.unpack_from('<HH',s,t+4); raw=np.frombuffer(s[t+16:t+16+W*H//2],np.uint8)
        a=np.zeros(W*H,np.uint8); a[0:2*raw.size:2]=raw&15; a[1:2*raw.size:2]=raw>>4
        f['atlas']=a.reshape(H,W); f['W']=W; f['H']=H; g={}
        for k in range(f['count']):
            code,w,h,x,y,adv,xo,yo,_=struct.unpack_from('<HBBHHBbbB',s,rec+k*12); g[code]=(w,h,x,y,adv,xo,yo)
        f['g']=g; out.append(f); i+=4
    return out
def bbox(m):
    ys,xs=np.nonzero(m>3)
    if len(xs)==0: return None
    return xs.min(),ys.min(),xs.max()+1,ys.max()+1
def bilinear(img,xs,ys):
    h,w=img.shape; x0=np.clip(np.floor(xs).astype(int),0,w-1); y0=np.clip(np.floor(ys).astype(int),0,h-1)
    x1=np.clip(x0+1,0,w-1); y1=np.clip(y0+1,0,h-1); fx=np.clip(xs-x0,0,1); fy=np.clip(ys-y0,0,1)
    v=img[y0,x0]*(1-fx)*(1-fy)+img[y0,x1]*fx*(1-fy)+img[y1,x0]*(1-fx)*fy+img[y1,x1]*fx*fy
    inside=(xs>=-0.5)&(xs<=w-0.5)&(ys>=-0.5)&(ys<=h-0.5)
    return np.where(inside,v,0)
def upscale(xb,pc,K):
    """xb's atlas at K times, each glyph redrawn from pc's glyph (ink box onto ink box); 0..15 floats"""
    out=np.kron(xb['atlas'].astype(np.float32),np.ones((K,K),np.float32))  # fallback: nearest
    out=np.array(Image.fromarray(xb['atlas'].astype(np.float32)).resize((xb['W']*K,xb['H']*K),Image.BILINEAR))
    for code,(w,h,x,y,adv,xo,yo) in xb['g'].items():
        if code not in pc['g'] or w<2 or h<2: continue
        pw,ph,px,py,*_=pc['g'][code]
        if pw<2 or ph<2: continue
        xg=xb['atlas'][y:y+h,x:x+w]; pg=pc['atlas'][py:py+ph,px:px+pw].astype(np.float32)
        bx=bbox(xg); bp=bbox(pg)
        if not bx or not bp: continue
        # dest pixels of the cell at K
        u=(np.arange(w*K)+0.5)/K; v=(np.arange(h*K)+0.5)/K   # xbox cell coords
        sx=(bp[2]-bp[0])/(bx[2]-bx[0]); sy=(bp[3]-bp[1])/(bx[3]-bx[1])
        X=bp[0]+(u-bx[0])*sx-0.5; Y=bp[1]+(v-bx[1])*sy-0.5
        XX,YY=np.meshgrid(X,Y)
        out[y*K:(y+h)*K, x*K:(x+w)*K]=bilinear(pg,XX,YY)
    return out
