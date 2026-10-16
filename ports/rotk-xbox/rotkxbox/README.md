# The Return of the King - PC port

An unofficial native PC port of the original Xbox game *The Lord of the Rings: The Return of the King* (US release), made by recompiling the game's own program for Windows with [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp). It adds frame rates up to 240, widescreen and ultrawide, sharper text and menus, keyboard and mouse, and fixes for slowdowns and vanishing enemies at high frame rates.

> [!NOTE]
> No game files are included. You need your own copy of the Xbox game: a disc image (`.iso`) or its extracted files. This port is not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.

## Playing

**With the Athelas launcher (recommended):** the `ROTK-Xbox` folder sits beside `Athelas.exe` in the Athelas download. Choose your game's folder or disc image in the launcher. The settings are on the same page as the PC version's (ROTK Fix), and are kept in its `ROTKFix.ini`.

**On its own:** put your game in the `assets` folder beside `ROTKXbox.exe` (the `.iso`, or `default.xbe` with the game's folders beside it) and run it. Settings are then in `settings.toml` beside the exe.

The US release of the game is supported. Another release is detected and you are asked before it starts.

## What it adds

**Display**
- Fullscreen, borderless or windowed, at any resolution.
- Widescreen and ultrawide, with a wider view rather than a stretched one. The HUD can keep its shape and sit at the screen's edges, and the Xbox's TV-safe border can be turned off.
- Frame rates of 30 (the original), 60, 90, 120, 144, 165, 180 and 240, with the game at its normal speed. Slowdowns and enemies vanishing at high frame rates are fixed.
- Anti-aliasing, with smooth edges on leaves, grass and fences, texture filtering and sharpening.
- Adjustable field of view, draw distance and brightness, and optional colour looks.

**Text and menus**
- Sharper text and menus: if you also have The Return of the King (PC), its larger fonts and title and menu pictures are used. Not required.

**Movies**
- Any video can be skipped, and the opening logos can be skipped altogether.

**Controls**
- Keyboard and mouse, as well as a controller (see below). With no controller plugged in, the game's hints show the keys.
- Xbox, PlayStation and Switch controllers, with rumble, button remapping and the right button pictures in the game's hints.
- Two-player co-op on one PC, with two controllers or a controller and the keyboard.

**In-game settings menu**
- `F10`, or both stick clicks together on a controller, opens a settings menu over the game, which stands still behind it.
- It holds the settings that take effect at once (picture, controls, field of view, draw distance) and the achievements list. It saves them when it closes (`F10`, `Esc`, B or Start). LB and RB, or Page Up and Page Down, change its page.

**In the background**
- "Pause in the background" stops the game, picture and sound, while another window is in front, and carries on where it was when you come back.
- "Mute in the background" only silences it. Both are off by default.

**Gameplay**
- Damage you take, damage enemies take, and every level and character unlocked. All off by default.

**Achievements**
- 29 achievements, the same as the PC version's: every level, every hero, co-op, a hero at level 10, enemies defeated. They are earned as you play, and what your saves already hold counts too, so an old save earns the levels it has finished the first time you play.
- A note shows in the bottom right corner when you earn one ("Achievement notes" setting). The list, earned and not, is in the launcher and on the in-game menu's Achievements page.
- They are kept in `achievements.txt` beside the log, and only on this PC; nothing is sent anywhere.

**Other**
- Texture packs, and saving the game's textures to make them.
- **Boot splash:** a `splash.png` (or `.bmp`) beside the exe is shown at launch.

## Keys

| Key | What it does |
|---|---|
| `F10` | Settings menu (the game pauses) |
| `\` (above Enter) | Screenshot, saved as a PNG in `Screenshots` |
| `]` | Next colour look |
| `[` | Sharpening off and on, to compare |

## Keyboard and mouse

The keys are the PC version's. Each one can be changed on the launcher's Controls tab: click a move, then press the key or mouse button.

| Key | What it does |
|---|---|
| `W` `A` `S` `D` | Move (hold Left Alt to walk) |
| Left mouse, `Enter` | Speed attack; in the menus, `Enter` goes on (and "press start") |
| Right mouse | Fierce attack |
| `Space`, middle mouse | Killing move |
| `Q` | Parry |
| Left Ctrl, Backspace | Physical attack (knock back), and back in menus |
| Left Shift | Ranged attack |
| `Tab` | Jump back |
| `E` | Action |
| `F5` | Special ability |
| `Esc` | Pause; in the menus, back |
| Arrow keys | Menus |

## Known issue

The picture is darker than the PC version's because it keeps the Xbox's brightness curve. Raise Brightness to about 1.2 to 1.3 to match.

## Requirements

- Windows 10 or 11, 64-bit.
- A graphics card that supports Direct3D 12.
- A keyboard and mouse or a controller.

## Files it writes

From Athelas: `ROTK-Xbox\saves` (save games), `ROTK-Xbox\rotkx.log` and `ROTK-Xbox\achievements.txt`, with the settings in `ROTKFix.ini`. On its own, beside the exe: `settings.toml`, `hdd` (save games), `shadercache` (safe to delete; it refills), `logs`, `textures` and `Screenshots`. Your game files are only read, never changed.

## If something goes wrong

The log says why the game stopped. A crash also leaves a `.dmp` file in `CrashDumps` beside the log. Include both when you report a problem.

## Building

1. Put the extracted game files in `assets`: `default.xbe` with the game's folders beside it.
2. Install Visual Studio 2022 or newer with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two), and [Git](https://git-scm.com/).
3. Run `build.bat`. The first build recompiles the game and takes a while. The exe is written to `out\build\release`.
4. `release.bat` builds, then makes `release\ROTKXbox`, the folder that Athelas ships as `ROTK-Xbox`.

The recompiled code in `generated` is made from your own game and is never committed.

## Credits

By Shagg, with Claude (Anthropic).

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis), used by the recompiler to read the game's code.
- [toml++](https://github.com/marzer/tomlplusplus), [Dear ImGui](https://github.com/ocornut/imgui), [SDL](https://github.com/libsdl-org/SDL) and [stb](https://github.com/nothings/stb), built into the exe.

The port is MIT licensed. The licences of the parts in the exe are in `THIRD_PARTY.txt` and `LICENSE.txt` (the runtime's MIT licence).
