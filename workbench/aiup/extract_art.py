# extract_art.py <out dir> <rotk xbox dir> <rotk pc dir> <tt dir> <fotr dir>: the launcher's background pictures, read
# from extracted copies the way the launcher reads them (launcher/src/gameart.cpp), as PNGs for Real-ESRGAN.
# Private: made from the games' own art; for Shagg's own backgrounds folder, never released.
import io, os, struct, sys
from PIL import Image

WANTED = ["mainback", "fellback", "aragback", "gandback", "frodback", "samback", "legoback", "gimlback", "pippback",
          "merrback", "faraback", "codebkgd", "frttt1", "frttt2", "frfotk1", "frfotk2"]


def unpack(src, size):
    out = bytearray(); i = 0
    while len(out) < size:
        if i + 2 > len(src): return None
        w = (src[i] << 8) | src[i + 1]; i += 2
        if (w & 0x8800) == 0x8800:
            n = (w >> 12) & 7
            if n == 0:
                c = w & 0x7ff; out += src[i:i + c]; i += c
            else:
                dist = ((w >> 5) & 0x38) | n; ln = (w & 0xff) + 3
                if dist > len(out): return None
                out += bytes([out[-dist]]) * ln
        else:
            ln = (w >> 12) & 7; off = w & 0xfff
            if ln == 7: ln = src[i] + 7; i += 1
            ln += 3
            if off > len(out): return None
            p = len(out) - off
            if w & 0x8000:
                p += 2
                for k in range(ln): out.append(out[p - k])
            else:
                for k in range(ln): out.append(out[p + k])
    return bytes(out[:size])


def dxt1(data, w, h):
    hdr = struct.pack('<4sIIIIIII44xII4s20xI16x', b'DDS ', 124, 0x1007, h, w, w * h // 2, 0, 1, 32, 4, b'DXT1', 0x1000)
    return Image.open(io.BytesIO(hdr + data)).convert('RGB')


def rotk(path, out, tag):
    if not os.path.exists(path): print('no', path); return
    d = open(path, 'rb').read(); n = len(d); o = 0; cur = False; res = bytearray(); got = {}

    def take(r):
        o2 = 8; txhe = txda = None
        while o2 + 8 <= len(r):
            ln = struct.unpack_from('<I', r, o2 + 4)[0]
            if ln > len(r) - o2 - 8: break
            if r[o2:o2 + 4] == b'TXHE': txhe = (o2 + 8, ln)
            if r[o2:o2 + 4] == b'TXDA': txda = (o2 + 8, ln)
            o2 += 8 + ln
        if not txhe or not txda: return
        for e in range(txhe[0], txhe[0] + txhe[1] - 79, 80):
            name = r[e:e + 8].split(b'\0')[0].decode('latin1')
            w, h = struct.unpack_from('<HH', r, e + 0x38); off = struct.unpack_from('<I', r, e + 8)[0]
            if name in WANTED and name not in got and r[e + 0x3c] in (1, 0x0c) and w >= 256 and h >= 256:
                got[name] = dxt1(r[txda[0] + off:txda[0] + off + w * h // 2], w, h)
    while o + 16 <= n:
        if d[o:o + 4] != b'COHS': o += 4; continue
        sz = struct.unpack_from('<I', d, o + 4)[0]
        if sz > n - o: break
        sub = d[o + 16:o + 20]
        if sub == b'RDHS':
            if cur: take(bytes(res))
            res = bytearray(); cur = d[o + 24:o + 28] in (b'gfxt', b'efxt')
        elif cur and sub == b'TADS' and sz > 0x40: res += d[o + 0x40:o + sz]
        elif cur and sub == b'tadR' and sz > 0x44:
            u = unpack(d[o + 0x44:o + sz], struct.unpack_from('<I', d, o + 0x40)[0])
            if u is None: cur = False; res = bytearray()
            else: res += u
        o += sz if sz >= 16 else 16
    if cur: take(bytes(res))
    for name, im in got.items(): im.resize((im.width, im.width * 3 // 4)).save(f'{out}/{tag}_{name}.png')  # square, shown 4:3
    print(tag, len(got), 'pictures')


def tt(d, out):
    pics = [(f'UI/stills{f + 1}.BFF', 0xF80 + k * 0x119000) for f in range(4) for k in range(10) if not (f == 1 and k == 3)]
    pics += [('UI/mainmenu.BFF', 0xF80), ('UI/mainmenu.BFF', 0x170F80), ('UI/options.BFF', 0xF80)]
    n = 0
    for f, at in pics:
        p = os.path.join(d, *f.split('/'))
        if not os.path.exists(p): continue
        b = open(p, 'rb'); b.seek(at); hd = b.read(0x80)
        if struct.unpack_from('<I', hd)[0] != 0x00056278: continue
        w, h, t = struct.unpack_from('<III', hd, 4)
        raw = b.read(w * h * 4 if t == 10 else w * h // 2)
        im = Image.frombytes('RGBA', (w, h), raw, 'raw', 'BGRA').convert('RGB') if t == 10 else dxt1(raw, w, h)
        im.save(f'{out}/tt_{os.path.basename(f)[:-4]}_{at:x}.png'); n += 1
    print('tt', n, 'pictures')


def fotr(d, out):
    n = 0
    for f in ('XboxMedia/load_screens/aragorn_load.xpr', 'XboxMedia/load_screens/frodo_load.xpr',
              'XboxMedia/load_screens/gandalf_load.xpr', 'XboxMedia/menus/load_game_menu/load_menu_background.xpr'):
        p = os.path.join(d, *f.split('/'))
        if not os.path.exists(p): continue
        b = open(p, 'rb').read()
        hs, fmt, size = struct.unpack_from('<I', b, 8)[0], struct.unpack_from('<I', b, 24)[0], struct.unpack_from('<I', b, 28)[0]
        w, h, pitch = (size & 0xFFF) + 1, ((size >> 12) & 0xFFF) + 1, ((size >> 24) + 1) * 64
        im = Image.frombytes('RGBA', (w, h), b[hs:hs + pitch * h], 'raw', 'BGRA', pitch).convert('RGB')
        im.save(f'{out}/fotr_{os.path.basename(f)[:-4]}.png'); n += 1
    print('fotr', n, 'pictures')


out = sys.argv[1]; os.makedirs(out, exist_ok=True)
rotk(os.path.join(sys.argv[2], 'Data', 'Game', 'fro', 'fro03.scx'), out, 'rotkx')
rotk(os.path.join(sys.argv[3], 'Data', 'Game', 'fro', 'fro03.scw'), out, 'rotkpc')
tt(sys.argv[4], out)
fotr(sys.argv[5], out)
