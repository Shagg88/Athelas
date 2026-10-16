# The Two Towers - PC port

An unofficial native PC port of the original Xbox game, made by recompiling the game's own program for Windows. It plays as it did on the Xbox by default; widescreen, higher frame rates, sharper graphics and the other improvements are settings you turn on.

> [!NOTE]
> No game files are included. You need your own copy of the Xbox game: a disc image (`.iso`) or its extracted files. This port is not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.

## Playing

**With the Athelas launcher (recommended):** the `TT-Xbox` folder sits beside `Athelas.exe` in the Athelas download. Choose your Two Towers files in the launcher. All the settings are on its Two Towers page.

**On its own:**

1. Put your game in the `assets` folder beside `TwoTowers.exe`: the `.iso`, or `default.xbe` with the game's folders beside it.
2. Run `TwoTowers.exe`.
3. Settings are in the in-game menu (`F10`) and in `settings.toml`, which is created beside the exe on the first run. Change the file while the game is closed.

The US release of the game is supported. Another release is detected and you are warned before it starts.

## What it adds

**Display**
- Fullscreen, borderless or windowed, on any monitor, at any resolution and refresh rate.
- Widescreen from 16:9 up to 32:9 ultrawide, with a wider view rather than a stretched one. The HUD can sit at the screen's edges.
- Render resolution from 50% to 300%, anti-aliasing up to 8x, texture filtering up to 16x, sharper textures and sharpening.
- Frame rates of 30 (the original), 60, 90, 120, 144, 165, 180 and 240, with the game at its normal speed.
- HDR on HDR screens.
- Adjustable field of view and brightness, and optional colour looks (Film, Vivid, Dark fantasy).

**Menus and movies**
- Menus fill the screen, and menu pictures and text are upscaled smoothly instead of turning blocky.
- Movies shown at 4:3 as on the Xbox, or fitted or stretched to the screen, with optional sharpening and a sound delay setting.
- The opening logos, or all videos, can be skipped.

**Controls**
- Keyboard and mouse, as well as a controller (see below). The game's hints still show controller buttons.
- Xbox, PlayStation and Switch controllers, plugged in at any time, with rumble.
- Button remapping, rumble strength, stick deadzone and trigger sensitivity.

**In-game settings menu**
- `F10`, or both stick clicks together on a controller, opens a settings menu over the game, which stands still behind it.
- It holds the settings that take effect at once (picture, controls, field of view, damage) and the achievements list. It saves them when it closes (`F10`, `Esc`, B or Start). LB and RB, or Page Up and Page Down, change its page.

**In the background**
- "Pause in the background" stops the game, picture and sound, while another window is in front, and carries on where it was when you come back.
- "Mute in the background" only silences it. Both are off by default.

**Gameplay**
- Damage you take and damage enemies take, each from 25% to 300% of the chosen difficulty's own.

**Achievements**
- 14 achievements: every level, and the secret mission. They are earned as you play, and what your saves already hold counts too, so an old save earns the levels it has finished the first time you play.
- A note shows in the bottom right corner when you earn one ("Achievement notes" setting). The list, earned and not, is in the launcher and on the in-game menu's Achievements page.
- They are kept in `achievements.txt` beside the log, and only on this PC; nothing is sent anywhere.

**Other**
- Texture packs (PNG files in `textures\replace`), and saving the game's textures to make them.

## Keys

| Key | What it does |
|---|---|
| `F10` | Settings menu (the game pauses) |
| `\` (above Enter) | Screenshot, saved as a PNG in `Screenshots` |
| `F1` | Hide or show the HUD |
| `]` | Next colour look |
| `[` | Sharpening off and on, to compare |

## Keyboard and mouse

The keys are the PC version of The Return of the King's. Each one can be changed on the launcher's Controls tab, or in the `[keyboard]` part of `settings.toml`.

| Key | What it does |
|---|---|
| `W` `A` `S` `D` | Move (hold Left Alt to walk) |
| Left mouse, `Enter` | Speed attack; in the menus, `Enter` goes on |
| Right mouse | Fierce attack |
| `Space`, middle mouse | Killing move |
| `Q` | Parry |
| Left Ctrl, Backspace | Knock back, and back in menus |
| Left Shift | Ranged attack |
| `Tab` | Jump back |
| `Esc` | Pause; in the menus, back |
| Arrow keys | Menus |

## Requirements

- Windows 10 or 11, 64-bit.
- A graphics card that supports Direct3D 12.
- A keyboard and mouse or a controller.

## Files it writes

Beside the exe unless the launcher chooses other folders: `settings.toml`, `hdd` (save games), `shadercache` (safe to delete; it refills), `logs` (the log and `achievements.txt`), `textures` and `Screenshots`. From Athelas, the saves are in `TT-Xbox\saves` and the log is `TT-Xbox\tt.log`. Your game files are only read, never changed.

## If something goes wrong

The log says why the game stopped. A crash also leaves a file in `CrashDumps` beside the log. Include both when you report a problem.

## Building

1. Put the extracted game files in `assets`: `default.xbe` with the game's folders beside it.
2. Install Visual Studio 2022 or newer with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two), and [Git](https://git-scm.com/).
3. Run `build.bat`. The first build recompiles the game and takes 30 to 90 minutes. The exe is written to `out\build\release`.
4. `release.bat` makes the folder that Athelas ships as `TT-Xbox`, and a zip for playing without the launcher.

The recompiled code in `generated` is made from your own game and is never committed.

## Credits

By Shagg, with Claude (Anthropic).

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis), used by the recompiler to read the game's code.
- [toml++](https://github.com/marzer/tomlplusplus), [Dear ImGui](https://github.com/ocornut/imgui), [SDL](https://github.com/libsdl-org/SDL) and [stb](https://github.com/nothings/stb), built into the exe.

The port is MIT licensed. The licences of the parts in the exe are in `THIRD_PARTY.txt` and `LICENSE-XboxXBE-Recomp.txt`.
