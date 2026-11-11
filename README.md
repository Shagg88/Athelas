# Athelas

A free, unofficial launcher for four Lord of the Rings games, so you can play your own copies on a modern PC from
one place. (Athelas is kingsfoil, the healing herb.)

I'm Shagg, and I made this with the help of Claude (Anthropic). I played these games with my siblings and friends for
hours on end, and it's a shame they can't be bought today. Tolkien's world deserves to be lived in every way
possible. This is my first project of this size, so expect a few bugs; reports and contributions are welcome.

| Game | How it runs |
|---|---|
| **The Return of the King (PC, 2003)** | ROTK Fix, a mod for your installed game (the recommended way to play ROTK) |
| **The Return of the King (Xbox)** | Native PC port |
| **The Two Towers (Xbox)** | Native PC port |
| **The Fellowship of the Ring (Xbox)** | Native PC port |
| **The Third Age (Xbox)** | Native PC port (newest, still being polished) |

> No game files are included: you need your own copy of each game (the PC game installed, or your own US Xbox disc as
> an `.iso`). Not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.

## Quick start

1. **Download** `Athelas-<version>.7z` from [Releases](https://github.com/Shagg88/Athelas/releases) (a copy on [GitLab](https://gitlab.com/shagg88/athelas/-/releases)).
2. **Extract it** into a folder of its own, for example `C:\Games\Athelas` (Windows 11: right-click > Extract All;
   Windows 10: [7-Zip](https://www.7-zip.org)). Not into a game folder.
3. **Double-click `Athelas.exe`.** That's the launcher; there's no installer.
   - **No `Athelas.exe`?** Your antivirus removed it (it isn't code-signed). Restore it from quarantine, add an
     exception for the folder, and extract again.
   - **"Windows protected your PC"?** Click **More info > Run anyway**.
4. **Pick a game and show the launcher your copy**, then press **Play**.

To uninstall: **About → Remove ROTK Fix** in the launcher, then delete the Athelas folder.

## Features

**In every game**
- Widescreen and ultrawide (up to 32:9) without stretching, any resolution, 60 to 240 fps at normal game speed
- Xbox, PlayStation and Switch controllers, with the right button pictures; keyboard and mouse
- Sharper text, anti-aliasing, texture filtering, sharpening; FSR, DLSS and XeSS upscaling in the Xbox games
- Texture packs, screenshots (`\` or Print Screen), achievements, the games' own cheat codes as switches
- Faithful by default: every enhancement can be turned off, with presets from Original to Ultra

**Game highlights**
- **The Return of the King (PC):** two-player co-op on one PC (or online through Parsec or Steam Remote Play
  Together), mods, cutscenes that fill the screen, restored 3D sound and EAX reverb
- **The Return of the King (Xbox):** co-op, fixes for slowdowns and vanishing enemies at high frame rates
- **The Two Towers:** an in-game settings menu (`F10`), fast forward (hold `F9`)
- **The Fellowship of the Ring:** chapter select, the developers' unused Combat Test arenas, nine languages
- **The Third Age:** field of view, draw distance, difficulty sliders

**The launcher:** every setting in one place, save backups, desktop shortcuts, 11 languages, and "Report a problem",
which gathers the logs for you.

## Requirements

- Windows 10 or 11 (64-bit). Steam Deck and Linux through Proton; Mac through CrossOver (less tested).
- Xbox games: a DirectX 12 graphics card.
- The Return of the King (PC): a Vulkan graphics driver is recommended (without one, a few graphics options are off).

**Steam Deck / Linux:** extract Athelas into a folder, then in Steam use **Add a Non-Steam Game** on `Athelas.exe`
and set **Properties > Compatibility** to the newest Proton. The Deck's controls work as an Xbox controller.

More detail on every setting, key and platform is in `README.txt` in the download.

## Reporting a problem

Use **Report a problem** in the launcher: it packs the logs and settings into a zip and opens a new
[GitHub issue](https://github.com/Shagg88/Athelas/issues) for you.

## Building

Players don't need this. ROTK Fix and the launcher cross-compile on Linux with MinGW-w64:

```sh
sudo apt install gcc-mingw-w64-i686 g++-mingw-w64-i686 p7zip-full
SDL_DIR=path/to/SDL3-3.4.20 DXVK_DIR=path/to/dxvk-3.1.1 DSOAL_DIR=third_party/dsoal sh scripts/package.sh <version>
```

The Xbox ports build on Windows with each port's `build.bat` (see its README in `ports/`). Recompiling them needs
your own game's `default.xbe`, which is never part of this repository.

| Folder | What's in it |
|---|---|
| `launcher/` | The launcher |
| `src/` | ROTK Fix |
| `ports/` | The four Xbox ports |
| `sdk/` | The shared engine the ports build on |
| `dist-files/`, `scripts/` | The download's README and packaging |
| `licenses/` | Every licence |

## Credits

Made by Shagg, with Claude (Anthropic).

Thanks to Stormfront Studios, EA Redwood Shores and Hypnos Entertainment for the games; to J.R.R. Tolkien and the
films for their world; to [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) by TGP482, which made the Xbox
ports possible; to the open-source projects listed below; and to everyone who plays and reports problems.

## Licences

Athelas, ROTK Fix and the ports are MIT licensed, © 2026 Shagg (see `LICENSE`). If you use the code, please credit
"Athelas by Shagg" with a link to https://github.com/Shagg88/Athelas.

Bundled components keep their own licences; every text is in `licenses/` and in the download's `licenses` folder.

| Component | Licence |
|---|---|
| [DXVK](https://github.com/doitsujin/dxvk), [SDL3](https://libsdl.org) | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui), [stb](https://github.com/nothings/stb), [miniz](https://github.com/richgel999/miniz), [toml++](https://github.com/marzer/tomlplusplus), [Zydis](https://github.com/zyantific/zydis), ROTKPlus mod | MIT |
| [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) | MIT, © 2026 TGP482 |
| [LZMA SDK](https://www.7-zip.org/sdk.html) | Public domain |
| [DSOAL](https://github.com/kcat/dsoal), [OpenAL Soft](https://openal-soft.org) | LGPL 2.1 (source in the download's `licenses\source`) |
| [glslang](https://github.com/KhronosGroup/glslang) | BSD-3-Clause and others |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Apache 2.0 / MIT |
| Uncial Antiqua, Cinzel, EB Garamond, Cormorant Unicase, PT Sans, Liberation Sans | SIL Open Font License 1.1 |
| [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) (FSR, FSR frame generation) | AMD binary licence |
| [NVIDIA Streamline](https://github.com/NVIDIA-RTX/Streamline) | MIT, with its third-party notices |
| NVIDIA DLSS, DLSS frame generation, Reflex | NVIDIA RTX SDKs licence |
| [Intel XeSS](https://github.com/intel/xess), XeFG, XeLL | Intel Simplified Software License, with its third-party notices |

Symbol data from [XbSymbolDatabase](https://github.com/Cxbx-Reloaded/XbSymbolDatabase) and
[Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded).

Athelas uses NVIDIA DLSS, AMD FidelityFX Super Resolution and Intel XeSS. Their DLLs (the download's `upscalers`
folder) belong to NVIDIA, AMD and Intel, ship unmodified and stay under their own licences, not Athelas' MIT licence;
they may not be reverse engineered or redistributed apart from Athelas. NVIDIA, DLSS, Reflex, AMD, FidelityFX, Intel
and XeSS are trademarks of their owners, and Athelas isn't affiliated with or endorsed by them.
