# The Fellowship of the Ring (Xbox) - PC port

An unofficial native PC port of the original Xbox game *The Lord of the Rings: The Fellowship of the Ring* (US release, 2002). The game's own program is statically recompiled for Windows with [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp). It plays as it did on the Xbox by default (30 fps, 4:3); higher frame rates, widescreen and ultrawide, field of view, difficulty, sharper cutscenes, the game's text in nine languages, cheats as switches and achievements are settings you turn on.

The port is part of **Athelas**, a launcher for three Lord of the Rings games. Players get it from the Athelas download (the Releases page of this repository), where it comes as `FOTRXbox.exe` in the `FOTR-Xbox` folder beside `Athelas.exe`. This folder holds the port's source. It is new in Athelas 1.4, a first version.

> [!NOTE]
> No game files are included. You need your own copy of the Xbox game: a disc image (`.iso`) or its extracted files. This project is not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.

## What you need

- Windows 10 or 11, 64-bit, and a Direct3D 12 graphics card.
- Your own copy of the US Xbox release.
- To play: the Athelas download. To build: see below.

## Layout

| Path | What it is |
|---|---|
| `fotrxbox/` | The port: its build, its own game code (`src/`) and the recompiler's config for this game (`config/`). Build here. |
| `fotrxbox/sdk/` | The copy of XboxXBE-Recomp the port is built with, shared with the other two ports. |
| `fotrxbox/lang/` | The game's text in nine languages, our own translations (no English game text is included). |
| `fotrxbox/fonts/` | Open fonts (SIL Open Font License) with the Polish and Russian letters the game's fonts lack. |
| `xdk_4831/` | The hand-proven XDK addresses for this game (`manual.toml`). |
| `notes/` | Development notes: bring-up, the fixes found, frame pacing, the camera, damage and achievements. |
| `tools/` | Python scripts for looking at an xbe and for the translations. They contain no game data. |

## Building

1. Install Visual Studio 2022 or newer with **Desktop development with C++**. `build.bat` offers to add **C++ Clang tools for Windows** and **C++ CMake tools for Windows** if they are missing. [Git](https://git-scm.com/) is needed too.
2. Put your own game's `Default.xbe` in `fotrxbox\assets`, and the folder with the rest of the game's files in `fotrxbox\assets\redirect.txt` (or the files themselves beside it).
3. Run `fotrxbox\build.bat`. The first build recompiles the game. The exe is written to `fotrxbox\out\build\release`; `release.bat` makes the folder Athelas ships.

The recompiled code (`fotrxbox\generated`) is made on your PC from your own `Default.xbe`. It is game code, so it is never committed, and neither are your game files, the build output or analysis output made from the game. `.gitignore` keeps them out.

## Licence

MIT, see [`LICENSE`](LICENSE). The SDK in `fotrxbox/sdk` is under its own MIT licence, (c) 2026 TGP482 (`fotrxbox/sdk/LICENSE`), and the libraries built into the exe are listed in `fotrxbox/sdk/THIRD_PARTY.txt`. The fonts in `fotrxbox/fonts` are under the SIL Open Font License (their licence files are beside them).

Using our code? You're welcome to. If you use The Fellowship of the Ring port's code in your own project, please credit "Athelas by Shagg (The Fellowship of the Ring port)" and link to https://github.com/Shagg88/Athelas in your readme or credits. (The MIT licence already asks you to keep the copyright notice in copies of the code.)

## Credits

By Shagg, with Claude (Anthropic).

Built on [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) by TGP482.
