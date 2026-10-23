# Athelas 1.5.1

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept.

## The same settings in all three Xbox ports

- **The Return of the King (Xbox) has its own settings page**, laid out like The Two Towers' and Fellowship's: the
  quality presets (Original, Balanced, High, Ultra), the button layout on the Controls tab, and every option of its
  own as a setting (damage, unlock all, Éowyn, the cheats, translation subtitles, second controller), with a Network
  tab for co-op. Up to 1.5 it shared the PC version's ROTKFix.ini; the first time Athelas opens the new page, your
  choices and button layout are brought over from it. (ROTK Fix's Special button and layouts using Back, Start or
  the D-pad have no counterpart in the new layout and go back to the game's own.)
- **Button pictures** in The Two Towers and Fellowship (Controls tab), as ROTK already had: the game's hints and
  menus show Xbox, PlayStation or Nintendo buttons, or the keyboard's keys. Auto follows what you play with.
- **Fellowship: Sharper text** (Graphics tab), as ROTK has: the game's text (menus, hints, dialogue, the quest
  journal) is drawn at three times its size, so it stays sharp at high resolutions.
- **The Two Towers:** **Unlock all levels and characters** (every level, Isildur and the extras from the start, as
  once the game is beaten; your saved progress isn't changed) and **Smooth leaf edges** (with anti-aliasing on).
- **The Return of the King (Xbox):** **Skip opening logos** (once you have a save, the game opens with the EA and
  New Line logos; this goes straight past them).
- **Movie subtitles** (all three, Graphics tab, Movies): the games have no subtitles for their film clips, so the
  ports now show them from subtitle files: put an .srt file named as the movie (`fmv_2.srt`, `HELMFMV1.srt`,
  `mont.srt`; the log names each movie as it starts) in the game's `subtitles` folder (Open folder on the same tab).
- **F1 hides the HUD** in The Return of the King and Fellowship too (for screenshots), as it did in The Two Towers.
- The damage settings run from 10% to 500% in all three.
- Some of ROTK's options aren't in the other two because there they would change nothing: draw distance (neither
  game ever cuts off distant scenery), the TV-safe border (both fill the screen already), Skip any video in
  Fellowship (all its movies skip already), and HUD at the screen edges in Fellowship (its own widescreen mode puts
  the HUD there).

## Fixes

- **DLSS didn't upscale** in the Xbox ports since 1.5: NVIDIA's Streamline turned down each frame, so the game showed
  its render resolution stretched instead (and wrote an error to the log at every frame). DLSS now runs; if an
  upscaler ever turns a frame down, the log says so.
- **With DLSS on, closing the game could end in "the game closed unexpectedly"**: NVIDIA's DLSS files crashed in
  their own clean-up after the game had already closed. The game now ends without running it.
- **The Two Towers, keyboard: Enter didn't press Start on the title screen** ([#8](../../issues/8)), since 1.5's
  fix for #17. Enter presses Start on the title and main menu again; the Yes/No pop-ups between levels still work.
- **Xbox 360 controllers (wireless receiver) not working** in the Xbox ports ([#18](../../issues/18)): Xbox
  controllers go through XInput again (other controllers still through Windows.Gaming.Input).
- **The Fellowship of the Ring: crash entering Bywater** ([#21](../../issues/21)) while the game cleaned up after
  the scene change: an object's data had been written over. The clean-up now checks it and leaves damaged parts
  out instead of closing.

## Checksums

- `Athelas-1.5.1.zip`: `5872b59dd0eb7c3b6920168cb8bb61739bde99ccbba75c41b7572daebe3451fb`
- `Athelas-1.5.1-arm64.zip` (Windows on ARM): `6d3982543ebfffd1487a9dddbeb42aacf9cd836de1d804a395be2c1a982fb780`
