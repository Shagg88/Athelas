#!/usr/bin/env python3
"""unpack.py <in.scx> <out.bin>: join SHOC payloads into the logical stream (shoc.stream)."""
import sys; sys.path.insert(0,'/home/claude/rotkx/work/scripts'); import shoc
open(sys.argv[2],'wb').write(shoc.stream(open(sys.argv[1],'rb').read()))
