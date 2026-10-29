# Athelas 1.7.0

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept. This release is about the launcher: it can now do what my other launcher, COGBox, does.

## New in the launcher

- **Athelas in 11 languages**: English, Spanish, French, German, Italian, Portuguese (Brazil), Polish, Russian,
  Japanese, Chinese (Simplified) and Korean. It asks on first start; change it under **This window** on any game's
  About tab. The Xbox ports' settings are translated too. The translations are in the `lang` folder, and
  corrections are welcome.
- **Back up and restore your saves** on each game's About tab. A backup is a zip on your desktop. Restoring one first
  zips the saves you have now ("before restore"), so nothing is lost.
- **A log folder for every play** (Open logs): the log, how the game closed and what that means, and any crash report.
  The newest 15 are kept.
- **Clear shaders** for the Xbox ports, if a game ever looks wrong after a driver update. The saved shaders are set
  aside, not deleted.
- **Play time for The Return of the King (PC)** too. Athelas now waits in the background while it plays, and it says
  so if the game crashes.
- **Desktop shortcuts for each game**: they open Athelas and start that game with its settings.
- **The window grows with you**: resize or maximise it and everything scales. **F11, or LT + RT together**, switches
  fullscreen from any screen. A **borderless** option hides the title bar. On a Steam Deck-sized screen it starts
  maximised and borderless.
- **Controller on the carousel, as in COGBox**: LB / RB light Play or Settings, A presses the lit one. The hints at
  the bottom now show the fullscreen shortcut too.
- **Made by Shagg** and the version (v1.7.0) in the top right corner; the box art no longer has a black outline.
- **The Shire theme** plays on The Fellowship of the Ring's box, from your own copy of the game.
- **Mods from a .zip** (The Return of the King, PC): "Add a mod (.zip)" on the Mods tab. Data mods each get their own
  folder, with a tick, **Up / Down** to set which one wins when two change the same file, and Remove. A mod that is a
  program (a .dll) is only added after a warning.

## Fixes

- **The Two Towers' menu music didn't play** when a European copy was added before the US one. A box now plays (and
  takes its music from) the US copy first.
- ROTK Fix's log now says when a code mod has no `ModInit` (it was reported as started).

The three Xbox ports are the same as in 1.6.1.

## Checksums

- `Athelas-1.7.0.zip`: `51402ef21c31dcc795b831a43ecf7a9e0739084ecfa2f8ab0f7242843a781109`
- `Athelas-1.7.0-arm64.zip` (Windows on ARM): `c194d51b7d80ca685086613135d74c57ad70eca15812d99c6d610a59deaad958`
