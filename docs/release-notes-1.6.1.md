# Athelas 1.6.1

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept. Everything in [1.6.0](../../releases/tag/1.6.0) is in this release too.

## Fixes

- **The Return of the King (PC) closed straight after starting on a PC with no Vulkan driver**
  ([#27](../../issues/27)), such as a virtual machine. Without Vulkan, the Vulkan renderer (DXVK) ended the game
  instead of letting ROTK Fix fall back. ROTK Fix now checks for a Vulkan graphics device before it loads DXVK and
  uses Windows' own Direct3D 8 without one, with the reason in `ROTKFix.log`. This also works when `rotk.exe` is
  started directly.
- **The launcher no longer resets `Renderer = native`** in `ROTKFix.ini` to `dxvk` when it loads the PC game's
  settings, so a renderer set by hand is kept.
- **Valar Boons: the first boon was on from the start, with no time limit** (a setting left over from testing). Every
  boon now starts off and lasts its own duration when you call it.
- **"Last played" after a game that didn't start**: a game that couldn't start no longer shows as played, and
  The Return of the King (PC) is marked played only once it has actually started.

## Fixes in all three Xbox ports

From an independent review of the code, each finding checked against the games before it was changed:

- **A rare wrong result in a math instruction** (`PMADDWD`) that The Return of the King and The Fellowship of the
  Ring use: for one combination of values the recompiled code's result was undefined.
- **Copies between two rendered pictures kept at different scales** read the source at the wrong scale; they are now
  drawn across at the right size.
- **Safer handling of unusual requests from the game**: memory released past its block is refused, a damaged game file
  is turned down with a message instead of being read past its end, a folder listing no longer skips a file when the
  game's buffer is small, the file position is updated safely when two threads share a file, a cancelled background
  task no longer runs, an oversized drawing call is skipped and logged, and `nan` in `settings.toml` is ignored.

Thanks to dqunbp for the detailed report on #27, and to the reviewer who went through the code.

## Checksums

- `Athelas-1.6.1.zip`: `9591c2aacdc7d79a554d93654fc2618a95198d3ff346b5d1c19a34e08d37fe77`
- `Athelas-1.6.1-arm64.zip` (Windows on ARM): `75dafcf6ea7a091c5591060528ab62480c2e5f6ea3dd4b10d48c1d2cec6fb672`
