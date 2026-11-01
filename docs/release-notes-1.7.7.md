# Athelas 1.7.7

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept.

## Fixed

- **The Third Age: a crash after some minutes of play** (#40): a streamed sound's next block could arrive just after
  the game had stopped that stream (a scene change or a cutscene), and the game read through the emptied slot. Such a
  block is now dropped (the same crash as #43, "random crash in Moria"). If The Third Age still crashes on its
  file-reading thread, its log now names the exact callback, so we can fix that too.
- **The Third Age: the HUD stretched sideways with widescreen on** (#37): the battle HUD keeps its own 4:3 shape in
  the middle of the screen (with "HUD at the screen edges" off). Menus still widen with the screen, and pieces that
  run across the whole screen keep its width. On ultrawide screens the HUD is now 4:3 too (it was 16:9).
- **The Return of the King (Xbox) at 30 fps: a steady 30.** It ran at about 34, with uneven frames, because a wait
  for the screen's refresh only slept a fixed time. Game speed was always right; now the pacing is too.
- **"The port didn't answer"** (#42, seen under Wine): the launcher gives a port longer to answer the first time,
  says plainly why one didn't (timed out, a missing file, a crash), and writes `port-query.log` in that game's
  folder; problem reports include it.

## New

- **One download for every PC**: Windows on ARM no longer has a zip of its own. `Athelas-1.7.7.zip` carries both the
  x64 and the ARM64 builds of the Xbox ports, and each PC runs the right one. (`Athelas-1.7.7-arm64.zip` is the same
  file, for older launchers on ARM PCs.)
- **Fast forward** (#41): hold **F9** in any of the four Xbox games to speed it up, 3x by default (Gameplay > Fast
  forward speed, 2x to 8x). Sound is muted while F9 is held. Pausing while holding it is fine.
- **Vulkan: smoother HUD and text, and texture packs**: Vulkan now has the "Smooth 2D" setting and texture packs,
  texture dumping and the games' own replacement pictures (button pictures, sharper text), as DirectX 12 has.
- **Launcher front screen**: only the games you've added are on the carousel, and it says how to add one (drag
  its folder or disc image onto the window).
- **Texture dumps name the HUD and text pictures** in The Return of the King too, for texture-pack makers.

## Under the hood

- The four Xbox ports now share one copy of the engine code, and the launcher's code was split up and cleaned up
  (unused code removed, stricter checks, a few small launcher bugs fixed, such as a rare crash reading game art).

## Checksums (SHA-256)

- Athelas-1.7.7.zip: `7e859377e23501326b58b176d6c7497a2f2b61fa25baa94adecdfc0b6417a035`
- Athelas-1.7.7-arm64.zip: the same file
