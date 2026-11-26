# Athelas 1.9.4

Safer saves and achievements, The Third Age's 30 fps setting working as labelled, and a set of fixes from a source
review.

Update with the launcher's **Update** button. Your settings, saves and achievements are kept.

## Fixed

- **Achievements are never lost to a file in use.** If the achievements file couldn't be read when a game started
  (another program holding it), the game wrote an empty one over it. It now waits until the file can be read, keeps
  anything earned meanwhile, and adds them together. A failed write also no longer empties the old file.
- **Earlier saves are always brought over.** The Xbox games' saves moved beside the launcher long ago; older saves in
  the game's own folder were brought over only if the new saves folder didn't exist yet, so opening the **Saves**
  folder before the first Play kept them out of sight. They're now brought over whenever the new folder has no saves,
  copied whole first. A failed copy stops Play with a message (press Play again to start without them), and saves
  you delete later never come back from the old folder.
- **The Third Age at 30 fps runs at 30.** On a fast PC, the "30 fps" setting ran at 60. It now waits for two of the
  screen's refreshes a frame, as the console did. (The game's speed was right either way.)
- **The Fellowship of the Ring at 30 fps** now waits for two of the screen's refreshes a frame, like the console, for
  smooth, even motion.
- **Adding a game by dragging it onto the launcher** works when the launcher runs as administrator (it does when The
  Return of the King PC is installed under Program Files) (#113).
- **Save backups check every file's contents**, not only names and sizes, before a restore trusts them.
- **The Logs folder**: only Athelas' own session folders are tidied away; a folder of your own there is left alone.
- **Settings files** keep a setting with a dot or a space in its name (added by hand or by a mod) exactly as it was.
- **The Third Age's damage settings and battle animations agree** (#118). The game picks a blow's animation from
  its damage before the settings change it, so an enemy could fall and get back up, or a hero stagger and then die.
  Now a blow that would fell its target at the default damage (100%) still fells it, and one it would survive still
  leaves it standing: the settings change how many blows a fight takes, not how a single blow ends. (So on a low
  damage setting, a blow that would kill at normal damage still kills.)
- **Saving in the Xbox games**: a save file that couldn't be resized or written out is now reported to the game
  instead of being reported as done.
- **ROTKPlus**: unplugging player 3's or 4's controller no longer leaves it held open.
- The launcher only lists disc images the games can actually start, and a few smaller fixes in disc reading, sound
  output errors and game restarts.

## New

- **Unlocked frame rate** in The Return of the King (Xbox) and The Fellowship of the Ring, as The Third Age has.
- The Third Age: **movie subtitles** (your own .srt files, as the other games), and damage, cheats and HUD edges in
  the in-game menu (F10).
- **16:10 window sizes** for every game, including **1280 x 800 (Steam Deck)**, beside the 16:9 ones in the
  Resolution list when the game runs in a window. (Full screen already offers your screen's own size.)

## For bug reports

- The Third Age at **Detailed** logging records each blow in battle (HP before and after, the damage before and after
  the damage settings).

## Checksum

- Athelas-1.9.4.7z: (added at release)
