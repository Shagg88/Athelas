# texmerge.py <runs root> <out dir>: merge per-level TEXPACK dumps into one set (no duplicates, junk dropped)
import os, sys, shutil, numpy as np
from PIL import Image
root, out = sys.argv[1], sys.argv[2]
pool = sys.argv[3] if len(sys.argv) > 3 else None
os.makedirs(out + '/textures', exist_ok=True)
info = {}
for L in sorted(os.listdir(root)):
    idx = f'{root}/{L}/1/cap/pack/index.txt'
    if not os.path.exists(idx): continue
    for l in open(idx):
        p = l.split()
        if p[0] == 'tex':
            e = info.setdefault(p[1], {'fmt': p[2], 'drawn': 0, 'tile': 0, 'levels': set(), 'src': (f'{pool}/{p[1]}' if pool else f'{root}/{L}/1/cap/pack/{p[1]}')})
            e['drawn'] |= int(p[4]); e['levels'].add(L)
        elif p[0] == 'tile' and p[1] in info: info[p[1]]['tile'] = 1
kinds = {'surface': 0, 'other': 0}; junk = 0
with open(f'{out}/index.csv', 'w') as f:
    f.write('name,width,height,format,drawn,tile,kind,levels\n')
    for n, e in sorted(info.items()):
        if not os.path.exists(e['src']): continue
        im = Image.open(e['src']); a = np.asarray(im.convert('RGBA')).astype(float); rgb = a[..., :3]
        h, w = rgb.shape[:2]
        dx = np.abs(np.diff(rgb, axis=1)).mean() if w > 1 else 0; dy = np.abs(np.diff(rgb, axis=0)).mean() if h > 1 else 0
        if not e['drawn'] and (rgb.std() < 2 or dx + dy > 60 or a[..., 3].max() == 0): junk += 1; continue
        seam = 99
        if w >= 8 and h >= 8: seam = (np.abs(rgb[:, 0] - rgb[:, -1]).mean() / (dx + 1e-3) + np.abs(rgb[0] - rgb[-1]).mean() / (dy + 1e-3)) / 2
        kind = 'surface' if e['tile'] and seam < 2.2 else 'other'; kinds[kind] += 1
        shutil.copy(e['src'], f'{out}/textures/{n}')
        f.write(f"{n},{w},{h},{e['fmt']},{e['drawn']},{e['tile']},{kind},{' '.join(sorted(e['levels']))}\n")
print(f'{out}: {len(info)} found, {sum(kinds.values())} kept ({kinds["surface"]} surfaces, {kinds["other"]} other), {junk} junk dropped')
