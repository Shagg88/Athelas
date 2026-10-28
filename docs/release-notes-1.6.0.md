# Athelas 1.6.0

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept.

## A new Athelas: your games on a carousel

- **Athelas opens on your games as boxes on a carousel.** The one in front is large; the others stand smaller and
  turned away at the sides. Turn it with the arrow keys, the D-pad or stick, the mouse wheel, or a click on a box.
  Behind it, art from the game in front (from your own copy of the game), changing as you turn. **Play** starts it,
  **Settings** zooms into its page, and **Library** on the page zooms back out.
- **Box art:** **Get box art** downloads the original Xbox covers of the three games from RetroArch's collection
  (libretro thumbnails). Or use your own scans: put `fotr.png`, `tt.png` or `rotk.png` (or .jpg) in the `covers`
  folder beside Athelas. Without either, each box gets a cover made from the game's own art.
- **The Return of the King is one box** for both versions. With both, Play and Settings ask PC or Xbox, and can
  remember your choice ("Ask which version" under the box undoes it).
- Games you haven't added show as faded boxes with the buttons to add them.
- With a controller: A plays, X opens Settings, Y gets box art; Y on a page goes back to the carousel.
- The settings pages are centred, and the game is chosen on the carousel instead of from menus.
- Disc images named `.xiso` are listed when you add a game, as well as `.iso`.

## New in the games

- **The Fellowship of the Ring: chapter select** (Gameplay, "New game starts at"). The game has no level select,
  so this starts a new game at the beginning of any chapter: the Shire (the game's own start), the Old Forest,
  Bree, Weathertop, Rivendell, Moria, Lothlórien or the River Anduin, with that chapter's hero (Aragorn at
  Weathertop and the Anduin, Gandalf in Moria, Frodo elsewhere). Items and quest steps from the chapters you skip
  aren't given: it's for replaying a chapter. Saved games aren't affected.
- **The Fellowship of the Ring: Combat Test** (Gameplay). The developers left three test arenas in the game but
  never switched them on. With this on, Bilbo's room in Rivendell offers a choice of Frodo, Aragorn or Gandalf, and
  each one takes you to an arena with its own enemies. Its exit leads back to Bilbo's room.
- **The Two Towers: Sharper text** (Graphics), as the other two have it: the game's letters are redrawn at four times
  their size from its own fonts, so menus, hints and the between-level screens stay crisp at high resolutions.
- **Movie display: Zoom** (all three Xbox ports, Graphics, Movies). Like Fit, and movies made for a square 4:3
  screen fill a widescreen too, with their top and bottom cut off: Fellowship's logos and 4:3 movies, The Two
  Towers' full-frame ones, ROTK's trailer. The films look the same as with Fit.
- Also new: drag a game folder or disc image onto Athelas to add it; play time and the day last played under each
  box; the games' own menu music on the carousel (The Two Towers and Fellowship; Music on/off); each page opens with
  its game's box.
- **Steam Deck and Linux**: the readme now has the steps for playing through Proton ([#23](../../issues/23)).

## Mods for The Return of the King (PC)

**Contributed by Rodrigo Vieira** ([#26](../../pull/26)): the mod system and Valar Boons are his work. Thank you, Rodrigo!

- **ROTK Fix loads code mods**: a mod's `.dll` and `.ini` in the game's `ROTKFix\mods` folder. Mods can change hits
  in combat, run every frame, draw over the picture, add hotkeys and show notes on screen. A mod can also replace one
  of the game's data files by putting a file with the same `Data\...` path in `ROTKFix\mods`.
- **A Mods tab** on The Return of the King (PC) page lists the installed mods with their details; tick or untick one
  to turn it on or off.
- **Valar Boons**, an example mod that comes with Athelas, **off until you turn it on**: call on the blessings of
  Manwë, Varda or Aulë with the number keys (more damage, a shield that strikes back, or blows that can't hit hard),
  each with its own glow around the screen and a cooldown. Everything is set in `ValarBoons.ini`.

## Fixes

- **The Fellowship of the Ring: crash at a scene change** ([#21](../../issues/21), [#25](../../issues/25)). Besides
  its main thread the game has a timer thread. On the Xbox both shared one processor; on a PC they ran at the same
  time, and the most likely cause of the crash is the timer writing into an object the main thread had just
  freed. They now take turns, as on the console: the timer runs whenever the main thread waits, reads a file or shows a frame. 1.5.1's guard stays too.

## Contributing from now on

From 1.6.0, the releases are published as downloads only: the code on GitHub stays at 1.5 for now. Everything else
works as before: the launcher's **Update** button, the issues, and pull requests.

- **Found a bug or have an idea?** Open an issue. It's the quickest way to get it into the next release.
- **Want to contribute code?** Open an issue first to describe the change, then a pull request against the code on
  GitHub. Accepted changes are brought into the next release by hand, so small, focused changes are easiest to take.
- **Every contribution that ships is credited** in that release's notes by name, as Rodrigo's mod system is here.
- **Mod makers:** ask in an issue for the notes on ROTK Fix's mod interface and a starter mod.

## Checksums

- `Athelas-1.6.0.zip`: `37716c801cb9a5f7eb45df0c2e6612d4bdb78ae0fb17d77247f9d2c1b81c6813`
- `Athelas-1.6.0-arm64.zip` (Windows on ARM): `6908b130ab37ba633862d4f808f6eb7af6619199fa36363d3348b8f195b649e5`
