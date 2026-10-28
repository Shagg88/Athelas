# recolor.py: a library texture's colours brought to the game texture's (mean and spread of each channel in Lab), so a
# replacement keeps the game's palette and overall darkness (2002 textures have their shading painted in)
import numpy as np
from skimage import color
def recolor(src, ref):
    a = color.rgb2lab(np.asarray(src.convert('RGB')) / 255.0); b = color.rgb2lab(np.asarray(ref.convert('RGB')) / 255.0)
    for c in range(3):
        sa, sb = a[..., c].std() + 1e-6, b[..., c].std()
        k = sb / sa if c == 0 else min(sb / sa, 1.5)  # don't blow up colour noise
        a[..., c] = (a[..., c] - a[..., c].mean()) * k + b[..., c].mean()
    from PIL import Image
    return Image.fromarray((np.clip(color.lab2rgb(a), 0, 1) * 255).astype('uint8'))
