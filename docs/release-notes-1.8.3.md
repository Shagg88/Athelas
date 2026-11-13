# Athelas 1.8.3

Athelas is back on GitHub: https://github.com/Shagg88/Athelas, with a copy of every release on GitLab
(https://gitlab.com/shagg88/athelas). If GitHub ever can't be reached, the launcher looks for updates on GitLab.

Coming from 1.8.2 or before? Your launcher can't unpack this download itself: extract the .7z over your Athelas
folder with 7-Zip (or Windows 11's Extract All). Your settings, saves and achievements are kept. From 1.8.3 on,
**Update** does it for you.

## New

- **Frame generation** for the Xbox games (D3D12): FSR frame generation, Intel XeSS frame generation (XeFG, with
  XeLL for lower latency) and DLSS frame generation. Turn it on under Graphics > Upscaler > Frame generation; it
  uses the one that matches your upscaler.
- **Intel XeSS** upscaling (XeSS 3.0.2) for the Xbox games, next to FSR and DLSS.
- **A .7z download**, smaller than the old zip (about 124 MB instead of 153 MB, now with Intel XeSS). The launcher's Update unpacks
  it itself.
- **Updates from GitHub or GitLab:** the launcher checks GitHub, and GitLab when GitHub doesn't answer.
- **Clean updates:** each update (through Update, or a .7z extracted by hand) now removes the files an older
  Athelas shipped that this version doesn't, so nothing left over can clash with it. Only files exactly as Athelas
  put them are removed: your settings, saves, shader caches, texture packs and anything you added are never touched.
  What was removed is listed in athelas-cleanup.log. ROTK Fix's own DLL folders in the PC game are tidied the same way.
- **Launcher:** the About tab is laid out more simply, one line per item with the details in hover tips.
- Updated: SDL 3.4.20, Dear ImGui 1.93, glslang 16.6, Vulkan headers 1.4.368, Streamline 2.14.16, DLSS 310.9.

## Fixed

- **Return of the King (PC):** the menus no longer stutter (#58): slow frames went from about 70 to 2 in a test, and
  the worst frames from 33 ms to under 6 ms.
- **The Two Towers:** the HUD and menus no longer shake with FSR or DLSS in a level (#54).
- **The Two Towers:** "No" on the save prompt after a level no longer counts as "Yes" (#56).
- **The Two Towers:** Breached Wall's 30 fps lock stays on for the whole level (#68).
- **Return of the King (PC):** the subtitle outline is back as it was in 1.8.1 (#61: the softer outline looked
  doubled).

## Known issues

Being worked on; if you see one, please report it with the launcher's "Report a problem" button:
the Two Towers map screen's glow, the Perfect Mode ring in ROTK (PC), ROTK (PC) stopping behind the death menu, the
in-game subtitle shadow in ROTK (PC), a ghost image in the ROTK (PC) credits and a graphics glitch at Helm's Deep in
The Third Age.

## Checksums (SHA-256)

- Athelas-1.8.3.7z: `8810be13f034c201e6374d8ce936e14ef2963163970fa6fecfcdc2a14ec4925e`
