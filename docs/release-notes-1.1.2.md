# Athelas 1.1.2

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept.

## New

- **Less stutter in places you've played before** (both Xbox ports). The port now remembers every graphics setup
  it has built, and when the game starts it builds them again in the background, before you reach them. The first
  visit to an area can still hitch once; later visits shouldn't.
- **Stutter is now recorded in the log.** A frame that takes 50 ms or more is written to `tt.log` / `rotkx.log`
  with what it waited on (shaders, graphics setups, textures). If you report stutter, please attach the log.

- **Original texture softness** (Graphics tab, both Xbox ports) ([#6](../../issues/6)). At high resolutions the
  games' textures show their full, raw detail, so lava, rock and water look grainier than they did on a TV. With
  this on, textures are drawn at the detail the Xbox used at its own resolution: softer, as you remember them. Menus
  and the HUD are not changed. Off by default; the Original preset turns it on.

- **The Two Towers: kill points** (Gameplay tab). A killing blow now shows the points it earned after its rating
  ("Good +26"), in the game's own font, as The Return of the King does. Off by default.

- **All heroes in completed levels** (Gameplay tab, all three games). In a level you've already completed, the
  game's own character select offers every hero, as it does once you've beaten the game: Aragorn, Legolas, Gimli
  and Isildur in The Two Towers; all nine heroes in The Return of the King. A level you haven't completed yet is
  played as the game has it, so the story is the game's the first time through. Levels built around one hero are
  left alone: the Prologue and Weathertop in The Two Towers, and Helm's Deep (Gandalf's) in The Return of the
  King. In The Return of the King (PC and Xbox), Unlock all levels and characters offers every hero everywhere.
  Off by default.

- **Cheats** (Gameplay tab, both Xbox ports). The games' own secret codes as switches, without entering them on
  the pause screen: Invulnerable, Always Devastating, Infinite Missiles and Slow Motion in The Two Towers;
  Invulnerable, Infinite Missiles, Always Devastating and Perfect Mode in The Return of the King. Off by default.
- **Mouse aiming and mouse in the menus** (Controls tab, both Xbox ports). Hold the ranged attack and move the mouse
  to aim. In the game's menus the wheel moves up and down and a right click goes back.
- **Controller in the background** (both Xbox ports). Turn it off so controllers only reach the game while its
  window is in front. On by default, as before.

## Fixes

- **The Two Towers: the game's clock no longer drifts or stops in long sessions.** The game adds up its time in a
  number that loses precision over hours; at high frame rates it ran a few percent off after about two hours in
  one level, and it could stop after days. It now keeps exact time.
- **The Two Towers: Slow Motion is the same speed at any frame rate.** The game's own code slowed time by a fixed
  amount a frame, so above 30 fps it ran faster, not slower.
- Keys held while switching windows (Alt-Tab) are ignored until let go.

- **Smooth menus and HUD: blocky menu text at 4K** ([#1](../../issues/1)). The menu fonts are smoothed and then
  given a crisp edge, so letters have clean curves at high resolutions instead of soft stair steps. Menu pictures
  and the HUD are smoothed as before.
- **The Two Towers: levels played as Legolas, Gimli or Isildur now count.** Completing a level as one of them
  didn't earn its achievement, and the port didn't notice it was in a level (so Esc behaved as in the menus).
- The README now notes that The Two Towers has been reported working on a real Steam Deck.

## Windows on ARM

The ARM test build is updated to this version (see the `arm-test` pre-release).

## Checksum

`Athelas-1.1.2.zip` SHA-256: `a7d2160406e64d303dd4421719b3bc418670e292fa825e3b3341cdd303afd577`
