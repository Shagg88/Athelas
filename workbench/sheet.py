# sheet.py <lo> <hi> <n> <out>: random pairs from matches2.csv scored in [lo, hi): original | recoloured replacement,
# each tiled 2 x 2 as the game repeats them
import csv, random, sys, warnings; warnings.filterwarnings('ignore')
from PIL import Image, ImageDraw
from recolor import recolor
P = {'rotk': 'rxpack', 'tt': 'ttpack', 'fotr': 'fxpack'}
lo, hi, n, out = float(sys.argv[1]), float(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
def tile(im, k=2):
    w, h = im.size; t = Image.new('RGB', (k * w, k * h))
    for x in range(k):
        for y in range(k): t.paste(im, (x * w, y * h))
    return t
r = [x for x in csv.DictReader(open('matches2.csv')) if lo <= float(x['score']) < hi]
random.Random(3).shuffle(r); r = r[:n]
S = Image.new('RGB', (2 * 170 * 2 + 20, ((len(r) + 1) // 2) * 170), (25, 25, 25)); d = ImageDraw.Draw(S)
for i, x in enumerate(r):
    X, Y = (i % 2) * 350, (i // 2) * 170
    o = Image.open(f"{P[x['game']]}/textures/{x['name']}").convert('RGB').resize((80, 80), Image.BICUBIC)
    L = tile(Image.open('lib/thumbs/' + x['library']).convert('RGB'), int(x['repeat'])).resize((80, 80), Image.BICUBIC)
    S.paste(tile(o), (X, Y)); S.paste(tile(recolor(L, o)), (X + 165, Y))
    d.text((X + 3, Y + 150), f"{x['score']} L{x['look']} P{x['pattern']}", fill=(255, 255, 0))
S.save(out); print(len(r))
