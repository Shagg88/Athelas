# Athelas 1.0

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
- Two-player co-op with two controllers, and online by streaming it with Parsec (or Steam Remote Play Together)
- Gameplay options: damage taken, enemy damage, and every level and character unlocked (all off by default)

**The Return of the King (Xbox) 1.0.1**
- Runs natively on PC, up to 240 fps, widescreen and ultrawide
- Sharp text and menus, using the PC version's larger fonts and pictures when you have it installed
- Two-player co-op on one screen, and online by streaming it with Parsec (or Steam Remote Play Together)
- The same gameplay options as the PC version: damage taken, enemy damage, unlock everything
- Fixes for slowdowns and for enemies vanishing instead of fading at high frame rates
- Keyboard and mouse play, with the PC version's keys and key pictures in the game's hints

**The Two Towers (Xbox) 1.0.1**
- Runs natively on PC: widescreen up to 32:9, up to 240 fps, higher resolutions, HDR
- Controller remapping and adjustable difficulty
- Keyboard and mouse play, with the PC version of The Return of the King's keys
- Plays as on the Xbox by default; every enhancement is a setting

**Both Xbox ports**
- An in-game settings menu: F10, or both stick clicks together; the game pauses behind it
- Pause or mute while the game is in the background (off by default)
- A frame rate counter

**Athelas, the launcher**
- All three games' settings in one place, with presets; every key and button can be changed
- Background art taken from your own game files, rotating, or your own pictures
- Sets ROTK Fix up in your PC game folder and removes it cleanly

## Requirements

- Windows 10 or 11, 64-bit
- ROTK (PC): a graphics card with a Vulkan 1.3 driver is recommended
- ROTK (PC) 3D sound: Microsoft's Visual C++ Redistributable (x86), which most PCs already have; without it the game
  plays its original sound
- The Xbox games: a DirectX 12 graphics card. A controller, or the keyboard and mouse.
- Tested on Windows 11 with an NVIDIA GeForce RTX 3080 Ti. Windows 10 and AMD and Intel graphics are expected to
  work but haven't been tested yet; reports are welcome.
- Steam Deck and Linux: through Proton (add Athelas.exe as a non-Steam game). Tested in a Deck-like setup, not yet on a
  real Deck.

## Install

Unzip into a folder of its own (for example `C:\Games\Athelas`), run `Athelas.exe`, and show it where your games are.
The README in the zip has the details.

## Known issues

- Only the US releases are supported. Others warn before starting and will most likely not work.
- ROTK (Xbox) looks darker than the PC version. That's faithful to the console; Brightness 1.2 to 1.3 matches the PC.
- The files aren't code-signed, so Windows SmartScreen may say "unrecognised app": choose "More info", then
  "Run anyway".

## Checksum

`Athelas-1.0.zip` SHA-256: `4bb398110c2ecd92379c010f3e1e356c4fe7f8c3688b8242f9aaa8571244a65c`

## Credits

Made by Shagg, with Claude (Anthropic).

Built on DXVK, SDL3, Dear ImGui, DSOAL and OpenAL Soft, and XboxXBE-Recomp (MIT, © 2026 TGP482). Full licences are
in the `licenses` folder.

Not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.
