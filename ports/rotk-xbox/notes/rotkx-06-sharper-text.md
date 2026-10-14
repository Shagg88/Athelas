# ROTK Xbox port, 06: sharper text from ROTK PC's fonts (2026-10-13 night)

## Movies after the HUD-edges fix (21:41 play)

The films fill the width at the right shape. The opening logos are LOGOS.vid, a full 4:3 picture, shown 4:3. ROTK PC
shows its logos at 4:3 too (its 21:29 screenshot has the EA logo pillarboxed).

## Why the PC's menus look sharper

Both releases keep their fonts in Data/Game/GlobScen (.scx on Xbox, .scw on PC):

- **Xbox: three fonts.** A body face at 31 pixel lines and a title face at 38 and 19. They're 4-bit atlases of
  256x232, 512x312 and 256x184 for a 640x480 TV, put in P8 textures (format 0x0B), one colour per coverage level.
- **PC: eight fonts.**
  - The body face at 15, 24, 31 and 50.
  - The title face at 19, 32, 38 and 61.
  - Atlases up to 1024x1622.
- **How the PC uses them.** It picks a size by resolution. The Xbox text is the small size scaled up three times on a
  1440 line screen.

**File layout.** The files are runs of chunks: tag, size including the header, then a 0x40-byte header. The SDAT
payloads, joined, hold the FNTS chunks:

- +0xA: glyph count.
- +0x13: line height.
- +0x14: offset of the glyph records, 12 bytes each: code, w, h, x, y, advance, x offset, y offset, pad.
- +0x1C: offset of the atlas header (width at +4, height at +6), then 4-bit pixels, low nibble first.

Reading the file without removing the chunk headers gives atlases that shift every 0x2000 bytes.

## sharp_text (`src/game/rotk_fonts.cpp`; off by default; ROTKFix.ini `[Graphics] SharperText`)

**Setup.** When the image loads, a thread reads the Xbox GlobScen.scx and ROTK PC's GlobScen.scw. The PC file is the
`Data\Game` folder beside the ROTKFix.ini the launcher passes.

**Matching faces.** Each Xbox font is matched to a PC face by letter shapes ("aegnoRSt", each letter's ink box
resampled onto the other's), and the largest PC size of that face is taken:

| Xbox font | PC font |
|---|---|
| 31 | 50 |
| 38 | 61 |
| 19 | 38 |

**The replacement atlas.**

- It's four times the Xbox one, with the same layout, so the game's glyph coordinates still apply.
- Each glyph is the PC glyph, scaled from its ink box onto the Xbox glyph's ink box, so the Xbox game's spacing stays.
- Glyphs the PC lacks are the Xbox ones scaled up.
- Colours are the texture's own, blended between levels, with mip levels made.

**Finding the texture.** The texture is found by its contents: P8, the atlas's power-of-two size, one level, and the
sum of its indices equal to the atlas's summed coverage. Swizzling doesn't change a sum.

**Without the PC files** the log says so and nothing changes. Nothing of ROTK PC is in the port or the repo.

**Test build.** With `ROTK_INI` pointing at a ROTKFix.ini beside a Data\Game\GlobScen.scw, all three fonts match. The
31 and 19 fonts are replaced at the title and in Helm's Deep. `ROTK_FONT_DUMP=<dir>` writes the replacement atlases
as PNG files; the body atlas lines up glyph for glyph.

The game's own palette is logged once on Windows ("font colours ..."), to check the blend.

## SDK: ROTK patch v9 (replaces v8)

- **`gpu/texture_pack.*`:**
  - `g_builtin_contents`: a built-in replacement found from a texture's contents (format, size, levels, guest bytes,
    P8 colours), asked before `g_builtin`.
  - `MakeLevels`.
- **`gpu/d3d12.cpp`:** calls it in TexturePack.
- **`harness/reference_renderer.cpp`:** previews it.

## Not done yet

- **Menu pictures.** The Xbox menu backgrounds are 256x256 textures. The PC's GlobScen.scw is 5.5 MB against the
  Xbox's 0.7 MB, so its menu art is likely larger too. Matching those pictures would be the next step.
- **Sharpening halos.** Sharpening at 80 puts light halos round scaled-up 2D.

## Menu pictures (`src/game/rotk_menu_art.cpp`, part of SharperText)

**Where the pictures are.** The Xbox title and main menu are full-screen pictures the game decodes at run time into
a 512x448 YUY2 texture (linear, format 0x24). They aren't stored raw in any file. ROTK PC has them as 1024x1024 DXT1
textures in `Data/Game/fro/fro03.scw`:

- `layer1`: the title.
- `mainback`: the main menu.
- `codebkgd`, `coopbkgd` and the character backgrounds (`faraback`...): one texture table.

**Level files.**

- The chunks are the same as GlobScen's, plus `FILL`, which pads to the next 64 KB, and `Rdat` payloads, which are
  compressed.
- The decompressor is Xbox 0xF2CE0. It works on big-endian 16-bit tokens:
  - With 0x8800 set, the low 11 bits give a literal run when bits 12-14 are 0. Otherwise it's a fill: the byte at
    distance ((t>>5)&0x38)|n, repeated (t&0xFF)+3 times.
  - Otherwise it's a copy: length (t>>12)&7 (7 means the next byte + 7), plus 3, from distance t&0xFFF. With bit 15 set
    the copy runs backwards from distance-2.
- `TXHE` tables: 0x50-byte records with name[8], offset at +8, width/height at +0x38/+0x3A and format at +0x3C. The
  pixels start 0x18 bytes after the table.
- Formats: Xbox 0x0C DXT1, 0x0E DXT3; PC 1 DXT1, 2 DXT3.
- The Xbox level-select scenes use the same textures at the same sizes as PC (frttt1 512x512 on both), so nothing
  is gained there.

**Framing.** The PC pictures are framed differently. The Xbox-to-PC texel transforms were measured from both
pictures, with SIFT matches and an ECC refinement:

| Picture | PC x | PC y |
|---|---|---|
| layer1 | 1.8660·x + 68.17 | 2.4779·y − 31.81 |
| mainback | 1.6762·x + 43.97 | 2.3042·y − 5.81 |

**Replacing the texture.**

- **Matching.** A 512x448 YUY2 texture is compared, as a 64x56 thumbnail, with each PC picture seen through its
  transform. The test build's differences: title 15.8, main menu 19.8; the save-screen picture is over 50. The best
  match under 30 is used.
- **Drawing.** It's drawn at 1024x896 from the PC picture, which has about 1.7 times the Xbox detail. It blends into
  the Xbox picture over the last 48 PC texels where the Xbox picture reaches further (the title's bottom rows).

**Not replaced.** The Xbox save and options background (mountains under a fiery sky) is different art from the PC's
coopbkgd, which has an ornate border, so it stays as it is.

## SDK: ROTK patch v10 (replaces v9)

- **`d3d12.cpp`.** Linear textures can be replaced by `g_builtin_contents`. They're normalised by the guest size in
  the shader, so a bigger replacement still lines up. They're never dumped, and a linear texture with no
  replacement isn't cached (movie frames).
- **Harness.**
  - TEXDUMP=2 dumps every drawn texture in every stage, linear ones included, with the raw guest bytes as .bin.
  - Copies are logged.
  - The reference renderer previews contents replacements of linear textures.
