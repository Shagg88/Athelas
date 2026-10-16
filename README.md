# Athelas

I'm Shagg and I created this project with the help of Claude (Anthropic). This is a free, unofficial launcher for three Lord of the Rings games, so you can play
your own copies on a modern Windows PC from one place.  Tolkien created an amazing world that deserves to be lived and experienced in every way possible. I used to play this with my siblings 
and friends for hours on end, day after day and it's an absolute shame they're not distributable today, so that's why I'm here. (Athelas is kingsfoil, the healing herb).  
After the release of the War in the North "remaster" I decided to really take on this project. This is my first ever project of this magnitude so I expect there to be a few bugs.  
I have a Windows PC that I tested this on, that's about it.  It's supposed to run on Linux and Steam Deck(Mac and Android maybe in the future) but only time will tell. Mods and Texture 
packs are supported but I have not done any of those.  I'll be open to accepting public contributions and pull requests as well.

- **The Return of the King (PC, 2003)**, through **ROTK Fix** (This is the recommended way to play The Return of the King, at least in my opinion)
- **The Return of the King (Xbox)**, through a native PC port
- **The Two Towers (Xbox)**, through a native PC port

Download it from [Releases](../../releases): one zip with the launcher, ROTK Fix and both ports.

> Not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.
> No game files are included. You need your own copy of each game: the PC game installed, or your own Xbox disc as
> an `.iso` or extracted files (US releases). ROTK Fix doesn't change or bypass the PC game's copy protection. The
> Xbox ports skip the console's check for an original Xbox disc, which a PC can't answer as it has no Xbox drive;
> the games' own files are never decrypted or changed.

## Screenshots

<img width="880" height="664" alt="Screenshot 2026-10-16 114801" src="https://github.com/user-attachments/assets/03aa7abf-9251-4ba7-9da1-0c436dd3f7aa" />
<img width="878" height="664" alt="Screenshot 2026-10-16 114814" src="https://github.com/user-attachments/assets/c96dd4be-73fc-46d5-9999-c967dc6f78ca" />
<img width="879" height="664" alt="Screenshot 2026-10-16 114911" src="https://github.com/user-attachments/assets/3944288d-4964-43c0-a82d-1f3b4a6191f5" />


*The background pictures in these screenshots are artwork from the games themselves. They aren't included in the
download: Athelas reads them from your own copy of a game, so you'll only see them once the launcher has found one
of your games. Until then, or if you choose "None" under About → Background pictures, the launcher shows its own
drawn landscape instead. All game and film artwork belongs to its owners.*

## In this repository

| Path | What it is |
|---|---|
| `launcher/` | Athelas, the launcher |
| `src/`, `build.sh` | ROTK Fix, for the PC game |
| `ports/two-towers/` | The Two Towers PC port ([its README](ports/two-towers/README.md)) |
| `ports/rotk-xbox/` | The Return of the King Xbox PC port ([its README](ports/rotk-xbox/README.md)) |
| `dist-files/README.txt` | The README that comes in the download, with every setting |
| `docs/` | Release notes and engine notes |
| `scripts/` | Packaging the release zip |
| `third_party/`, `licenses/` | Bundled libraries and every licence |

## ROTK Fix: what it does

- **Any screen:** widescreen and ultrawide without stretching (menus and HUD stay at most 16:9), any resolution, borderless/fullscreen/windowed with ultrawide window sizes, Alt-Tab that works
- **Smooth:** 60 to 240 fps at normal game speed, even frame pacing, V-Sync aware
- **Sharper:** anti-aliasing, texture filtering, sharpening, working brightness in borderless
- **Controllers:** Xbox, PlayStation, Switch and most others; button remapping; the right button names in hints
- **Co-op:** two controllers on one PC, or online by streaming with Steam Remote Play Together or Parsec (see README.txt)
- **Movies:** cutscenes fill widescreen screens with correct proportions; optional logo skip; a sound delay setting for Bluetooth speakers
- **Texture packs:** dump the game's textures to PNG and load replacements from a folder (no game art included)
- **Screenshots:** `\` or Print Screen saves a PNG
- **Gameplay:** damage taken, enemy damage, and every level and character unlocked (off by default)
- **Achievements:** in all three games, earned as you play (and from what your saves already hold), with a note on screen and the list in the launcher
- **Launcher:** every setting in one place, with presets (Original, Balanced, High, Ultra)

## Requirements

- Windows 10 or 11 (64-bit), or Linux / Steam Deck through Proton (tested in a Deck-like setup, not yet on a real Deck; see README.txt).
- Your own US copy of each game. No game files are included.
- **The Return of the King (PC):** a Vulkan 1.3 graphics driver is recommended. Without one, the game runs
  through Windows' Direct3D 8 with the DXVK-only options (sharpening, colour looks, texture filtering)
  turned off. The launcher's About tab shows which renderer you'll get.
- **The Two Towers and The Return of the King (Xbox):** a DirectX 12 graphics card.

## The Two Towers (Xbox)

Played through its PC port, `TwoTowers.exe`, started from the launcher.

- Supported: the US Xbox release, as a disc image (`.iso`) or the extracted files. Other releases are recognised,
  with a warning before they start.
- Keyboard and mouse or a controller. `F10` (or both stick clicks) opens an in-game settings menu; every setting is
  also on the launcher's page or in `settings.toml`.
- The game's own files are only read, never changed.
- Keys: `F10` settings menu, `\` screenshot, `F1` hide the HUD, `[` sharpening off/on, `]` next colour look.
- Problem reports: the log (`TT-Xbox\tt.log` next to the launcher), plus the newest `.dmp` in
  `TT-Xbox\CrashDumps` after a crash.

## The Return of the King (Xbox)

Played through its PC port, `ROTKXbox.exe`, started from the launcher with the same settings page as the PC game.

- Supported: the US Xbox release, as a disc image (`.iso`) or the extracted files.
- Frame rates up to 240, widescreen and ultrawide, fixes for slowdowns and vanishing enemies at high frame rates.
- Sharper text and menus: with the PC game installed too, its larger fonts and menu pictures are used; other menu
  pictures are scaled up sharply.
- Two-player co-op on one PC, or online by streaming (Parsec, or Steam Remote Play Together).
- Problem reports: `ROTK-Xbox\rotkx.log`, plus the newest `.dmp` in `ROTK-Xbox\CrashDumps` after a crash.

## Reporting a problem

Open an [issue](../../issues) and choose **Bug report**. Attach the game's log: `ROTKFix.log` in the PC game's folder,
`ROTK-Xbox\rotkx.log` or `TT-Xbox\tt.log` beside the launcher. After an Xbox port crashes, add the newest `.dmp` from its
`CrashDumps` folder, zipped first (GitHub doesn't take `.dmp` files as they are).

## Antivirus and SmartScreen

ROTK Fix replaces `d3d8.dll`/`dinput8.dll` in the game folder and patches the game in memory, the usual way
for game mods, and its files aren't code-signed. So SmartScreen may call the launcher "unrecognised" and
some antivirus programs may flag it. Every zip has a `.sha256` next to it (`Get-FileHash` in PowerShell to compare). ROTK Fix makes no network connections.

## Install

1. Download the zip from [Releases](../../releases).
2. Unzip it into a folder of its own (not a game folder).
3. Run `Athelas.exe` (the launcher) and show it your games. For the PC game it puts ROTK Fix's files
   (the DLLs and a `ROTKFix` folder) next to `rotk.exe`; the Xbox games are only read.
4. Press **Play**.

To uninstall, use **About → Remove ROTK Fix** in the launcher, then delete the launcher's folder.

## How it works

`d3d8.dll` and `dinput8.dll` are small loaders that hand the game's graphics and input calls to `ROTKFix.dll`:

- **Graphics:** drawn through [DXVK](https://github.com/doitsujin/dxvk) (Vulkan), with ROTK Fix's own changes applied on the way.
- **Controllers:** read through [SDL3](https://libsdl.org) and shown to the game as standard pads (one per player), with button names or pictures in the game's hints.
- **Sound:** the game's 3D sound and EAX reverb restored through [DSOAL](https://github.com/kcat/dsoal) and [OpenAL Soft](https://openal-soft.org).
- **Launcher:** writes `ROTKFix.ini` and the game's own `ROTK.ini`.

## Building (for developers)

Players don't need this: download the zip from [Releases](../../releases) instead. This section is only for
changing the code and building it yourself.

Builds cross-compile on Linux with MinGW-w64 (32-bit, because the game is 32-bit):

```sh
sudo apt install gcc-mingw-w64-i686 g++-mingw-w64-i686 zip
# unpack SDL3-devel-3.4.18-mingw.tar.gz and dxvk-3.1.1.tar.gz, then:
SDL_DIR=path/to/SDL3-3.4.18 DXVK_DIR=path/to/dxvk-3.1.1 DSOAL_DIR=third_party/dsoal sh scripts/package.sh 1.0
```

The 3D sound libraries (DSOAL and OpenAL Soft) are kept in `third_party/dsoal` with checksums, so every release ships the tested versions.

The GitHub Actions workflow (`.github/workflows/build.yml`) builds ROTK Fix and Athelas the same way on every push. It can't build the two Xbox ports: recompiling them needs the game's own `default.xbe`, which can never be in a repository. Each port builds on Windows with its `build.bat` (see the ports' READMEs), and the full Athelas download is packaged with `TT_DIR` and `RX_DIR` pointing at the ports' release folders.

## Credits

Made by Shagg, with Claude (Anthropic).

Thank you to:
- **The developers:** Stormfront Studios, EA Redwood Shores and Hypnos Entertainment, for making these games. And
  to J.R.R. Tolkien and the films, for the world they're set in.
- **[XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp)** by TGP482, which made the Xbox ports possible.
- **The open-source projects** this is built on: [DXVK](https://github.com/doitsujin/dxvk),
  [SDL](https://libsdl.org), [DSOAL](https://github.com/kcat/dsoal), [OpenAL Soft](https://openal-soft.org),
  [Dear ImGui](https://github.com/ocornut/imgui), [toml++](https://github.com/marzer/tomlplusplus),
  [stb](https://github.com/nothings/stb), [XbSymbolDatabase](https://github.com/Cxbx-Reloaded/XbSymbolDatabase)
  and [Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded).
- **Everyone who plays it and reports a problem.**

Every licence is in the [licenses](licenses) folder.

## Licences

Athelas, ROTK Fix and the ports are MIT licensed, © 2026 Shagg (see `LICENSE`). Bundled components keep their own licences, all in `licenses/` and shipped in `ROTKFix\licenses`.:

- DXVK (zlib)
- SDL3 (zlib)
- Dear ImGui (MIT)
- stb (MIT / public domain)
- DSOAL and OpenAL Soft (LGPL; their source at the shipped commits is in the download's `licenses\source`)
- Uncial Antiqua, Cinzel and EB Garamond fonts (SIL OFL)
- Liberation Sans (SIL OFL), used to draw the letters in the button pictures
- XboxXBE-Recomp (MIT, © 2026 TGP482) and toml++ (MIT), inside the bundled Xbox ports
