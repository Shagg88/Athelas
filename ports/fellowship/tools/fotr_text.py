#!/usr/bin/env python3
"""fotr_text.py <dialog\\*_English.bin>: the game's text table as "key<TAB>text" lines (UTF-8, "\\n" for a new line).
Layout: u32 1, u32 n, n names (u16 length, the name, then a u32 index except after the last), u32 flags, u32 m, then m
entries in index order: u16 length, UTF-16 text, u16 length, the sound's name, u32 colour, u32, u32 k, k colour
runs (u32 c, then c stops of a u32 character position and a u32 colour: highlighted words)."""
import struct, sys

def read(fn):
    d = open(fn, 'rb').read()
    p = 4
    n = struct.unpack_from('<I', d, p)[0]; p += 4
    names = {}
    order = []
    for i in range(n):
        l = struct.unpack_from('<H', d, p)[0]; p += 2
        nm = d[p:p + l].decode('latin1'); p += l
        order.append(nm)
        if i < n - 1:
            names[struct.unpack_from('<I', d, p)[0]] = nm; p += 4
    p += 4
    m = struct.unpack_from('<I', d, p)[0]; p += 4
    texts = []
    for i in range(m):
        l = struct.unpack_from('<H', d, p)[0]; p += 2
        t = d[p:p + 2 * l].decode('utf-16-le'); p += 2 * l
        l2 = struct.unpack_from('<H', d, p)[0]; p += 2 + l2
        k = struct.unpack_from('<I', d, p + 8)[0]; p += 12
        for _ in range(k):  # a colour run: c stops of (u32 position, u32 colour)
            c = struct.unpack_from('<I', d, p)[0]; p += 4 + 8 * c
        texts.append(t)
    if p != len(d): raise SystemExit(f'{fn}: {len(d) - p} bytes left over')
    # the last name has no index: the one index 0..m-1 not named
    missing = [i for i in range(m) if i not in names]
    if len(missing) == 1: names[missing[0]] = order[-1]
    return [(names.get(i, f'#{i}'), t) for i, t in enumerate(texts)]

if __name__ == '__main__':
    for key, t in read(sys.argv[1]):
        print(key + '\t' + t.replace('\r\n', '\\n').replace('\n', '\\n').replace('\r', '\\n'))
