#!/usr/bin/env python3
"""check_lang.py <en_all.txt> <lang.txt>...: each translation has every key in order, no empty text, and the same
<...> tags, % placeholders and \\n count as the English line."""
import re, sys
src = [l.rstrip('\n').split('\t', 1) for l in open(sys.argv[1], encoding='utf-8')]
for fn in sys.argv[2:]:
    lines = [l.rstrip('\n') for l in open(fn, encoding='utf-8') if l.strip() and not l.startswith('#')]
    bad = []
    if len(lines) != len(src): bad.append(f'{len(lines)} lines, {len(src)} expected')
    for (k, e), l in zip(src, lines):
        if '\t' not in l: bad.append(f'no tab: {l[:40]}'); continue
        lk, t = l.split('\t', 1)
        if lk != k: bad.append(f'key {lk} for {k}'); break
        if not t.strip() and e.strip(): bad.append(f'{k}: empty')
        if sorted(re.findall(r'<[^>]*>', t)) != sorted(re.findall(r'<[^>]*>', e)): bad.append(f'{k}: tags')
        if re.findall(r'%[-0-9]*[a-z%]', t) != re.findall(r'%[-0-9]*[a-z%]', e): bad.append(f'{k}: placeholders')
    print(fn, 'ok' if not bad else f'{len(bad)} problems: ' + '; '.join(bad[:5]))
