# junkdark.py <set>...: undrawn textures that are less than 10% non-black in colour (some formats have no transparency), unless their transparency holds a picture (grass, shadows) (memory the scan
# read as a texture: buffers that change between scans). Prints counts; with --drop removes them from the set.
import csv, os, sys, numpy as np
from PIL import Image
drop = '--drop' in sys.argv
for s in [a for a in sys.argv[1:] if a != '--drop']:
    rows = list(csv.DictReader(open(f'{s}/index.csv'))); keep = []; bad = 0
    for r in rows:
        if r['drawn'] == '0':
            a = np.asarray(Image.open(f"{s}/textures/{r['name']}").convert('RGBA')).astype(int)
            al = a[..., 3]
            if (a[..., :3].max(2) >= 12).mean() < 0.1 and not (al.std() > 20 and (al >= 12).mean() > 0.1):
                bad += 1
                if drop: os.remove(f"{s}/textures/{r['name']}")
                continue
        keep.append(r)
    if drop:
        with open(f'{s}/index.csv', 'w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(keep)
    print(f'{s}: {bad} of {len(rows)} dark scan junk{" dropped" if drop else ""}')
