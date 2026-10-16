# ROTK Xbox port, 05: fixes from the first play through the launcher (2026-10-13 evening)

The user played at 180 fps from the launcher with 16 screenshots, taken with the backslash key. F3 only writes a
thread dump unless XBE_CAPTURE_DIR is set, which capture.bat does. Reported problems:

## Black bars on every side

The Xbox game keeps a TV-safe border: 64 pixels across ([0x1E2F08]) and 32 down ([0x1E2F18]; 16 in field mode).
These are set at 0xBF131/0xBF13A, 0xBF1FC/0xBF205 and 0xBF25C/0xBF265, and every viewport is inset by them (0xBE720).
On the user's 2560x1440 screen that left 128 black pixels at each side and 48 at the top and bottom, which ROTK PC
doesn't have.

`[[hook]]`s at 0xBF143, 0xBF20E and 0xBF26E (`ROTK_SafeArea`) zero both values. `[graphics] safe_area` (off) keeps the
console's border. The test build's frames now reach all four edges, and movies fill the frame too.

## Gameplay black edges and floating trebuchets (widescreen)

Only the renderer's projection was widened, so the game still culled for 4:3 and the ground at the sides wasn't
drawn. The fix is now at the source:

- **Viewports.** 0xA2B50 (Viewport.c) makes viewports with a 1 x 0.75 shape (+0x14, +0x1C) and a 4:3 aspect (+0x24,
  +0x2C). With widescreen they're made 16:9.
- **Cameras.** 0xA32F0 builds a camera's frustum and projection from its horizontal angle (camera = [ctx + 4], radians
  at +0xE0) and the viewport's aspect. The game keeps the horizontal angle, so the angle is widened for the call to
  keep the console's vertical view (Hor+). FieldOfView scales the vertical angle there too, as ROTK Fix does
  (cameras under 25 degrees are left alone, 150 at most). The camera's value is put back after the call.
- **Result.** The log shows "3D camera 35.2 deg at aspect 1.7778", the same vertical angle as 4:3, now culled for the
  wide picture. The renderer-side _11 scaling still covers any camera made some other way.

## Movies cut off

"auto" (fit) cropped every movie to its middle 16:9, The Two Towers' rule, which cut the full-frame opening logos.
It now works as ROTK Fix's auto:

- **Films.** Shown as their middle 16:9 when they have black bars. They span rows 65 to 413 of 480 once the border is
  gone, so the 16:9 cut (rows 60 to 420) fits them.
- **Logos.** Shown whole at 4:3 when the picture reaches into the bar area.
- **How a frame is checked.** The player draws each frame from three planes. The Y plane is 768x544, LIN_AL8, in
  stage 2. The SDK's new `gpu::g_draw_observer` hands each draw to the game. Rows 69 and 475 of that plane (screen rows
  50 and 430) are checked: both black means a film.
  - Bright bars switch to the whole frame at once.
  - After six seconds of black bars it goes back to 16:9, because the logos have dark moments.
  - In the test build: logos shown whole from 21.5 s, the film at 16:9 from 30.3 s.

The movie flag (`gpu::g_movie`, which cutscene_audio_delay and movie_display depend on) now comes from these draws.
The reader 0xC46A0 decodes ahead in bursts, so the flag used to drop between them.

## D-pad far too fast at 180 fps

The menus (0xFD730) count frames while a direction is held. The counter is [0x1DA4A8]: a step at 10, then every 2
frames, at 0xFDB13, 0xFDC84, 0xFDDE3 and 0xFDEC8. At 180 fps that's six times the console's speed.

`[[hook]]`s there (`ROTK_MenuRepeat`, unlocked frame rates only) let the counter go up once per 1/30 s of real time.
The menus repeat after a third of a second, then 15 steps a second, at any frame rate. The game's virtual button bits
(table 0x1E3120): d-pad up 0x100, down 0x200, left 0x40, right 0x80.

## Button pictures

PlayStation symbols are now plain light lines, not the console maker's colours. Those colours are its trade dress,
which is the line ROTK Fix drew.

## SDK: ROTK patch v6 (replaces v5)

`gpu/backend.h`: `g_movie_letterbox` and `g_draw_observer`. `gpu/d3d12.cpp`: the observer call, and fit only for
letterboxed movies. The test build's null backend calls the observer too.

## Second play (20:48): movies

- **Paths of the Dead film shows 4:3.** That is right for this file. PAT1FMV1.vid is a full-height 4:3 picture: the
  PC's pat1fmv1.avi is the letterboxed film, and the Xbox copy is cut down to 4:3. Proportions match, so it is not
  squeezed. HELMFMV1's film part is letterboxed and fills the width. The bar check shows each file as it is.
- **Movies blurrier than PC.** ROTK Fix (its own notes, 2026-10-13) does not upscale the PC movies. It sharpens at
  the movie's own texel size before the bilinear scale to the screen. The SDK kept each movie frame at 640x480, which
  is smaller than the decoded picture (about 704x512), then scaled it up 3x. Frames are now kept at up to twice the
  console's size (1280x960), so movie_sharpening works at about one movie pixel, as ROTK Fix's does.
- **Movie flag.** Before the movie flag fix, the flag dropped between the reader's bursts. So movies sometimes
  skipped the movie path (sharpening, fit, audio delay) altogether.
- **SDK.** ROTK patch v7 (`KeepMovieFrame`) replaces v6.

## Third play (21:13): movies squeezed, logos wrong. The cause was hud_edges

The films showed pillarboxed at 4:3 with the picture squeezed sideways, and the logos were cut. In the test build
with `widescreen` and `hud_edges` both on, the game's own 640x480 frame had the movie in cols 80 to 560 only. The
player draws each movie frame as a full-screen textured quad through the orthographic 2D path, and `HudPlace` kept
it at 4:3 as a "full-screen picture". movie_display then cropped and stretched that squeezed frame. The test runs
before this one had hud_edges off, so they never showed it.

- **Fix.** `HudPlace` leaves movie draws alone (Y plane 768x544 in stage 2).
- **What the movies really are.** With that fixed, HELMFMV1 (logos and film) and PAT1FMV1 are both letterboxed at
  rows 65 to 413 across the full width, the same as the PC's .avi files. The earlier readings in this note were made
  from squeezed frames and were wrong: PAT1FMV1 is not a 4:3 cut, and no film is anamorphic.
- **Shape by name, as ROTK Fix does it.** Names with FMV (except ATTRFMV1) use fit: the middle 16:9, filling the
  width. Everything else is 4:3. The bar-row check and kMovieWide's use are gone. The SDK keeps `kMovieWide` as an
  option for other games.
- **Logos.** They're inside the band too, so they now fill the width. The small "EA GAMES is an Electronic Arts
  brand" line sits at row 428, just under the 16:9 cut (rows 60 to 420).
- **SDK.** ROTK patch v8 (`g_movie_shape` with kMovieFit, kMovieWide and kMovie43, replacing `g_movie_letterbox`)
  replaces v7.
