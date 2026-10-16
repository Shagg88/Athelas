#!/usr/bin/env python3
"""pcx.py <hexVA>...: code refs to addresses in ROTK PC rotk.exe (read only), with enclosing context."""
import sys,struct
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
P='/mnt/user-data/uploads/ROTK-Test/rotk.exe'
d=open(P,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; ns=struct.unpack_from('<H',d,pe+6)[0]; osz=struct.unpack_from('<H',d,pe+20)[0]
base=struct.unpack_from('<I',d,pe+24+28)[0]; secs=[]
for i in range(ns):
    o=pe+24+osz+40*i; n=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ra=struct.unpack_from('<IIII',d,o+8)
    secs.append((n,base+va,vs,ra,rs))
def va2off(v):
    for n,va,vs,ra,rs in secs:
        if va<=v<va+rs: return ra+v-va
md=Cs(CS_ARCH_X86,CS_MODE_32)
text=[s for s in secs if s[0]=='.text'][0]
def refs(a):
    p=struct.pack('<I',a); _,tva,_,tra,trs=text; i=d.find(p,tra,tra+trs); out=[]
    while i!=-1:
        va=tva+i-tra
        for back in range(1,9):
            ins=list(md.disasm(d[i-back:i-back+16],va-back,1))
            if ins and ins[0].address+ins[0].size>=va+4 and ('%x'%a) in ins[0].op_str: out.append((ins[0].address,ins[0].mnemonic+' '+ins[0].op_str)); break
        i=d.find(p,i+1,tra+trs)
    return out
if __name__=='__main__':
    for a in sys.argv[1:]:
        for va,t in refs(int(a,16)): print(a,hex(va),t)
