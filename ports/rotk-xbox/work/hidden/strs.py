#!/usr/bin/env python3
"""Dump ASCII and UTF-16LE strings from an XBE with their VAs. usage: strs.py xbe out.tsv"""
import sys,re
sys.path.insert(0,'/home/claude/rotkx/tools')
from xbe_info import XBE
x=XBE(sys.argv[1]); d=x.d
out=open(sys.argv[2],'w')
for s in x.sections:
    raw=d[s['raw']:s['raw']+s['rsize']]
    for m in re.finditer(rb'[\x20-\x7e\t]{4,}',raw):
        out.write(f"{s['va']+m.start():08x}\tA\t{s['name']}\t{m.group().decode()}\n")
    for m in re.finditer(rb'(?:[\x20-\x7e]\x00){4,}',raw):
        out.write(f"{s['va']+m.start():08x}\tW\t{s['name']}\t{m.group().decode('utf-16le')}\n")
