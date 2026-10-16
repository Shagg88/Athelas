# The Return of the King (Xbox) - PC port

An unofficial native PC port of the original Xbox game *The Lord of the Rings: The Return of the King* (US release). The game's own program is statically recompiled for Windows with [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp). It adds frame rates up to 240, widescreen and ultrawide, sharper text and menus, keyboard and mouse, and fixes for slowdowns and vanishing enemies at high frame rates.

The port is part of **Athelas**, a launcher for three Lord of the Rings games. Players get it from the Athelas download (the Releases page of this repository), where it comes as `ROTKXbox.exe` in the `ROTK-Xbox` folder beside `Athelas.exe`. This folder holds the port's source.

> [!NOTE]
> No game files are included. You need your own copy of the Xbox game: a disc image (`.iso`) or its extracted files. This project is not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner Bros. or the Tolkien Estate.

## What you need

- Windows 10 or 11, 64-bit, and a Direct3D 12 graphics card.
- Your own copy of the US Xbox release.
- To play: the Athelas download. To build: see below.

What the port does, its keys and settings are in [`rotkxbox/README.md`](rotkxbox/README.md).

## Layout

| Path | What it is |
|---|---|
| `rotkxbox/` | The port: its build, its own game code (`src/`) and the recompiler's config for this game (`config/`). Build here. |
| `rotkxbox/sdk/` | The copy of XboxXBE-Recomp the port is built with: the recompiler, the runtime and its changes for this game. |
| `sdk_5558/` | The Return of the King's SDK changes as patches over the Two Towers port's SDK (the newest is current), and the hand-proven XDK addresses (`manual.toml`). |
| `sdk_4928/` | The Two Towers port's SDK patches over upstream XboxXBE-Recomp, the base the patches above sit on. |
| `notes/` | Development notes: `rotkx-01` to `rotkx-11` for this port, in order, and `01` to `08` from the Two Towers port. |
| `launcher-handoff/` | The interface between the ports and the Athelas launcher: how the launcher starts them and passes their settings. |
| `tools/` | Python scripts for looking at an xbe: headers, disassembly, callers, call graphs, config merging. They contain no game data. |
| `ref/` | The Two Towers port's config and game code, for reference. |
| `shared/` | The Linux test harness scripts, shared with the Two Towers port. |
| `work/` | The Linux test harness's working folder: build and sweep scripts, controller input scripts and small analysis tools. Its runs and builds are not kept. |

## Building

1. Install Visual Studio 2022 or newer with **Desktop development with C++**. `build.bat` offers to add **C++ Clang tools for Windows** and **C++ CMake tools for Windows** if they are missing. [Git](https://git-scm.com/) is needed too.
2. Put your own game's extracted files in `rotkxbox\assets`: `default.xbe` with the game's folders beside it.
3. Run `rotkxbox\build.bat`. The first build recompiles the game and takes a while. The exe is written to `rotkxbox\out\build\release`.

The recompiled code (`rotkxbox\generated`) is made on your PC from your own `default.xbe`. It is game code, so it is never committed, and neither are your game files, the build output or analysis output made from the game. `.gitignore` keeps them out.

## Licence

MIT, see [`LICENSE`](LICENSE). The SDK in `rotkxbox/sdk` is under its own MIT licence, (c) 2026 TGP482 (`rotkxbox/sdk/LICENSE`), and the libraries built into the exe are listed in `rotkxbox/sdk/THIRD_PARTY.txt`.

## Credits

By Shagg, with Claude (Anthropic).

Built on [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) by TGP482. The other parts are credited in [`rotkxbox/README.md`](rotkxbox/README.md).
