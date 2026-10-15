#!/usr/bin/env python3
"""cheats.py: decode the cheat-code table at 0x178138 in default.xbe (98 x 8 bytes:
4 code bytes, u16 group, u16 value). Matcher 0xF9460: on game page 0xD05, both triggers held
(logical 0x08|0x20), four presses. Press -> code byte: A=1 B=2 Y=4 X=8 Up=0x10 Down=0x20."""
import sys,struct; sys.path.insert(0,'/home/claude/rotkx/work/hidden')
from xref import x,d
BTN={1:'A',2:'B',4:'Y',8:'X',0x10:'Up',0x20:'Down',0x40:'other'}
PS2={1:'cross',2:'circle',4:'tri',8:'square',0x10:'up',0x20:'down'}
GROUP={0:'char +1000 XP (0xF8AD0)',1:'char 0xF8B60(1)',2:'char 0xF8B60(2)',3:'char 0xF8B60(3)',4:'char 0xF8B60(4)',
       5:'char 0xF8C90',6:'char 0xF8F60 (sets 0x1DA24C bits)',7:'char 0xF8D20',8:'char 0xF8D20',9:'char 0xF8E50',10:'global effect 0xF9030'}
EFF={0:'Invulnerable: toggle player[+0x634] bit 4 (damage skipped at 0x2A1ED)',1:'no-op (case returns)',
     2:'All Upgrades: buys abilities 0..0x24 (0x9DAC0)',3:'Restore Health (0x294F0, max health)',
     4:'co-op only: toggle global 0x1DA24C bit 0x40 (read 0x52EF0)',5:'Infinite Missiles: player bit 8 (ammo check 0x6220F)',
     6:'Always Devastating: player bit 0x10 (attack type 9 at 0x7C305)',7:'player bit 0x20 (Perfect Mode, by elimination)',
     8:'global bit 0x200 (projectile flag at 0x36950; Kinetic Missiles?)',9:'co-op only: global bit 0x80 (read 0x2A0F0)',
     10:'global bit 0x100 (read 0x587C9)',11:'global bit 0x10 set, no unlock check (HUD 0x104FD0; Targeting Indicator?)'}
o=x.va2off(0x178138); i=0
while True:
    b=d[o+8*i:o+8*i+8]
    if b==b'\0'*8: break
    g,v=struct.unpack_from('<HH',b,4)
    seq=' '.join(BTN[c] for c in b[:4]); ps=' '.join(PS2[c] for c in b[:4])
    what=EFF.get(v,'?') if g==10 else f'{GROUP[g]} char id 0x{v:02x}'
    print(f'0x{0x178138+8*i:06X}  hold LT+RT, {seq:16s} (PS2 icons: {ps:28s}) group {g:2d} value 0x{v:02x}  {what}')
    i+=1
