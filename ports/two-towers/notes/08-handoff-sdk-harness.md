# 08: Handoff — the SDK, the Linux harness, and lessons for ROTK Xbox

Written 2026-10-12 by the Two Towers chat, for the chat starting the ROTK Xbox recompile.

## 1. Is `C:\TT\project\game\sdk` the latest SDK?

**Yes.** Every file in `game\sdk` on this PC matches the Two Towers chat's workspace. That was checked file by file tonight, and no SDK source exists only in that workspace.

- The SDK is upstream **XboxXBE-Recomp** (https://github.com/TGP482/XboxXBE-Recomp) at commit **`65e959d4`** (2026-10-11, "Rename 'Controller' section to 'Input'"), plus our changes.
- **`sdk_4928\sdk_vs_upstream_65e959d4.patch`** is the complete, current set of changes against that commit: 30 files, 3,600 lines. Use it.
  - It leaves out `runtime/gpu/stb/`, which holds two unmodified headers (stb_image 2.30 and stb_image_write 1.16, from nothings/stb commit `2c980bb5`).
- The other `sdk_4928\*.patch` files are older and split by topic. They record how the work went, but they're partial and overlap. Don't apply them on top of the complete patch.
- `sdk_4928\manual.toml` holds the hand-proven 4928 addresses for **The Two Towers**. It's game-specific; ROTK Xbox needs its own.
- `xdk/4928/layout.toml` is the CDevice and DirectSound object layout for XDK 4928. XDK 5558 needs its own, made the same way. The method is in `notes\04-xdk4928-support.md`.

## 2. The Linux headless harness

The harness runs the recompiled game on Linux with no window, GPU, sound device or controller. A CPU reference renderer draws chosen frames to `.tga` files, and a scripted pad drives the input. It's how most of the Two Towers bugs were found without the player at the PC.

**Toolchain:** Ubuntu with `clang++` (18), `cmake`, `ninja` and Python 3 with `capstone` (for the analysis tools). For compile-checking the Windows-only files there's MinGW (`x86_64-w64-mingw32-g++ -fsyntax-only`), which needs the SDL3 headers for `input.cpp`.

### Steps

1. **Build the recompiler tools on Linux.** `shared\harness-linux\linux-tools-CMakeLists.txt` builds `xbe_symbols` and `xbe_recomp` from the SDK's `tools/` (Zydis 4.1.1, toml++ 3.4.0). Adjust its `SDK` path, then:
   ```
   cmake -S <dir> -B <dir>/b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
   ninja -C <dir>/b
   ```
2. **Make `config/game.toml`.** For The Two Towers this was `config/make_config.sh`. It merges the `xbe_symbols` output, the XbSymbolDatabase symbols, `sdk_4928/manual.toml` and `config/hooks.toml` using `tools\merge_config.py`. Those inputs are made from the player's `default.xbe`, so they stay out of the repository.
3. **Recompile:**
   ```
   xbe_recomp config/game.toml --xdk sdk/xdk
   ```
   This writes `generated/game/` (8,701 functions for TT, about a million lines).
4. **Compile the generated code once.** `shared\harness-linux\compile_gen.sh` builds it with clang at `-O1`, two jobs at a time, and caches the objects.
5. **Build the harness:**
   ```
   TOML_INCLUDE=<toml++ include dir> sdk/harness/build.sh -r <repo> -g generated/game -o <out> -n tt_headless -G <out>/gen
   ```
   `shared\harness-linux\rebuild.sh` runs steps 3 to 5 in one go. Its paths are the TT workspace's, so edit them.
6. **Run:**
   ```
   sdk/harness/run.sh -x <out>/tt_headless -g <game folder> -o runs/<name> -t <seconds> -s <pad script>
   ```
   Each instance writes to `runs/<name>/1/` (`logs/`, `cap/`, `hdd/`, `run.out`).

### Things that aren't obvious

- **`tt_harness_level.cpp` is already on the PC**, in `game\src\game\`. It reads `TT_LEVEL=<name>` (plus `TT_STREAM`) and swaps the game's level-name pointers, so the game boots straight into that level. ROTK Xbox will want its own version.
- **The movie-less data trick.** `gamedata_fast` was the game folder with symlinks to everything except the 3-minute intro movie (`FMV\mont.bik`). The game then skips straight past it. The catch is that in TT the missing movie leaves sound effects paused, so use the full data for any sound test.
- **Only drawn frames run vertex processing.** In the harness, assembly runs only on frames the reference renderer draws (`REF=N`). Anything that hooks vertices needs `REF=1` or a small N, which is slow.
- **Pad scripts** (`shared\harness-linux\pad_*.txt`) are lines of `frame buttons [frames held]`. The buttons are `UP DOWN LEFT RIGHT START BACK A B X Y BLACK WHITE LT RT LS LSD LSL LSR`.

### Environment switches

**Harness:**

| Variable | What it does |
|---|---|
| `REF=N` | Draw every Nth frame to `cap/ref_N.tga` |
| `CAP=N` | List every Nth frame's draws |
| `PROBES="x,y"` | Log what's written at those pixels |
| `SCRIPT` | The pad script |
| `AUDIO_WAV=<file>` | Record the mix to a WAV |
| `QUERY_LOG=1` | Log each visibility test |
| `INSTANCE` | Set by `run.sh` when several instances run (system link) |

**Runtime** (also works on Windows):

| Variable | What it does |
|---|---|
| `DSOUND_STATS=1` | Every 2 s: voices, sound calls, reverb send and wet levels |
| `DSOUND_REVERB=0` | Turn the reverb off |
| `XBE_PERF=1` | Per-second performance lines and slow frames |
| `XBE_CAPTURE_DIR` | Turn on F3 frame captures |
| `XBE_RENDER_THREAD=1` | The threaded renderer |

**TT only:** `TT_LEVEL`, `TT_STREAM`, `TT_FOV_PERCENT`, `TT_HUD_HIDDEN`.

## 3. The analysis tools

**In `C:\TT\project\tools\`:**

- `xbe_info.py`: header, certificate, libraries and sections
- `xbe_dis.py <xbe> <hexVA> [n]`: disassemble at an address
- `xbe_fn.py`: one function
- `xbe_scan.py` / `xbe_cfg.py` / `guest_reach.py`: imports, MMIO, reachability
- `merge_config.py`: builds `game.toml`

**In `shared\analysis\` (added tonight):**

- `callers.py <xbe> <hexVA>...`: every `call rel32` to those addresses
- `targets.py`: call targets from one address range into another, named from `image_symbols.h`

Both use plain `E8` scans. Finding indirect references, such as data pointers or `push imm32`, takes a small byte search for the address.

## 4. Lessons worth carrying over

1. **Crash triage.** A crash writes `CrashDumps\*.dmp` beside the exe, or beside the log when `--log` is used.
   1. Read the exe's base address from the dump's module list.
   2. Subtract it to get the relative address.
   3. Run `llvm-symbolizer --obj=TwoTowers.exe --relative-address <rva>`, with the `.pdb` beside the exe, to get `__imp__sub_XXXXXXXX`: the recompiled guest function.
   4. Disassemble the host code around that address with `llvm-objdump`. The guest return address is pushed as a constant (`movl $0xD0267, ...`), which gives the exact guest instruction.
2. **Guest threads really run in parallel.** The Xbox had one CPU; the PC has many. TT crashed on a loading-screen race that couldn't happen on the console.
   - Before adding a lock of your own, look for the game's own critical section around the same data. A second lock deadlocked in TT, while reusing the game's one was safe.
3. **Render scaling breaks texel-exact passes.** TT's water is a 128×128 wave simulation that reads its neighbour texels at offsets of exactly one texel. At 3× scale it saturated into stripes.
   - The fix is the renderer extension's `MakeTarget` hook: keep such targets at the console's size (`src\game\tt_renderer.cpp`).
4. **A missing effect usually means an Xbox feature the PC side didn't do yet.** The tell is an option set by the game that changes nothing on screen or in the sound. In TT:
   - the I3DL2 room reverb (the game sends every sound to it; now `audio\reverb.cpp`);
   - visibility tests (glows and flares; now real occlusion queries).

   Check the game's sends and listener calls with `DSOUND_STATS`.
5. **Release logs keep only `LOGR` and `LOGE` lines.** Diagnostics a player's log must show have to use `LOGR`.
6. **Game developer settings are gold.** TT registers named settings, for example "GRAPHICS/Cap to 30 fps" and "GRAPHICS/DMA Issue Mode". Flipping one from the host gave 60 fps, unlimited fps, HUD toggles and field of view with no code patching. Look for ROTK Xbox's own list early.
   - In TT every registration goes through one function, 0xE4870 (name, type, data, size), with the value's address first in data. `src\game\tt_devsettings.cpp` hooks it and keeps a name-to-address list.
   - With `TT_DEV_LOG=1` the harness logs every setting and its value; `TT_DEV="Name=value;..."` holds settings at test values. The draw count in the log shows when a hidden render pass turns on.
   - Found this way in TT: floor reflections, shipped switched off, and difficulty damage multipliers. Both are now player settings. Camera distance did nothing, because TT's cameras are scripted.
7. **Frame timing.** TT times each frame by `QueryPerformanceCounter`, which is why any frame rate keeps the right speed. Check this first for ROTK before offering 60 fps or more.
8. **Copying files to the PC through the device bridge.** Sending a second file from the same temporary path has delivered a stale copy. Stage each delivery under a new folder, then read the files back from the PC and compare them byte for byte.
9. **The launcher interface is game-neutral SDK code.** It covers the arguments, exit codes, the settings file and `--describe-settings`, PNG texture packs, screenshots and the folder options. ROTK Xbox gets all of it for free and only fills in its own `GameInfo` (options, presets, title ID). The full contract is in `launcher-handoff\TT-PORT-INTERFACE.md`.

## 5. What a first look at ROTK Xbox showed

The files looked at were `C:\ROTKX\extracted`, read only.

- **Title and region:** title ID `4541003E`, region North America ("ROTK_NTSC_EN_Fin").
- **XDK and libraries:** XDK **5558** for every library (XAPILIB, LIBC, D3D8, D3DX8, XGRAPHC, DSOUND, XBOXKRNL).
- **Sound:** no XACT; it uses DirectSound directly. There's a `DOLBY` section, likely the DSP image that holds the effects (Dolby surround and possibly reverb).
- **Engine:** EA's own (debug path `EAThirdPerson\lotr\rotk`), not TT's Stormfront Fusion engine. So TT's game-specific fixes don't carry over, but the SDK work does.
- **Data:** `Data\Game\<level>\*.scx` (18 level archives), `Data\Movies\*.vid`, `Data\Player\*.scx` and `Data\Special\*.vid` (bonus videos). This is the same layout as ROTK PC.
- **Movies:** `.vid` files with a `VID1` header, 640×480 at 29.97 fps, made of `FRAM` chunks. The game seems to decode them itself, so no Bink support should be needed.
- **Co-op:** there's two-player co-op (`CharSelScreen.Player2*`, co-op stats), so controller port 2 matters.
- **XDK 5558 groundwork** comes first. The SDK has `xdk/4928` and `xdk/5849` layouts; 5558 needs `xdk/5558/layout.toml` and its own symbol pass. TT's 4928 work, described in `notes\04` and the `d3d8_4928` / `dsound_4928` patches, is the template.

## 6. Added on 2026-10-13

**Harness:**

- **Settings:** a `settings.toml` in the run folder is now read, with the same keys as the port. For example, `[graphics] skip_all_videos = true` tested the movie skip.
- **Disc images:** `-g <file>.iso` runs the game from a disc image, mounted as D: just as the port does it. Both a trimmed XISO (made with extract-xiso from github.com/XboxDev/extract-xiso, `extract-xiso -c <folder> out.iso`) and a full-dump style image (video partition in front) ran 14 levels' worth of frames cleanly.
- **`TEX2D_DUMP=<folder>`:** saves every texture a 2D draw uses (HUD, menus, prompts) once, as a PNG named the way texture packs name them, plus a `.raw` copy of its guest bytes. A texture's raw bytes can then be searched for in the game files to find where it's stored. That's how the Xbox button pictures were found: they're page-aligned in the level and UI archives.
- **Levels need their stream sound bank.** A level whose `S_WB_<level>.xwb` is missing stalls on loading. Fangorn's 560 MB bank lives in `gamedata_fang`; the plain `gamedata` copy lacks it.

**Crash sweep:** all 14 levels, each played for about 3 minutes with scripted moves and attacks. No crashes, hangs or errors.

**SDK additions since section 1 (all in the complete patch):**

- MSAA with a multisampled twin per frame-size target: `ReadyRes` / `ReadyMs`.
- The movie ring, for movie display and sound delay.
- HDR (scRGB FP16 swap chain).
- Colour looks in the frame blit.
- HUD sharpening (`kTexSharp`).
- `choice_labels` in `--describe-settings`.
- `[controls]` remapping in `core\input.cpp`, with `GameInfo::button_actions` giving the game's move names.
- `[input]` vibration, deadzone and trigger sensitivity.
- `/MT` in `cmake\XbeRecomp.cmake`.

All of it is game-neutral, so ROTK Xbox gets it by filling in its own `GameInfo`.
