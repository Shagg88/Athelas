# Athelas 1.4

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept. One version number for everything: Athelas, ROTK Fix and the three Xbox ports are all 1.4.

## New: The Fellowship of the Ring (Xbox)

A third Xbox game. **The Fellowship of the Ring** (US Xbox release, 2002) now plays on PC through its own native port,
`FOTRXbox.exe`, in the new `FOTR-Xbox` folder, with its own page in Athelas. Point the launcher at your own disc dump
(an `.iso` or the extracted folder), as for the other Xbox games.

This is its **first version**. Every level starts and runs in testing, but it hasn't been played much yet, so please
use **Make a problem report** on the About tab if anything goes wrong. Fixes from your reports go into 1.5.

- **As on the Xbox by default:** 30 fps and the console's 4:3.
- **Higher frame rates,** up to 240. The game times itself by the clock, so it keeps its normal speed.
- **Widescreen:** the game's own 16:9 mode, with a wider view and its HUD laid out for it.
- **Ultrawide, 21:9 and 32:9:** the world reaches the sides of the screen, while the HUD and menus keep their shape in
  the middle 16:9.
- **Field of view:** 70 to 150% of the game's own, for the camera while you play (cutscenes keep theirs).
- **Picture:** render resolution, anti-aliasing, texture filtering, sharpening, HDR, and the Original / Balanced /
  High / Ultra presets.
- **Cutscenes:** sharper and upscaled, as in the other Xbox ports. The story films can fill a widescreen monitor, and
  the logos keep their 4:3 shape.
- **Skip the opening logos**, straight to the title screen.
- **Game text in nine languages:** German, French, Spanish, Italian, Brazilian Portuguese, Polish, Dutch, Swedish and
  Russian (our own translations). Menus, hints, items, the quest journal and dialogue text replace the English inside
  the game's own menus and text boxes. The voices stay English. Polish and Russian use free open fonts for the letters
  the game's own fonts lack.
- **Difficulty:** the damage you take and the damage your hits do, each from 25 to 300% of the game's own.
- **Achievements:** eight, one for each part of the journey (the Shire, the Old Forest, Bree, Weathertop, Rivendell,
  Moria, Lothlórien) and one for the whole story. A save further along earns what it has passed.
- **Cheats as switches:** the game's own secret codes for infinite health, spirit, ammunition and Ring.
- **Controls tab:** the controller's buttons, named after the game's own moves.
- **The launcher page** shows the game's own art, taken from your game files: the loading-screen sketches.
- **Not yet:** an ARM64 build (on Windows on ARM it runs through Windows' translation).

## New in The Two Towers (Xbox)

- **Play as Gandalf or Boromir.** In the Gates of Moria and Balin's Tomb, the hero screen now has Gandalf and Boromir
  after Aragorn, each shown in his own clothes. Each plays with his own model, walk, run and stance. Gandalf carries
  his staff, and his ranged attack is a blast of light. Boromir has his sword and shield.
- **Translation subtitles in nine languages** (Gameplay tab, Language): the game's menus, prompts and descriptions
  translated at the bottom of the screen. The game's own text and voices stay English.

## New in The Return of the King (Xbox)

- **Éowyn instead of Faramir** (Gameplay tab). Éowyn takes his place on the character select, on the HUD and in
  every level he plays, with his moves, combos, bow and upgrades. She is built from her own model in your game files
  when the game starts. She has no voice of her own, so she fights quietly.
- **Translation subtitles in nine languages** (Gameplay tab, Language): the menus, prompts and spoken lines
  translated at the bottom of the screen.

## Fixes

- **The Two Towers: crash a moment after starting** ([#5](../../issues/5), [#9](../../issues/9),
  [#10](../../issues/10)). On some PCs, the Steam Deck among them, the game asked for a line of its text before the
  text was loaded, and closed with 0xC0000005. It now carries on, and the text shows once it is loaded.
- **The Two Towers: the keyboard and Show Moves** ([#11](../../issues/11)). Opening Show Moves from the pause menu
  with the keyboard left the left mouse button pausing the game; it attacks again.
- **The Two Towers: the Invulnerable cheat** now works on the player.
- **Stutters on a slow drive** ([#12](../../issues/12)), both Xbox ports. Disc images are now read in larger pieces,
  with the next piece read ahead.
- **ROTK on PC: stutters while loading** ([#12](../../issues/12)). ROTK Fix now reads the game's data files through
  Windows' file cache. The stutter lines in `ROTKFix.log` show the time spent reading.
- **All three Xbox ports share an improved engine,** built for The Fellowship of the Ring. It now draws the game's
  queued graphics commands in the order the game sent them, allocates the room the game asks for, and guards against
  running out of memory. The Two Towers (13 levels) and The Return of the King (17 levels) were tested against the
  previous engine, with the same results.

## Launcher

- The Fellowship of the Ring's page, with its own settings, presets, art and achievements.
- A **Language** section on the Gameplay tab for the Xbox ports. The launcher's font now has Polish and Cyrillic
  letters.
- The Original preset's description names each game's own Xbox year.

## Docs

- README: The Fellowship of the Ring section, Gandalf and Boromir, Éowyn, translation subtitles, and the new
  ROTKFix.ini settings (`[Gameplay] Eowyn`, `[Language] Subtitles`, `[System] OneCore`).

## Checksums

`Athelas-1.4.zip` SHA-256: `83a6acad45fbdea6fe458695915629ebffe57ceefcf3aa625adee2a7a525004d`
`Athelas-1.4-arm64.zip` SHA-256: `231fb9d6c9babaf27e8484079a94b0bf6b77e52977220d673aafb7bd9cdd419f`
