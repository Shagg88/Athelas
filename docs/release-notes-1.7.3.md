# Athelas 1.7.3

> # ⚠️ This update installs differently from every release before it
>
> **Until now, each game came as a ready-made program inside the download. From 1.7.3 on, Athelas builds each
> game's program on your own PC, from your own copy of the game.** No game code is in the download any more,
> only a build kit for each game and the tools to use it.
>
> **What happens when you update:**
>
> 1. Press **Update** in the launcher (or unzip the new release over your Athelas folder). Your **settings,
>    saves, achievements and Journey progress are kept**: none of them are in the download, so nothing touches
>    them.
> 2. Open a game's page. Athelas sees that game needs building and **starts on its own**: a progress bar shows
>    "Building ... for your PC" and how long is left.
> 3. Wait for **"... is ready to play."** This takes **under a minute per game on most PCs (longer on older
>    ones), once**. After that the game starts as fast as before. Each game builds the first time you open its
>    page; you don't have to build all four.
> 4. Play.
>
> **If something goes wrong:**
>
> - **Your antivirus blocks or deletes the new program:** Athelas has just made a new program on your PC, which
>   some antivirus software is wary of. Allow your Athelas folder in your antivirus, then press **Try again**.
> - **The build stops with a message:** press **Try again**. If it fails again, press **Play the previous version
>   for now**: the program you were playing before is kept until the new one builds, so you can keep playing.
>   Athelas tries the update again the next time it starts. Then please open an issue on GitHub with the
>   build log (the **Show the log** button) so we can fix it.
> - **It seems stuck:** the first build of a game is the slow part, and on an older PC it can take longer. The
>   time left is shown under the progress bar.
> - **The download is larger than before:** it carries the build tools. It's a one-time cost.
> - **Left over from earlier versions:** once a game has built, Athelas removes the files the old version left
>   beside it that nothing uses any more. Nothing of yours is touched.
>
> The Return of the King (PC) is not affected: ROTK Fix works as it always has.

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept.

## New: The Third Age

- **The Third Age (Xbox)** joins the launcher as the fourth game, built on your PC like the others.
- **Achievements** for each of its nine areas, from Eregion to the Pelennor Fields, plus the kill deeds and Fate's
  deeds every game shares.
- **On the Journey page:** its areas on the map (with Eregion), level records, quests and Virtues.
- **Widescreen, unlocked frame rate, field of view, draw distance, sharper 4x text** (the title too), smooth leaf
  edges, the HUD at the screen edges, movie zoom, and gameplay and cheat options as in the other games.
- **Drawing on a second core** (on by default): a large jump in frame rate. Turn it off in the Graphics tab if
  characters ever show a ghost image.

## New: SMAA (all four Xbox games)

- **SMAA** (Graphics tab, **off by default**): smooths jagged edges after the picture is drawn, including leaves
  and grass that anti-aliasing misses. Works with anti-aliasing on or off and costs very little.

## New: the Journey

A new page from the front screen (the **Journey** button, or **J**), in the spirit of The Lord of the Rings
Online. Everything on it that changes how a game plays is **off by default**.

- **Deeds and titles:** every game's deeds in one place, with progress bars. All four Xbox games now count the
  enemies you defeat (deeds at 100, 250, 500, 1,000 and 5,000). Any deed you've earned can be your title, shown
  under Athelas on the front screen.
- **Quests:** three a day and two a week ("Defeat 60 enemies at Helm's Deep"), with titles for 10, 25 and 50
  completed.
- **Map:** the road of the games across Middle-earth, on a parchment of our own with The Two Towers' own
  map of Rohan and the Misty Mountains drawn in from your copy. Places light up as you complete their levels, and
  the road you've walked is marked in red.
- **Records:** each level you complete, with how long it took and how many enemies you defeated, and your bests.
- **Virtues (opt-in):** five virtues ranked by your deeds, three slotted for their full bonus, as in LOTRO. They
  make your hits a little stronger or the damage you take a little lighter (at most 30%), in every game.
- **Fate (opt-in):** a harder road: enemies hit 50% harder and take 25% more to bring down, every enemy counts
  twice towards the kill deeds, and two deeds of its own.
- **Fellowship (opt-in):** in The Return of the King's co-op, while both players are fighting, both hit 10%
  harder.
- **Lore:** a short page for each level, opened by completing it.
- **Minstrel:** play The Fellowship of the Ring's music by name, and The Two Towers' menu theme, from your copies.

## New: graphics

- **Recommended for this PC** (Graphics tab): looks at your graphics card, its memory, your screen and
  processor, and picks a preset and frame rate. **Undo** puts your own settings back.
- **DLSS and FSR:** the HUD, subtitles and menus stay sharp and no longer smear or ghost when upscaling.

## Fixed

- **The Third Age:** the picture no longer changes shape for a few seconds after a movie; leaves, branches and
  fences are no longer blocky, and no longer have a ghostly shadow around them with anti-aliasing on.

- **Renderers:** steadier start-up and plain error messages when the graphics driver stops responding; the
  game no longer picks a software adapter over your graphics card; minimising and resizing are handled.
- **The Return of the King (PC):** when the Vulkan renderer can't start, it now falls back to Direct3D 8 at
  the right resolution.

## Checksums (SHA-256)

- Athelas-1.7.3.zip: `(filled in after the build)`
- Athelas-1.7.3-arm64.zip: `(filled in after the build)`
