# ROTK Xbox port, 07: ultrawide screens (2026-10-13 night)

The port showed at most 16:9: on a 21:9 screen the game sat in a 16:9 picture with bars at the sides. It now fills
ultrawide screens the way ROTK Fix does on PC.

## SDK (ROTK patch v11)

- **`app::FrameAspect()` and `app::g_max_frame_aspect`.** The frame is shown at the output's shape, between 16:9 and
  `g_max_frame_aspect`. The default is 16:9, so a game that doesn't opt in is unchanged; ROTK sets 32:9.
  - BlitFrame and Screenshot use it.
  - The headless harness takes `ASPECT=2.37`.
- **`MovieFitAspect()`.** Fit shows the middle of the 4:3 frame at the screen's shape: 16:9 up to the letterboxed
  films' own 1.84:1 (rows 65 to 413). Wider screens get bars at the sides instead of losing the top of the film, as
  ROTK Fix does.
- **`OrthoExtent`.** It also measures fixed-function draws fed by Begin/End or pushed vertices: a declaration without
  a program, with the position a FLOAT3 or FLOAT4 in register 0. The menus draw that way (vs_handle 0x143, 0x43), so
  before this they were never placed.

## Game (`src/game/rotk_widescreen.cpp`)

- **3D.** Viewports (0xA2B50), the camera angle (0xA32F0) and the renderer's _11 fallback use FrameAspect: Hor+ at
  the screen's shape, with culling to match.
- **2D (`Place2D`, which replaces HudPlace).**
  - On screens wider than 16:9, menus, text and the HUD are held at 16:9 in the middle, as ROTK Fix does (its
    HUD_MAX_STRETCH).
  - Left full width: untextured draws (fades), screen copies and movie frames.
  - Gradients and shading bands stretched across the screen keep the whole width at any shape. Held at 16:9 they
    would end in a visible edge, as the bottom shading did in the first try.
- **Finding: hud_edges has almost no effect.** The HUD, like the menus, draws through Begin/End. The SDK couldn't
  measure those draws before, so hud_edges only ever moved vertex-buffer draws (and movie frames, now exempt).
  - Applying it to the Begin/End draws breaks the HUD's pieces apart (the health bar's segments go separate ways)
    and squeezes the menus to 4:3.
  - So it stays limited to vertex-buffer draws, and 16:9 looks exactly as before.
  - Making hud_edges work for the real HUD needs piece grouping that holds up for Begin/End text and bars. That's
    still to do.
- **Test build at ASPECT=2.37.**
  - Helm's Deep fills the width, with the HUD and prompts inside the middle 16:9.
  - The save screen and main menu sit at 16:9 between black sides.
  - The bottom shading spans the whole width.
  - At 16:9 with hud_edges on, the menus are unchanged.

## Known limit

The game still renders a 4:3 frame that is stretched sideways. At 2560x1080 that's 1440 pixels across stretched to
2560, so the picture is softer across than at 16:9. A render_percent above 100 makes up for it, at a cost in speed.

## First Windows play at 2560x1080 (23:03)

- **Working.** The frame is 2.37:1 and the viewports were made 2.37:1. Gameplay fills the width, with the HUD inside
  the middle 16:9. The ISENFMV1 film is shown 1986x1080 (1.84:1, bars at the sides). The menus sit at 16:9.
- **Fixed: stray menu effects at the screen edges.**
  - The rings round the save screen's arrows and the cursor's light streak were thrown out to the screen edges.
  - The rings are vertex-buffer draws, which hud_edges moved to the edges of the full 2.37 picture. On ultrawide
    screens everything is now held at 16:9, hud_edges included.
  - The streak is a wide quad of a small texture that was kept full width as a "gradient". Only small textures over
    the whole screen height keep the full width now.

## Second play at 2560x1080 (23:10): blue glows beside the main menu

The blue blobs outside the menu picture weren't 2D. They were the level-select hall's magic effects: the menus draw
their 3D scenes behind the full-screen picture, and once the picture is held at 16:9 the scene shows at the sides. The
test build's reference renderer doesn't draw those effects, which is why it never showed them.

**SDK `gpu::g_frame_sides`.** A game can ask for the frame's sides to be painted black, as a centred share of the
width. It's reset after each frame. BlitFrame and Screenshot clear the side columns. (ROTK Fix also paints the bars
black on menus, from its Present.)

**Game.** On an ultrawide screen, a full-screen menu picture (spanning the width and the height, large texture) sets
it to its 16:9 share.

ROTK patch v12.

## HUD at the screen edges, working (hud_edges)

**Grouping.** The HUD draws through Begin/End, a piece at a time:

- bottom left: portrait, sword, bar segments, digits, ring
- top right: the button prompt

Each piece's box is kept. At the next frame (the game's counter [0x1E2F04]) they're joined into groups of touching
boxes, with a gap of 0.04 in clip space:

- A group from the left quarter to no further than 0.1 past the middle is a left group; the mirror is a right group.
- Anything else is a middle group: the pause menu's panel, subtitles, messages.

**Placing a piece.** Every piece keeps the HUD's 4:3 shape (a = (4/3)/frame) and follows:

- the group its centre falls in, or
- a group it overlaps (pieces new this frame, such as the flashing low-health bar), or
- its own extent, judged by the strict rule.

Left groups' left sides go to the screen's left, and the mirror for right groups. Middle groups stay centred.

**Measured boxes** (test build, Helm's Deep):

| Piece | Box |
|---|---|
| Portrait | −0.91 … −0.66 |
| Bars | −0.62 … −0.09 |
| Ring | −0.22 … 0.03 |
| Prompt | 0.70 … 0.99 |
| Pause panel | −0.42 … 0.44, with its ornaments touching it |

**Menu frames.** A frame with a full-screen menu picture is a menu's frame: nothing moves to the edges, and on
ultrawide screens it's held at 16:9 with black sides. Untextured draws: a full-screen fade keeps the full width;
anything smaller is held at 16:9 on ultrawide screens.

**Tested** at 16:9 and 2.37:1. The HUD sits at the left edge and the prompt at the right; the pause menu stays
whole, including its opening frames.

**Test build.** The null backend now asks `g_ortho_place` on every frame, as the renderer does, so state kept across
frames matches Windows. `HUD_BOXES=1` logs each 2D draw's box.

## Sharper picture across: render targets at the screen's width (SDK `gpu::g_wide_targets`)

The game's 4:3 frame used to be rendered at 4:3 (1920x1440 at 1440p) and stretched to the screen's width. With
`g_wide_targets` (ROTK sets it), targets get FrameAspect/(4/3) more width: 2560x1440 at 16:9, 2560x1080 at 21:9. The
frame is drawn at the screen's own resolution, sharper across at 16:9 too.

Per-axis scales where one was assumed:

- SetViewportScissor (now given the guest height)
- ScaledRect (clears)
- CopyByDraw and CopyByRegion (y uses its own scale)

ApplySettings remakes the targets when the width multiplier changes, and the log line gives the scale across.
Projections, viewports and texture coordinates are in guest units, so nothing else changes. The test build has no
D3D12 renderer, so this one is only proven on Windows.

ROTK patch v13.
