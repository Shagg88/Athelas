# The Two Towers (Xbox) - PC port

An unofficial native PC port of the original Xbox game *The Lord of the Rings: The Two Towers* (US release). The game's own program is statically recompiled for Windows with [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp). It plays as it did on the Xbox by default; widescreen, higher frame rates, sharper graphics and the other improvements are settings you turn on.

The port is part of **Athelas**, a launcher for three Lord of the Rings games. Players get it from the Athelas releases page, where it comes as `TwoTowers.exe` in the `TT-Xbox` folder beside `Athelas.exe`. This repository holds the port's source.

> [!NOTE]
> No game files are included. You need your own copy of the Xbox game: a disc image (`.iso`) or its extracted files. This project is not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.

## What you need

- Windows 10 or 11, 64-bit, and a Direct3D 12 graphics card.
- Your own copy of the US Xbox release.
- To play: the Athelas download. To build: see below.

What the port does, its keys and settings are in [`game/README.md`](game/README.md).

## Layout

| Path | What it is |
|---|---|
| `game/` | The port: its build, its own game code (`src/`) and the recompiler's config for this game (`config/`). Build here. |
| `game/sdk/` | The copy of XboxXBE-Recomp the port is built with: the recompiler, the runtime and its changes for this game. |
| `sdk_4928/` | Those SDK changes as patches over upstream XboxXBE-Recomp, and the hand-proven XDK addresses (`manual.toml`). |
| `notes/` | Development notes, in order: the xbe analysis, the plan, bring-up and the first Windows runs. |
| `launcher-handoff/` | The interface between the port and the Athelas launcher: how the launcher starts it and passes its settings. |
| `tools/` | Python scripts for looking at an xbe: headers, disassembly, call graphs, config merging. They contain no game data. |
| `shared/` | Analysis helpers and the Linux test harness scripts, shared with the Return of the King port. |
| `HANDOFF.md` | A developer handoff: the state of the port and how its parts fit together. |

## Building

1. Install Visual Studio 2022 or newer with **Desktop development with C++**. `build.bat` offers to add **C++ Clang tools for Windows** and **C++ CMake tools for Windows** if they are missing. [Git](https://git-scm.com/) is needed too.
2. Put your own game's extracted files in `game\assets`: `default.xbe` with the game's folders beside it.
3. Run `game\build.bat`. The first build recompiles the game and takes 30 to 90 minutes. The exe is written to `game\out\build\release`.

The recompiled code (`game\generated`) is made on your PC from your own `default.xbe`. It is game code, so it is never committed, and neither are your game files, the build output or analysis output made from the game. `.gitignore` keeps them out.

The analysis scripts in `tools\` need Python and `pip install capstone`.

## Licence

MIT, see [`LICENSE`](LICENSE). The SDK in `game/sdk` is under its own MIT licence, (c) 2026 TGP482 (`game/sdk/LICENSE`), and the libraries built into the exe are listed in `game/sdk/THIRD_PARTY.txt`.

## Credits

By Shagg, with Claude (Anthropic).

Built on [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) by TGP482. The other parts are credited in [`game/README.md`](game/README.md).
