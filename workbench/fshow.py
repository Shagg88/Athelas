import pickle,bisect,re,sys
sys.path.insert(0,'/home/claude/work/fotr/re'); import xbe
ins=pickle.load(open('/home/claude/work/fins.pkl','rb')); fs=pickle.load(open('/home/claude/work/ffs.pkl','rb')); ks=sorted(ins)
def fn(a): return fs[bisect.bisect_right(fs,a)-1]
def show(lo,hi):
    for a in ks[bisect.bisect_left(ks,lo):bisect.bisect_left(ks,hi)]:
        s=ins[a]; m=re.search(r'\[(0x[0-9A-F]{8})\]',s); x=''
        if m and s.startswith('f'):
            try:x='   ; %g'%xbe.f32(int(m.group(1),16))
            except: pass
        print(hex(a),s,x)
def callers(t): return [(hex(a),hex(fn(a))) for a,s in ins.items() if re.search(r'(call|j\w+) 0x%08X$'%t,s)]
if __name__=='__main__':
    lo=int(sys.argv[1],16); hi=int(sys.argv[2],16) if len(sys.argv)>2 else None
    if hi is None:
        i=bisect.bisect_right(fs,lo); hi=fs[i] if i<len(fs) else lo+0x400
    print('callers',callers(lo)[:10]); show(lo,hi)
