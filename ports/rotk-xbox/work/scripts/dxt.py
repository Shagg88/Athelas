import numpy as np
def c565(c):
    r=((c>>11)&31)*255//31; g=((c>>5)&63)*255//63; b=(c&31)*255//31
    return np.stack([r,g,b],-1).astype(np.int32)
def dxt1(data,w,h):
    n=(w//4)*(h//4); a=np.frombuffer(data[:n*8],np.uint8).reshape(n,8)
    c0=a[:,0].astype(np.int32)|a[:,1].astype(np.int32)<<8; c1=a[:,2].astype(np.int32)|a[:,3].astype(np.int32)<<8
    p0=c565(c0); p1=c565(c1)
    p2=np.where((c0>c1)[:,None],(2*p0+p1)//3,(p0+p1)//2); p3=np.where((c0>c1)[:,None],(p0+2*p1)//3,0)
    pal=np.stack([p0,p1,p2,p3],1)  # n,4,3
    bits=a[:,4].astype(np.uint32)|a[:,5].astype(np.uint32)<<8|a[:,6].astype(np.uint32)<<16|a[:,7].astype(np.uint32)<<24
    idx=np.stack([(bits>>(2*i))&3 for i in range(16)],1)  # n,16
    px=np.take_along_axis(pal,idx[:,:,None].astype(np.int64).repeat(3,2),1)  # n,16,3
    px=px.reshape(h//4,w//4,4,4,3).transpose(0,2,1,3,4).reshape(h,w,3)
    return px.astype(np.uint8)
