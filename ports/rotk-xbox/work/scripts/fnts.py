import struct,sys
def fonts(path):
    d=open(path,'rb').read(); out=[]; i=0
    while True:
        i=d.find(b'FNTS',i)
        if i<0: break
        size=struct.unpack_from('<I',d,i+4)[0]
        cnt=struct.unpack_from('<H',d,i+0xa)[0]; lh=d[i+0x13]
        rec=struct.unpack_from('<I',d,i+0x14)[0]; tex=struct.unpack_from('<I',d,i+0x1c)[0]
        out.append(dict(off=i,size=size,count=cnt,line=lh,rec=i+rec,tex=i+tex,d=d))
        i+=4
    return out
if __name__=='__main__':
    for f in fonts(sys.argv[1]):
        d=f['d']; t=f['tex']
        print(hex(f['off']),'size',hex(f['size']),'glyphs',f['count'],'line',f['line'],'texhdr',d[t:t+16].hex())
        for k in range(3):
            print('  ',d[f['rec']+k*12:f['rec']+k*12+12].hex())

def atlas(f):
    from PIL import Image
    d=f['d']; t=f['tex']
    w,h=struct.unpack_from('<HH',d,t+4)
    px=d[t+16:t+16+w*h//2]
    im=Image.new('L',(w,h)); a=bytearray(w*h)
    for k,b in enumerate(px):
        if 2*k+1>=len(a): break
        a[2*k]=(b&15)*17; a[2*k+1]=(b>>4)*17
    im.frombytes(bytes(a)); return im
def glyphs(f):
    d=f['d']; g={}
    for k in range(f['count']):
        code,w,h,x,y,adv,xo,yo,pad=struct.unpack_from('<HBBHHBbbB',d,f['rec']+k*12)
        g[code]=(w,h,x,y,adv,xo,yo)
    return g
