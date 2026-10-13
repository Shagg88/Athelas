# The Return of the King engine: what ROTK Fix learned from the PC version

From the ROTK Fix chat, for the ROTK Xbox recompilation. Everything here comes from rotk.exe
(PC, 2003) and from comparing the PC and Xbox discs. The source of the mod is in
`rotkfix-source-2026-10-12b.zip`: `notes.md`, `src/graphics.c`, `src/textures.c`,
`src/rotkfix.c` and `tools/scw_read.py`.

## 0. The short version

- **One engine, one code base.** The Xbox XBE and the PC exe were built from the same tree. Both
  contain source paths under `\Dev\Branches\EAThirdPerson\lotr\rotk\code\...`; the Xbox-only code
  is in `code\LL\Xbox\` (for example `LLVideoDivX_XBox.c`).
- **PC addresses don't carry over, but behaviour does.** Expect the same functions in the same
  shape, the same data layouts, the same file formats and the same frame-timing design. Matching
  strings, constants and byte patterns between the two binaries is the fastest way to find things.
- **Not The Two Towers' engine.** The Two Towers (Xbox) is Stormfront's "Fusion" engine: Lua,
  Bink, `.bfa`/`.bfg` worlds. Only the recompiler and runtime transfer from that port, not its
  game hooks.
- **Level files are the same format.** Xbox `.scx` and PC `.scw` use the same SHOC chunks and the
  same LZ compression.
- **Movies are not the same format.** PC uses `.avi` (DivX 5, 512x480, 23.976 fps). Xbox uses its
  own `.vid` container (DivX, 640x480, 29.97 fps). Details in section 4.

## 1. Level files: `.scw` (PC) and `.scx` (Xbox)

**Paths:** `Data\Game\<lvl>\<lvl>NN.scw` (26–117 MB each), plus `Data\Player\*.scw`,
`GlobScen.scw` and `LoadOnce.scw`. On Xbox the extension is `.scx`, in the same layout;
`Data\Game\GlobScen.scx` is what the launcher uses to recognise an extracted ROTK Xbox.

**Chunks.** All values are little-endian. The reader steps through the file 4 bytes at a time.

- A chunk starts with the bytes `COHS` ("SHOC" reversed). The u32 at +4 is the size of the whole
  chunk. The sub-type tag is at +16.
- **`RDHS` (SHDR)** starts a new resource. Its type fourcc is at +24..+28, stored reversed. Types
  seen include `txfe`, `txfs`, `txfi` (textures), `ter` (terrain) and `bnk` (sound bank).
- **`TADS` (SDAT)** holds stored data. The raw data runs from +0x40 to the end of the chunk and
  belongs to the current resource.
- **`tadR` (Rdat)** holds compressed data. The uncompressed size is the u32 at +0x40, and the
  compressed data starts at +0x44.
- To move to the next chunk, advance by the size if it is ≥ 16, otherwise by 16.

**Compression** is the game's own LZ (PC function `0x4dfe10`; a Python port is in
`tools/scw_read.py`). Read big-endian 16-bit words `w`:

- **If `(w & 0x8800) == 0x8800`**, let `n = (w >> 12) & 7`:
  - `n == 0`: a literal run. Copy the next `w & 0x7ff` bytes as they are.
  - Otherwise, a byte fill. `dist = ((w >> 5) & 0x38) | n` and `len = (w & 0xff) + 3`. The byte
    at `out[-dist]` is repeated `len` times.
- **Otherwise**, a back-reference. Let `ln = (w >> 12) & 7` and `off = w & 0xfff`:
  - If `ln == 7`, then `ln = next byte + 7`.
  - Then `ln += 3` and `p = len(out) - off`.
  - If bit 15 is set: `p += 2`, and copy backwards (`out[p-k]` for k = 0..ln-1).
  - If bit 15 is clear: copy forwards from `out[p]`.

**PC vs Xbox content** (compared on pal02):

- The same 20 texture names are in both. The Xbox texture resources are the same size or smaller;
  for example `opaabody` has size field 0x0800 on PC and 0x0200 on Xbox.
- The Xbox files are bigger because of sound data, not graphics. The PC version has no better
  textures to borrow, and the Xbox version has none to give.

**Fonts.** `GlobScen` has 8 `FNTS` chunks.

- Header: glyph count at +0xa, line height at +0x13.
- +0x14 points to 12-byte glyph records: `code u16, w, h, x u16, y u16, adv, xoff, yoff`.
- +0x1c points to a texture header (width and height as shorts at +4/+6), followed by 4-bit pixels.

## 2. Frame timing (expect the same design on Xbox)

**PC timer and main loop.**

- The QPC timer is set up at `0x4a12a9`. The frequency goes to `0x99caf0` and `1/freq` to
  `0x99caf4`.
- The per-frame tick is `0x4be6e0`. It updates the subsystems, renders (`0x4a5550`), then calls
  the frame pacer `0x4828e0`.

**The frame pacer (`0x4828e0`).**

- The target frame rate is the int at `0x562adc`, and is **62**. When it is above 0, the pacer
  busy-waits on QPC.
- The timestep is **variable**. Delta seconds go to `0x99ce88` and are clamped to [0, 0.1].
- The first frame uses a delta of 1/29.97 (`0x538a70`), and there are unused 33 Hz "task" ticks.
  Both are leftovers from the 30 fps console version.

**Higher frame rates.**

- With the cap set to 0, ROTK Fix runs the game at 90, 120 and 180 fps with no speed problems,
  because everything is driven by delta time. Movie playback is driven by delta time too.
- On Xbox, look for the same pacer and the same 0.1 clamp, probably capped or vsynced at 30. A
  60 fps option should only need the cap or vsync interval changed.
- Small stalls worth removing: a `Sleep(50)` in input teardown (`0x4b13f0`, called on screen
  changes and movie starts), and a `Sleep(50)` while paused or inactive (`0x4bf002`).

## 3. Camera, field of view and widescreen

**The projection matrix.** The game builds a normal perspective matrix from the camera struct
(+0x50) and sets it with `SetTransform(PROJECTION)` at `0x4a337a`.

- In the gameplay camera, `_11` is **negative** (mirrored), and `|_22/_11| = 1.3333`: the game
  assumes 4:3.
- The vertical FOV is 46.8° (60° horizontal at 4:3). Other cameras use 52.2°, 55.8° and 64.4°
  (level select animates 64.4° → 54.2°). The menu camera is 11.3°.
- Depth is reversed: `_33 = -0.000117`, `_43 = 0.4775`. The near plane is about 0.48 and the far
  plane about 4081 units.
- The float at `0x562190` (60.0) is a helper value, not the gameplay FOV.

**How ROTK Fix fixes widescreen**, all in SetTransform:

- A **perspective** matrix has `_34 ≈ 1, _44 ≈ 0`; an **ortho** matrix has `_34 ≈ 0, _44 ≈ 1`.
- Vertical FOV = `2*atan(1/_22)`, and the assumed aspect = `|_22/_11|`.
- Hor+ for wide screens: `_11 *= gameAspect / screenAspect`. Vert+ for screens narrower than 4:3:
  `_22 *= screen / gameAspect`.
- The player's FOV % scales the matrix. Cameras under 25° (the menu) are left alone.
- A frame counts as "gameplay" when it has 40 or more perspective draws.

**Resolution.** The PC game only offers 4:3 modes (the filter at `0x4a443a`:
`(w*3)>>2 == h`) and defaults to 640 wide. The 2D art was made for 640x480.

## 4. Movies

| | PC | Xbox |
|---|---|---|
| Files | `Data\Movies\<name>.avi`, and `Data\Special\<name>` for *SPC* names | `Data\Movies\<name>.vid` |
| Container | AVI | EA's own chunked `.vid` (below) |
| Codec | DivX 5.03 (fourcc `DXGM`), MPEG-4 ASP, no B-frames | DivX (`LLVideoDivX_XBox.c`) |
| Size and rate | 512x480, 23.976 fps | **640x480, 29.97 fps** (30000/1001) |
| Audio | none in the file | none in the file (video chunks only) |

**The Xbox `.vid` layout.** All chunks are a 4-byte tag stored reversed, then a u32 size of the
whole chunk.

- `1DIV` (VID1): the file header, 0x20 bytes.
- `DAEH` (HEAD): 0x40 bytes. It contains `HDIV` (VIDH, 0x20 bytes):
  `u32 1, u16 width 640, u16 height 480, u32 frame count, u32 largest frame (?), u32 30000, u32 1001`.
  For example, logos.vid has 550 frames and crakfmv1.vid has 724.
- Then one `MARF` (FRAM) chunk per frame, with a 0x20-byte header. Each holds one `DDIV` chunk:
  the DivX frame data, with a 0x10-byte header before the bitstream.
- The frames carry no MPEG-4 start codes, so feed them to the decoder as raw VOPs (as the AVI
  reader does). Treat this as a first look: check it against a decoder before relying on it.

**Story FMVs** have black bars baked in. On PC the active rows are 66..416 of 480, which is about
1.83:1. ROTK Fix scales them to fill the width; the PC constant is 480/352. The logos movie mixes
letterboxed New Line footage with a full-frame 4:3 EA logo.

**Movie script, PC.** The command handler `0x50ae50` builds the path, `0x50abd0` starts playback,
and a table at `0x572e00` holds the type of each movie (LOGOS = 0, ATTRFMV1 = 1, story = 2). The
movie clock adds the delta to `[movie+0x11a4]` and decodes up to 3 frames per update.

## 5. HUD and 2D

**How the HUD is drawn.**

- 2D uses FVF `0x142` (XYZ | DIFFUSE | TEX1) through a fixed ortho matrix, not pre-transformed
  vertices. The only `XYZRHW` draw (FVF `0x1c4`) is the movie quad.
- UI positions are fractions of the screen size: the words at `0x569c24`/`0x569c28` are multiplied
  by normalized coordinates.
- The game never uses DrawPrimitiveUP. It uses vertex buffers, all created with FVF 0.

**How ROTK Fix recognises a HUD picture** (for sharpening). A draw counts when all of these hold:

- FVF is 0x142 and a texture is bound.
- The texture isn't a render target, a screen copy or a font texture.
- No pixel shader is set.
- Stage 0 combines TEXTURE and DIFFUSE, with `COLOROP == ALPHAOP`, which is **MODULATE or
  MODULATE2X**. The HUD itself uses MODULATE2X.
- Stage 1 is DISABLE, or SELECTARG2(CURRENT).

**Keeping the HUD 4:3 versus stretching it.** ROTK Fix keeps HUD pictures at their 4:3 shape and
moves edge-anchored pieces outward. Full-width bands and full-screen effects stay stretched. The
tests, done in the ortho space, are in `graphics.c`: spanning draws, bands under 25% of the
height touching the top or bottom, untextured draws, and screen copies. The stretch is capped at
16:9.

**Fonts.** A4R4G4B4 managed textures, with 0x28-byte glyphs. The loader is `0x49b740` and the
draw function `0x49cc50`.

## 6. Textures

**Creation and formats.**

- One create wrapper (`0x4a6930`). Every texture is created in the MANAGED pool and filled with
  LockRect.
- Formats: DXT1, DXT3, DXT5, A8R8G8B8 and A4R4G4B4. Cut-outs (foliage, fences) are DXT1/DXT3 drawn
  with alpha test, ALPHAREF = 2.
- A level load creates 400+ textures in about 1.2 s.

**Texture-pack hash** (`src/textures.c`, `examine()`). This is a modified FNV-1a 64:

```c
h = 1469598103934665603;                        // FNV offset basis
h = (h ^ Width)  * 1099511628211;               // FNV prime
h = (h ^ Height) * 1099511628211;
h = (h ^ (DWORD)Format) * 1099511628211;        // the D3DFORMAT value
for each row of level 0 (no pitch padding):
    for each 8-byte little-endian word v:  h = (h ^ v) * 1099511628211;  h ^= h >> 29;
    for each leftover byte b:              h = (h ^ b) * 1099511628211;
```

- Only mip level 0 is hashed.
- For DXT formats, rows = (H+3)/4, and row bytes = (W+3)/4 × 8 (DXT1) or × 16 (DXT2–5). For other
  formats, row bytes = W × bytes per pixel.
- File names are `<w>x<h>_<16 hex>.png`. A pack file matches when its name ends in those 16 hex
  digits; a prefix is allowed.
- Folders: dumps go in `ROTKFix\textures\dump\`, and packs anywhere under
  `ROTKFix\textures\replace\`.
- The hash is computed at the texture's first UnlockRect while dumping, otherwise at its first
  SetTexture. Textures unlocked more than 3 times (movie frames) are skipped.

**Matching hashes between PC and Xbox.** For a pack to work in both versions, the Xbox port should
hash the same way, over the same bytes:

- **Same format:** convert the texture to the same D3DFORMAT the PC uses.
- **Same layout:** un-swizzle Xbox textures first; Xbox stores non-DXT textures swizzled.
- **Same size:** many Xbox textures are smaller than the PC ones (see section 1), so those can
  never match. Matching will only work where the data is identical.

It may be simpler to give ROTK Xbox its own packs, and only share where hashes happen to match.

## 7. Other PC findings that will probably apply

- **Gamma:** brightness, contrast and gamma floats at `0x569c2c/30/34`. The ramp is built by
  `0x4a5010`.
- **Input:** two-player co-op, with bindings per player and mode. The PC has empty stubs for the
  Xbox rumble calls (`0x4a9340` heavy, `0x4a9350` light, `0x4a9360` stop); on Xbox these are the
  real rumble functions, a good thing to match.
- **Sound:** DirectSound plus EAX on PC. The Xbox uses its own sound banks (`bnk` resources in
  `.scx`), which is why the Xbox level files are bigger.
- **Settings:** the PC keeps them in `Documents\ROTK.ini` `[Video]`: Enable32Bit, EnableShadows,
  EnableSimpleShadows, AlternateCameras, LowResTexture and SfxMode.
- **Debug leftovers:** developer menu buttons (hidden by `0x4f6f90`), a Codes screen, and a debug
  command that triggers rumble. The Xbox build may still have more of these.

## 8. PC versions, for reference

| Copy | Size | MD5 |
|---|---|---|
| Exe without the SafeDisc layer | 1,703,936 | `2b5ddee3e5957f3b4c3673feb561c86e` |
| Disc exe (SafeDisc) | 2,874,240 | `3d00346256ef09082393f9afdf317c98` |

Both copies have PE timestamp 0x3f807074 and were built with MSVC 7.0. The image base is
0x00400000.
