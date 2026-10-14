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
