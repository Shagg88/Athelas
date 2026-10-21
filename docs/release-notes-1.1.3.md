# Athelas 1.1.3

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept. One version number for everything now: Athelas, ROTK Fix and both Xbox ports are all 1.1.3.

## New

- **Make a problem report** (About tab, and after a crash). One click puts the logs, the settings, the newest crash
  dump of each Xbox port and a description of your PC (Windows, processor, graphics card and driver, screens,
  controllers) into one zip in the `Reports` folder, and opens a new bug report on GitHub with those details filled
  in. Write what happened and drag the zip in. Athelas sends nothing itself, and your Windows user name is taken out
  of the files.
- **The launcher works fully with a controller** (Steam Deck, TV). The D-pad moves through every page and setting,
  LB / RB change tabs, Menu (Start) plays and View (Back) saves from anywhere, B closes dialogs, and a setting's
  explanation shows when the cursor is on it. A line under the summary names the shortcuts in your controller's own
  button names.
- **Stutter lines say more** (both Xbox ports). Each slow frame in `tt.log` / `rotkx.log` is now followed by what
  the game spent that frame on: reading files, waiting, sleeping, and the slowest file read. If you report stutter,
  please make a problem report: this is what we need to fix it.

## Fixes

- **The Two Towers: the lake at the Gates of Moria.** Wading through the water left ripples that grew into big
  bright stripes across the lake and never went away. The lake's ripple simulation is now read the way the game
  expects, so ripples spread and fade as on the Xbox.
- **The Two Towers: "Press Start" with the keyboard** ([#8](../../issues/8)). The title screen takes only Start, and
  in the menus the keyboard's Esc goes back, so nothing on the keyboard got past it. Enter (the A key) now presses
  Start as well in the front end, so it goes on from the title screen and works in the menus as before.
- **Blocky textures at high resolutions** ([#1](../../issues/1)) (both Xbox ports). Some textures the game enlarges
  without smoothing (moss, reeds, grass and other cut-out plants, and a few others) now enlarge smoothly at higher
  resolutions instead of showing big square pixels. At the Xbox's own resolution nothing changes.
- **Save times in your own time zone** (both Xbox ports). Saved games showed the time in UTC; they now use your PC's
  time zone and daylight saving.
- **The Two Towers: the save screen's layout on wide screens.** Its animated map counted as gameplay, so the
  screen's pictures and text were spread to the edges as the HUD is; they now stay where the game puts them.
- **Windows on ARM: its own complete download.** `Athelas-1.1.3-arm64.zip` has everything the usual zip has plus the
  native ARM64 builds of the Xbox ports, and updates on an ARM PC fetch it. If the ARM64 builds in the folder are
  from another version, the launcher uses the usual ones and offers to install the right ones.
- **The About tab checks the ROTK Fix files.** It shows their version and warns if the game folder, or Athelas's
  own copy, has an older one.
- **Remove ROTK Fix keeps your achievements and texture packs.** It deleted the whole `ROTKFix` folder, with the
  achievements earned and any texture packs in it; those now stay.
- **Windows on ARM: "One processor core" for ROTK Xbox is saved.** The launcher didn't write the setting, so ticking it
  did nothing.
- **Games on a drive that isn't plugged in stay in the launcher's list.** A game on a USB drive or network share that
  was offline when the launcher started was dropped from the list for good; it now comes back when the drive does.
- **The launcher and a controller:** pressing Start or Back to stop setting buttons no longer also starts the game or
  saves, and a controller only works the launcher while its window is in front.
- The launcher works from a folder whose name has letters outside Windows' language setting (a Cyrillic user name on
  English Windows, say), and a crash dump too big for a problem report is left out instead of cut in two.
- The Two Towers' unused Network setting is gone from its settings (the game has no network play).

## Docs

- README: how to make a problem report, the launcher with a controller, and stricter Windows on ARM emulation for
  ROTK on PC if it crashes at random on an ARM PC.

## Checksums

`Athelas-1.1.3.zip` SHA-256: `b6f2c827852b4d4a21a3acf92810b5133d69733deac2b57d9bfbbdb10b652f9a`
`Athelas-1.1.3-arm64.zip` SHA-256: `0722277b05f914365ad43ad08daa5538be05d38951303a87717194d582df99ee`
