# review_page.py: the side-by-side review page for the proposed replacements (matches2.csv, look >= LOOK and
# pattern >= PAT): one HTML file with the images inside it, opened from the PC. Each card: the game's texture and its
# proposed replacement (recoloured to the game's colours), both repeated 2 x 2 as the game draws them. Clicking a card
# rejects it; "Copy my choices" copies the rejected numbers to paste to Claude. Choices are remembered in the browser.
# Also writes review_ids.csv (number, game, name, library, repeat) so the numbers can be turned back into textures.
import base64, csv, html, io, warnings; warnings.filterwarnings('ignore')
from PIL import Image
from recolor import recolor
LOOK, PAT = 0.86, 0.70
P = {'rotk': 'rxpack', 'tt': 'ttpack', 'fotr': 'fxpack'}
NAMES = {'rotk': 'The Return of the King', 'tt': 'The Two Towers', 'fotr': 'The Fellowship of the Ring'}
def tile(im, k=2):
    w, h = im.size; t = Image.new('RGB', (k * w, k * h))
    for x in range(k):
        for y in range(k): t.paste(im, (x * w, y * h))
    return t
def b64(im):
    b = io.BytesIO(); im.save(b, 'JPEG', quality=82); return base64.b64encode(b.getvalue()).decode()

rows = [r for r in csv.DictReader(open('matches2.csv')) if float(r['look']) >= LOOK and float(r['pattern']) >= PAT]
order = {'rotk': 0, 'tt': 1, 'fotr': 2}
rows.sort(key=lambda r: (order[r['game']], -float(r['score'])))
cards = {g: [] for g in P}
with open('review_ids.csv', 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['number', 'game', 'name', 'library', 'repeat_x', 'repeat_y', 'score'])
    for i, r in enumerate(rows, 1):
        o = Image.open(f"{P[r['game']]}/textures/{r['name']}").convert('RGB')
        k = int(r['repeat']); ow, oh = o.size; rx, ry = (k * max(1, round(ow / oh)), k) if ow >= oh else (k, k * max(1, round(oh / ow)))
        L = Image.open('lib/thumbs/' + r['library']).convert('RGB'); T = Image.new('RGB', (rx * 256, ry * 256))  # the library texture repeated
        for x in range(rx):                                                                                   # to the game texture's shape
            for y in range(ry): T.paste(L.resize((256, 256)), (x * 256, y * 256))
        sz = (110, max(1, round(110 * oh / ow))) if ow >= oh else (max(1, round(110 * ow / oh)), 110)
        a = tile(o.resize(sz, Image.BICUBIC)); b = tile(recolor(T.resize(sz, Image.LANCZOS), o.resize(sz)))
        lib = r['library'].split('_', 1)[1].rsplit('.', 1)[0]; src = 'Poly Haven' if r['library'].startswith('ph_') else 'ambientCG'
        cards[r['game']].append(f'<div class="card" data-n="{i}"><div class="pair"><img src="data:image/jpeg;base64,{b64(a)}" alt="original">'
                                f'<img src="data:image/jpeg;base64,{b64(b)}" alt="replacement"></div><div class="cap"><b>#{i}</b> '
                                f'{html.escape(r["size"])} · {html.escape(lib)} <span>({src})</span></div><div class="x">✕ rejected</div></div>')
        w.writerow([i, r['game'], r['name'], r['library'], rx, ry, r['score']])
sections = ''.join(f'<section id="{g}"><h2>{NAMES[g]} <small>{len(cards[g])} pairs</small></h2><div class="grid">{"".join(cards[g])}</div></section>' for g in P if cards[g])
page = f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Texture Swap Review</title><style>
:root{{--bg:#16171a;--card:#202227;--ink:#e8e6e1;--dim:#9a9890;--accent:#c9a54a;--bad:#c4473a}}
*{{box-sizing:border-box}} body{{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,Segoe UI,sans-serif}}
header{{position:sticky;top:0;z-index:2;background:#111214ee;backdrop-filter:blur(6px);padding:12px 16px;border-bottom:1px solid #2c2e33;display:flex;flex-wrap:wrap;gap:10px 18px;align-items:center}}
h1{{font-size:18px;margin:0;color:var(--accent);font-weight:600}} header p{{margin:0;color:var(--dim);flex:1 1 260px;min-width:0}} nav{{display:flex;flex-wrap:wrap;gap:4px 0}}
nav a{{color:var(--ink);margin-right:12px;text-decoration:none;border-bottom:1px dotted var(--dim)}}
button{{background:var(--accent);color:#16171a;border:0;border-radius:6px;padding:8px 14px;font-weight:600;cursor:pointer}}
#count{{color:var(--dim)}} main{{padding:8px 16px 60px}} h2{{font-size:17px;font-weight:600;margin:26px 0 10px}} h2 small{{color:var(--dim);font-weight:400}}
.grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(300px,100%),1fr));gap:12px}}
.card{{min-width:0;background:var(--card);border-radius:8px;padding:8px;cursor:pointer;position:relative;border:2px solid transparent;user-select:none}}
.card:hover{{border-color:#3a3d44}} .pair{{display:grid;grid-template-columns:1fr 1fr;gap:6px}} .pair img{{width:100%;aspect-ratio:1;object-fit:contain;background:#16171a;display:block;border-radius:4px;image-rendering:auto}}
.cap{{margin-top:6px;font-size:13px;color:var(--dim);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}} .cap b{{color:var(--ink)}} .cap span{{opacity:.7}}
.x{{display:none;position:absolute;inset:8px 8px 30px;background:#000a;color:#fff;font-weight:700;font-size:20px;align-items:center;justify-content:center;border-radius:4px;border:2px solid var(--bad)}}
.card.no{{border-color:var(--bad)}} .card.no .x{{display:flex}} .legend{{color:var(--dim);font-size:13px;margin:6px 0 0}}
#done{{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#2b2d33;padding:8px 14px;border-radius:6px;display:none}}
</style></head><body><header><h1>Texture swap review</h1>
<p>Left: the game's texture. Right: the free replacement, recoloured to match. Click any pair that looks wrong. Then press Copy and paste it to Claude.</p>
<nav>{''.join(f'<a href="#{g}">{NAMES[g].replace("The ","")}</a>' for g in P if cards[g])}</nav>
<span id="count"></span><button id="copy">Copy my choices</button></header>
<main><p class="legend">{len(rows)} pairs, each shown repeated 2 × 2 the way the game tiles it. Nothing is changed in any game until you approve.</p>{sections}</main>
<div id="done">Copied — paste it to Claude.</div>
<script>
const K='texture-review-rejects',total={len(rows)};let no=new Set();
try{{no=new Set(JSON.parse(localStorage.getItem(K)||'[]'))}}catch(e){{}}
const save=()=>{{try{{localStorage.setItem(K,JSON.stringify([...no]))}}catch(e){{}};document.getElementById('count').textContent=no.size+' rejected of '+total}};
document.querySelectorAll('.card').forEach(c=>{{const n=+c.dataset.n;if(no.has(n))c.classList.add('no');
 c.onclick=()=>{{no.has(n)?no.delete(n):no.add(n);c.classList.toggle('no');save()}}}});
document.getElementById('copy').onclick=async()=>{{const t='Texture review: rejected '+(no.size?[...no].sort((a,b)=>a-b).join(', '):'none')+' (of '+total+')';
 try{{await navigator.clipboard.writeText(t)}}catch(e){{prompt('Copy this:',t)}};const d=document.getElementById('done');d.style.display='block';setTimeout(()=>d.style.display='none',2500)}};
save();
</script></body></html>'''
open('texture_review.html', 'w').write(page)
print(len(rows), {g: len(c) for g, c in cards.items()})
