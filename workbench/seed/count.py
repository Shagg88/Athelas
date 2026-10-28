import sys,struct
d=open(sys.argv[1],'rb').read(); i=4; n=0
while i<len(d):
    for _ in range(3):
        l=struct.unpack_from('<I',d,i)[0]; i+=4+l
    n+=1
print(n)
