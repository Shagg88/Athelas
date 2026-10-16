# ROTK Xbox port, 04: level sweep, game speed, button pictures, 4:3 HUD (2026-10-13 night)

## Game speed above 30 fps

The game's only frame-paced timing is the time step at 0x9EB20, which the port already replaces.

- **Blank counter** [0x1E2F14]: the 0x9EB20 time step reads it, the frame-end wait reads it, and 0xBF0E0 only
  resets it to zero.
- **Frame counter** [0x1E2F04]: read only for its lowest bit (`and eax, 1` before 0xC0BB0 at 0xC72F4 and 0xD035C),
  which alternates between two buffers. It is reset by 0xBEF80.
- **The other use of 1/59.94 s** (0xA4110): turns a length given in 60 Hz frames into seconds. It does not depend on
  the frame rate.
- **The time step's users** (0x68C70, 0x80A40, 0x84800, 0xA0480, 0xD0BD0, 0xF6380, and the tick count at 0x292D0,
  0xF3270, 0xF4030) all read the step, so they follow real time.
- **ROTK PC** has the same engine with a real-clock step clamped to 0.1 s, and ROTK Fix confirmed 90, 120 and 180 fps
  at normal speed. The port's step after a stall is now clamped to 0.1 s too (it was 0.25 s).

## Level sweep (Linux test build)

`ROTK_LEVEL=<name>` (`src/game/rotk_harness_level.cpp`, compiled out of the Windows exe) puts a level in Helm's
Deep's slot in the level table at 0x16CF00. Each level ran headless for 110 s with videos skippable and a pad script
that skips the movie, then fights.

All 17 levels load and play with no warnings, apart from the two known harmless ones (wbinvd, dsound exports):

- Hel01 and Pat01 from earlier runs.
- Pat03, Min02, Pel02, Bla01, Ise01, Min00, Min04, Osg01, She01, Cir00, Cra01, Cir01, Pat02, Pal01 and Pal02: each
  ran 3,800 to 4,300 frames.

Their last frames all show the level drawn correctly with the HUD. The sweep is in `work/sweep.sh` and its results
in `work/runs/sweep/summary.txt`.

## Button pictures (`src/game/rotk_prompts.cpp`)

The game's prompts ("A Speed", "Y Fierce") are 64x64 DXT3 textures. Each holds a disc in its top-left 43x43. The
test build found them in memory (`TEXSCAN=<frame>`) and by draw (`TEXDUMP=1`):

| Picture | Pack hash |
|---|---|
| A, green | 0d3c98d63170de0d |
| B, red | 1893c32b5190d3fb |
| X, blue | 2accbbd50631e250 |
| Y, yellow | 8fc49efe4a49539a |
| white button, white disc | 3ada26e2ce4fdef3 |
| black button, black disc | 5f36f0391f521310 |
| left trigger, grey disc with L | 495a8fcbed4702f9 |

Grey discs reading G, V, I and SX are loaded too; what they mean is unknown, so they are left alone. No R trigger
picture turned up in Hel01, Min00 or Pal01.

**Setting:** `[graphics] button_pictures`, which is `[Gamepad] ButtonNames` in ROTKFix.ini.

- auto: follows the controller on port 1, through the SDK's new `input::Style`.
- xbox (also "original"): the game's own pictures.
- playstation or nintendo: the port's own pictures (`tools/mkprompts.py`, written into `rotk_prompts_data.h` as PNG
  bytes). They are original art in the Xbox prompts' style:
  - PlayStation: dark discs with plain light line symbols.
  - Nintendo: dark discs with white letters, labelled by position.
  - L1/R1, L2/R2 (L/R, ZL/ZR on Nintendo) for the white and black buttons and the triggers.

No console maker's artwork is copied, which is the same rule ROTK Fix follows. A texture pack file with the same
name still wins.

## HUD at the screen edges (`[graphics] hud_edges`, ROTKFix.ini `[Graphics] HUDEdges`, off by default)

With widescreen on, HUD pictures keep their 4:3 shape. Pieces near a side move to that edge of the 16:9 picture; the
rest stays centred. A draw that touches the one before it goes where that one went, so text and bars move as one.

ROTK draws its 2D through an orthographic projection, so the SDK gets a per-draw placement hook:
`gpu::g_ortho_place` with `OrthoExtent`, which applies clip x' = a·x + b·w to the draw's transform. ROTK Fix's tests
keep the full width for:

- untextured draws
- screen copies
- small textures stretched across
- bands along the top or bottom

A full-screen picture keeps its shape, centred. In the test build the left HUD sits at the left edge, the prompt at
the right, and the target ring is round.

## SDK (ROTK patch v5, replaces v4)

- `gpu/vertex.*`: `g_ortho_place` and `OrthoExtent`.
- `gpu/d3d12.cpp`: the placement in the transform, and the built-in texture hook through `CreatePackTexture`.
- `gpu/texture_pack.*`: `DecodePng` and `g_builtin`.
- `core/input.*`: `Style(port)`.
- harness:
  - `TEXDUMP` and `TEXSCAN`
  - reference renderer previews of built-in textures and placement
  - `PAD_STYLE`

## For the launcher

New ROTKFix.ini keys the Xbox port reads:

- `[Gamepad] ButtonNames`: already written by the launcher (auto/xbox/playstation/nintendo/original).
- `[Graphics] HUDEdges = 0/1`: new, Xbox only for now. A checkbox, "HUD at the screen edges", shown only with
  widescreen on.
