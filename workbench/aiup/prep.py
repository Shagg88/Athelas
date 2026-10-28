# prep.py <out dir>: every dumped texture of the three Xbox games as an input for Real-ESRGAN on the PC.
#   <out>/<GAME>/<name>.png      colour, padded 16 px (wrapped for repeating surfaces, edge pixels repeated otherwise),
#                                colours under clear pixels filled from their opaque neighbours first
#   <out>/<GAME>/<stem>_a.png    transparency as grey, padded the same way (only when the texture has any)
#   <out>/<GAME>/manifest.csv    name,w,h,pad,kind,alpha
# Private: made from the games' own art.
import csv, os, sys
import numpy as np
from PIL import Image

PAD = 16
PACKS = {'ROTK-Xbox': 'rxpack', 'FOTR-Xbox': 'fxpack', 'TT-Xbox': 'ttpack'}


def bleed(rgb, known):
    rgb = rgb.copy()
    for _ in range(64):
        if known.all(): break
        acc = np.zeros_like(rgb); cnt = np.zeros(known.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            k = np.roll(known, (dy, dx), (0, 1)); acc += np.roll(rgb, (dy, dx), (0, 1)) * k[..., None]; cnt += k
        new = (~known) & (cnt > 0)
        rgb[new] = acc[new] / cnt[new][:, None]; known = known | new
    return rgb


out = sys.argv[1]
for game, pack in PACKS.items():
    d = f'{out}/{game}'; os.makedirs(d, exist_ok=True)
    rows = list(csv.DictReader(open(f'/home/claude/work/{pack}/index.csv')))
    man = open(f'{d}/manifest.csv', 'w', newline=''); w = csv.writer(man); w.writerow(['name', 'w', 'h', 'pad', 'kind', 'alpha'])
    n_alpha = 0
    for r in rows:
        im = np.asarray(Image.open(f"/home/claude/work/{pack}/textures/{r['name']}").convert('RGBA')).astype(np.float32)
        rgb, a = im[..., :3], im[..., 3]
        has_a = a.min() < 250
        mode = 'wrap' if r['kind'] == 'surface' else 'edge'
        if has_a:
            if (a > 5).any(): rgb = bleed(rgb, a > 5)
            ap = np.pad(a, PAD, mode=mode)
            Image.fromarray(ap.round().clip(0, 255).astype(np.uint8), 'L').convert('RGB').save(f"{d}/{r['name'][:-4]}_a.png", optimize=False, compress_level=6)
            n_alpha += 1
        rp = np.pad(rgb, ((PAD, PAD), (PAD, PAD), (0, 0)), mode=mode)
        Image.fromarray(rp.round().clip(0, 255).astype(np.uint8), 'RGB').save(f"{d}/{r['name']}", compress_level=6)
        w.writerow([r['name'], r['width'], r['height'], PAD, r['kind'], int(has_a)])
    man.close()
    print(game, len(rows), 'textures,', n_alpha, 'with transparency', flush=True)
