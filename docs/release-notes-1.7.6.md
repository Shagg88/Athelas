# Athelas 1.7.6

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept.

## Fixed

- **European, Japanese and other copies of the Xbox games** (#36): the game's page no longer refuses them. It
  warns that the version is untested and unsupported (a European or Japanese copy is expected not to start yet: the
  games are built from the US discs' code). Two buttons, **It worked** and **It didn't work**, open a GitHub page
  with the copy's details to send us. A version built from the other regions' discs is planned.
- **The Return of the King (PC): PlayStation buttons as white discs in the combo list and the upgrade shop** (#34):
  the check for button pictures in a line of text never fired; it now compares against where the font's letters end
  (thanks to brig-dev's analysis).
- **Vulkan: textures dropping out in The Third Age** (it ran out of room for the game's ~13,600 draws a frame and skipped
  the rest).
- **The Return of the King (Xbox): black bars flashing at the sides on ultrawide screens** (#38) in Southern
  Gate and the Minas Tirith Courtyard: a full-screen effect in those levels was taken for a menu picture.
- **The Third Age: a short stutter every few seconds with Sharper text on** (#37): when the game changed a few
  words, the whole page of text was redrawn (about 40 ms); now only the part that changed is.
- **The Third Age: characters bursting into spikes with widescreen on** (#37), in cutscenes and sometimes in play.

## New

- **The Third Age on ultrawide screens** (#35): the game fills the whole width (3440x1440 and wider); the text and
  the HUD stay 16:9 in the middle, as in the other three games.
- **Ultrawide menus** (#35): the menus of The Two Towers, The Return of the King and The Third Age widen with the
  screen up to 21:9 (black beyond that, on 32:9), instead of staying 16:9; play and in-game cutscenes fill the
  whole width.
- **Movies never wider than 21:9**: on a 32:9 screen the intro and story films get bars at the sides instead of
  being zoomed (The Third Age's lost their top and bottom) or stretched.
- **The Third Age: Button pictures** (Graphics): PlayStation, Nintendo or keyboard keys in place of the Xbox buttons,
  as in the other three games. For now the A button; more to come.

## Checksums (SHA-256)

- Athelas-1.7.6.zip: `16c0abafec3af919861dc5f37ca8176df28e2b96f845e490abd8c49f808f9ffd`
- Athelas-1.7.6-arm64.zip: `758963bad52b569c734c2f88c6b9adf31ff01ababbe044aaa12888f9871c88cf`
