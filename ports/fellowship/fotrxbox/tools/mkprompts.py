"""Draws the port's own button pictures for the game's on-screen prompts, in the Xbox prompts' style (a glossy disc
filling the picture; fotr_prompts.cpp makes the game's 32x32 button pictures from them), for
PlayStation and Nintendo controllers, and writes them as PNG bytes into src/game/fotr_prompts_data.h. The same
discs as The Return of the King's and The Two Towers' ports. Original artwork: no console maker's art is copied. The PlayStation face
buttons are dark discs with their symbols in their usual colours (blue cross, red circle, pink square, green triangle); Nintendo's are dark discs with white letters, labelled by
position (the bottom button reads B, the right A, the left Y, the top X).
Letters: Liberation Sans Bold (SIL Open Font License 1.1), rendered into the pictures.
usage: python3 tools/mkprompts.py src/game/fotr_prompts_data.h [preview.png]"""
import io, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

FONT = '/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf'
N = 256                 # the picture's size: 4x the game's 64x64 texture (8x the 32x32 ones)
SS = 4                  # drawn at 4x, then reduced
DISC = 1.0              # the disc's diameter, as a share of the picture (it fills it)

def disc(base, symbol):
    W = N * SS
    img = Image.new('RGBA', (W, W), (0, 0, 0, 0))
    d = int(W * DISC)
    # shadow rim at the bottom right, as the game's discs have
    sh = Image.new('L', (W, W), 0)
    ImageDraw.Draw(sh).ellipse((int(d * 0.03), int(d * 0.05), d, d), fill=200)
    img.paste((0, 0, 0, 255), (0, 0), sh)
    # the disc: its colour, lighter towards the top left
    body = Image.new('RGBA', (d, d))
    px = body.load()
    for y in range(d):
        for x in range(d):
            fx, fy = (x - d * 0.36) / d, (y - d * 0.32) / d
            t = min(1.0, (fx * fx + fy * fy) ** 0.5 * 1.6)
            k = 1.35 - 0.55 * t
            px[x, y] = tuple(min(255, int(c * k)) for c in base) + (255,)
    mask = Image.new('L', (d, d), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, d - int(d * 0.04), d - int(d * 0.04)), fill=255)
    img.paste(body, (0, 0), mask)
    # a light edge, so a dark disc still reads against a dark scene
    e = int(d * 0.035)
    ImageDraw.Draw(img).ellipse((e // 2, e // 2, d - int(d * 0.04) - e // 2, d - int(d * 0.04) - e // 2),
                                outline=(150, 150, 160, 255), width=e)
    symbol(ImageDraw.Draw(img), (d - int(d * 0.04)) / 2, d)
    return img.resize((N, N), Image.LANCZOS)

def letter(t, colour=(255, 255, 255, 255)):
    def f(dr, c, d):
        size = int(d * 0.62)
        font = ImageFont.truetype(FONT, size)
        b = dr.textbbox((0, 0), t, font=font)
        dr.text((c - (b[2] - b[0]) / 2 - b[0], c - (b[3] - b[1]) / 2 - b[1]), t, font=font, fill=colour)
    return f

def word(t):  # a shoulder or menu button's name, smaller
    def f(dr, c, d):
        size = int(d * (0.46 if len(t) <= 2 else 0.26))
        font = ImageFont.truetype(FONT, size)
        b = dr.textbbox((0, 0), t, font=font)
        while b[2] - b[0] > d * 0.74 and size > 8:  # long names fit inside the disc
            size -= 2
            font = ImageFont.truetype(FONT, size)
            b = dr.textbbox((0, 0), t, font=font)
        dr.text((c - (b[2] - b[0]) / 2 - b[0], c - (b[3] - b[1]) / 2 - b[1]), t, font=font, fill=(255, 255, 255, 255))
    return f

def shape(kind, colour):
    def f(dr, c, d):
        r = d * 0.27
        lw = int(d * 0.09)
        if kind == 'cross':
            dr.line((c - r, c - r, c + r, c + r), fill=colour, width=lw)
            dr.line((c - r, c + r, c + r, c - r), fill=colour, width=lw)
        elif kind == 'circle':
            dr.ellipse((c - r, c - r, c + r, c + r), outline=colour, width=lw)
        elif kind == 'square':
            q = r * 0.88
            dr.rectangle((c - q, c - q, c + q, c + q), outline=colour, width=lw)
        else:
            pts = [(c, c - r * 1.08), (c + r * 1.1, c + r * 0.78), (c - r * 1.1, c + r * 0.78)]
            dr.line(pts + [pts[0], pts[1]], fill=colour, width=lw, joint='curve')
    return f

DARK = (64, 64, 72)
LIGHT = (236, 236, 240, 255)
# the PlayStation symbols in their usual colours, the same as ROTK Fix's button pictures (src/icons.c k_faceColour)
CROSS, CIRCLE, SQUARE, TRIANGLE = (102, 153, 255, 255), (238, 85, 85, 255), (221, 119, 204, 255), (68, 204, 170, 255)
PICTURES = {
    # Xbox button the game shows -> (style, picture)
    'playstation': {
        'A': disc(DARK, shape('cross', CROSS)),
        'B': disc(DARK, shape('circle', CIRCLE)),
        'X': disc(DARK, shape('square', SQUARE)),
        'Y': disc(DARK, shape('triangle', TRIANGLE)),
        'WHITE': disc(DARK, word('L1')), 'BLACK': disc(DARK, word('R1')),
        'LT': disc(DARK, word('L2')), 'RT': disc(DARK, word('R2')),
        'START': disc(DARK, word('OPTIONS')), 'BACK': disc(DARK, word('SHARE')),
    },
    'nintendo': {
        'A': disc(DARK, letter('B')), 'B': disc(DARK, letter('A')),
        'X': disc(DARK, letter('Y')), 'Y': disc(DARK, letter('X')),
        'WHITE': disc(DARK, word('L')), 'BLACK': disc(DARK, word('R')),
        'LT': disc(DARK, word('ZL')), 'RT': disc(DARK, word('ZR')),
        'START': disc(DARK, letter('+')), 'BACK': disc(DARK, letter('−')),
    },
}

out = ['// Generated by tools/mkprompts.py: the port\'s own button pictures (original artwork) as PNG files.',
       '// Letters drawn with Liberation Sans Bold (SIL Open Font License 1.1).',
       '#pragma once', '#include <cstddef>', '#include <cstdint>', '',
       'struct PromptPicture {', '    const char* style;', '    const char* button;', '    const uint8_t* png;',
       '    size_t size;', '};', '']
table = []
for style, pics in PICTURES.items():
    for button, im in pics.items():
        buf = io.BytesIO()
        im.save(buf, 'PNG', optimize=True)
        b = buf.getvalue()
        name = 'k%s_%s' % (style.capitalize(), button.lower())
        out.append('const uint8_t %s[%d] = {' % (name, len(b)))
        for i in range(0, len(b), 24):
            out.append('    ' + ','.join(str(x) for x in b[i:i + 24]) + ',')
        out.append('};')
        table.append('    {"%s", "%s", %s, sizeof %s},' % (style, button, name, name))
out.append('const PromptPicture kPromptPictures[] = {')
out += table
out.append('};')
open(sys.argv[1], 'w').write('\n'.join(out) + '\n')
if len(sys.argv) > 2:
    rows = list(PICTURES.items())
    prev = Image.new('RGBA', (10 * 100 + 10, len(rows) * 100 + 10), (70, 62, 54, 255))
    for r, (style, pics) in enumerate(rows):
        for i, im in enumerate(pics.values()):
            prev.alpha_composite(im.resize((90, 90), Image.LANCZOS), (10 + i * 100, 10 + r * 100))
    prev.save(sys.argv[2])
