#!/usr/bin/env python3
"""callers.py <hexVA>...: E8/E9 rel32 sites targeting each address, with containing function."""
import sys; sys.path.insert(0,'/home/claude/rotkx/work/hidden'); from xref import callers,fn_of
for a in sys.argv[1:]:
    t=int(a,16)
    for c in callers(t): print(f'{t:08x} <- {c:08x} fn {fn_of(c):08x}')
