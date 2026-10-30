# prep_dump.py <dump dir> <out dir>: a Windows texture dump (textures\dump\<w>x<h>_<hash>.png, texture_dump = true)
# as Real-ESRGAN inputs, the way prep.py prepares the harness packs: padded 16 px (wrapped for textures that tile,
# edge pixels repeated otherwise), colours under clear pixels filled from their neighbours, transparency as its own
# grey picture, and manifest.csv for texfinish.exe. Runs on the PC (Python + numpy + Pillow). Private: game art.
# A texture "tiles" (kind surface) when its opposite edges continue each other about as well as its inner rows do.
import csv, os, sys
import numpy as np
from PIL import Image

PAD = 16


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


def tiles(rgb):
    h, w = rgb.shape[:2]
    if h < 16 or w < 16 or (w & (w - 1)) or (h & (h - 1)): return False
    inner_x = np.abs(np.diff(rgb, axis=1)).mean(); inner_y = np.abs(np.diff(rgb, axis=0)).mean()
    seam_x = np.abs(rgb[:, 0] - rgb[:, -1]).mean(); seam_y = np.abs(rgb[0] - rgb[-1]).mean()
    return seam_x <= 2.0 * inner_x + 2 and seam_y <= 2.0 * inner_y + 2


src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
names = sorted(n for n in os.listdir(src) if n.lower().endswith('.png') and '_' in n and 'x' in n.split('_')[0])
w = csv.writer(open(f'{out}/manifest.csv', 'w', newline='')); w.writerow(['name', 'w', 'h', 'pad', 'kind', 'alpha'])
n_alpha = n_surface = 0
for i, name in enumerate(names):
    im = np.asarray(Image.open(f'{src}/{name}').convert('RGBA')).astype(np.float32)
    rgb, a = im[..., :3], im[..., 3]
    has_a = a.min() < 250
    kind = 'surface' if tiles(rgb) else 'other'
    n_surface += kind == 'surface'
    mode = 'wrap' if kind == 'surface' else 'edge'
    if has_a:
        if (a > 5).any(): rgb = bleed(rgb, a > 5)
        ap = np.pad(a, PAD, mode=mode)
        Image.fromarray(ap.round().clip(0, 255).astype(np.uint8), 'L').convert('RGB').save(f'{out}/{name[:-4]}_a.png', compress_level=6)
        n_alpha += 1
    rp = np.pad(rgb, ((PAD, PAD), (PAD, PAD), (0, 0)), mode=mode)
    Image.fromarray(rp.round().clip(0, 255).astype(np.uint8), 'RGB').save(f'{out}/{name}', compress_level=6)
    w.writerow([name, im.shape[1], im.shape[0], PAD, kind, int(has_a)])
    if (i + 1) % 200 == 0: print(f'  {i + 1} of {len(names)} prepared', flush=True)
print(f'{len(names)} textures, {n_alpha} with transparency, {n_surface} tiling', flush=True)
