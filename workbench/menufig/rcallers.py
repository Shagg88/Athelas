import re,glob,bisect,sys,pickle,os
cache='/home/claude/work/menufig/rins.pkl'
if os.path.exists(cache): fs,ins=pickle.load(open(cache,'rb'))
else:
    os.chdir('/home/claude/rotkx-port/rotkxbox/generated/game')
    fs=sorted(set(int(x,16) for x in re.findall(r'\{0x([0-9A-F]+)u, sub_', open('x86_func_mapping.cpp').read())))
    ins={}
    for f in glob.glob('x86_recomp.*.cpp'):
        for l in open(f):
            m=re.match(r'\s*// 000([0-9A-F]{5}) (.*)',l)
            if m: ins[int(m.group(1),16)]=m.group(2).rstrip()
    pickle.dump((fs,ins),open(cache,'wb'))
def func_of(a): return fs[bisect.bisect_right(fs,a)-1]
