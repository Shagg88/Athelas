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
- **The Fellowship of the Ring (Xbox)**, through a native PC port (new in 1.4)

Download it from [Releases](../../releases): one zip with the launcher, ROTK Fix and all three ports.

Since 1.6 Athelas opens on your games as boxes on a carousel, with their original box art if you'd like it (see README.txt).

Since 1.7 the launcher also has:
- **Save backups**: back up a game's saves to a zip on your desktop and restore them (your current saves are zipped first)
- **A log folder per play**, plain-English exit messages, and **Clear shaders** for the Xbox ports (set aside, not deleted)
- **Mods from a .zip** for the PC game, with an order you set (Up / Down) and a warning before any code mod
- **Desktop shortcuts** for Athelas and for each game
- **A resizable window**: fullscreen with F11 or LT + RT, a borderless option, and maximised and borderless on a Steam Deck-sized screen
- **11 languages**: English, Spanish, French, German, Italian, Portuguese (Brazil), Polish, Russian, Japanese, Chinese (Simplified), Korean
- **Play time for the PC game** too

> Not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.
> No game files are included. You need your own copy of each game: the PC game installed, or your own Xbox disc as
> an `.iso` or extracted files (US releases). ROTK Fix doesn't change or bypass the PC game's copy protection. The
> Xbox ports skip the console's check for an original Xbox disc, which a PC can't answer as it has no Xbox drive;
> the games' own files are never decrypted or changed.

## Screenshots

<p align="center">
  <img src="https://github.com/user-attachments/assets/7a19a7d8-5653-4a2c-909e-cd8dd1dcf653" width="49%">
  <img src="https://github.com/user-attachments/assets/a3e6f93c-6f4a-49a3-838f-882b6918a74c" width="49%">
  <img src="https://github.com/user-attachments/assets/3430b371-e91e-4384-84d3-0da9656f378a" width="49%">
  <img src="https://github.com/user-attachments/assets/12192f57-5179-470c-bee4-6dec1affec9d" width="49%">
</p>

<p align="center">
  <img src="https://github.com/user-attachments/assets/4bea88f9-01f9-484e-8f0b-d22c6fbf1864" width="32%">
  <img src="https://github.com/user-attachments/assets/f5a0d251-7208-46ff-aea8-03d63577a4f2" width="32%">
  <img src="https://github.com/user-attachments/assets/d9600fa1-c760-4185-a82c-ada85b03ea87" width="32%">
</p>




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
| `ports/fellowship/` | The Fellowship of the Ring Xbox PC port ([its README](ports/fellowship/README.md)) |
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
- **Gameplay:** damage taken, enemy damage, every level and character unlocked, every hero in levels you've completed, the games' own cheat codes as switches, and kill points in The Two Towers (all off by default)
- **Achievements:** in all four games, earned as you play (and from what your saves already hold), with a note on screen and the list in the launcher
- **Launcher:** every setting in one place, with presets (Original, Balanced, High, Ultra)

## Requirements

- Windows 10 or 11 (64-bit), or Linux / Steam Deck through Proton (The Two Towers reported working on a real Steam Deck; see README.txt).
- Mac: through CrossOver (DirectX 12 through Apple's D3DMetal). Tested in a Wine setup like CrossOver's, not yet on a
  real Mac: reports welcome. See README.txt's Mac section.
- Your own US copy of each game. No game files are included.
- **The Return of the King (PC):** a Vulkan 1.3 graphics driver is recommended. Without one, the game runs
  through Windows' Direct3D 8 with the DXVK-only options (sharpening, colour looks, texture filtering)
  turned off. The launcher's About tab shows which renderer you'll get.
- **The Two Towers, The Return of the King and The Fellowship of the Ring (Xbox):** a DirectX 12 graphics card.

## The Two Towers (Xbox)

Played through its PC port, `TwoTowers.exe`, started from the launcher.

- Supported: the US Xbox release, as a disc image (`.iso`) or the extracted files. Other releases are recognised,
  with a warning before they start.
- Keyboard and mouse or a controller. `F10` (or both stick clicks) opens an in-game settings menu; every setting is
  also on the launcher's page or in `settings.toml`.
- The game's own files are only read, never changed.
- Sharper text: the game's fonts redrawn at four times their size, crisp at high resolutions.
- Keys: `F10` settings menu, `\` screenshot, `F1` hide the HUD, `[` sharpening off/on, `]` next colour look,
  `F8` texture packs off/on, `F7` a pair of screenshots with and without texture packs.
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

## The Fellowship of the Ring (Xbox)

Played through its PC port, `FOTRXbox.exe`, started from the launcher (since 1.4).

- Supported: the US Xbox release, as a disc image (`.iso`) or the extracted files.
- As on the Xbox by default (30 fps, 4:3); frame rates up to 240 at normal game speed, the game's own widescreen
  mode and ultrawide (21:9, 32:9), field of view, difficulty, sharper upscaled cutscenes, logo skip.
- The game's text in nine languages (our own translations), the game's own cheat codes as switches, and
  achievements for each part of the journey.
- A chapter select (a new game can start at the beginning of any chapter) and the developers' unused Combat Test
  arenas, switched on from Bilbo's room in Rivendell.
- Problem reports: `FOTR-Xbox\fotrx.log`, plus the newest `.dmp` in `FOTR-Xbox\CrashDumps` after a crash.

## Steam Deck and Linux

There's no Linux build or AppImage: Athelas and the games run through Proton, Steam's Windows layer, which also
covers their DirectX 12. The Two Towers has been reported working on a real Steam Deck.

1. In Desktop Mode, unzip Athelas into a folder of its own, for example `/home/deck/Games/Athelas`.
2. In Steam: **Games > Add a Non-Steam Game to My Library**, and choose `Athelas.exe`. In its **Properties >
   Compatibility**, tick **Force the use of a specific Steam Play compatibility tool** and pick the newest Proton.
3. Start it from Steam (Game Mode works too) and point it at your games: disc images (.iso) or extracted folders
   anywhere on the Deck or an SD card. For The Return of the King (PC), a folder with the installed game copied from a
   Windows PC (the original disc's copy protection doesn't run under Proton).

The Deck's controls work as an Xbox controller in Athelas and in every game. The Balanced preset at 60 fps suits the
Deck. DLSS needs an NVIDIA card, so on the Deck leave the upscaler off or try FSR (it hasn't been tried on a Deck yet; if
it can't start, the game shows its normal picture). Other Linux PCs work the same way
through Steam, or through any Proton or Wine launcher with DXVK and VKD3D-Proton. More details are in README.txt.

## Reporting a problem

Open an [issue](../../issues) and choose **Bug report**. Attach the game's log: `ROTKFix.log` in the PC game's folder,
`ROTK-Xbox\rotkx.log`, `TT-Xbox\tt.log` or `FOTR-Xbox\fotrx.log` beside the launcher. After an Xbox port crashes, add the newest `.dmp` from its
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

Athelas, ROTK Fix and the ports are MIT licensed, © 2026 Shagg (see `LICENSE`). Bundled components keep their own licences, all in `licenses/` and shipped in `ROTKFix\licenses`:

- DXVK (zlib)
- SDL3 (zlib)
- Dear ImGui (MIT)
- stb (MIT / public domain)
- DSOAL and OpenAL Soft (LGPL; their source at the shipped commits is in the download's `licenses\source`)
- Uncial Antiqua, Cinzel and EB Garamond fonts (SIL OFL)
- Liberation Sans (SIL OFL), used to draw the letters in the button pictures
- XboxXBE-Recomp (MIT, © 2026 TGP482) and toml++ (MIT), inside the bundled Xbox ports
