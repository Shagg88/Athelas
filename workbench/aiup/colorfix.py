# colorfix.py <sample.json> <in dir> <out dir>: the AI result's colours brought back to the game texture's: each 4x4
# block of the result gets the original pixel's average colour (the AI's detail kept, its colour drift removed).
import json, sys, numpy as np
from PIL import Image
items = json.load(open(sys.argv[1]))
for it in items:
    s = Image.open(f"/home/claude/work/{it['pack']}/textures/{it['name']}").convert('RGBA')
    a = Image.open(f"{sys.argv[2]}/{it['name']}").convert('RGBA')
    A = np.asarray(a).astype(np.float32)
    low_ai = np.asarray(a.resize(s.size, Image.BOX).resize(a.size, Image.BILINEAR)).astype(np.float32)
    low_src = np.asarray(s.resize(a.size, Image.BILINEAR)).astype(np.float32)
    A[..., :3] = A[..., :3] + (low_src[..., :3] - low_ai[..., :3])
    Image.fromarray(A.clip(0, 255).round().astype(np.uint8), 'RGBA').save(f"{sys.argv[3]}/{it['name']}")
