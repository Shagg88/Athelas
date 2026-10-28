# build_pack.py: the approved library replacements (review_ids.csv) as texture pack files: for each game texture, its
# CC0 library texture repeated to the game texture's shape (repeat_x x repeat_y), at 4x the game texture's size (the
# longer side at most 1024), brought to the game texture's colours (recolor.py), its transparency taken from the game
# texture where it has any, with a full mip chain, as DDS: BC1 when opaque, else BC3. The file name is the game
# texture's dump name with .dds, which the ports' loader matches by its last 16 hex digits.
# Out: packs/<game>/library/*.dds and packs/<game>/library/SOURCES.txt (which library texture each came from).
import csv, os, struct, sys, warnings; warnings.filterwarnings('ignore')
import numpy as np, etcpak
from PIL import Image
from recolor import recolor
W = '/home/claude/work'; FULL = '/mnt/user-data/uploads/Documents/AthelasTextures/AthelasTextures/library/full'
P = {'rotk': 'rxpack', 'tt': 'ttpack', 'fotr': 'fxpack'}; GAME = {'rotk': 'ROTK-Xbox', 'tt': 'TT-Xbox', 'fotr': 'FOTR-Xbox'}

def dds(path, w, h, levels, fourcc):
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000 | (0x20000 if len(levels) > 1 else 0)  # caps height width pf linear mips
    hdr = struct.pack('<4sIIIIIII44x', b'DDS ', 124, flags, h, w, len(levels[0]), 0, len(levels))
    pf = struct.pack('<II4s20x', 32, 0x4, fourcc)
    caps = struct.pack('<IIII4x', 0x1000 | (0x400008 if len(levels) > 1 else 0), 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + pf + caps); [f.write(l) for l in levels]

def mips(im):  # RGBA PIL image -> list of RGBA images, halving to 4 px (block size)
    out = [im]
    while min(out[-1].size) > 4:
        w, h = out[-1].size; out.append(out[-1].resize((max(4, w // 2), max(4, h // 2)), Image.LANCZOS))
    return out

rows = list(csv.DictReader(open(f'{W}/review_ids.csv')))
cache = {}; n = 0; size = 0
for g in GAME: os.makedirs(f'{W}/packs/{GAME[g]}/library', exist_ok=True)
src = {g: [] for g in GAME}
for r in rows:
    o = Image.open(f"{W}/{P[r['game']]}/textures/{r['name']}").convert('RGBA'); ow, oh = o.size
    s = min(4, 1024 / max(ow, oh)); tw, th = int(ow * s), int(oh * s)
    rx, ry = int(r['repeat_x']), int(r['repeat_y'])
    if r['library'] not in cache: cache = {r['library']: Image.open(f"{FULL}/{r['library']}").convert('RGB')}
    L = cache[r['library']]; lw, lh = tw // rx, th // ry
    T = Image.new('RGB', (lw * rx, lh * ry)); t = L.resize((lw, lh), Image.LANCZOS)
    for x in range(rx):
        for y in range(ry): T.paste(t, (x * lw, y * lh))
    T = T.resize((tw, th), Image.LANCZOS)
    img = recolor(T, o.convert('RGB')).convert('RGBA')
    a = np.asarray(o)[..., 3]; opaque = a.min() >= 250
    if not opaque: img.putalpha(o.getchannel('A').resize((tw, th), Image.BICUBIC))
    levels = [(etcpak.compress_bc1 if opaque else etcpak.compress_bc3)(m.tobytes(), m.size[0], m.size[1]) for m in mips(img)]
    out = f"{W}/packs/{GAME[r['game']]}/library/{r['name'][:-4]}.dds"
    dds(out, tw, th, levels, b'DXT1' if opaque else b'DXT5'); size += os.path.getsize(out); n += 1
    src[r['game']].append(f"{r['name'][:-4]}.dds  <-  {r['library']} ({'Poly Haven' if r['library'].startswith('ph_') else 'ambientCG'}, CC0)")
    if n % 50 == 0: print(n, round(size / 1e6), 'MB', file=sys.stderr, flush=True)
for g in GAME:
    with open(f'{W}/packs/{GAME[g]}/library/SOURCES.txt', 'w') as f:
        f.write('Library replacements: free CC0 textures from Poly Haven (polyhaven.com) and ambientCG (ambientcg.com),\n'
                'recoloured to the game\'s own. Delete this folder to go back to the game\'s textures.\n\n' + '\n'.join(src[g]) + '\n')
print(n, 'files', round(size / 1e6), 'MB')
