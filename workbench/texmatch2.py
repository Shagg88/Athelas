# texmatch2.py: second round of texmatch.py. For each game surface, the 12 library textures most like it (OpenCLIP)
# are tried as they are and repeated 2 x 2 (blocks / planks half the size), each brought to the game texture's colours
# (recolor.py), then scored on:
#   look    - OpenCLIP similarity of the game texture and the recoloured candidate, both tiled 2 x 2
#   pattern - how alike their spectra are (log-polar: the size and direction of blocks, stripes, grain), less the
#             library's average spectrum so only what is particular to each counts; 0..1
# score = (look + pattern) / 2; the best candidate is kept. Out: matches2.csv
import csv, sys, numpy as np, torch, open_clip
from PIL import Image
from recolor import recolor
import warnings; warnings.filterwarnings('ignore')
torch.set_num_threads(2)
W = '/home/claude/work'; P = {'rotk': 'rxpack', 'tt': 'ttpack', 'fotr': 'fxpack'}
m, _, pre = open_clip.create_model_and_transforms('ViT-B-32-quickgelu', pretrained=f'{W}/models/vit_b_32_laion400m.pt')
m.eval()
z = np.load(f'{W}/lib/emb.npz'); L = z['emb']; lib = list(z['names'])

def tile(im, k=2):
    w, h = im.size; t = Image.new('RGB', (k * w, k * h))
    for x in range(k):
        for y in range(k): t.paste(im, (x * w, y * h))
    return t
def emb(ims):
    with torch.no_grad():
        e = m.encode_image(torch.stack([pre(i) for i in ims])); return (e / e.norm(dim=-1, keepdim=True)).numpy()
R = np.linspace(0, 1, 13)[1:]; A = np.linspace(0, np.pi, 13)
yy, xx = np.mgrid[-64:64, -64:64]; rad = np.hypot(xx, yy) / 64; ang = np.mod(np.arctan2(yy, xx), np.pi)
def spectrum(im):  # one tile, 128 px grey: log power in 12 radial x 12 angle bins, normalised
    g = np.asarray(im.convert('L').resize((128, 128), Image.BICUBIC), float); g -= g.mean()
    f = np.log1p(np.abs(np.fft.fftshift(np.fft.fft2(g))))
    v = np.zeros((12, 12))
    for i in range(12):
        ri = (rad > (R[i - 1] if i else 0.02)) & (rad <= R[i])
        for j in range(12): v[i, j] = f[ri & (ang >= A[j]) & (ang < A[j + 1])].mean()
    return v.ravel()
def unit(v):
    v = v - MU; v = v - v.mean(); return v / (np.linalg.norm(v) + 1e-9)

import os
MU = np.mean([spectrum(Image.open(f'{W}/lib/thumbs/{n}').convert('RGB')) for n in lib], axis=0)  # the average texture's spectrum
rows = [r for r in csv.DictReader(open(f'{W}/matches.csv')) if r['library']]
thumbs = {}
done = set()  # resumes: textures already in matches2.csv are kept and skipped
if os.path.exists(f'{W}/matches2.csv'): done = {(r['game'], r['name']) for r in csv.DictReader(open(f'{W}/matches2.csv'))}
out = open(f'{W}/matches2.csv', 'a' if done else 'w', newline=''); w = csv.writer(out)
if not done: w.writerow(['game', 'name', 'size', 'class', 'library', 'repeat', 'look', 'pattern', 'score'])
for n, r in enumerate(rows):
    if (r['game'], r['name']) in done: continue
    o = Image.open(f"{W}/{P[r['game']]}/textures/{r['name']}").convert('RGB').resize((128, 128), Image.BICUBIC)
    if np.asarray(o.convert('L'), float).std() < 4: continue  # one flat colour: nothing to match, anything matches
    go = emb([tile(o)])[0]; so = unit(spectrum(o))
    cand = np.argsort(-(L @ go))[:12]; ims, meta = [], []
    for c in cand:
        if lib[c] not in thumbs: thumbs[lib[c]] = Image.open(f'{W}/lib/thumbs/{lib[c]}').convert('RGB').resize((128, 128), Image.BICUBIC)
        for k in (1, 2):
            t = recolor(tile(thumbs[lib[c]], k).resize((128, 128), Image.BICUBIC), o)
            ims.append(tile(t)); meta.append((lib[c], k, unit(spectrum(t))))
    look = emb(ims) @ go
    pat = np.array([(1 + float(s @ so)) / 2 for _, _, s in meta])
    sc = (look + pat) / 2; b = int(sc.argmax())
    w.writerow([r['game'], r['name'], r['size'], r['class'], meta[b][0], meta[b][1], round(float(look[b]), 3), round(float(pat[b]), 3), round(float(sc[b]), 3)])
    if n % 50 == 0: print(f'{n}/{len(rows)}', file=sys.stderr, flush=True); out.flush()
out.close(); print('done', file=sys.stderr)
