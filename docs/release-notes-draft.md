# Athelas [VERSION]: release notes (draft)

<!-- Draft for the GitHub release page. Still to decide: the version number (1.0 or 0.9) and the name/credits line.
     Fill in the checksum from dist/Athelas-<version>.zip.sha256 when the final zip is built. -->

Athelas is a free, unofficial launcher for three Lord of the Rings games, so you can play your own copies on a modern
Windows PC from one place:

- **The Return of the King (PC, 2003)**, through ROTK Fix
- **The Return of the King (Xbox)**, through a native PC port
- **The Two Towers (Xbox)**, through a native PC port

No game files are included. You need your own copy of each game: the PC game installed, or your own Xbox disc as an
`.iso` or extracted files (US releases). Nothing bypasses copy protection, and the games' own files are never changed.

## What's new

This is the first public release.

**The Return of the King (PC), with ROTK Fix**
- Any resolution and refresh rate, borderless or fullscreen, widescreen with the right field of view
- Smooth frame rates up to 240 fps with the game's timing fixed to match
- Sharper picture: render scaling, anti-aliasing, texture filtering, sharpening and colour looks (Vulkan through DXVK)
- The game's 3D sound and EAX reverb restored
- Modern Xbox, PlayStation and Switch controllers, with remapping and matching button pictures

**The Return of the King (Xbox) 1.0.0**
- Runs natively on PC, up to 240 fps, widescreen and ultrawide
- Sharp text and menus, using the PC version's larger fonts and pictures when you have it installed
- Fixes for slowdowns and for enemies vanishing instead of fading at high frame rates

**The Two Towers (Xbox) 1.0.0**
- Runs natively on PC: widescreen up to 32:9, up to 240 fps, higher resolutions, HDR
- Controller remapping and adjustable difficulty
- Plays as on the Xbox by default; every enhancement is a setting

**Athelas, the launcher**
- All three games' settings in one place, with presets
- Background art taken from your own game files, rotating, or your own pictures
- Sets ROTK Fix up in your PC game folder and removes it cleanly

## Requirements

- Windows 10 or 11, 64-bit
- ROTK (PC): a graphics card with a Vulkan 1.3 driver is recommended
- The Xbox games: a DirectX 12 graphics card. The Two Towers needs a controller.
- Tested on an NVIDIA GeForce RTX 3080 Ti. AMD and Intel graphics are expected to work but haven't been tested yet;
  reports are welcome. Linux (Proton) is untested.

## Install

Unzip into a folder of its own (for example `C:\Games\Athelas`), run `Athelas.exe`, and show it where your games are.
The README in the zip has the details.

## Known issues

- Only the US releases are supported. Others warn before starting and will most likely not work.
- ROTK (Xbox) looks darker than the PC version. That's faithful to the console; Brightness 1.2 to 1.3 matches the PC.
- Loading a ROTK (Xbox) disc image (`.iso`) hasn't been tested with a real disc image yet; an extracted folder is
  tested.
- The files aren't code-signed, so Windows SmartScreen may say "unrecognised app": choose "More info", then
  "Run anyway".

## Checksum

`Athelas-[VERSION].zip` SHA-256: `[from the .sha256 file]`

## Credits

[name/credits line to be decided]

Built on DXVK, SDL3, Dear ImGui, DSOAL and OpenAL Soft, and XboxXBE-Recomp (MIT, © 2026 TGP482). Full licences are
in the `licenses` folder.

Not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.
