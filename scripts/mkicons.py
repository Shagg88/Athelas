"""Draws ROTK Fix's button icons (original, unbranded; the mod colours the face buttons) and writes
src/icons_data.h. Letters: Liberation Sans Bold (SIL Open Font License 1.1).
Each icon is 48 px high; two 4-bit channels per pixel: coverage (alpha) and
ink (0 = light fill, 15 = dark letter/outline). The game tints them with the
text colour."""
import sys
from PIL import Image, ImageDraw, ImageFont
FONT = '/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf'
H = 48; SS = 4; HH = H * SS
PH = ['A','B','X','Y','LB','RB','BACK','START','LS','RS','LT','RT','UP','DOWN','LEFT','RIGHT']
STYLES = ['xbox', 'playstation', 'nintendo']

def text_size(d, t, f):
    b = d.textbbox((0, 0), t, font=f); return b[2]-b[0], b[3]-b[1], b

def new(w):
    return Image.new('L', (w, HH), 0), Image.new('L', (w, HH), 0)   # coverage, ink

def put_text(cov, ink, t, frac, cx=None, cy=None):
    d = ImageDraw.Draw(ink)
    size = int(HH * frac)
    f = ImageFont.truetype(FONT, size)
    tw, th, b = text_size(d, t, f)
    while tw > cov.width * 0.78 and size > 10:
        size -= 4; f = ImageFont.truetype(FONT, size); tw, th, b = text_size(d, t, f)
    cx = cov.width / 2 if cx is None else cx; cy = HH / 2 if cy is None else cy
    d.text((cx - tw / 2 - b[0], cy - th / 2 - b[1]), t, font=f, fill=255)

def ring(cov, ink, box, radius=None, width=None, ellipse=False):
    dc, di = ImageDraw.Draw(cov), ImageDraw.Draw(ink)
    w = width or int(HH * 0.07)
    if ellipse:
        dc.ellipse(box, fill=255); di.ellipse(box, outline=255, width=w)
    else:
        dc.rounded_rectangle(box, radius, fill=255); di.rounded_rectangle(box, radius, outline=255, width=w)

def disc(label=None, shape=None, frac=0.55):
    cov, ink = new(HH); m = int(HH * 0.03)
    ring(cov, ink, (m, m, HH - m, HH - m), ellipse=True)
    if label: put_text(cov, ink, label, frac)
    if shape: shape(ImageDraw.Draw(ink))
    return cov, ink

def pill(label, wfac=1.6, trigger=False, frac=0.48):
    w = int(HH * wfac); cov, ink = new(w); m = int(HH * 0.05)
    if trigger:   # rounded top, flatter bottom
        dc, di = ImageDraw.Draw(cov), ImageDraw.Draw(ink)
        r = int(HH * 0.42); lw = int(HH * 0.07)
        dc.rounded_rectangle((m, m, w - m, HH - m), r, fill=255)
        dc.rectangle((m, HH // 2, w - m, HH - m), fill=255)
        di.rounded_rectangle((m, m, w - m, HH - m), r, outline=255, width=lw)
        di.rectangle((m, HH // 2, w - m, HH - m), outline=255, width=lw)
        di.rectangle((m + lw, HH // 2 - lw, w - m - lw, HH - m - lw), fill=0)
        di.rounded_rectangle((m + lw, m + lw, w - m - lw, HH // 2 + lw), r - lw, fill=0)
    else:
        ring(cov, ink, (m, int(HH * 0.14), w - m, HH - int(HH * 0.14)), radius=int(HH * 0.3))
    put_text(cov, ink, label, frac)
    return cov, ink

def dpad(which):
    cov, ink = new(HH); dc, di = ImageDraw.Draw(cov), ImageDraw.Draw(ink)
    a = HH * 0.34; b = HH * 0.66; m = HH * 0.04; lw = int(HH * 0.06)
    cross = [(a, m), (b, m), (b, a), (HH - m, a), (HH - m, b), (b, b), (b, HH - m), (a, HH - m), (a, b), (m, b), (m, a), (a, a)]
    dc.polygon(cross, fill=255)
    di.polygon(cross, outline=255, width=lw)
    arms = {'UP': (a, m, b, a), 'DOWN': (a, b, b, HH - m), 'LEFT': (m, a, a, b), 'RIGHT': (b, a, HH - m, b)}
    di.rectangle(arms[which], fill=255)
    return cov, ink

def stick(label):
    cov, ink = disc(label, frac=0.42)
    r = HH * 0.36; c = HH / 2
    ImageDraw.Draw(ink).ellipse((c - r, c - r, c + r, c + r), outline=255, width=int(HH * 0.03))
    return cov, ink

def ps_shape(kind):
    def f(d):
        c = HH / 2; r = HH * 0.2; lw = int(HH * 0.075)
        if kind == 'cross':
            d.line((c - r, c - r, c + r, c + r), fill=255, width=lw); d.line((c - r, c + r, c + r, c - r), fill=255, width=lw)
        elif kind == 'circle':
            d.ellipse((c - r, c - r, c + r, c + r), outline=255, width=lw)
        elif kind == 'square':
            d.rectangle((c - r * 0.9, c - r * 0.9, c + r * 0.9, c + r * 0.9), outline=255, width=lw)
        else:
            d.polygon([(c, c - r * 1.1), (c + r * 1.1, c + r * 0.8), (c - r * 1.1, c + r * 0.8)], outline=255, width=lw)
    return f

def icon(style, ph):
    if ph in ('UP', 'DOWN', 'LEFT', 'RIGHT'): return dpad(ph)
    if style == 'xbox':
        t = {'A': 'A', 'B': 'B', 'X': 'X', 'Y': 'Y'}
        if ph in t: return disc(t[ph])
        if ph in ('LB', 'RB'): return pill(ph)
        if ph in ('LT', 'RT'): return pill(ph, trigger=True)
        if ph == 'BACK': return pill('BACK', 2.1, frac=0.36)
        if ph == 'START': return pill('START', 2.3, frac=0.36)
        return stick('L' if ph == 'LS' else 'R')
    if style == 'playstation':
        sh = {'A': 'cross', 'B': 'circle', 'X': 'square', 'Y': 'triangle'}
        if ph in sh: return disc(shape=ps_shape(sh[ph]))
        lab = {'LB': 'L1', 'RB': 'R1', 'LT': 'L2', 'RT': 'R2', 'LS': 'L3', 'RS': 'R3'}
        if ph in ('LB', 'RB'): return pill(lab[ph])
        if ph in ('LT', 'RT'): return pill(lab[ph], trigger=True)
        if ph in ('LS', 'RS'): return stick(lab[ph])
        if ph == 'BACK': return pill('SHARE', 2.4, frac=0.34)
        return pill('OPTIONS', 2.8, frac=0.34)
    # nintendo: positions bottom/right/left/top are labelled B/A/Y/X
    t = {'A': 'B', 'B': 'A', 'X': 'Y', 'Y': 'X'}
    if ph in t: return disc(t[ph])
    lab = {'LB': 'L', 'RB': 'R', 'LT': 'ZL', 'RT': 'ZR'}
    if ph in ('LB', 'RB'): return pill(lab[ph], 1.4)
    if ph in ('LT', 'RT'): return pill(lab[ph], trigger=True)
    if ph == 'BACK': return disc('−', frac=0.7)
    if ph == 'START': return disc('+', frac=0.7)
    return stick('L' if ph == 'LS' else 'R')

out = ['/* Generated by scripts/mkicons.py - ROTK Fix button icons (original artwork).',
       ' * Letters drawn with Liberation Sans Bold (SIL Open Font License 1.1).',
       ' * Per pixel: high nibble = coverage, low nibble = ink (dark). */',
       '#define ICON_H %d' % H, 'typedef struct { unsigned char w; unsigned int off; } IconInfo;']
blob = bytearray(); infos = []
sheet = []
for s in STYLES:
    for ph in PH:
        cov, ink = icon(s, ph)
        w = max(1, round(cov.width / SS))
        cov = cov.resize((w, H), Image.LANCZOS); ink = ink.resize((w, H), Image.LANCZOS)
        infos.append((w, len(blob)))
        cp, ip = cov.load(), ink.load()
        for y in range(H):
            for x in range(w):
                c = cp[x, y] * 15 // 255; k = ip[x, y] * 15 // 255
                blob.append(c << 4 | k)
        sheet.append((cov, ink))
out.append('static const IconInfo k_iconInfo[3][16] = {')
for si in range(3):
    out.append('    { ' + ', '.join('{ %d, %d }' % infos[si * 16 + i] for i in range(16)) + ' },')
out.append('};')
out.append('static const unsigned char k_iconPixels[%d] = {' % len(blob))
for i in range(0, len(blob), 32):
    out.append('    ' + ','.join(str(b) for b in blob[i:i + 32]) + ',')
out.append('};')
open(sys.argv[1], 'w').write('\n'.join(out) + '\n')
# preview: light text colour over a dark game-like background
pw = sum(c.width + 6 for c, _ in sheet[:16]) + 10
prev = Image.new('RGB', (pw, 3 * (H + 12) + 10), (40, 34, 28))
y = 10
for si in range(3):
    x = 8
    for c, k in sheet[si * 16:(si + 1) * 16]:
        col = Image.new('RGB', c.size, (235, 228, 210))
        dark = Image.new('RGB', c.size, (20, 18, 16))
        rgb = Image.composite(dark, col, k)
        prev.paste(rgb, (x, y), c)
        x += c.width + 6
    y += H + 12
prev = prev.resize((prev.width * 2, prev.height * 2), Image.NEAREST)
prev.save(sys.argv[2])
print(len(blob), 'bytes')
