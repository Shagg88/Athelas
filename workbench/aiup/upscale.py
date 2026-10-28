# upscale.py <sample.json> <out dir>: the game textures 4x with Real-ESRGAN (x4plus), for review.
#   colour: padded before the model (wrapped for repeating surfaces, so the result still tiles; edge pixels repeated
#           otherwise), cropped after
#   transparency: colours under fully clear pixels filled from their opaque neighbours first (no dark fringes), the
#           alpha channel upscaled on its own through the model as grey
#   large textures in overlapping tiles (CPU memory)
import json, sys, time
import numpy as np, torch, spandrel
from PIL import Image

PAD, TILE, OVER = 16, 160, 16
model = spandrel.ModelLoader().load_from_file('/home/claude/work/aiup/models/RealESRGAN_x4plus.pth').model.eval()
torch.set_num_threads(2)


def run(img):  # HxWx3 float 0..1 -> 4H x 4W x 3
    h, w, _ = img.shape
    out = np.zeros((h * 4, w * 4, 3), np.float32)
    weight = np.zeros((h * 4, w * 4, 1), np.float32)
    ys = list(range(0, max(h - OVER, 1), TILE - OVER)) or [0]
    xs = list(range(0, max(w - OVER, 1), TILE - OVER)) or [0]
    for y in ys:
        for x in xs:
            y0, x0 = min(y, max(h - TILE, 0)), min(x, max(w - TILE, 0))
            t = img[y0:y0 + TILE, x0:x0 + TILE]
            with torch.no_grad():
                r = model(torch.from_numpy(t).permute(2, 0, 1)[None])[0].permute(1, 2, 0).clamp(0, 1).numpy()
            th, tw = t.shape[:2]
            # feathered weights so overlapping tiles blend
            wy = np.minimum(np.arange(th * 4) + 1, np.arange(th * 4)[::-1] + 1).astype(np.float32)
            wx = np.minimum(np.arange(tw * 4) + 1, np.arange(tw * 4)[::-1] + 1).astype(np.float32)
            wgt = np.minimum.outer(np.minimum(wy, OVER * 4), np.minimum(wx, OVER * 4))[..., None]
            out[y0 * 4:(y0 + th) * 4, x0 * 4:(x0 + tw) * 4] += r * wgt
            weight[y0 * 4:(y0 + th) * 4, x0 * 4:(x0 + tw) * 4] += wgt
    return out / np.maximum(weight, 1e-6)


def padded(a, wrap):
    return np.pad(a, ((PAD, PAD), (PAD, PAD), (0, 0)), mode='wrap' if wrap else 'edge')


def bleed(rgb, alpha):  # colours of clear pixels from their opaque neighbours, a few pixels at a time
    rgb = rgb.copy(); known = alpha > 0.02
    for _ in range(64):
        if known.all(): break
        acc = np.zeros_like(rgb); cnt = np.zeros(known.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            k = np.roll(known, (dy, dx), (0, 1)); c = np.roll(rgb, (dy, dx), (0, 1))
            acc += c * k[..., None]; cnt += k
        new = (~known) & (cnt > 0)
        rgb[new] = acc[new] / cnt[new][:, None]; known = known | new
    return rgb


items = json.load(open(sys.argv[1])); outdir = sys.argv[2]
for it in items:
    t0 = time.time()
    im = np.asarray(Image.open(f"/home/claude/work/{it['pack']}/textures/{it['name']}").convert('RGBA')).astype(np.float32) / 255
    rgb, alpha = im[..., :3], im[..., 3]
    wrap = it['kind'] == 'surface'
    if it['alpha']: rgb = bleed(rgb, alpha)
    up = run(padded(rgb, wrap))[PAD * 4:-PAD * 4, PAD * 4:-PAD * 4]
    if it['alpha']:
        a = run(padded(np.repeat(alpha[..., None], 3, 2), wrap))[PAD * 4:-PAD * 4, PAD * 4:-PAD * 4].mean(2)
    else:
        a = np.ones(up.shape[:2], np.float32)
    o = np.dstack([up, a])
    Image.fromarray((o * 255 + 0.5).clip(0, 255).astype(np.uint8), 'RGBA').save(f"{outdir}/{it['name']}")
    print(f"{it['game']} {it['name']} {it['w']}x{it['h']} -> {up.shape[1]}x{up.shape[0]} {time.time() - t0:.1f}s", flush=True)
