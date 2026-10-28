# review.py <sample.json> <out dir> <page.html>: before/after page for the AI upscale sample (private: game art).
import base64, io, json, sys
from PIL import Image

items = json.load(open(sys.argv[1])); outdir = sys.argv[2]; S = 512


def checker(w, h, n=16):
    im = Image.new('RGB', (w, h), (200, 200, 200))
    for y in range(0, h, n):
        for x in range(0, w, n):
            if (x // n + y // n) % 2: im.paste((150, 150, 150), (x, y, x + n, y + n))
    return im


def flat(im):  # RGBA over a checkerboard, as RGB
    bg = checker(*im.size).convert('RGBA'); bg.alpha_composite(im.convert('RGBA')); return bg.convert('RGB')


def b64(im, q=90):
    buf = io.BytesIO(); im.save(buf, 'JPEG', quality=q); return 'data:image/jpeg;base64,' + base64.b64encode(buf.getvalue()).decode()


def fit(im):
    k = S / max(im.size); return (max(1, round(im.width * k)), max(1, round(im.height * k)))


cards = []
for i, it in enumerate(items, 1):
    src = Image.open(f"/home/claude/work/{it['pack']}/textures/{it['name']}").convert('RGBA')
    ai = Image.open(f"{outdir}/{it['name']}").convert('RGBA')
    size = fit(ai)
    before = flat(src.resize(size, Image.BILINEAR)); after = flat(ai.resize(size, Image.LANCZOS))
    # a close-up: the middle quarter of each at 2x, where the difference shows
    cw, ch = ai.width // 4, ai.height // 4; cx, cy = ai.width * 3 // 8, ai.height * 3 // 8
    zb = flat(src.resize(ai.size, Image.BILINEAR).crop((cx, cy, cx + cw, cy + ch)).resize(size, Image.NEAREST))
    za = flat(ai.crop((cx, cy, cx + cw, cy + ch)).resize(size, Image.NEAREST))
    tiled = ''
    if it['kind'] == 'surface':
        t = Image.new('RGBA', (ai.width * 2, ai.height * 2))
        for y in (0, 1):
            for x in (0, 1): t.paste(ai, (x * ai.width, y * ai.height))
        tiled = f'<figure><img src="{b64(flat(t.resize(size, Image.LANCZOS)))}"><figcaption>AI, repeated 2×2 (seams would show here)</figcaption></figure>'
    tags = ', '.join(x for x in (it['kind'] == 'surface' and 'repeating surface', it['alpha'] and 'see-through') if x) or 'one-off art'
    cards.append(f'''<section><h2>{i}. {it['game']} <small>{it['name']} · {it['w']}×{it['h']} → {ai.width}×{ai.height} · {tags}</small></h2>
<div class="row"><figure><img src="{b64(before)}"><figcaption>Original (as the game enlarges it)</figcaption></figure>
<figure><img src="{b64(after)}"><figcaption>AI 4×</figcaption></figure></div>
<div class="row"><figure><img src="{b64(zb)}"><figcaption>Close-up, original</figcaption></figure>
<figure><img src="{b64(za)}"><figcaption>Close-up, AI</figcaption></figure>{tiled}</div>
<p class="pick">Your call: <label><input type="radio" name="p{i}" value="ai"> use AI</label> <label><input type="radio" name="p{i}" value="orig"> keep original</label></p></section>''')

page = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AI Upscale Sample</title><style>
:root{{--bg:#16130f;--fg:#e9e1d3;--mute:#a99f8d;--line:#3a3226;--acc:#c9a45c}}
body{{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 Georgia,serif}}
main{{max-width:1180px;margin:auto;padding:24px 16px}} h1{{color:var(--acc);font-weight:normal;margin:0 0 4px}}
.intro{{color:var(--mute);max-width:760px}} section{{border-top:1px solid var(--line);padding:18px 0}}
h2{{font-size:18px;font-weight:normal;margin:0 0 10px}} small{{color:var(--mute);font-size:13px}}
.row{{display:flex;flex-wrap:wrap;gap:12px;margin-bottom:12px}} figure{{margin:0;flex:1 1 300px;max-width:512px}}
img{{width:100%;height:auto;display:block;border:1px solid var(--line);image-rendering:auto}}
figcaption{{color:var(--mute);font-size:13px;margin-top:4px}} .pick{{color:var(--mute)}} #sum{{position:sticky;top:0;background:var(--bg);padding:8px 0;color:var(--acc)}}
</style></head><body><main><h1>AI upscale sample</h1>
<p class="intro">30 textures from the three Xbox games, enlarged 4× by Real-ESRGAN (x4plus). Left: the original as the game
blows it up on a big screen. Right: the AI version. The close-ups show the middle of each at 2×. Repeating surfaces also
show the AI result tiled, to check for seams. Private: made from the games' own art.</p><p id="sum"></p>
{''.join(cards)}
<script>const s=document.getElementById('sum');function c(){{const v=[...document.querySelectorAll('input:checked')].map(x=>x.value);s.textContent=`Picked: ${{v.filter(x=>x=='ai').length}} AI, ${{v.filter(x=>x=='orig').length}} original, of {len(items)}`}}document.addEventListener('change',c);c()</script>
</main></body></html>'''
open(sys.argv[3], 'w').write(page)
print(sys.argv[3], len(page) // 1024, 'KB')
