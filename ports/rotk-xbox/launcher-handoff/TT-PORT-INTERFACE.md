# The Two Towers port: what the launcher can rely on

From the Two Towers chat, for the ROTK Fix chat. This answers `LAUNCHER-HANDOFF.md`. Where a name differs from that note's suggestions, the reason is given. Program version: `tt-port 0.1.0`.

**Status:** built and unit-tested on Linux (settings reading and writing, migration, PNG packs, name matching). The game itself still runs as before in the test harness. The first Windows run of the new arguments is still to come.

## 1. Start-up

```
TwoTowers.exe --game "<folder with default.xbe, or a disc image .iso>" --config "<path>\settings.toml"
              [--textures "<folder>"] [--log "<path>\tt.log"] [--saves "<folder>"] [--shader-cache "<folder>"]
              [--screenshots "<folder>"]
              [--no-dialogs]
TwoTowers.exe --version              -> "tt-port 0.1.0" on stdout, exit 0
TwoTowers.exe --describe-settings    -> JSON on stdout, exit 0
```

**Arguments:**

- `--game`: an extracted folder or an `.iso`. Both work.
  - With a folder, any `.iso` inside it is also tried.
  - Nothing is copied or written there, so read-only game files are fine.
- `--config`: if the file is missing, it is created with every setting at its default.
- `--textures`: holds `replace\` and `dump\`. The default is a `textures` folder beside the exe.
- `--log`: the file name's stem is used as the log's name, so `tt.log` gives `tt.log`. Crash dumps go to a `CrashDumps` folder beside the log.
- `--no-dialogs` (new): failures end the run with their exit code and a line in the log, without a message box. Pass it, so the player sees the launcher's own message instead of two.

**Behaviour:**

- Relative paths are resolved against the launcher's working directory at start-up.
- The program is GUI-subsystem, so no console window opens. `--version` and `--describe-settings` write to the stdout handle the launcher passes in (a redirected pipe). From a command prompt they print to the console.
- `--saves`: the Xbox hard disk, which holds the save games and the game's own cache. The default is `hdd\` beside the exe.
- `--shader-cache`: compiled shaders, kept so later runs don't stall. The default is `shadercache\` beside the exe. Deleting it is safe; it refills.
- `--screenshots`: where the **\\** key (the key above Enter) saves PNGs of the picture as shown, named `TT_YYYY-MM-DD_HH-MM-SS_mmm.png` like ROTK Fix's. The default is `Screenshots\\` beside the exe.
- F3 debug captures go in a `captures` folder beside the log's folder.
- With all of `--config`, `--textures`, `--log`, `--saves` and `--shader-cache` given, nothing is written beside the exe. The port's folder can then be read-only, for example under Program Files.

## 2. Settings

`settings.toml` uses plain `[section]` headers and `key = value` lines.

**Value formats:**

- Values are `true` or `false`, whole numbers, or strings in double quotes.
- Choices are always written as quoted strings, even numeric ones (`fps = "30"`).

**Editing rules:**

- Keys the port doesn't know are kept, in whatever section they're in.
- The port writes the file when the game quits. Edit it only while the game is closed.
- An older flat file (from before sections) is migrated with its values intact.

`--describe-settings` returns this list. The `default` field is the true default:

| section | key | type | default | values | label |
|---|---|---|---|---|---|
| display | mode | choice | `"borderless"` | fullscreen, borderless, windowed | Display mode |
| display | screen | choice | `"main"` | "main", then this PC's screens by Windows name ("DISPLAY1", "DISPLAY2"...). The list is read on the player's PC; a name that isn't connected is kept, and the main screen is used meanwhile. | Monitor |
| display | refresh | int | 0 | 0 to 500 Hz, fullscreen only (0 = the highest the screen has for the size) | Refresh rate |
| display | width | int | 0 | 0 to 16384 (0 = desktop) | Width |
| display | height | int | 0 | 0 to 16384 (0 = desktop) | Height |
| display | vsync | bool | true | | V-Sync |
| display | render_percent | choice | `"100"` | 50, 75, 100, 125, 150, 200, 250, 300 (percent of the screen's size) | Render resolution |
| graphics | anti_aliasing | choice | `"0"` | 0, 2, 4, 8 (MSAA samples; labels Off, 2x, 4x, 8x) | Anti-aliasing |
| graphics | anisotropic_filtering | choice | `"0"` | 0, 2, 4, 8, 16 | Texture filtering |
| graphics | lod_bias | float | 0.00 | -1.00 to 0.00 (lower is sharper) | Texture sharpness |
| graphics | sharpening | int | 0 | 0 to 100 (contrast adaptive, CAS-style) | Sharpening |
| graphics | brightness | float | 1.00 | 0.50 to 2.00 | Brightness |
| graphics | widescreen | bool | false | | Widescreen correction |
| graphics | fps | choice | `"30"` | 30, 60, 90, 120, 144, 165, 180, 240 | Frame rate |
| graphics | hud_edges | bool | true | (only matters with widescreen on) | HUD at the screen edges |
| graphics | fov | int | 100 | 70 to 150 (percent of the game's own) | Field of view |
| graphics | skip_logos | bool | false | | Skip opening logos |
| textures | replace | bool | true | | Use texture packs |
| textures | dump | bool | false | | Save game textures |
| network | join_ip | string | `""` | | Co-op host address |

Each JSON entry also carries `label`, `help`, and either `min`/`max` (int and float) or `choices` (choice). A choice can also carry `choice_labels`: one display name per choice, in the same order (for example `"0"` → `"Off"`, `"16"` → `"16x"`, `"60"` → `"60 fps"`). Show those in the dropdown; the file still stores the choice itself. `screen` has none, since its names come from the PC. Floats are written with two decimals (`brightness = 1.00`).

The JSON also has a `presets` object: **Original**, **Balanced**, **High** and **Ultra**. Each preset maps `"section.key"` to a JSON value. A preset only lists the keys it sets; anything it leaves out, such as brightness or field of view, stays the player's own choice. Show "Custom" when the current values match no preset.

| Preset | render_percent | widescreen | anisotropic_filtering | lod_bias | fps |
|---|---|---|---|---|---|
| Original | "100" | false | "0" | 0.00 | "30" |
| Balanced | "100" | true | "8" | 0.00 | (not set) |
| High | "100" | true | "16" | -0.50 | (not set) |
| Ultra | "150" | true | "16" | -0.50 | (not set) |

Frame rate is only in Original, which puts back the console's 30. Balanced, High and Ultra are picture quality only, as in ROTK Fix, and leave the player's frame rate alone.

Each preset also sets `graphics.sharpening` (Original 0, Balanced 30, High 60, Ultra 60) and `graphics.anti_aliasing` (Original "0", Balanced "2", High "4", Ultra "8", as ROTK Fix's presets).

Presets will gain anti-aliasing, sharpening and the colour looks as those settings are added.

**Differences from the suggestions:**

- `fps` defaults to `"30"`, not `"60"`, and is the one dropdown with an extra entry over ROTK Fix's. Every default is faithful to the original Xbox game, and that game runs at 30.
- There is no `fps_cap` setting any more. `graphics.fps` carries the limit itself, and an old `fps_cap` key is ignored. The game keeps its real speed at any rate, since it times each frame by the clock. Above 60, V-Sync still holds frames to the screen's refresh.

### Matching ROTK Fix

The labels, help lines and dropdowns follow the ROTK PC page of launcher 0.8, so the two games can share one look:

| ROTK Fix (ROTKFix.ini) | Two Towers (settings.toml) | Shown as |
|---|---|---|
| Graphics/Screen | display.screen | Monitor |
| Graphics/DisplayMode | display.mode | Display mode: Borderless, Fullscreen, Windowed |
| Graphics/Width, Height | display.width, height | Resolution (0 = the screen's own) |
| Graphics/RefreshRate | display.refresh | Refresh rate (0 = highest) |
| Graphics/VSync | display.vsync | V-Sync |
| Frame/TargetFPS | graphics.fps | Frame rate: the same list, 60 to 240 fps, with 30 fps first (the Xbox original, and the default) |
| Graphics/MSAA | graphics.anti_aliasing | Anti-aliasing: Off, 2x, 4x, 8x |
| Graphics/AnisotropicFiltering | graphics.anisotropic_filtering | Texture filtering: Off, 2x, 4x, 8x, 16x |
| Graphics/TextureLODBias | graphics.lod_bias | Texture sharpness, -1.00 to 0.00 |
| Graphics/Sharpening | graphics.sharpening | Sharpening, 0 to 100; **[** toggles it while playing, as in ROTK Fix |
| Graphics/Brightness | graphics.brightness | Brightness, 0.50 to 2.00 |
| Graphics/WidescreenFix | graphics.widescreen | Widescreen correction |
| Graphics/FieldOfView | graphics.fov | Field of view (TT in percent of the original, ROTK in degrees) |
| Textures/Replace, Dump | textures.replace, dump | Use texture packs, Save game textures |
| Video/SkipOpeningLogos | graphics.skip_logos | Skip opening logos |

Not planned for The Two Towers: smooth leaf edges (alpha to coverage).

Two Towers only: render resolution, HUD at the screen edges, co-op host address. ROTK only, for now: colour look and strength, movie display, sharper cutscenes, skip any video, cutscene sound delay, draw distance. The Two Towers keeps `fps` in `[graphics]`; the launcher can show it on its Display tab with ROTK's.

Render resolution is a dropdown now, not a slider: `render_percent` is a choice (stored quoted, `render_percent = "150"`), with labels like `"100% (screen's own)"` and `"200% (supersampling)"`. An older file's bare number (`render_percent = 150`) still reads; a number not in the list falls back to `"100"`. The presets give it as a string too (`"display.render_percent": "150"`).

The frame-rate list changed: `"unlimited"` is gone, and 90 and 180 were added. A file still holding `"unlimited"` falls back to `"30"`.

## 3. Exit codes

| Code | Meaning |
|---|---|
| 0 | Normal quit |
| 1 | The game failed to boot after its files were found (see the log) |
| 2 | `--game` doesn't exist, or holds no `default.xbe` or `.iso` with one, or holds another game (title ID isn't 0x45410010) |
| 3 | The Two Towers, but not the release this build was made from (the US release). Without `--no-dialogs` the player is asked whether to start it anyway; "No" exits 3. |
| 4 | Direct3D 12 couldn't start (no device at feature level 11_0, or the swap chain failed) |
| 5 | `settings.toml` exists but isn't valid TOML. The log has the parser's message and line. |
| other | A crash. The log ends with the exception, and a dump is in `CrashDumps`. |

Each failure writes `Exit (N): <reason>` as the last line of the log.

## 4. Texture packs

**Layout:**

- Packs go anywhere under `<textures>\replace\`, and subfolders are allowed.
- Dumps go in `<textures>\dump\`.

This differs from the suggestion of dumps loose in `<textures>\`. A `dump\` subfolder matches ROTK Fix's actual layout (`ROTKFix\textures\dump\`).

**Names:**

- A dumped file is named `<width>x<height>_<16 lower-case hex digits>.png`. The number is a 64-bit hash of the texture's original data, its format and size, and its palette for 8-bit textures. It is stable across runs.
- A pack file matches when its name ends in the same 16 digits. A prefix is allowed (`hud_health_256x64_0123456789abcdef.png`), and case doesn't matter.

**Formats:**

- **PNG:** any size. Mipmaps are generated.
- **DDS:** (DXT1/3/5, BC7, 32-bit) is still accepted, and uses its own mip levels.

**Settings and logging:**

- The settings are `[textures] replace = true` and `[textures] dump = false`.
- While dumping, the first time a texture is drawn in 2D (HUD, menus, text), the log gets one line: `texture pack: drawn in 2D (HUD, menu, text): 256x64_0123456789abcdef.png`.

## 5. A release copy

The launcher looks for the port in a `TwoTowers` folder beside itself. That folder needs:

- `TwoTowers.exe`, the only program file. The renderer, input (SDL) and settings code are all built in.
- `THIRD_PARTY.txt`, the licences of the libraries built into it (toml++, Dear ImGui, SDL, stb). These licences require it to ship with the program.

It doesn't need these files from the build folder:

- `TwoTowers.pdb`: debugging symbols. Keep it for reading crash dumps, but don't ship it.
- `xbe_recomp.exe` and `xbe_symbols.exe`: build tools.
- `TwoTowers_xbe.cpp`, `build.ninja`, `CMakeFiles` and `_deps`: build files.
- `hdd`, `logs`, `shadercache`, `textures`, `settings.toml` and `CrashDumps`: the developer's own runtime data.

**Windows requirements:**

- Windows 10 or 11 with Direct3D 12. `D3DCOMPILER_47.dll`, `d3d12.dll` and `dxgi.dll` are part of Windows.
- **Open point:** the exe currently also needs Microsoft's Visual C++ 2015–2022 runtime (`MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`). Many PCs have it, but not all. The plan is to build the runtime into the exe instead (the static runtime, `/MT`), so nothing extra is needed. Until then, the launcher should tell the player to install the "Microsoft Visual C++ Redistributable (x64)" if the port fails to start with a missing-DLL error. Windows reports that as exit code `0xC0000135` (missing DLL), before the port can write a log.

## 6. Not done yet

- Colour looks, movie display and movie sharpening, HUD sharpening, and controller remapping and prompts. These are in the Two Towers roadmap, and the settings list will grow to include them.
