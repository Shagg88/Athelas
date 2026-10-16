#!/usr/bin/env python3
"""xref.py <hexVA>... : find code (and data-table) references to addresses in default.xbe.
Code refs: any instruction in .text whose encoding contains the 4-byte address as imm/disp.
Data refs: dword pointers in .rdata/.data. Function start = nearest preceding E8 call target."""
import sys,struct,bisect,os,pickle
sys.path.insert(0,'/home/claude/rotkx/tools')
from xbe_info import XBE
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
XBEP='/home/claude/rotkx/gamedata/default.xbe'
x=XBE(XBEP); d=x.d; md=Cs(CS_ARCH_X86,CS_MODE_32)
T0=0x11000; TS=0x11929c; o0=x.va2off(T0)
cache=os.path.join(os.path.dirname(os.path.abspath(__file__)),'fstarts.pkl')
if os.path.exists(cache): fs=pickle.load(open(cache,'rb'))
else:
    s=set()
    for i in range(o0,o0+TS-5):
        if d[i]==0xE8:
            t=T0+(i-o0)+5+struct.unpack_from('<i',d,i+1)[0]
            if T0<=t<T0+TS: s.add(t)
    fs=sorted(s); pickle.dump(fs,open(cache,'wb'))
def fn_of(va):
    j=bisect.bisect_right(fs,va)-1; return fs[j] if j>=0 else 0
def refs(a,data=True):
    out=[]; p=struct.pack('<I',a); i=d.find(p,o0,o0+TS)
    while i!=-1:
        va=T0+(i-o0); best=None
        for back in range(1,9):
            ins=list(md.disasm(d[i-back:i-back+16],va-back,1))
            if ins and ins[0].address+ins[0].size>=va+4 and ins[0].address<=va: 
                if ('%x'%a) in ins[0].op_str: best=ins[0]
        if best: out.append(('code',best.address,f'{best.mnemonic} {best.op_str}',fn_of(best.address)))
        i=d.find(p,i+1,o0+TS)
    if data:
        for s in x.sections:
            if s['name'] in ('.rdata','.data'):
                raw=d[s['raw']:s['raw']+s['rsize']]; k=raw.find(p)
                while k!=-1:
                    if k%4==0: out.append(('data',s['va']+k,'',0))
                    k=raw.find(p,k+1)
    return out
if __name__=='__main__':
    for a in sys.argv[1:]:
        a=int(a,16)
        for kind,va,txt,f in refs(a): print(f'{a:08x} {kind} {va:08x} {txt}' + (f'  fn {f:08x}' if f else ''))
def callers(t):
    out=[]
    for i in range(o0,o0+TS-5):
        if d[i] in (0xE8,0xE9):
            if T0+(i-o0)+5+struct.unpack_from('<i',d,i+1)[0]==t: out.append(T0+(i-o0))
    return out
