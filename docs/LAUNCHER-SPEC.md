# Launcher spec: how the launcher finds games and starts ports

From the ROTK Fix chat. This is the current state as of launcher 0.8 (git c0e49cf). The source
is in `rotkfix-source-2026-10-12b.zip` next to this file; the relevant code is in
`launcher/src/library.cpp`, `port.cpp` and `main.cpp` (search for `tt_`).

## 1. Game kinds

| Kind key (games.ini) | Game | How it's recognised |
|---|---|---|
| `rotk-pc` | The Return of the King, PC (2003) | `rotk.exe` in the folder. Played through ROTK Fix (DLL in the game folder), not a port. |
| `tt-xbox` | The Two Towers, Xbox | Title ID **0x45410010** in `default.xbe`. Fallback when there's no XBE: a `Worlds\*.bfa` file. |
| `rotk-xbox` | The Return of the King, Xbox | Title ID **0x4541003E** in `default.xbe`. Fallback: `Data\Game\GlobScen.scx`. |

**Reading `default.xbe`:**

- The certificate address is at XBE offset 0x118 and the base address at 0x104. The certificate
  sits at file offset `cert - base`.
- In the certificate, the title ID is at +0x08, the UTF-16 title at +0x0C, and the **game region
  flags** at +0xA0. The flags are 1 = North America, 2 = Japan, 4 = Europe and the rest.
- Only copies whose flags include 1 are supported. Others are listed, but their page says "This is
  a European/Japanese copy. Only the US version is supported for now." Both of the player's US
  discs have flags 0x1.

**Disc images (.iso):** the launcher reads the XDVDFS filesystem itself.

- It looks for "MICROSOFT*XBOX*MEDIA" at sector 32, at partition offsets 0, 0x18300000, 0xFD90000
  and 0x89D80000.
- It then finds `default.xbe` in the directory tree. Each tree entry is
  `u16 left, u16 right, u32 sector, u32 size, u8 attr, u8 namelen, name`.

**The games list** is kept in `<launcher>\ROTKFix\games.ini`:

```
[Games]
Game = rotk-pc|C:\Games\ROTK-Test
Game = tt-xbox|C:\TT\extracted
Game = rotk-xbox|D:\Discs\ROTK (USA).iso
```

## 2. Finding a port

Both ports are found the same way, in this order:

1. The path in `<launcher>\ROTKFix\ports.ini`, `[Ports] TwoTowers=<full path>` or
   `[Ports] ROTKXbox=<full path>`. The player sets it with "Locate TwoTowers.exe…" or
   "Locate ROTKXbox.exe…", or by editing the file. Use it to point at a development build, such as
   `C:\ROTKX\rotkxbox\out\build\release\ROTKXbox.exe`.
2. Otherwise, the copy next to the launcher: `<launcher>\TwoTowers\TwoTowers.exe` or
   `<launcher>\ROTKXbox\ROTKXbox.exe` (release copies).

If the ports.ini path doesn't exist, the launcher falls back to step 2. Every write goes to the
data folders, `<launcher>\ROTKFix\TwoTowers\` and `<launcher>\ROTKFix\ROTKXbox\`, never next to
the port's exe.

## 3. What the launcher passes on start-up (exactly)

**Queries.** When the game's page is first shown, the launcher runs:

```
TwoTowers.exe --version
TwoTowers.exe --describe-settings
```

- stdout is captured through a pipe, the program runs with CREATE_NO_WINDOW, and each query has a
  15-second timeout.
- The working directory is the exe's folder.
- If either query fails, the page shows the error and a "Try again" button.

**Play.** Play first saves the settings file, then starts the port:

```
"<exe>" --game "<game path>" --config "<D>\settings.toml" --textures "<D>\textures"
        --log "<D>\tt.log" --saves "<D>\saves" --shader-cache "<D>\shadercache"
        --screenshots "<launcher>\Screenshots" --no-dialogs
```

- `D` is `<launcher>\ROTKFix\TwoTowers`.
- The game path is the folder or `.iso` from games.ini.
- Screenshots share ROTK's folder. The `TT_` file names keep them apart.
- The working directory is the exe's folder. The launcher creates `<D>\textures\replace` before
  starting.
- **First time only:** if `<D>\saves` doesn't exist and an `hdd\` folder exists next to the exe,
  `hdd\*` is copied into `<D>\saves`, so earlier progress carries over.

**While the game runs**, the launcher hides its window and waits on the process.

- **Exit 0:** the launcher closes, as it does after ROTK PC.
- **Any other code:** the window comes back with a message. The text after `Exit (N): ` on the
  log's last such line is used where it helps.

| Code | Launcher message |
|---|---|
| 1 | "The game couldn't start: `<reason>`. Details are in tt.log in `<D>`." |
| 2 | "The game files couldn't be found there, or they aren't The Two Towers." |
| 3 | "This copy isn't the US release. Only the US version is supported for now." |
| 4 | "Direct3D 12 couldn't start. Update your graphics driver…" |
| 5 | "The settings file is damaged (`<reason>`). Use "Reset to defaults"…" |
| other | "The game closed unexpectedly (0x…). Details are in tt.log in `<D>`." |

The launcher also checks the region before Play, so code 3 is a backstop.

## 4. Settings

**The settings page is built entirely from `--describe-settings`.**

- Each entry needs `section`, `key`, `type`, `default`, `label` and `help`. It also needs
  `min`/`max` for int and float, or `choices` for choice.
- Types:
  - `bool`: a checkbox.
  - `choice`: a drop-down menu; the value is written quoted.
  - `int`: a slider when max − min ≤ 400, otherwise a number box.
  - `float`: a slider, written with two decimals.
  - `string`: a text box, written quoted.

**The settings file** (`settings.toml`) is edited in place, line by line.

- Unknown keys and comments are kept.
- Missing keys are added to their section, with the section created if needed.
- On save, every listed setting is written, so the file is always complete.
- The file is saved through a temporary file and a rename.

**Tabs come from the section name:**

| Section | Tab |
|---|---|
| `display` | Display |
| `graphics`, `textures`, anything unknown | Graphics |
| `controls`, `input` | Controls |
| `audio`, `sound` | Audio |
| `network` | Network |
| `gameplay` | Gameplay (between Controls and Network; ROTK has no such tab) |

With six tabs the names are drawn without their extra spacing so they fit. An About tab always follows: location, version, port path, folder buttons, "Reset to defaults" and
the games list. A tab with no settings isn't shown.

**Presets.** The `presets` object (`{ "Name": { "section.key": value, … } }`) becomes a Preset menu at
the top of the Graphics tab.

- A preset lists only the keys it sets.
- The menu shows the first preset whose keys all match the current values (numbers compared by
  value), or "Custom" when none match.
- Picking a preset writes its keys.

**Keys the launcher treats specially.** These are by name, so please keep the names:

| Key | What the launcher does |
|---|---|
| `display.width` + `display.height` | Shown as one "Resolution" menu: Desktop, then the chosen monitor's modes. Both 0 means desktop. |
| `display.refresh` (int) | Shown right under Resolution as a menu of the rates the monitor offers at that size, plus "Highest (N Hz)" = 0. Greyed out unless `display.mode` is `"fullscreen"`. |
| `display.screen` (choice) | Each choice is shown as the monitor's real name ("main" = the primary). A `DISPLAYn` choice that is the primary screen is hidden, since it duplicates "main". |
| `display.fps_cap` | Hidden. It's written equal to `graphics.fps` on each save. Harmless now that the port has dropped it. |
| `graphics.fps` | Shown on the Display tab, as in ROTK. "unlimited" shows as "Unlimited", others as "N fps". |
| `graphics.hud_edges` | Greyed out while `graphics.widescreen` is false. |
| `graphics.anisotropic_filtering` | "0" shows as "As the game asks", others as "Nx". |
| `graphics.msaa` | "0" shows as "Off", others as "Nx". |
| `graphics.fov`, `display.render_percent` | The slider shows "%". `render_percent` choices also show the pixel size they draw at. |
| `graphics.smooth_2d` | "Smooth menus and HUD", right after Sharpening (no ROTK counterpart). |
| `gameplay.damage_taken`, `gameplay.enemy_damage` | Gameplay tab, "Difficulty" heading; sliders with "100% (game's own)". |
| `[controls]` (choice settings) | The Controls tab's Buttons list, worked like ROTK's: click a move, press the button; the move that had it swaps. Values must be `A B X Y LB RB LT RT LS RS none`. "Original layout" writes the defaults back. |
| `input.vibration`, `input.stick_deadzone`, `input.trigger_threshold` | Controller options on the Controls tab, as ROTK's rows. |

All other choices are shown with their first letter capitalised. New settings need no launcher
change.

## 5. The player's files (per game)

```
<launcher>\ROTKFix\TwoTowers\
    settings.toml   tt.log   CrashDumps\   captures\ (F3)
    textures\replace\   textures\dump\
    saves\   shadercache\
<launcher>\Screenshots\TT_*.png   (ROTK's are ROTK_*.png in the same folder)
```

## 5b. ROTK Xbox: ROTKFix.ini keys the launcher writes for the port

The Xbox page shares ROTK PC's settings (one `ROTKFix.ini`), greys out the PC-only rows, and adds:

| Key | Launcher row | Notes |
|---|---|---|
| `[Graphics] HUDEdges` 0/1 | "HUD at the screen edges" (View) | Xbox page only, shown only with Widescreen correction on. Default 0. |
| `[Graphics] SharperText` 0/1 | "Sharper text and menus" (Image, after Sharpening) | Xbox page only. Greyed out unless `Data\Game\GlobScen.scw` is beside ROTKFix.ini; the help says so if `Data\Game\fro\fro03.scw` is missing. Default 0. |
| `[Graphics] RenderScale` 50–200 | "Render resolution" (Display, under Resolution) | Both versions. % of the chosen resolution the game is drawn at, then fitted. Default 100. ROTK Fix (PC) shows fullscreen as borderless when it isn't 100. |
| `[Gamepad] ButtonNames` | "Button names" (Controller options) | Both versions (auto, xbox, playstation, nintendo, original). |

## 6. ROTK PC (for comparison)

ROTK PC needs no arguments. The launcher writes `ROTKFix.ini` in the game folder with
GetPrivateProfileString, then starts `rotk.exe` with no arguments. The ROTK Fix DLL, loaded by the
game through the `d3d8.dll` and `dinput8.dll` forwarders, reads that ini. If the disc version asks
for administrator rights, it's started through ShellExecute.
