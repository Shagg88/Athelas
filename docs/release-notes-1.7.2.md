# Athelas 1.7.2

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept.

## Fixed

- **The Two Towers: HUD pieces coming apart on wide screens** (#33). With "HUD at the screen edges" on, part of
  the health bar or the special meter could be left behind towards the middle of the screen, most often once
  you had lost some health. The pieces now stay together at the edge.
- **The Return of the King (PC): button pictures shown as plain white shapes** (#34) in the pause menu's combo
  list, the in-game hints and the upgrade shop. They now show in colour, as in the Options screen.
- **The Return of the King (PC): the special ability's buttons** (#34) now show the real buttons with a
  controller in the shop and the Options screen, instead of "Button 7" / "Button 8" / "Buttons 7+8".
- **The Return of the King (PC): Vulkan check** - the Vulkan renderer needs a Vulkan 1.4 graphics driver. A PC
  with an older (Vulkan 1.3) driver used to get a black screen; it now falls back to Direct3D 8 and runs.
- **Linux / Steam Deck (Proton):** the settings Athelas makes for ROTK Fix are now checked and written to
  `ROTKFix\wine-overrides.txt` for reports, and Remove ROTK Fix puts back any settings you had before.

## Checksums (SHA-256)

- Athelas-1.7.2.zip: `1285e045404b0a88005fc65c8d53e031ed8b89ca14d5d39ead2ee8b2dc616b6d`
- Athelas-1.7.2-arm64.zip: `3c8856180942c3dc13026c68f4665f75ebea209337d9d92113a25b519bf3317b`
