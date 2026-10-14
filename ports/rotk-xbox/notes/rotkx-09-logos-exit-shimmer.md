# ROTK Xbox: opening logos at 16:9, Exit label after a level, shimmer at RenderScale over 100

2026-10-14, after the user's first run with Exit (25 screenshots, rotkx.log).

## Opening logos fill the width

- With a saved game the game starts with LOGOS (logos.vid) after the copyright page. On a first start the logos
  are the start of HELMFMV1 instead. The old comment in `rotk_movies.cpp` said LOGOS never plays at startup; that
  was wrong.
- LOGOS was shown 4:3. Measured over the whole movie in the harness (640x480 frames):
  - EA: a blue glow over nearly the full height. The logo and text sit in rows 94 to 433, and "EA GAMES is an
    ELECTRONIC ARTS brand" ends at row 433.
  - New Line: in film bars, rows 65 to 413.
- LOGOS is now kMovieFit (fills the width). Its window is placed by a new SDK value, `gpu::g_movie_fit_bottom`
  (440/480), so the window is rows 80 to 440 at 16:9. Films stay centred (0).
- On ultrawide, kMovieFit's 1.84:1 window is rows 92 to 440.

## Exit label after returning from a level

- The main menu reached after quitting a level showed no "exit" text. The item was still there and selectable (the
  focus flare sat under Credits).
- Cause: GlobScen's string table is loaded again with the front end, and LabelExit only ran once.
- Fix: it now looks for the unrenamed "mainMenuOnline\0Online" each time 0x102D50 runs. The scan takes about 11 ms
  and runs only on a menu transition.
- Harness: Pat01 → pause → Exit To Menu → Yes reaches the main menu and labels it.
- Pause menu layout, for pad scripts: Resume, Mission Tips, Show Upgrades, Options, Exit To Menu. Then "are you
  sure?" with the cursor on No; UP then A confirms.

## Shimmer: render_percent over 100 was shown with one bilinear tap

- The user had RenderScale 150 at 2560x1440, which draws 3840x2160. BlitFrame took one bilinear sample per screen
  pixel.
- At 1.5x, the drawn pixels get weights 0.75 / 0.5 / 0.75, so fine detail pulses lighter and darker as the camera
  moves. Sharpening (80) worked on the drawn pixels before this decimation, which made it worse.
- SDK fix (patch v14): BlitFrame first averages the frame down to the shown size with a box of ceil(k) taps,
  through `Shrink` into a kept `shown_` texture. Then it blits that with sharpening at the shown pixel size.
  `BlitFrame` now takes the back buffer's view, to rebind it after the shrink.
- Harness check for a game-side cause: Pat01, walking, every frame, at a fixed 1/180 s step (`ROTK_DT=180`,
  test build only) and at 30 fps. The frame brightness changes smoothly at both rates, so nothing in the game
  flickers frame to frame.
- Other contributors in the user's ROTKFix.ini: TextureLODBias -0.75 (sharper mips, more texture aliasing),
  Sharpening 80, MSAA 8 with AlphaEdgeSmoothing.

## Darkness

- The game sets one gamma ramp at start-up (0xBF0E0 → 0xBEFE0 → SetGammaRamp, params {1.0, 1.1, 0}):
  out = 1.1·in − 0.05, clamped. This deepens the darks, and the port applies it as the console did.
- ROTK PC under DXVK shows its scenes brighter: Path of the Wizard has a median of 26 on PC against 5 to 15 here.
- Faithful default kept. ROTKFix.ini Brightness lifts the darks (v^(1/b)); 1.2 to 1.3 is close to PC.

## Harness additions

- `REF_FROM=n`: no frames drawn before frame n, so REF=1 gives a run of consecutive frames late in a run. Queries
  stay off with it.
- `ROTK_DT=180`: the game's step fixed at 1/180 s (Linux test build).
