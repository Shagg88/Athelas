# Athelas 1.5

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept.

## New: FSR and DLSS upscaling (all three Xbox ports)

- **Upscaler** on the Graphics page (and the in-game menu, F10): **AMD FSR** (FSR 4.1 on the newest Radeon cards,
  FSR 3.1 on every other card, NVIDIA and Intel included) or **NVIDIA DLSS 4.5** (any GeForce RTX card). The game is
  drawn smaller and rebuilt sharp and smooth from several frames: faster, and it smooths jagged edges better than
  anti-aliasing. **Upscaler quality**: Native (full size, smoothing only: DLAA with DLSS), Quality, Balanced,
  Performance, Ultra performance.
- These 2002-2003 games never told the graphics card how things move, which both upscalers need: the ports now work
  it out for every object, every frame. A few effects (some particles, CPU-animated characters) rely on the
  upscalers' own detection.
- DLSS takes effect the next time you start the game. Not on Windows on ARM (AMD's and NVIDIA's files are x64 only).
  The files are in Athelas' new `upscalers` folder, under AMD's and NVIDIA's own licences.

## New in The Fellowship of the Ring (Xbox)

- **Invert camera** ([#14](../../issues/14)): two settings on the Controls page and in the in-game menu (F10),
  *Invert camera up/down* and *Invert camera left/right*, for the controller's right stick. Off by default.
- **ARM64 build:** Windows on ARM PCs (Snapdragon laptops) now run Fellowship natively, as The Two Towers and The
  Return of the King already did. The launcher picks it on its own.

## All three Xbox ports

- **Invert mouse up/down** (Controls page and F10 menu): reverses up and down for mouse aiming. Off by default.
- **Texture packs, compare as you play:** **F8** switches texture packs off and on. A note in the corner says when
  they are off; screenshots never show it. **F7** saves a matched pair of screenshots, one with your packs and the
  very next frame without, to the Screenshots folder (`..._pack.png` and `..._original.png`).

## Performance

- **Less stutter the first time you play** (all three Xbox ports): the first time the game met a new effect, it
  paused to prepare its shader. 1.5 ships the shaders of every level, recorded beforehand, and prepares them in the
  background when you first start a game (while you're in the menus). Later starts are as before.
- **The Two Towers uses far less processor time** at 30 and 60 fps: while waiting for the next frame, the game asked
  for a zero-length pause about two million times a second, keeping a whole processor core busy for nothing. It now
  sleeps until the screen's next refresh and wakes at the same moment as before. About 90% less processor time at
  the same frame rate, so less heat, fan noise and battery drain on laptops and the Steam Deck.
- **Frame rate caps above 60 fps** (all three Xbox ports) hold their rate with Windows' high-resolution timer instead
  of spinning for the last 2 ms of each frame: at 240 fps, about 2% of a core instead of about 30%.
- **The Fellowship of the Ring:** the drawing commands the game records ahead of time are read in larger pieces,
  about twice as fast (they were the largest part of the port's own work per frame).
- **All three Xbox ports:** the renderer reuses the previous draw's texture views when they are the same, which
  they are for more than half the draws.
- **The game's own code runs faster** (all three Xbox ports): the translated game code now keeps the console's
  floating point math (positions, angles, animation) in fast local storage instead of going through memory for
  every step, and clears memory in one go instead of one element at a time. In The Fellowship of the Ring that is
  about 12% less processor time for the game itself per frame; it helps most on slower processors (Windows on ARM
  laptops, handhelds).

## Mac

- **Playing on a Mac through CrossOver:** a new Mac section in the README with the steps. Athelas and the Xbox ports
  were tested in a Wine setup like CrossOver's (DirectX 12 translated); real Mac reports are very welcome. Logs now
  say when a game runs under Wine, and on which system ("on macOS"), so Mac reports explain themselves.

## Fixes

- **The Two Towers, keyboard: Yes/No pop-ups between levels closed at once** ([#17](../../issues/17)). Saving and
  buying upgrades work with the keyboard again. Enter (and the left mouse button) presses Start only on the title
  screen now, where "Press Start" needs it.
- **The Fellowship of the Ring: crash leaving Bilbo's room, Galadriel's mirror and the Lothlórien forest.** At these
  scene changes the game frees textures and buffers that are still set; the port read them anyway and the game could
  close. It no longer reads them. Found while recording the shader seeds.
- **ROTK on PC: no control in game with the keyboard** ([#15](../../issues/15)). With controller support on, player
  1 always got the controller layout, which has no keyboard controls for play, so without a working controller
  nothing moved. Now player 1 gets the controller layout only when a controller is connected as you press Play, and
  the game's own keyboard and mouse controls otherwise. A missing SDL3.dll in the game folder is put back, and
  turning controller support off gives the keyboard back as well.

- **Under Wine and CrossOver: the Xbox ports could close at their second start** while saving achievements.

## Checksums

- `Athelas-1.5.zip`: `7c395254cef64893736a7337824d0bad23964a9bf5c266b56a41eff2ad86151d`
- `Athelas-1.5-arm64.zip` (Windows on ARM): `6647bc08b3b6ae180075be9fd2af894290341d62d741d8d52d58b3cbfbc18e10`
