# The Two Towers PC port: handoff

Written 2026-10-14 by the Two Towers chat for the developer taking over all three projects (ROTK Fix / Athelas, this port, the ROTK Xbox port). It covers **this port**. It mentions the other two only where they touch it.

Current state: **version 1.0.0 is built and released into the Athelas bundle** (see "Release" below). Nothing has been uploaded anywhere.

---

## 1. What the project is

A native Windows port of the original Xbox game *The Lord of the Rings: The Two Towers* (US release, title ID `45410010`, XDK 4928). The Xbox program (`default.xbe`) is **statically recompiled**: every x86 function in it is translated to C++ by a recompiler, and that C++ is compiled for Windows. A host runtime stands in for the Xbox: kernel, memory, files, threads, Direct3D 8 on top of Direct3D 12, DirectSound on XAudio2, and controllers through SDL.

- The player supplies their own game: an extracted folder or a disc image (`.iso`). No game data or recompiled code is in this repository.
- The recompiler and runtime are **XboxXBE-Recomp** (https://github.com/TGP482/XboxXBE-Recomp, MIT), vendored in `game/sdk/` with our changes. Upstream base: commit `65e959d4` (2026-10-11).
- By default it plays as the Xbox did (30 fps, 4:3). Every enhancement is a setting.
- Players start it through the **Athelas / ROTK Fix launcher**, which writes its settings file and passes the paths on the command line.

### Layout

The repository root is `C:\TT\project` on the user's PC.

| Path | What it is |
|---|---|
| `HANDOFF.md` | This file |
| `README.md` | The original working-folder notes (analysis tools) |
| `game/` | **The port.** Build here. |
| `game/CMakeLists.txt` | The game's build: version (`XBE_VERSION`), product name, the exe target `TwoTowers` |
| `game/CMakePresets.json` | `release` / `debug` / `relwithdebinfo` presets: Ninja, clang, `-march=x86-64-v2`, Windows only |
| `game/build.bat` | Finds Visual Studio's clang, CMake and Ninja (offers to install them), writes symbols if needed, builds |
| `game/release.bat` | Makes the release folder, the zip, and keeps the symbols (section 6) |
| `game/capture.bat`, `capture_threaded.bat`, `capture_level.bat` | Start the game with F3 frame captures on, or in one level (testing) |
| `game/README.md` | Player readme, shipped in the zip |
| `game/config/game.toml` | The recompiler's input: the xbe path, 32 partitions, symbols (function names and addresses), hooks. Made by `config/make_config.sh` |
| `game/config/hooks.toml` | Hand-added hook entries appended to `game.toml` |
| `game/src/common/game_info.cpp` | **`GameInfo`**: title, version, settings list, presets, button names, title ID |
| `game/src/game/*.cpp` | **Game-specific hooks** (section 2.3) |
| `game/sdk/` | XboxXBE-Recomp plus our changes: `tools/` (recompiler, symbol finder), `runtime/` (the host), `harness/` (Linux headless runner), `cmake/XbeRecomp.cmake`, `xdk/<version>/layout.toml` |
| `game/assets/` | **Not committed.** `default.xbe` (a copy, for the build) and `redirect.txt` (the folder with the game files) |
| `game/generated/` | **Not committed.** The recompiled C++, written by the build |
| `game/out/build/release/` | **Not committed.** The build output: `TwoTowers.exe`, `.pdb`, the `TwoTowers/` release folder |
| `game/symbols/` | **Not committed.** Each release's exe and `.pdb`, by the exe's SHA-256 |
| `sdk_4928/sdk_vs_upstream_65e959d4.patch` | **The complete set of SDK changes against upstream.** Regenerate it after every SDK change (section 2.4) |
| `sdk_4928/*.patch` (others) | Older partial patches by topic, kept as history. Don't apply them on top of the complete one |
| `sdk_4928/manual.toml` | Hand-proven XDK 4928 function addresses for this game |
| `sdk_4928/xdk/4928/layout.toml` | XDK 4928 object layout (the same file is in `game/sdk/xdk/4928/`) |
| `launcher-handoff/TT-PORT-INTERFACE.md` | **The contract with the launcher**: arguments, settings keys, exit codes, folders, texture packs, release folder. Keep it current |
| `launcher-handoff/` (other `.md`) | Earlier requests and specs exchanged with the launcher chat |
| `notes/01`–`08` | Investigation and status notes (section 10) |
| `tools/` | Python analysis scripts (`capstone`), `merge_config.py`, `tt_level_test.bat` |
| `shared/analysis/` | `callers.py`, `targets.py`: who calls an address |
| `shared/harness-linux/` | Linux harness build and test scripts, pad scripts, the level sweep (section 5.2) |

---

## 2. From game files to exe

### 2.1 Inputs, and where they are on the user's PC

| What | Where |
|---|---|
| The game, extracted (used for play and by the launcher) | `C:\TT\extracted` (`Default.xbe`, `FMV\`, `SOUND\`, `UI\`, `Worlds\`) |
| `default.xbe` copy for the build | `C:\TT\project\game\assets\default.xbe` (SHA-1 `3073f7d710dda0274f2f4b7d9ace4fd91bf544f3`) |
| Redirect to the game files | `C:\TT\project\game\assets\redirect.txt`, one line: `C:\TT\extracted` |
| Analysis output (from the xbe) | `C:\TT\analysis\` (`symbols_raw.txt`, `game_try5849.toml`, `cfg.json` and others). Never commit |
| A disc image | None on the PC that I know of. The `.iso` path was tested only in the Linux harness |

The other two projects: the launcher install is `C:\Games\ROTK-Test` (`ROTKFixLauncher.exe`, `ROTKFix.ini`, `ROTKFix\ports.ini`, `ROTKFix\games.ini`). ROTK Xbox is `C:\ROTKX\rotkxbox` (its own copy of the SDK) with its game in `C:\ROTKX\extracted`.

### 2.2 The pipeline

1. **Symbols** (once per game; already done, the result is in `game/config/game.toml`):
   - `xbe_symbols` (SDK tool) finds XDK library functions by signature.
   - XbSymbolDatabase (`XbSymbolDatabaseCLI`) output and the hand-proven `sdk_4928/manual.toml` are merged in by `tools/merge_config.py`, run by `game/config/make_config.sh`. It reads `C:\TT\analysis\game_try5849.toml` and `symbols_raw.txt`, then appends `config/hooks.toml`.
   - `build.bat` skips this step when `game.toml` already has a `[symbols]` section, which it does.
2. **Recompile:** `xbe_recomp config/game.toml --xdk sdk/xdk` writes `game/generated/game/`: 8,701 functions in 32 partitions (`x86_recomp.N.cpp`), `x86_func_mapping.cpp`, `image_symbols.h`. About a million lines. The CMake target `recompile_TwoTowers` runs it whenever the xbe, `game.toml` or `xdk/*.toml` change.
3. **Compile and link:** `TwoTowers.exe` = the runtime + `src/` hooks + the generated code + Dear ImGui + SDL3 (static) + the manifest and version resource.
   - The SHA-1 of `assets/default.xbe` is compiled in (`TwoTowers_xbe.cpp`). At start-up the player's xbe is compared with it. A different release gives exit code 3.
   - At run time the player's own `default.xbe` is loaded from their game folder. The exe holds no game data, but the recompiled code is the game's program translated.

**Generated vs written by hand:**

- Generated: everything in `game/generated/`, `out/`, `TwoTowers_xbe.cpp` and `TwoTowers_manifest.rc` (in `out/`).
- Written by hand: `src/`, `config/hooks.toml`, `sdk_4928/manual.toml`, and all SDK changes.
- `config/game.toml` is made by the script, then committed.

### 2.3 How game code is changed (hooks)

A recompiled function `sub_XXXXXXXX` is a weak symbol. To replace or wrap it:

```cpp
X86_EXTERN_FUNC(__imp__sub_000E4870);   // the original, still callable
X86_FUNC(sub_000E4870) {                // our version; lld picks it over the weak one
    uint32_t name = ::kernel::R32(ctx.esp + 4);   // guest stack arguments
    __imp__sub_000E4870(ctx, base);               // call the original
}
```

- Guest memory: `kernel::R32` / `W32` / `HostPtr`.
- Calling guest code from the host: `CallGuest`.
- Renderer extension points are in `sdk/runtime/gpu/ext.h`.

**`game/src/game/` files:**

- `tt_hooks.cpp`, `hooks.cpp`: hook registration.
- `tt_framerate.cpp`: fps (the game's own "Cap to 30 fps" developer setting, frame pacing).
- `tt_widescreen.cpp`: the camera aspect, HUD squeeze and edges, the menu panel zoom (the menu art sits at 24–616 × 18–462 and is scaled by 640/592).
- `tt_movies.cpp`: per-movie Fit, skipping.
- `tt_renderer.cpp`: keeps the water simulation's targets at console size.
- `tt_d3d.cpp`, `tt_input.cpp`: device and pad fixes.
- `tt_threads.cpp`: a loading race fixed with the game's own lock.
- `tt_extras.cpp`: F1 hides the HUD, FOV, the per-frame tick.
- `tt_devsettings.cpp`: the game's named developer settings, hooked at 0xE4870 and 0xE4E60. The damage settings use them.
- `tt_harness_level.cpp`: the `TT_LEVEL` test switch.

**Rule:** game-specific code goes only in `src/game`; the SDK stays game-neutral, so ROTK Xbox gets SDK work for free.

### 2.4 SDK changes

`game/sdk` is upstream plus our changes. After **any** SDK change, regenerate the complete patch and copy it to `sdk_4928/`:

```
# with an upstream checkout at commit 65e959d4 in XboxXBE-Recomp/
diff -ruN --exclude=.git --exclude=stb XboxXBE-Recomp game/sdk > sdk_4928/sdk_vs_upstream_65e959d4.patch
```

The ROTK Xbox port has its **own, older SDK copy** in `C:\ROTKX\rotkxbox\sdk`. It doesn't have this session's changes: the per-axis widescreen render, smooth 2D, the manifest, the version resource, the ImGui pin and others. Bring them across with the patch when needed.

---

## 3. Building from scratch (Windows)

The game build needs **Windows**. It uses Direct3D 12, XAudio2, Win32 and the Windows SDK's `rc.exe`. Only the recompiler tools and the headless test harness build on Linux (section 5.2). Cross-compiling the exe from Linux was never set up; MinGW was used only for `-fsyntax-only` checks.

### 3.1 Tools (the versions on the user's PC, which built 1.0.0)

| Tool | Version | Where from |
|---|---|---|
| Visual Studio Community 2026 (18) | with **Desktop development with C++**, **C++ Clang tools for Windows**, **C++ CMake tools for Windows** | https://visualstudio.microsoft.com/ (2022 should also work; not tested) |
| clang / lld-link | 22.1.3 (bundled: `VC\Tools\Llvm\x64\bin`) | the Clang tools component |
| CMake | 4.3.1 (bundled with VS) | the CMake tools component; 3.25 minimum |
| Ninja | bundled with VS | the CMake tools component |
| Windows SDK | 10.0.26100.0 (`rc.exe`) | installed with the C++ workload |
| Git for Windows | 2.56 | https://git-scm.com/download/win (CMake FetchContent needs it) |
| Python 3 + `capstone` | only for `tools/` and `shared/analysis/` | https://www.python.org/, `pip install capstone` |

`build.bat` finds Visual Studio with `vswhere` and sets `PATH` itself. If the Clang or CMake components are missing it offers to install them (Y/N). No environment variables are needed for a normal build.

**Libraries, downloaded by CMake FetchContent on the first configure** (pinned in `game/sdk/cmake/XbeRecomp.cmake`; needs internet):

| Library | Version | Note |
|---|---|---|
| Zydis | v4.1.1 | recompiler tools only |
| toml++ | v3.4.0 | |
| Dear ImGui | **v1.91.0** | **Don't raise this** without the change in section 8.1: 1.92.x crashed every launch |
| SDL3 | release-3.4.18 | static |
| stb_image 2.30 / stb_image_write 1.16 | vendored in `sdk/runtime/gpu/stb/` | |

### 3.2 Steps

1. Put the game's `default.xbe` in `game\assets\`.
   - For play from another folder, add `game\assets\redirect.txt` holding that folder's path (the user's says `C:\TT\extracted`).
   - Alternatively, put the whole game or an `.iso` in `assets\`.
2. Run `game\build.bat`. It:
   - configures (`cmake --preset release`, output in `out\build\release`);
   - writes symbols only if `config\game.toml` has none;
   - runs `cmake --build out\build\release`, which builds `xbe_recomp`, recompiles, compiles and links.
3. The result is `game\out\build\release\TwoTowers.exe`. The bat offers to start it.

The command-line equivalent, from a "x64 Native Tools" prompt with VS's Llvm `bin` on `PATH`:

```
cd C:\TT\project\game
cmake --preset release
cmake --build out\build\release
```

**How long:** the first full build took about **6 minutes** on the user's Ryzen 7 3700X; `build.bat` says 30 to 90 minutes for slower machines. A change to `src/` or the runtime takes a minute or two. A change to `game.toml` or the recompiler regenerates and recompiles everything.

### 3.3 Things that have gone wrong in builds

- **"duplicate resource" at link** (fixed): lld-link made its own manifest beside ours. `XbeRecomp.cmake` now swaps clang's built-in `/MANIFEST:EMBED` for `/MANIFEST:NO`. Keep that if you touch the link rule.
- **Every launch crashed at the first frame** (fixed by pinning ImGui 1.91.0): see section 8.1.
  - **When you change a FetchContent tag**, delete `out\build\release\_deps\<name>-src`, `-build` and `-subbuild`, or the old copy may be used.
  - A tag change takes effect only on a reconfigure. That is why the ImGui update shipped unnoticed into one build.
- **`.bat` files must keep CRLF line endings.** `release.bat` once lost them and cmd misread it. `.gitattributes` now forces CRLF for `*.bat`.
- `build.bat` stops on `choice` prompts. In CI set `CI=1`.
- Reading crash dumps needs the **exact** `.pdb` of that exe (kept in `game\symbols\<sha256>\`).

---

## 4. What Athelas (the launcher) relies on

The full, current contract is **`launcher-handoff/TT-PORT-INTERFACE.md`**. In short:

- **The exe:** `TwoTowers.exe`, in a `TwoTowers\` folder. The launcher's `ROTKFix\ports.ini` points at it; on the user's PC it points at the build folder: `TwoTowers=C:\TT\project\game\out\build\release\TwoTowers.exe`.
- **Command line:**
  ```
  TwoTowers.exe --game <folder or .iso> --config <settings.toml> [--textures <dir>] [--log <file>]
                [--saves <dir>] [--shader-cache <dir>] [--screenshots <dir>] [--no-dialogs]
  TwoTowers.exe --version              -> "tt-port 1.0.0"
  TwoTowers.exe --describe-settings    -> JSON: every setting, its type, range, choices, labels, help, presets
  ```
  - `--game` may be an extracted folder (holding `default.xbe`) or an `.iso`. Any `.iso` inside the folder is also tried.
  - The game files are only read.
  - `--no-dialogs` replaces message boxes with an exit code.
- **The settings file:** `settings.toml`. TOML sections `[display] [graphics] [gameplay] [textures] [input] [controls] [network]`. Choices are quoted strings.
  - Unknown keys are kept.
  - The port writes the file when it quits, so the launcher must edit it only while the game is closed.
  - The launcher's copy is `C:\Games\ROTK-Test\ROTKFix\TwoTowers\settings.toml`.
- **The log:** `--log ...\tt.log` (launcher: `ROTKFix\TwoTowers\tt.log`).
  - **Crash dumps** go to `CrashDumps\` **beside the log**: `ROTKFix\TwoTowers\CrashDumps\Game.<date>_<time>.dmp`.
  - Without `--log`, `logs\` and `CrashDumps\` sit beside the exe.
  - The log's last line is `Exit (N): reason`.
- **Exit codes:**

  | Code | Meaning |
  |---|---|
  | 0 | ok |
  | 1 | boot failed |
  | 2 | no game, or another game |
  | 3 | another release of this game |
  | 4 | no Direct3D 12 |
  | 5 | bad settings file |
  | 6 | GPU lost |
  | other | crash |

- **Other folders** (defaults beside the exe unless given): `hdd\` (saves), `shadercache\`, `textures\replace|dump`, `Screenshots\`, `captures\` (F3).
- **Launcher background art:** the launcher reads text-free pictures straight from the player's `UI\*.BFF` files at fixed offsets: `stills1`–`stills4` (except `stills2` #3), `mainmenu` pictures 0 and 1, and `options`. That's the game's own data, so nothing to keep in sync on our side.
  - **Don't add** an `--art` option; the user declined it.
  - The format is in the TT chat's reply, also summarised here: a 0x80-byte header before data aligned to 0x1000. Width is at +4, height at +8, and the D3D format dword at +0x38 (bits 8–15: 0x12 linear BGRA, 0x0C DXT1).

---

## 5. Testing

### 5.1 On Windows (the user's PC)

- **Normal:** start from the launcher, or run `out\build\release\TwoTowers.exe` (it uses `assets\` or `redirect.txt`).
- **"Working" looks like:**
  - the logos, legal screen and intro movie play, then the menu;
  - a level loads;
  - the log has no `error:` lines (except F3 dumps, below);
  - on quit the log ends `Exit (0)`.
  - The user plays at 180 fps (21:9 about 150 fps) on an RTX 3080 Ti.
- **Straight into a level:** `C:\TT\project\tools\tt_level_test.bat`. It sets `TT_LEVEL` and starts the launcher.
  - The level replaces the opening battle, so when it ends the game carries on to Weathertop.
  - Level codes include: `01WEAT1`, `02GATE2`, `04BALIN1`, `13AMON1`, `14FANG2`, `16ROHAN1-3`, `17TOP1`, `18HOLE1`, `20YARD1`, `21ORTH1-2`.
- **Logs:**
  - Under the launcher: `C:\Games\ROTK-Test\ROTKFix\TwoTowers\tt.log`, with dumps in `...\CrashDumps\`.
  - Standalone: `out\build\release\logs\`.
  - Release logs keep only `LOGR` and `LOGE` lines; use `LOGR` for anything a player's log must show.
- **F3** writes a thread dump to the log as `error:` lines without an exception line. That's not a crash.
  - With `XBE_CAPTURE_DIR` set (`capture.bat`), F3 also saves a frame capture.
- **Crash triage:**
  1. Get the module base from the dump (Python `minidump` package) and subtract it from the exception address.
  2. Run `llvm-symbolizer --obj=TwoTowers.exe --relative-address <rva>` with the matching `.pdb` beside the exe.
  3. If the symbol is a library function, check which object contains the address with `llvm-pdbutil dump --section-contribs`. The nearest public symbol can mislead.
  4. Guest return addresses in the log's stack lines can be disassembled with `tools/xbe_dis.py <xbe> <hexVA>`.
- **Useful switches:**

  | Switch | What it does |
  |---|---|
  | `TT_DEV_LOG=1` | Log the game's developer settings |
  | `TT_DEV="Name=v;..."` | Hold settings at test values |
  | `TT_FOV_PERCENT` | Field of view |
  | `TT_HUD_HIDDEN` | Hide the HUD |
  | `XBE_PERF=1` | Per-second performance lines |
  | `DSOUND_STATS=1` | Sound statistics |
  | `XBE_RENDER_THREAD=1` | The threaded renderer (it ghosts in TT; this game uses the title thread) |

### 5.2 On Linux: the headless harness

This runs the recompiled game with no window, GPU, sound or pad. A CPU reference renderer draws chosen frames to `.tga` files, and pad scripts drive input. It finds crashes, hangs and logic bugs without the user. The full guide is `notes/08-handoff-sdk-harness.md` section 2. It **doesn't cover** D3D12, ImGui, SDL or the window, so Windows-only breakage (like the ImGui crash) needs a Windows run.

**Tools:** Ubuntu, clang++ 18, CMake ≥3.25 (3.28 used), Ninja, Python 3, `capstone`; MinGW-w64 for syntax checks.

**Steps:**

1. Build `xbe_symbols` and `xbe_recomp` with `shared/harness-linux/linux-tools-CMakeLists.txt`.
2. Recompile with `xbe_recomp game/config/game.toml --xdk game/sdk/xdk`.
3. Compile the generated code with `compile_gen.sh`.
4. Build the harness with `game/sdk/harness/build.sh`.
5. Run with `game/sdk/harness/run.sh -x <exe> -g <game folder or .iso> -o runs/<name> -t <s> -s <pad script>`.

`shared/harness-linux/rebuild.sh` does steps 2–4; edit its paths. A rebuild after a source change takes about 40 s.

**Level sweep** (run before every release):

- `shared/harness-linux/lvsweep.sh` boots all 14 levels for about 2 minutes each, two at a time, with ultrawide, 60 fps, smooth 2D and damage settings on. It takes about 15 minutes.
- `lvreport.py` summarises the results: the level loaded, frames, movies, stalls, warnings and crashes.
- The last run (2026-10-14, the 1.0.0 code) was clean on all 14.
- The game folder needs Fangorn's stream bank `SOUND\S_WB_14FANG2.xwb`, or Fangorn stalls on loading.
- `TT_ASPECT=<w/h>` sets the harness's screen shape.
- The harness reads a `settings.toml` in the run folder.

The Linux workspace used so far was a temporary cloud container (`/home/claude/tt`: `build_sym/`, `hbuild/`, `gamedata*`, `runs/`). It **won't survive**, so rebuild it with the steps above. Its game data copies came from the user's files and must never be committed.

---

## 6. Release

**Process:**

1. Bump `XBE_VERSION` in `game/CMakeLists.txt`. It sets `--version` (`tt-port X.Y.Z`) and the exe's Windows version resource.
2. Run `game\build.bat`.
3. Start the game once.
4. Run `game\release.bat`. It writes:
   - **`game\out\build\release\TwoTowers\`**, the launcher folder: `TwoTowers.exe`, `THIRD_PARTY.txt`, `LICENSE-XboxXBE-Recomp.txt`, `TwoTowers.exe.sha256`. Nothing else ships.
   - `game\TwoTowers.zip`, the standalone zip: the same files plus `README.md` and an empty `assets\`. It's built with Windows' `tar`.
   - `game\symbols\<exe sha256>\`: that exe and its `.pdb`, kept to read crash dumps. Never ship or commit it.
5. The launcher chat copies the `TwoTowers\` folder into the Athelas package itself; don't write into the launcher's folders.

**1.0.0** (built 2026-10-14 20:16 by the user's `build.bat` then `release.bat`):

| File | SHA-256 |
|---|---|
| `TwoTowers.exe` (12,661,760 bytes) | `89d4e75d81a4d717531ccc2c2f499598efb3d5c26f11b427f61f64016c19b130` |
| `TwoTowers.zip` (4,672,049 bytes) | `d5cf2614c5e3bb81433ebcbdcc71adfc649560eec633b05d5c85859ddb810810` |

- The exe reports `tt-port 1.0.0`. Its Properties show "The Two Towers PC port", 1.0.0. The manifest is embedded (UTF-8 code page, PerMonitorV2 DPI, long paths). It uses ImGui 1.91.0 and SDL 3.4.18.
- Its `.pdb` is in `game\symbols\89d4e75d…\`.
- **Not verified at handoff:** whether this exact exe was started before release. The build just before it, which differed only in the version text and resource, ran fine on the user's PC.

**Release notes text** given to the launcher chat:

> **The Two Towers 1.0.0**: first release. A native PC port of the original Xbox game, with widescreen up to 32:9, up to 240 fps, higher resolutions, HDR, controller remapping and adjustable difficulty. It plays as on the Xbox by default.
>
> Known issues: only the US Xbox release is supported (others warn before starting and will most likely crash); a controller is required.

The exe is **not code-signed**, so Windows SmartScreen may warn "unknown publisher".

---

## 7. Rules and decisions from the user

**Never:**

- Put game code or assets in the project or any repository: no `default.xbe`, game files, `generated/`, screenshots or captures of the game, or analysis output. Players use their own dump.
- Bypass DRM.
- Use EA branding: no EA names or logos in the program name, window title or icons. The window title is "The Two Towers (PC port)".
- Upload or push anything to GitHub until the user says so. The roadmap's earlier rule was "nothing on GitHub until all three games work". Local commits only.
- Write into the launcher's folders. The launcher chat copies from ours.

**Decisions:**

- **Faithful first:** the defaults are the Xbox's (30 fps, 4:3 movies, widescreen off). Enhancements are settings, off by default. Presets: Original, Balanced, High, Ultra; only Original sets the frame rate.
- **Shipping:** "I'm going to ship the .exe, I'm not worried about the code." The port ships as the exe only, **inside the Athelas download**, version **1.0.0**, on GitHub Releases (the launcher chat packages it, when the user says so).
- **Movies:** the user chose the per-movie Fit handling. Logos, the intro, the story clips and the idle trailer fill the width (trailer: option "2", fit the whole attract movie). The making-of, interviews and death Eye stay 4:3. Screenshots during movies are at screen size with the bars.
- **Ultrawide:** menus show as on 16:9 with side bars, and the frame is drawn at full width. The user chose "let's go for it" for full-width drawing.
- **Said no to, or dropped:**
  - a `floor_reflections` setting (forcing it showed nothing outside Orthanc; removed);
  - camera follow distance (scripted cameras, no effect);
  - smooth leaf edges (alpha to coverage);
  - PlayStation button pictures (the user prefers Xbox ones);
  - an `--art` option for the launcher (the launcher reads the BFFs itself).
- **Kept:** custom difficulty (`[gameplay] damage_taken`, `enemy_damage`, 25–300%, scaling the game's own Good Guy / Bad Guy multipliers); smooth menus (`smooth_2d`, "same upscaling as ROTK PC movies").

**Working style:**

- The user relays messages between chats and tests on the PC, sending screenshots (the launcher's Screenshots folder).
- They are often away, so do the unattended work (harness runs, docs) meanwhile.
- They want plain explanations and short, ready-to-paste messages for the launcher chat.

**Bridge procedure** (when working through the device bridge rather than a shell on the PC): stage each delivery in a new folder, commit with `expectedMtimeMs`, then read the file back and compare byte for byte. A reused path once delivered a stale copy.

---

## 8. Open bugs and known issues

### 8.1 Dear ImGui must stay at 1.91.x (until the backend init changes)

- **What happened:** ImGui was raised to v1.92.9b. The first build that actually downloaded it crashed on every launch at the first frame: an access violation writing `0xA4` in `ImFontAtlasUpdateNewFrame` (`atlas->Builder` is null).
- **Cause:** `gpu/d3d12.cpp` calls the **legacy** `ImGui_ImplDX12_Init(device, frames, fmt, heap, cpu, gpu)`.
  - In 1.92 the legacy path turns off `ImGuiBackendFlags_RendererHasTextures`.
  - Nothing then builds the font atlas, and the assert is not fatal in our build.
- **Fixed** by pinning v1.91.0.
- **To upgrade:** switch to `ImGui_ImplDX12_InitInfo`. It needs our command queue, and `SrvDescriptorAllocFn` / `FreeFn` over a UI heap with room for several textures. Then test on Windows; the Linux harness doesn't include ImGui.

### 8.2 Not yet tested on Windows

These have no known fault; nobody has checked them yet.

- **Windowed mode at 150% Windows scaling.**
  - Changes made: PerMonitorV2 in the manifest, and `AdjustWindowRectExForDpi` with the window's DPI in `main.cpp`.
  - Check that the window is the right size and sharp.
- **Plains of Rohan water at 21:9.**
  - The water's wave simulation is kept at console size (`tt_renderer.cpp`), because render scaling broke its texel-exact reads. Ultrawide now draws the frame wider.
  - Check that the water looks right (`tt_level_test.bat`, option 3).
- **Non-English folder names**, for example `C:\Игры\...`, for the game files and saves. The UTF-8 code-page manifest is in the exe (verified), but the folders haven't been tried.
- **A real `.iso` on Windows.** Disc images were tested only in the Linux harness: a trimmed XISO and a full dump.
- **HDR.** The user's monitor has no HDR.
- **Refresh-rate setting in fullscreen**, and the colour looks and HUD sharpening. The roadmap marks these [built], not [have].
- **The 1.0.0 exe itself:** see section 6.

### 8.3 Known limitations and harmless oddities

- **Only the US release** is supported. Others warn (exit 3 with `--no-dialogs`) and will most likely crash; their code differs from the recompiled one.
- **Controller only.** There's no keyboard play.
- **Orthanc part 2 under `TT_LEVEL`** ends after about 40 s and moves on to Weathertop. That comes from loading it in the prologue's slot: when it ends, the game carries on as after the prologue. The same happened with normal damage. Not seen in normal play.
- **Floor reflections:** the game sets them per level by script (object at `[0x8D2484]`, `+0x480D` on, `+0x4810` mirror height, set at `0xB24C0`). Orthanc has them. Forcing them elsewhere (Balin's Tomb) drew a pass that showed nothing, so the idea was dropped.
- **The threaded renderer** (`XBE_RENDER_THREAD=1`) ghosts characters in TT: the game rewrites vertex memory too early. TT draws on the title thread (`CMakeLists.txt`).
- **In the harness:** without `FMV\mont.bik` (the `gamedata_fast` trick) sound effects stay paused. Use the full data for sound tests.
- **Old settings files** may still hold `floor_reflections = true`. Unknown keys are kept, and it's harmless.
- **F3 in a release build** writes "error:" lines to the log (the thread dump). That could alarm a player who presses it by accident; consider gating it behind `XBE_CAPTURE_DIR`.

### 8.4 Half-finished

Nothing is half-done in the code.

- The art option (`--list-art` / `--art`) was started and fully removed before reaching the PC. The tree has no trace of it.

---

## 9. Next steps and ideas

1. Run the checks in 8.2: windowed at 150%, Rohan at 21:9, a Unicode folder, a real `.iso`.
2. When the user approves an upload, the launcher chat publishes the Athelas release with `TwoTowers\` inside. Keep `game\symbols\` for every release.
3. Move the ImGui backend to `ImGui_ImplDX12_InitInfo` (8.1) if a newer ImGui is wanted.
4. Consider code signing to avoid SmartScreen warnings. It costs money; the user's call.
5. Bring the SDK changes into the ROTK Xbox port's SDK copy (`C:\ROTKX\rotkxbox\sdk`) with the complete patch. Better, keep one shared SDK for both ports.
6. Possibly gate F3 thread dumps behind `XBE_CAPTURE_DIR` (8.3).
7. Possibly offer the game-neutral SDK work (launcher interface, D3D12 features, harness) back upstream to XboxXBE-Recomp, if the user wants.
8. Revisit the window title before a wider release (roadmap item).

---

## 10. Other notes and logs

**In this repository:**

- `notes/01-xbe-analysis.md`: what's in `default.xbe` (sections, libraries, imports).
- `notes/02-existing-tools.md`: existing Xbox recompilation and decompilation tools.
- `notes/03-plan.md`: the original plan.
- `notes/04-xdk4928-support.md`: how XDK 4928 support was added (the method for XDK 5558 in ROTK Xbox).
- `notes/05-first-bringup.md`: first boot in the harness.
- `notes/06-windows-first-run.md`: first Windows runs, the D3D12 fixes, faithful-by-default.
- `notes/07-launcher-roadmap.md`: feature status ([have] / [built] / [todo]).
- `notes/08-handoff-sdk-harness.md`: the SDK, the harness, crash triage, lessons (written for the ROTK Xbox chat).
- `launcher-handoff/*.md`: the launcher contract and earlier exchanges.

**Not in this repository:**

- `notes/*.png`: game screenshots (ignored).
- `launcher-handoff/rotkfix-source*.zip`: launcher source snapshots from 2026-10-12 (ignored; the launcher project has its own source).
- `C:\TT\analysis\`: analysis output from the xbe.
- The TT chat's own conversation log, in the cloud workspace: `/root/.claude/projects/-home-claude/6cedf222-1004-5a06-9313-6bf25df14f74.jsonl`. It isn't on the PC and will go when that workspace does.

There are no other memory files.
