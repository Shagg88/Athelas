# ROTK Xbox port: handoff

Written 2026-10-14 by the chat that built the port so far, for the developer taking over. Read this first, then the notes it
points to.

## 0. Where the code is right now

- **Git repository:** this file's folder. The repo was developed in a Linux cloud container (`/home/claude/rotkx`)
  that is deleted when that session ends. The committed history is delivered to the PC as
  **`C:\ROTKX\ROTKX.bundle`** (a git bundle, one branch `master`).
  - To get a working repo, run `git clone C:\ROTKX\ROTKX.bundle <folder>`.
  - The PC folder `C:\ROTKX` is not a git checkout. Its source files are byte-for-byte the same as the repo's tracked
    files: checked file by file on 2026-10-14. Some differ only in line endings (CRLF on the PC for `.bat`).
  - Some repo files aren't on the PC: notes `01`–`08` (from the Two Towers project), `launcher-handoff/`, `ref/`,
    `sdk_4928/`, `shared/`, and the `work/` scripts. They're only in the bundle.
- **History was rewritten on 2026-10-14.** The game files had been committed by mistake (`gamedata/`,
  `gamedata_fast/`, `assets_local/default.xbe`). `git filter-repo` removed them from every commit. Commit hashes
  quoted in the notes are the old ones. The map is in section 13.
- **Never pushed anywhere.** The user's rule: local only, nothing on GitHub.

## 1. What the project is

A native Windows PC version of **The Lord of the Rings: The Return of the King** for the original Xbox (US release,
title ID `4541003E`, `default.xbe` sha1 `6d4080dc324d68c6cf028af7583df9240f3f14b2`).

- It's made by static recompilation: the Xbox game's x86 code is translated to C++ and compiled into one exe.
- That exe is linked with a runtime that stands in for the Xbox: the kernel, the XDK libraries (D3D8 → Direct3D 12,
  DirectSound → XAudio2, XInput → SDL3 controllers), and file access.
- The recompiler and runtime are **XboxXBE-Recomp** (github.com/TGP482/XboxXBE-Recomp, MIT). The copy used here is
  the Two Towers port's SDK plus ROTK's own changes.
- The game itself (code and data) is never shipped. Players supply their own disc dump.
- It's one of three related projects the user runs:
  - **ROTK Fix / Athelas:** the launcher, and the ROTK PC fix DLL.
  - **The Two Towers Xbox port:** `C:\TT`, the same recompiler on another game.
  - **This one.**

  The launcher (Athelas) starts this port. See section 6.

## 2. Layout

| Path | What it is |
|---|---|
| `rotkxbox/` | **The game project.** Building it on Windows makes `ROTKXbox.exe`. |
| `rotkxbox/build.bat` | The Windows build: finds Visual Studio, configures with CMake, runs the recompiler, compiles. |
| `rotkxbox/release.bat` | Builds, then writes `rotkxbox/release/ROTKXbox/` with the files to ship (section 7). |
| `rotkxbox/capture.bat`, `capture_threaded.bat` | Starts the game with `XBE_CAPTURE_DIR` set, so **F3** writes a frame capture (section 5). |
| `rotkxbox/CMakeLists.txt`, `CMakePresets.json` | The CMake project (Ninja, clang-cl), preset `release`. |
| `rotkxbox/config/game.toml` | **The recompiler's config for this xbe:** XDK symbols, function and jump-table fixes, per-function options. Partly generated (the `[symbols]` part, by `xbe_symbols`), mostly hand-proven. |
| `rotkxbox/config/hooks.toml` | Hand-written mid-instruction hooks (`[[hook]] at=, call=, on_true=`). `make_config.sh` appends it to game.toml. |
| `rotkxbox/src/common/game_info.cpp` | What the runtime asks of the game: title, exe name, version, settings shown in the overlay, defaults. |
| `rotkxbox/src/game/*.cpp` | **The port's hand-written game code** (C++ functions that override or hook recompiled ones). See the list below. |
| `rotkxbox/tools/mkprompts.py` | Draws the port's own PlayStation/Nintendo button pictures into `src/game/rotk_prompts_data.h` (original art). |
| `rotkxbox/sdk/` | **The SDK copy** the game is built with: the recompiler (`tools/recomp`), the symbol finder (`tools/symbols`), the runtime (`runtime/`), the Linux test harness (`harness/`), XDK layouts and signatures (`xdk/`), CMake (`cmake/XbeRecomp.cmake`). |
| `rotkxbox/generated/` | *(ignored)* C++ generated from the xbe by `xbe_recomp`. Never commit: it's game code. |
| `rotkxbox/assets/` | *(ignored, PC only)* `default.xbe` (a copy for the build) and `redirect.txt` (one line, `C:\ROTKX\extracted`: the game files are read from there). |
| `rotkxbox/out/` | *(ignored)* build output. |
| `sdk_5558/` | ROTK's SDK changes as patches over the Two Towers SDK: **`sdk_rotk_vs_tt_v15.patch` is current** (v2–v14 are history). `manual.toml` holds the hand-proven XDK function addresses. |
| `notes/` | Investigation logs: `rotkx-01` … `rotkx-10` for this port (read in order), and `01`–`08` from the Two Towers project. |
| `launcher-handoff/` | The launcher interface documents: `LAUNCHER-SPEC.md`, `LAUNCHER-HANDOFF.md`, `TT-PORT-INTERFACE.md`, `ROTK-ENGINE-NOTES.md` (ROTK Fix's notes on ROTK PC). |
| `tools/` | Python analysis scripts for xbe files (info, scan, disassemble a function, callers, CFG, config merge), from the TT project. |
| `ref/` | The Two Towers port's config and game sources, for reference. |
| `sdk_4928/` | The Two Towers SDK patches against upstream XboxXBE-Recomp (the base ROTK's patch sits on). |
| `shared/harness-linux/` | Older copies of the Linux harness scripts, shared with TT. |
| `work/` | The Linux test harness's working folder: `rebuild.sh`, `compile_gen.sh`, `sweep.sh`, pad scripts `pad_*.txt`, `scripts/` (texture, font and surface tools), `hidden/` (string and table tools from notes 08), `tools/CMakeLists.txt` (Linux build of the recompiler). Runs and builds there are ignored. |

**The game code in `rotkxbox/src/game/`** (each file starts with a comment explaining it):

| File | Does |
|---|---|
| `rotk_settings.cpp` | Reads ROTK Fix's `ROTKFix.ini` (`--ini`) over settings.toml. |
| `rotk_framerate.cpp` | Unlocked frame rate (the game's time step 0x9EB20 path), the 0.1 s step clamp, and the move-and-slide tolerance hooks (`ROTK_Slide*`, notes 09). Also test-only `ROTK_DT`, `ROTK_DT_JITTER`, `ROTK_MEMDUMP`. |
| `rotk_widescreen.cpp` | 16:9/ultrawide viewports and Hor+ cameras at the source (0xA2B50, 0xA32F0), FOV, draw distance, HUD edges, TV-safe border off. |
| `rotk_movies.cpp` | Movie table (skip all videos), how each movie is fitted (films, logos). |
| `rotk_fonts.cpp`, `rotk_menu_art.cpp` | "Sharp text": the Xbox fonts and title/menu pictures redrawn at run time from **the player's own ROTK PC install** (nothing of ROTK PC is copied into the port). |
| `rotk_prompts.cpp`, `rotk_prompts_data.h` | PlayStation/Nintendo button pictures in the game's hints. |
| `rotk_controls.cpp`, `rotk_input.cpp` | Button remapping from ROTKFix.ini, second controller, reporting a standard gamepad. |
| `rotk_secrets.cpp` | The main-menu **Exit** item (always on), plus the test-build switches from notes 08. |
| `rotk_d3d.cpp`, `rotk_hooks.cpp`, `hooks.cpp` | Small D3D and hook glue. |
| `rotk_harness_level.cpp`, `rotk_harness_probe.cpp` | **Linux test build only** (`#ifndef _WIN32`): `ROTK_LEVEL=<name>` boots any level, and `ROTK_WATCH=<hex>` logs an object's position. |

## 3. Building

### Windows (the real exe; Windows only)

Tools (as on the user's PC; the CMake cache confirms clang, Ninja and Visual Studio 18 = 2026):

- **Visual Studio Community 2026** (2022 also works) with:
  - *Desktop development with C++*
  - *C++ Clang tools for Windows* (Clang **22.1.3** on the user's PC)
  - *C++ CMake tools for Windows* (CMake and Ninja come with it)

  Download from https://visualstudio.microsoft.com/downloads/. `build.bat` offers to install the two missing
  components through the VS installer.
- **Git for Windows** 2.56 (https://git-scm.com/download/win), for CMake's FetchContent (Zydis, toml++, SDL3, Dear
  ImGui are downloaded on the first configure).
- No environment variables are needed for the build. `build.bat` calls `VsDevCmd.bat` and puts VS's CMake, Ninja and
  LLVM on PATH itself. A standalone LLVM in `C:\Program Files\LLVM` is preferred if present.

**Paths that exist only on the user's PC:**

| Path | What it is |
|---|---|
| `C:\ROTKX\` | The working folder. |
| `C:\ROTKX\extracted\` | The game files from the user's own disc: `default.xbe` plus `Data\...`, about 2 GB. Never commit or share. |
| `C:\ROTKX\rotkxbox\assets\default.xbe` | A copy of the xbe for the build. |
| `C:\ROTKX\rotkxbox\assets\redirect.txt` | Says `C:\ROTKX\extracted`, so a dev run reads the game in place. |
| `C:\Games\ROTK-Test\` | The user's ROTK PC install with ROTK Fix and the launcher's test layout, which this port reads at run time. **Never edit it.** |
| `C:\TT\` | The Two Towers port. **Never edit it** (this chat only read from it). |

To set up from scratch: put the game files somewhere, copy `default.xbe` into `rotkxbox\assets\`, write that folder's
path into `rotkxbox\assets\redirect.txt` (or put the whole game in `assets\`), then:

```
cd C:\ROTKX\rotkxbox
build.bat
```

The steps:

1. Toolchain.
2. XDK symbols: only when `config\game.toml` has no `[symbols]` yet. It already has them, so this is skipped.
3. CMake/Ninja build: runs `xbe_recomp` on the xbe with `config\game.toml` into `generated\`, then compiles about 30
   generated partitions, the runtime and `src\`.

Output: `rotkxbox\out\build\release\ROTKXbox.exe` (also `.pdb`, `xbe_recomp.exe`, `xbe_symbols.exe`).

**Build times:**
- A first full build takes about 6 minutes on the user's Ryzen 7 3700X. `build.bat` says "30 to 90 minutes", which
  is the template's cautious text.
- A rebuild after changing one runtime or game file takes seconds. The largest single objects are about 10 s each.

**Things that have gone wrong before:**
- A missing Clang or CMake component: `build.bat` offers to install it.
- The SDK dir file `out\build\release\sdk_dir.txt` missing: `release.bat` re-runs `cmake --preset release`.
- The Two Towers project hit HLSL compile errors that only showed on Windows. Every D3D12/HLSL change here was
  syntax-checked with MinGW first (below), but shader compile errors still only show up when the game runs on the PC
  (`D3DCompile` at run time, with the error in the log).

### Linux (test harness only; can't make the Windows exe)

A headless Linux build of the same game exists for testing. It has no window, a null renderer plus a CPU
**reference renderer** that writes TGA frames, and pad scripts for input. Nothing on Linux can make the
D3D12/Windows exe; cross-compiling the exe has not been set up.

| Tool | Version used |
|---|---|
| clang++ | 18.1.3 (Ubuntu) |
| cmake | 3.28 |
| Python | 3.13 |
| MinGW-w64 g++ | 13 (syntax checks only) |

1. Build the Linux recompiler and symbol tool once: `cmake -S work/tools -B work/tools/b && cmake --build
   work/tools/b`. It fetches Zydis v4.1.1 and toml++ v3.4.0. **`SDK` is hard-coded to
   `/home/claude/rotkx/rotkxbox/sdk` in that CMakeLists:** change it for another location.
2. `bash work/rebuild.sh` does everything after that:
   - regenerates `config/game.toml` (`config/make_config.sh`)
   - runs `work/tools/b/xbe_recomp config/game.toml --xdk sdk/xdk` into `rotkxbox/generated/`
   - compiles the generated partitions (`work/compile_gen.sh`, cached in `work/hbuild/gen`)
   - links `work/hbuild/rotkx_headless` with `sdk/harness/build.sh`

   It needs `rotkxbox/assets/default.xbe`, a copy of the game's xbe.
   - A first run takes about 15 to 25 minutes on 2 cores, because of the generated code.
   - After that, about 5 s to 2 minutes.
   - The paths in `rebuild.sh` and `compile_gen.sh` are absolute (`/home/claude/rotkx/...`).
3. Windows syntax check, for any change to Windows-only code (d3d12.cpp, render_thread.cpp, main.cpp):

   ```
   x86_64-w64-mingw32-g++ -std=c++20 -fsyntax-only -DNOMINMAX -Isdk/runtime -Isdk -I<stub> -I<imgui> sdk/runtime/gpu/d3d12.cpp
   ```

   - `<stub>`: a folder with a few empty stand-in headers for MinGW.
   - `<imgui>`: Dear ImGui's headers. They came from the ROTK Fix launcher source's `third_party/imgui`.
   - Both were in the old container's temp folders and are **not** in the repo.

## 4. From game files to exe

**Input:** the US `default.xbe` (sha1 above) and the game's `Data\` tree from the user's own disc. The disc images
the launcher knows about are listed in the launcher's `ROTKFix\games.ini` (format `Game = rotk-xbox|<folder or
.iso>`, see `launcher-handoff/LAUNCHER-SPEC.md`). The user's extracted copy is `C:\ROTKX\extracted`.

**Pipeline:**

1. `xbe_symbols` matches XDK library functions in the xbe against `sdk/xdk/5558/signatures.toml` and writes the
   `[symbols]` table of `config/game.toml`. ROTK uses XDK 5558. 124 functions and 12 data items were signed from
   ROTK's config (notes rotkx-01).
2. `config/game.toml` adds hand-proven facts: function starts, jump tables, data-in-code, which XDK functions the
   runtime replaces (HLE), and per-function options. `sdk_5558/manual.toml` lists the hand-found XDK addresses.
   `config/hooks.toml` lists the instruction-level hooks.
3. `xbe_recomp` translates every function to C++ (`generated/game/x86_recomp.N.cpp`, `x86_func_mapping.cpp`,
   `image_symbols.h`). About 8,564 functions and 381,793 instructions.
4. CMake compiles that with the runtime (`sdk/runtime`) and the hand-written `src/`.
   - A hand-written `X86_FUNC(sub_XXXXXXXX)` in `src/game` overrides the recompiled function, which stays callable
     as `__imp__sub_XXXXXXXX`.
   - `[[hook]]` entries call a `bool f(X86Context&, uint8_t*)` in the middle of a function and jump to `on_true`
     when it returns true.

**Generated vs hand-written:**
- **Generated:** everything in `rotkxbox/generated/` and the `[symbols]` part of game.toml.
- **Hand-written:**
  - the rest of game.toml, hooks.toml
  - `src/`
  - the SDK changes (in `rotkxbox/sdk`, also exported as `sdk_5558/sdk_rotk_vs_tt_v15.patch`)
- `rotk_prompts_data.h` is generated by `tools/mkprompts.py` from original drawings, and committed.

**At run time** the exe finds the game where `--game` points (a folder with `default.xbe`, or an `.iso`). Without
`--game` it looks in `assets\` beside the exe, which can redirect elsewhere with `assets\redirect.txt`. It checks the
xbe's sha1:
- another release of ROTK gets a "start anyway?" prompt (exit code 3 if refused)
- another game, or nothing found, gives exit code 2

## 5. Testing

### On Windows (the user)

- **Run:** `out\build\release\ROTKXbox.exe`, or through the launcher, the usual way (section 6).
- **What "working" looks like:**
  - the game starts and the logos play (LOGOS.vid once a save exists; on a first boot the logos are the start of
    HELMFMV1)
  - the main menu shows "Exit" under Credits
  - levels play at the chosen frame rate without slowdowns
  - the log ends with `Exit (0)`
- **Log:**
  - Without `--log`: `logs\rotkx_NNN.log` beside the exe.
  - With the launcher: the `--log` file. In the user's test layout that's
    `C:\Games\ROTK-Test\ROTKFix\ROTKXbox\rotkx.log`.
  - The log starts with the settings, the PC and GPU, the game's sha1, and lines prefixed `rotk:` from the game code.
- **Crash dumps:** a minidump in `CrashDumps\` beside the exe, or beside the log when `--log` is given.
- **Frame captures:**
  - Turn on: set `XBE_CAPTURE_DIR` (`capture.bat`, or `setx XBE_CAPTURE_DIR <folder>`). Turn off afterwards with
    `setx XBE_CAPTURE_DIR ""`.
  - Then **F3** writes `captures\HHMMSS\` next to the log:
    - `frame.txt`: one line per draw with render target, primitive, count, shaders, blend, z, cull, colour mask, c0,
      textures; constants and vertices only for small draws
    - BMPs at render-target switches
    - `final.bmp`
    - `tex_*.bmp`
    - the translated `vs_*/ps_*.hlsl`
  - Without the variable, F3 only writes a thread dump.
- **Other keys:** `\` saves a PNG screenshot. The settings overlay is SDL/ImGui (see the SDK README).

### On Linux (the harness)

```
cd rotkxbox
ROTK_LEVEL=Ise01 REF=300 bash sdk/harness/run.sh -x ../work/hbuild/rotkx_headless -g ../gamedata -o ../work/runs/X -t 300 -s ../work/pad_ise.txt
```

- The folder `../work/runs/X/1/` must contain `settings.toml` (with `fps = "60"`, etc.) and `hdd/` (saves) **before
  the run**. Without them the 60 fps setting and video skipping are off, and the run sits on the level's opening film.
- **Output:**
  - `runs/X/1/run.out` (stdout/stderr)
  - `runs/X/1/logs/rotkx_001.log`
  - `runs/X/1/cap/ref_NNNNN.tga` (reference-renderer frames)
- **Environment switches:**

  | Variable | Does |
  |---|---|
  | `REF=n` | Draw every n-th frame. |
  | `REF_FROM=n` | Draw nothing before frame n. |
  | `CAP=n` | Draw lists. |
  | `ROTK_LEVEL=<Hel01, Pat01, Ise01, …>` | Boot that level in Helm's Deep's place. |
  | `ROTK_DT=<rate>` | Fixed time step. |
  | `ROTK_DT_JITTER=1` | Alternating steps. |
  | `ROTK_MEMDUMP=dir,step,...` | Guest memory dumps. |
  | `ROTK_WATCH=<hex>` | Position log. In Pat01 the player is 13D08A0. |
  | `ROTK_INI=<file>` | Read a ROTKFix.ini. |
  | `ROTK_UNLOCK`, `ROTK_DEVBUTTONS`, `ROTK_CODES`, `ROTK_DAMAGE` | Notes 08. |
  | `XBE_PERF=1` | Performance log. |
- **Pad script lines:** `frame BUTTON duration`. `LS` is left stick up.
- **Useful scripts:**
  - `work/pad_ise.txt` walks and fights in Road to Isengard.
  - `work/pad_walk.txt` walks forward in Pat01.
  - `work/sweep.sh` loads every level.
- **Limits:**
  - The harness has no D3D12, so renderer-only bugs (like the constant-alpha one) never show there.
  - It reads only extracted folders, not `.iso`.
  - It runs slower than real time.
  - `pkill -f rotkx_headless` can kill your own shell: use a narrower `pgrep` pattern.

## 6. What Athelas (the launcher) relies on

The launcher looks for the port in a **`ROTKXbox` folder beside itself** and starts it roughly like this. The
exact argument list is the launcher's (`launcher-handoff/LAUNCHER-SPEC.md` section 3, notes rotkx-02/03); the port
accepts all of these:

```
"<exe>" --game "<folder or .iso from games.ini>" --config "<D>\settings.toml" --textures "<D>\textures"
        --log "<D>\rotkx.log" --saves "<D>\saves" --shader-cache "<D>\shadercache"
        --screenshots "<launcher>\Screenshots" --no-dialogs --ini "<ROTK PC folder>\ROTKFix.ini"
```

Before showing its settings page the launcher also runs `--version` and `--describe-settings` (stdout through a
pipe, 15 s timeout, working directory = the exe's folder).

`<D>` is `<launcher>\ROTKFix\ROTKXbox`; in the user's test layout `C:\Games\ROTK-Test\ROTKFix\ROTKXbox\`. The spec's
text is written for The Two Towers (`TwoTowers`, `tt.log`); ROTK follows the same pattern plus `--ini`.

- **Exe name:** `ROTKXbox.exe`. Version: `ROTKXbox.exe --version` prints `rotkx-port 1.0.0`.
- **Arguments:** all defined in `sdk/runtime/app/app.h` (`CommandLine`). With all of them given, nothing is written
  beside the exe.
  - `--describe-settings` prints the settings list as JSON.
  - `--instance N` is for system-link tests.
- **Settings:**
  - `settings.toml` (TOML, sections `[display] [graphics] [textures] [input] [network]`). Read and written by the port;
    a missing file is created with defaults.
  - **ROTKFix.ini** (`--ini`, INI) is read after it and wins. It's never written. The key mapping is in
    `notes/rotkx-02-rotkfix-settings.md`, `rotkx-03`, `rotkx-05`.
  - ROTK PC's `Data\` folder beside ROTKFix.ini is read for "sharp text" (fonts and menu pictures), when that option
    is on.
- **Exit codes** (`app.h`):

  | Code | Meaning |
  |---|---|
  | 0 | OK (also when Exit is chosen on the main menu) |
  | 2 | Game files not found, or another game |
  | 3 | Another release, refused |
  | 4 | No Direct3D 12 |
  | 5 | Unreadable settings file |
  | other | A crash; `0xC0000135` would be a missing DLL, but the C++ runtime is linked in statically (`/MT`) |
- **Game files:** `--game` takes a folder or an `.iso`; with a folder, any `.iso` inside it is also tried. The SDK
  reads the Xbox disc format (`sdk/runtime/kernel/disc.cpp`). The launcher's own detection (title ID from the xbe,
  XDVDFS for .iso) is described in `launcher-handoff/LAUNCHER-SPEC.md`.
- **Files written:**
  - the log
  - `CrashDumps\` and F3 captures next to the log
  - saves in `--saves`
  - shader cache
  - screenshots
  - texture dumps (`textures\dump`); packs are read from `textures\replace`

## 7. Release

- `rotkxbox\release.bat`:
  1. builds (with `CI=1`, so no questions)
  2. empties `rotkxbox\release\ROTKXbox\`
  3. copies `ROTKXbox.exe`, `LICENSE.txt` (the SDK's MIT licence, © 2026 TGP482) and `THIRD_PARTY.txt` (the
     licences of toml++, Dear ImGui, SDL, stb, Zydis… built into the exe)
  4. writes `ROTKXbox.exe.sha256` (`<hash>  ROTKXbox.exe`)
  5. zips the folder to `release\ROTKXbox.zip` when 7-Zip is installed
- **Don't ship:** `.pdb`, `xbe_recomp.exe`, `xbe_symbols.exe`, anything else from `out\`, or any game or ROTK PC file.
- **1.0.0** was made by the user running `release.bat` on 2026-10-14 (exe built at 21:08 local). Folder:
  `C:\ROTKX\rotkxbox\release\ROTKXbox\`. SHA-256:

  | File | SHA-256 |
  |---|---|
  | ROTKXbox.exe (12,417,536 bytes) | `59c4cef26f377d9c95a2183a16e69cf6043ddbd9c2594ed77a3400df3e08dff7` |
  | LICENSE.txt | `b61d0b216ea6b3a6be922ef85f6348568069fb3c5fc816d021ee66ec43de8157` |
  | THIRD_PARTY.txt | `f1efb10027dc197548de31a233da93adb46e14d5903a4a4bc84c12c1546027dc` |
  | ROTKXbox.exe.sha256 | `50d7c59e729a3db4aee3be2b74a7ab8d456fc4c3cbc9d717c618aa6cbabd8e44` |

  It corresponds to commit `cdbf647` (old `063d866`).
- **For the combined Athelas release:**
  - The user copies from that folder. Don't write into the launcher's folders.
  - Readme lines proposed:
    - *New:* The Return of the King (Xbox) running natively on PC, up to 240 fps, widescreen, sharp text and menus
      from ROTK PC, fixes for slowdowns and vanishing enemies at high frame rates. Needs your own Xbox disc dump (the
      extracted folder or an .iso).
    - *Known issues:* .iso loading not yet tested with a real ROTK image; the picture is darker than ROTK PC (Xbox
      brightness curve), Brightness 1.2–1.3 matches it.
  - Credit proposed: "Xbox port: built with Claude (Anthropic). Recompiler runtime: XboxXBE-Recomp (MIT, © 2026
    TGP482)."

## 8. Open bugs and known issues

1. **`.iso` loading untested for ROTK.**
   - The code path (`game_files.cpp` FindGame → `kernel::Disc` → `MountDisc`) is the SDK's and is shared with TT.
   - It has never run with a real ROTK disc image. The harness only takes folders: `sdk/harness/main_test.cpp`
     reads `<folder>/default.xbe`.
   - Half-done and reverted: an `.iso` branch in `main_test.cpp` that mounts the disc as the Windows host does.
   - Testing tools used for it:
     - an xiso can be packed from the game files with `xdvdfs pack <folder> out.iso` (`cargo install xdvdfs-cli`)
     - the user's real images are listed in the launcher's games.ini
2. **Picture darker than ROTK PC: faithful, kept.**
   - The game sets a gamma ramp at start (0xBF0E0 → 0xBEFE0 → SetGammaRamp, out = 1.1·in − 0.05), and the port
     applies it as the console did. ROTK PC doesn't end up with it.
   - Measured: Path of the Wizard median 26 on PC against 5–15 here, which the ramp explains (26 → about 16).
   - The user **declined** an option to skip the ramp. Brightness 1.2–1.3 is the advice.
3. **Fading characters: fixed in 1.0.0**, the user confirmed it "looks just like the PC version".
   - D3D12 drew blend 0x8003/0x8004 (constant alpha) with the whole blend colour; the game fades with black ones, so
     faded orcs and dead bodies were invisible.
   - The fade speed is time-based (checked at 30/60/120 fps).
   - Details: notes 10.
4. **Gandalf grey or ghostly in shade, very dark orcs:** believed faithful (the reference renderer shows the same).
   Not compared side by side with ROTK PC in the same spot. Next step if questioned: an F3 capture that also logs the
   blend colour and constants c96 and c146–c155.
5. **Windows checks still open** (notes 02/03):
   - long play at 60+ fps (animations, timers, Gandalf's moves)
   - leaf edges with MSAA and AlphaEdgeSmoothing
   - long draw distance
   - swapped buttons in play
   - The user has played at 60–180 fps with the slide fix and found nothing else so far.
6. **Light/dark flicker at high frame rates:** mostly gone after the RenderScale downsample fix (notes 09) and the
   slide fix. Not reproducible in the harness.
7. **HUD at the screen edges (`hud_edges`).** Notes 07 first found it barely moved the real HUD, which is drawn
   through Begin/End. The later commit 3cc585a groups the pieces from the previous frame and makes it work. Screens
   with unusual Begin/End text or bars haven't all been checked.
8. **Ultrawide** renders a 4:3 frame stretched across, so it's softer. RenderScale above 100 compensates (notes 07).
9. **Menu art:** ROTK PC's larger menu pictures for other screens are not matched yet (notes 06 "Not done yet").
   Sharpening 80 leaves light halos round scaled 2D.
10. **The Exit label** is done by scanning guest memory for the unused "mainMenuOnline" string on every main-menu
    open (6–11 ms). Harmless, but a direct pointer to GlobScen's string table would be cleaner.
11. **Movies can't be skipped by pad in the harness** (also seen in TT). On Windows `skip_all_videos` works.
12. **System link / online:** Online is dead; system link is implemented in the SDK (xnet) but not tested for ROTK.
13. **Harmless log warnings:** `wbinvd`, and the dsound exports noted in notes 04.

## 9. Rules and decisions from the user

- **Faithful game first; every enhancement is opt-in and off by default.** The exceptions the user asked for:
  - Exit on the main menu is always on (no launcher switch)
  - the opening logos and films fit 16:9
- **Local only:** nothing on GitHub or any remote until the user says so. Don't push.
- **Neutral branding:** the window title is "ROTK Xbox (PC port)". No game logos or names as branding.
- **Never edit `C:\TT` or `C:\Games\ROTK-Test`**: read and stage only. (That was this chat's rule; the new main
  developer should confirm with the user what applies now.)
- **The ROTK SDK changes stay a separate patch** (`sdk_5558/sdk_rotk_vs_tt_vN.patch`, currently v15) over the Two
  Towers SDK. A new version is made by reverse-applying the previous patch to a clean SDK copy to get the TT
  baseline, then `diff -ruN` against the current SDK. Watch CRLF in `templates/game/*.bat`, which shows up as a
  false difference.
- **Nothing of ROTK PC is copied into the port.** Fonts, menu pictures and the like are read from the player's own
  ROTK PC install at run time.
- **No game data or generated code in any repository.**
- **Commits end with:**

  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01BevbsraeDrsYLqjab7eoW7
  ```

  This session's attribution; a new session will have its own.
- **Said no to:**
  - a 60 fps cap (ROTK PC plays fine at 180, so the port must too)
  - a "PC brightness curve" option
  - a launcher button for Exit
  - the PlayTrailer menu item
- **Frame rate choices:** 30, 60, 90, 120, 144, 165, 180, 240 (the same as the PC game offers).
- **Delivery to the PC (how this chat worked):**
  1. Stage the PC file and compare it with git HEAD.
  2. Write the new file through the device bridge with an mtime guard.
  3. Re-stage it and byte-compare.
  4. Then commit.

  Always use a fresh output folder, since a re-send from the same path once delivered a stale copy.

## 10. Ideas and next steps

1. Test `.iso` loading with the user's real ROTK disc image, via the launcher on Windows. Optionally finish the
   harness `.iso` branch (section 8.1).
2. A Windows play pass over the open checks in section 8.5, at 30 and at 180 fps.
3. Compare the shaded-Gandalf and dark-orc look with ROTK PC at the same spot (8.4).
4. More of ROTK PC's larger menu art for sharp menus (8.9).
5. A player README for the release, if Athelas doesn't cover it: setup, requirements (Windows 10/11, D3D12),
   controls, known issues.
6. Keep `sdk_5558` in step if the Two Towers SDK moves on. The SDK's own merge helper is
   `rotkxbox/sdk/tools/sdk_sync/sdk_sync.py`.

## 11. Other notes and logs

| Where | What |
|---|---|
| `notes/rotkx-01` … `rotkx-10` (repo, and on the PC in `C:\ROTKX\notes`) | The full investigation log of this port, in order. Each fix has its addresses, what was tried and the test results. |
| `notes/01`–`08` | The Two Towers port's notes, from which the method came. |
| `launcher-handoff/*.md` | The launcher interface. `ROTK-ENGINE-NOTES.md` is ROTK Fix's knowledge of ROTK PC (pacer 0x4828E0, slide 0x440CE0 and more). |
| `C:\TT\notes\` (PC) | The Two Towers project's own notes (read only from this chat). |
| `C:\Games\ROTK-Test\ROTKFix\ROTKXbox\` (PC) | The user's test run folder: `settings.toml`, `rotkx.log`, `captures\` (F3 captures from 2026-10-14, the character-fade investigation), `saves`. |

**Container-only files, gone when that session ends:**
- the pre-rewrite git backup
- `work/runs/` (harness runs)
- scratch scripts: trajectory comparison, fade-step analysis, MinGW stub headers
- the ROTK Fix launcher source copy (`/tmp/claude-0/rotkfix/rotkfix-source`)
- the session transcript

Nothing in them is needed: every result is written up in `notes/`.

No memory files are kept.

## 12. Quick facts

| | |
|---|---|
| Title ID | 4541003E (US) |
| XDK | 5558 |
| Recompiled | 8,564 functions |
| Main-menu setup | 0x102D50 |
| Menu input | 0xFE010 |
| Time step getter | 0xF81F0 (PC 0x4E5350) |
| Move and slide | 0x5EBD0 (PC 0x440CE0) |
| Level table | 0x16CF00 (18 entries) |
| Movie table | 0x179F30 |
| Gamma | 0xBF0E0 |
| Viewports | 0xA2B50 |
| Camera | 0xA32F0 |

Levels in `Data\Game\`: bla01, cir00/01, cra01, fro03, hel01, ise01, min00/02/04, osg01, pal01/02, pat01/02/03,
pel02, she01, plus GlobScen and LoadOnce.

## 13. Old to new commit hashes (rewrite of 2026-10-14)

| Old | New | Subject |
|---|---|---|
| e7f910c | d7decf0 | Baseline: XboxXBE-Recomp 65e959d4 + TT SDK patch |
| 005823e | 0ec9c61 | Boots to Helm's Deep in the harness |
| f90a358 | 0b2b058 | Vertex program memory (skinning) |
| 9e67a02 | 7f167ec | EndPush vertex formats |
| 7c4851c | d211e44 | XDK 5558 signatures |
| 847aa60 | 7dfb749 | Rebase onto TT SDK patch v2 |
| 08b1121 | dbe2173 | skip_all_videos |
| 17a4666 | e588e25 | ROTKFix.ini bridge, fps unlock, widescreen, FOV |
| 0ccb231 | f71b0ed | Draw distance, alpha to coverage, remap, release.bat |
| ad9f23e | 00b8019 | Level sweep, step clamp, button pictures |
| adfa781 | 3fd0e06 | First launcher play fixes |
| 939a252 | 279fd83 | Movies at twice console size |
| 22621c2 | d8b84e9 | Movie fitting |
| ce86dd0 | c64dc48 | Sharp text |
| 00ff113 | f0f7055 | Sharp menus |
| a9dadf8 | eaa72ff | Ultrawide |
| d3af486 | 8295a84 | Ultrawide menu effects |
| 13723ae | 3f97a89 | Ultrawide side bars |
| 4be6737 | 3cc585a | HUD at the screen edges |
| af84698 | 5296c93 | Hidden settings survey |
| 42b1000 | b43e223 | Main menu Exit |
| cf87535 | 6d9c047 | Logos 16:9, Exit relabel, shimmer fix (v14) |
| a10c911 | a42a6be | Notes 09 |
| ab34b33 | 46c5187 | 60 fps cap (later removed) |
| aac7de9 | 322b3fa | Move-and-slide fix, cap removed |
| b8dd454 | ddf5001 | Constant-alpha blending fix (v15) |
| 59be1ca | 7f8342b | Notes 10 |
| 063d866 | cdbf647 | Release 1.0.0 |
| — | df8db2f | Repo hygiene for the handoff |
