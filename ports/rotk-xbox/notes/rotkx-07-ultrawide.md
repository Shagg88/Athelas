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
