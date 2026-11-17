# Athelas 1.8.7

Fixes for The Third Age, The Fellowship of the Ring, The Return of the King (PC) and the launcher.

Update from 1.8.6 with the launcher's **Update** button. Coming from 1.8.3 or before? Extract the .7z over your
Athelas folder with 7-Zip (or Windows 11's Extract All); your settings, saves and achievements are kept.

Athelas is also on RecompLauncher, the free home for recompiled PC games: https://recomplauncher.com/game/athelas

[![Get it on RecompLauncher](https://recomplauncher.com/assets/press/launcher/badge-dark.svg)](https://recomplauncher.com/game/athelas)

## New

- **The Return of the King (PC):** the launcher's **Cheats** section (Invulnerable, Infinite missiles, Always
  devastating, Perfect mode) now works for the PC version too, not only the Xbox one. Changes take effect the next
  time the game starts.

## Faster

- **Xbox games, Direct3D 12:** about 2-4% more frames per second in busy scenes (measured in a Third Age battle on
  an RTX 3080 Ti), by not repeating settings the graphics card already has on each of the 20,000+ draws a frame.
- **Correction to 1.8.5's notes:** they said Vulkan ran a busy Third Age battle much faster than Direct3D 12 (78-84
  fps against 51). That figure came from a timing tool that slows Direct3D 12 far more than Vulkan. Measured without
  it, the two run at about the same speed there, Direct3D 12 often a little ahead.

## Fixed

- **The Third Age:** a crash when loading a save in Eregion (#85). The game keeps a few fixed-size memory pools for
  each area's shaders and models, and some saves need more there than the original 512 KB; they are 768 KB now.
- **The Return of the King (PC):** the game no longer quits when its Video options ask for 16-bit colour in
  fullscreen (#87). Today's graphics drivers have no 16-bit fullscreen modes; ROTK Fix keeps fullscreen at 32-bit.
- **The Fellowship of the Ring:** with Xbox-style button pictures, hints for the original controller's White and
  Black buttons show LB and RB, where today's controllers have them (#82). The block hint showed a plain white disc.
- **The Fellowship of the Ring:** the crickets in Bag End are no longer too loud (#92). The game plays them through
  mixers of its own, which Athelas now has, so they play at the game's own level and only while the game has them
  on, instead of at one fixed level.
- **The Two Towers:** the map screen's burning edges and the selected level's glow are back (#55). The game draws
  them with point sprites (small textured squares, one per particle), which were drawn one pixel each, leaving
  thin orange lines.
- **The Return of the King (PC):** after the credits, the menu no longer flashes with black bars cut into its
  sides while the last film frame fades out (#62).
- **Launcher:** problem reports and About show the right Windows edition (#89). The launcher read an old copy of it
  that Windows keeps for 32-bit programs and doesn't update after an upgrade.

## Known issues

Being worked on; if you see one, please report it with the launcher's "Report a problem" button:
the secret codes in ROTK (PC) (#89) and a graphics glitch at Helm's Deep in The Third Age (#64).

## Checksums (SHA-256)

- Athelas-1.8.7.7z: `34f29164b0333f92a70d6c88a3e8efce08cec47e026cc12393ea9877d688fc02`
