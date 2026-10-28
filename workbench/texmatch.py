# texmatch.py: proposes CC0 library replacements for the games' repeating surfaces (kind=surface in each pack's
# index.csv), with OpenCLIP (ViT-B-32, LAION-400M). A proposal needs: the same material class for both (zero-shot,
# from a fixed list), a high image similarity, and a clear margin over the next library texture. Textures under 64 px,
# or whose transparency holds a shape (cutouts: grass, fences), are left to upscaling.
# Out: matches.csv (game, name, size, class, best library file, its class, similarity, margin, verdict) and emb.npz.
import csv, os, sys, numpy as np, torch, open_clip
from PIL import Image
torch.set_num_threads(2)
W = '/home/claude/work'
CLASSES = ['brick wall', 'stone block wall', 'cobblestone', 'paving stones', 'rough rock', 'cliff rock face',
           'dirt ground', 'mud', 'grass', 'sand', 'gravel', 'snow', 'ice', 'moss', 'forest floor with leaves',
           'wooden planks', 'wood grain', 'tree bark', 'roof tiles', 'thatched straw roof', 'plaster wall',
           'concrete', 'marble', 'floor tiles', 'rusty metal', 'metal plate', 'lava', 'water', 'fabric', 'sky']
m, _, pre = open_clip.create_model_and_transforms('ViT-B-32-quickgelu', pretrained=f'{W}/models/vit_b_32_laion400m.pt')
m.eval(); tok = open_clip.get_tokenizer('ViT-B-32-quickgelu')
with torch.no_grad():
    T = m.encode_text(tok([f'a seamless texture of {c}' for c in CLASSES])); T /= T.norm(dim=-1, keepdim=True)

def embed(paths, prep):
    out = []
    for i in range(0, len(paths), 32):
        x = torch.stack([pre(prep(p)) for p in paths[i:i + 32]])
        with torch.no_grad():
            e = m.encode_image(x); out.append((e / e.norm(dim=-1, keepdim=True)).numpy())
        print(f'  {min(i + 32, len(paths))}/{len(paths)}', file=sys.stderr, flush=True)
    return np.concatenate(out)

def lib_img(p): return Image.open(p).convert('RGB')
def game_img(p):  # a 2 x 2 tile of the texture, so the model sees it as a surface the way the game repeats it
    im = Image.open(p).convert('RGB'); w, h = im.size; t = Image.new('RGB', (2 * w, 2 * h))
    for x in (0, w):
        for y in (0, h): t.paste(im, (x, y))
    return t

lib = sorted(os.listdir(f'{W}/lib/thumbs'))
if os.path.exists(f'{W}/lib/emb.npz') and list(np.load(f'{W}/lib/emb.npz')['names']) == lib:
    L = np.load(f'{W}/lib/emb.npz')['emb']
else:
    print('library', file=sys.stderr); L = embed([f'{W}/lib/thumbs/{n}' for n in lib], lib_img)
    np.savez(f'{W}/lib/emb.npz', emb=L, names=np.array(lib))
Lc = (L @ T.numpy().T).argmax(1)

games = []
for g, pack in (('rotk', 'rxpack'), ('tt', 'ttpack'), ('fotr', 'fxpack')):
    for r in csv.DictReader(open(f'{W}/{pack}/index.csv')):
        if r['kind'] != 'surface' or min(int(r['width']), int(r['height'])) < 64: continue
        a = np.asarray(Image.open(f"{W}/{pack}/textures/{r['name']}").convert('RGBA'))[..., 3]
        if a.std() > 20 and (a < 128).mean() > 0.05: continue  # a cutout: its shape is in the transparency
        games.append((g, pack, r))
print('game surfaces', len(games), file=sys.stderr)
G = embed([f"{W}/{p}/textures/{r['name']}" for g, p, r in games], game_img)
Gc = (G @ T.numpy().T).argmax(1)
S = G @ L.T
with open(f'{W}/matches.csv', 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['game', 'name', 'size', 'class', 'library', 'lib_class', 'sim', 'margin', 'verdict'])
    for i, (g, p, r) in enumerate(games):
        same = np.where(Lc == Gc[i])[0]
        if len(same) == 0:
            w.writerow([g, r['name'], f"{r['width']}x{r['height']}", CLASSES[Gc[i]], '', '', 0, 0, 'no library texture of this kind']); continue
        o = same[np.argsort(-S[i, same])]; best = o[0]; sim = S[i, best]; margin = sim - (S[i, o[1]] if len(o) > 1 else 0)
        verdict = 'propose' if sim >= 0.80 else 'maybe' if sim >= 0.72 else 'upscale'
        w.writerow([g, r['name'], f"{r['width']}x{r['height']}", CLASSES[Gc[i]], lib[best], CLASSES[Lc[best]], round(float(sim), 3), round(float(margin), 3), verdict])
print('done', file=sys.stderr)
