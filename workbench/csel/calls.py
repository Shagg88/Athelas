import sys,struct
sys.argv=[sys.argv[0]]+sys.argv[1:]
exec(open('xd.py').read().split('md=capstone')[0])
tgts=[int(x,16) for x in sys.argv[1:]]
for va,vs,ra,rs in secs:
    b=d[ra:ra+rs]
    for i in range(len(b)-5):
        if b[i]==0xE8:
            t=(va+i+5+struct.unpack_from('<i',b,i+1)[0])&0xffffffff
            if t in tgts: print('%x -> %x'%(va+i,t))
