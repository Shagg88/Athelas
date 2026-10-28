import sys,glob
from PIL import Image
fs=sorted(glob.glob(sys.argv[1]+'/ref_*.tga'))[::int(sys.argv[3]) if len(sys.argv)>3 else 1]
ims=[]
for f in fs:
    try: ims.append(Image.open(f).convert("RGB").resize((320,240)))
    except Exception: pass
#('RGB').resize((320,240)) for f in fs]
cols=8;rows=(len(ims)+cols-1)//cols
S=Image.new('RGB',(cols*320,rows*240))
for i,im in enumerate(ims): S.paste(im,((i%cols)*320,(i//cols)*240))
S.save(sys.argv[2]); print(fs[0],fs[-1],len(fs))
