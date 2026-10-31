# Athelas 1.7.5

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept.

## Fixed

- **"Athelas couldn't read this copy of the game. Is it the US release?"** The Xbox games no longer build
  themselves on your PC: they come ready to play again, as before 1.7.3. Nothing to wait for the first time you
  press Play. (In 1.7.4 the build stopped for many players, The Return of the King in particular; your copy of the
  game was fine.) The build files 1.7.4 left in your Athelas folder (the `kits` and `tools` folders, and a
  `build` folder in each Xbox game's folder, several hundred MB) are removed the first time the new launcher starts.

## New

- **Other releases of the games can be played:** a European, Japanese or other copy (or another US revision) is no
  longer refused. The game's page says it's an untested version that may work but may have problems. If it plays
  well, press **This version works**: Athelas stops asking for that copy, and a GitHub page opens with its details,
  so you can tell us and we can add it as known to work.

- **Sharper button pictures:** the PlayStation and Nintendo buttons in The Two Towers and The Return of the King are
  drawn at 4x the game's size.
- **AMD FSR frame generation (experimental):** Graphics, Frame generation, with the AMD FSR upscaler. Shows a
  made-up frame between each two the game draws, so 3D scenes move more smoothly, on any graphics card. The HUD
  can smear a little while the camera moves for now. Takes effect at the next start.
- **FSR updated** to AMD's FSR SDK 2.4.
- **Settings labels:** SMAA is marked *recommended*; AMD FSR, NVIDIA DLSS and frame generation are marked
  *experimental* (FSR can shimmer in The Third Age); Vulkan is marked *experimental* in The Third Age.

## Checksums (SHA-256)

- Athelas-1.7.5.zip: `3b726a07016776bccd346271890ac6713a9db450be1e79d39d8105f82c7e3a79`
- Athelas-1.7.5-arm64.zip: `351622b146b7feaac901b744d1fb957f2795d39a4df8fff609a1c23d64fe40c2`
