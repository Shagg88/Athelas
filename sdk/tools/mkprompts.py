"""The button pictures the ports show in place of a game's own (button_pictures: PlayStation, Nintendo, Steam Deck,
Steam Controller, the keyboard's keys and the mouse's buttons, and the Xbox bumpers some games have no picture
for), as PNG bytes in
sdk/runtime/app/prompt_pictures.h, shared by all four ports.

The pictures are Kenney's Input Prompts (www.kenney.nl, Creative Commons Zero: sdk/assets/kenney-input-prompts with
its licence). Each is cut to its drawn part and centred in a square (its own pixels, about 96 across, kept as they
are: small files), so a round button fills the picture: the ports fit it where the game's own disc is, enlarging it
smoothly (app::PlacePicture). Files are named by where the button is on
the controller (a.png is the bottom face button: Cross on a PlayStation pad, B on a Nintendo one), as the games ask
for them.

usage: python3 sdk/tools/mkprompts.py [sdk/runtime/app/prompt_pictures.h [preview.png]]"""
import io, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'assets', 'kenney-input-prompts')
# the folder of each style, and the name the ports use for it (app::PromptStyleName)
STYLES = [('xbox', 'xbox'), ('playstation', 'playstation'), ('switch', 'nintendo'), ('steamdeck', 'steamdeck'),
          ('steamcontroller', 'steamcontroller')]
# the file of each button, and the name the ports ask by (the first Xbox's white and black buttons are today's bumpers)
BUTTONS = [('a', 'A'), ('b', 'B'), ('x', 'X'), ('y', 'Y'), ('lb', 'WHITE'), ('rb', 'BLACK'), ('lt', 'LT'), ('rt', 'RT'),
           ('start', 'START'), ('back', 'BACK')]
# the keyboard style: by the key's name in the settings (sdk/runtime/core/keys.h kKeys), then its file; a key with no
# picture in the pack (the number pad's digits) is drawn as a plain key cap by the ports
KEYS = [('0', 'keyboard_0'), ('1', 'keyboard_1'), ('2', 'keyboard_2'), ('3', 'keyboard_3'), ('4', 'keyboard_4'),
        ('5', 'keyboard_5'), ('6', 'keyboard_6'), ('7', 'keyboard_7'), ('8', 'keyboard_8'), ('9', 'keyboard_9'),
        ('A', 'keyboard_a'), ('Apostrophe', 'keyboard_apostrophe'), ('B', 'keyboard_b'),
        ('Backspace', 'keyboard_backspace'), ('C', 'keyboard_c'), ('CapsLock', 'keyboard_capslock'),
        ('Comma', 'keyboard_comma'), ('D', 'keyboard_d'), ('Delete', 'keyboard_delete'),
        ('Down', 'keyboard_arrow_down'), ('E', 'keyboard_e'), ('End', 'keyboard_end'), ('Enter', 'keyboard_enter'),
        ('Equals', 'keyboard_equals'), ('Esc', 'keyboard_escape'), ('F', 'keyboard_f'), ('F1', 'keyboard_f1'),
        ('F11', 'keyboard_f11'), ('F12', 'keyboard_f12'), ('F2', 'keyboard_f2'), ('F4', 'keyboard_f4'),
        ('F5', 'keyboard_f5'), ('F6', 'keyboard_f6'), ('F7', 'keyboard_f7'), ('F8', 'keyboard_f8'),
        ('G', 'keyboard_g'), ('Grave', 'keyboard_tilde'), ('H', 'keyboard_h'), ('Home', 'keyboard_home'),
        ('I', 'keyboard_i'), ('Insert', 'keyboard_insert'), ('J', 'keyboard_j'), ('K', 'keyboard_k'),
        ('L', 'keyboard_l'), ('LAlt', 'keyboard_alt'), ('LCtrl', 'keyboard_ctrl'), ('LShift', 'keyboard_shift'),
        ('Left', 'keyboard_arrow_left'), ('M', 'keyboard_m'), ('Minus', 'keyboard_minus'), ('N', 'keyboard_n'),
        ('NumpadPlus', 'keyboard_numpad_plus'), ('O', 'keyboard_o'), ('P', 'keyboard_p'),
        ('PageDown', 'keyboard_page_down'), ('PageUp', 'keyboard_page_up'), ('Period', 'keyboard_period'),
        ('Q', 'keyboard_q'), ('R', 'keyboard_r'), ('RAlt', 'keyboard_alt'), ('RCtrl', 'keyboard_ctrl'),
        ('RShift', 'keyboard_shift'), ('Right', 'keyboard_arrow_right'), ('S', 'keyboard_s'),
        ('Semicolon', 'keyboard_semicolon'), ('Slash', 'keyboard_slash_forward'), ('Space', 'keyboard_space'),
        ('T', 'keyboard_t'), ('Tab', 'keyboard_tab'), ('U', 'keyboard_u'), ('Up', 'keyboard_arrow_up'),
        ('V', 'keyboard_v'), ('W', 'keyboard_w'), ('X', 'keyboard_x'), ('Y', 'keyboard_y'), ('Z', 'keyboard_z')]
MOUSE = [('mouse_left', 'Mouse1'), ('mouse_right', 'Mouse2'), ('mouse_scroll', 'Mouse3'), ('mouse_side', 'Mouse4'),
         ('mouse_side', 'Mouse5')]


def picture(path):
    im = Image.open(path).convert('RGBA')
    box = im.getbbox()
    im = im.crop(box)
    side = max(im.size)
    sq = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    sq.paste(im, ((side - im.size[0]) // 2, (side - im.size[1]) // 2))
    out = io.BytesIO()
    sq.save(out, 'PNG', optimize=True)
    return sq, out.getvalue()


def main():
    dst = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'runtime', 'app', 'prompt_pictures.h')
    entries, previews = [], []
    for folder, style in STYLES:
        for f, button in BUTTONS:
            img, png = picture(os.path.join(SRC, folder, f + '.png'))
            entries.append((style, button, png))
            previews.append(img)
    for button, f in KEYS:
        img, png = picture(os.path.join(SRC, 'keyboard', f + '.png'))
        entries.append(('keyboard', button, png))
        previews.append(img)
    for f, button in MOUSE:
        img, png = picture(os.path.join(SRC, 'mouse', f + '.png'))
        entries.append(('keyboard', button, png))
        previews.append(img)
    with open(dst, 'w', newline='\n') as o:
        o.write('// Generated by sdk/tools/mkprompts.py: the button pictures the ports show, as PNG files.\n'
                "// Kenney's Input Prompts (www.kenney.nl), Creative Commons Zero: sdk/assets/kenney-input-prompts.\n"
                '// Included once, by app/prompt_style.cpp: the ports ask app::FindPromptPicture.\n'
                '#pragma once\n#include <cstddef>\n#include <cstdint>\n\n#include "app/prompt_style.h"\n\n'
                'namespace app {\n')
        for style, button, png in entries:
            name = 'k%s_%s' % (style.capitalize(), button.lower())
            o.write('inline const uint8_t %s[%d] = {\n' % (name, len(png)))
            for i in range(0, len(png), 24):
                o.write('    ' + ','.join(str(b) for b in png[i:i + 24]) + ',\n')
            o.write('};\n')
        o.write('\ninline const PromptPicture kPromptPictures[] = {\n')
        for style, button, png in entries:
            o.write('    {"%s", "%s", k%s_%s, sizeof k%s_%s},\n' % (style, button, style.capitalize(), button.lower(),
                                                                   style.capitalize(), button.lower()))
        o.write('};\n}  // namespace app\n')
    print('%d pictures, %d bytes of PNG' % (len(entries), sum(len(e[2]) for e in entries)))
    if len(sys.argv) > 2:
        cols = len(BUTTONS)
        rows = (len(previews) + cols - 1) // cols
        sheet = Image.new('RGBA', (cols * 96, rows * 96), (36, 52, 78, 255))
        for i, p in enumerate(previews):
            sheet.alpha_composite(p.resize((88, 88), Image.LANCZOS), ((i % cols) * 96 + 4, (i // cols) * 96 + 4))
        sheet.convert('RGB').save(sys.argv[2])


main()
