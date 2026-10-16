import sys,struct,json,bisect; sys.path.insert(0,'/home/claude/rotkx/tools')
from xbe_info import XBE
from capstone import *
x=XBE('/home/claude/rotkx/assets_local/default.xbe'); d=x.d; md=Cs(CS_ARCH_X86,CS_MODE_32)
fs=sorted(json.load(open('/home/claude/rotkx/analysis/cfg/func_starts.json')))
o0=x.va2off(0x11000); end=x.va2off(0x11d400)
for a in [int(v,16) for v in sys.argv[1:]]:
    p=struct.pack("<I",a); i=d.find(p,o0,end)
    while i!=-1:
        va=0x11000+(i-o0); best=None
        for back in range(1,8):
            ins=list(md.disasm(d[i-back:i-back+15],va-back,1))
            if ins and ins[0].address+ins[0].size>=va+4 and hex(a) in ins[0].op_str: best=ins[0]
        j=bisect.bisect_right(fs,va)-1
        if best: print(hex(a),hex(best.address),best.mnemonic,best.op_str,"fn",hex(fs[j]))
        i=d.find(p,i+1,end)
