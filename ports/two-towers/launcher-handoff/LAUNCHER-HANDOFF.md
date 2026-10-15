# Launcher ⇄ Two Towers port: how they fit together

From the ROTK Fix chat, for the Two Towers chat.

## Where the launcher's source is

The source isn't on this PC. It lives in the ROTK Fix chat's workspace. A snapshot is
next to this note: `rotkfix-source.zip` (git commit a1d2516). The parts that matter for this note:

| File | What it does |
|---|---|
| `launcher/src/library.cpp/.h` | Finds the player's games (ROTK PC, ROTK Xbox, Two Towers Xbox), reads the title ID and region from `default.xbe` (folders and .iso), and keeps the list in `ROTKFix\games.ini`. |
| `launcher/src/config.cpp` | `launch_game()` (line ~755) starts ROTK PC with CreateProcess, working directory = game folder, no arguments. Settings are read and written with Get/WritePrivateProfileString to `ROTKFix.ini` (from line ~286). |
| `launcher/src/main.cpp` | The ImGui interface. `draw_other_game()` is where the Two Towers page will go. |

ROTK PC needs no arguments because the ROTK Fix DLL sits in the game folder and reads
`ROTKFix.ini` itself. The Two Towers port is a separate program and the game files may be
read-only (or an .iso), so the launcher has to tell it where everything is. Below is what
the launcher side needs. Names are suggestions; tell the user what you settle on and I'll match it.

## 1. Start-up arguments

```
TwoTowers.exe --game "<folder with default.xbe>"  --config "<path>\settings.toml"
              [--textures "<texture folder>"] [--log "<path>\tt.log"]
```

- `--game`: the extracted game folder. If the port can also read an .iso directly, accept
  that here too; if not, say so and the launcher will only offer extracted folders for TT.
- `--config`: the launcher keeps settings in its own folder (per game), not in the game
  folder. The port should create the file with defaults if it is missing.
- No admin rights, no console window in release builds.
- `--version` prints one line (e.g. `tt-port 0.3.0`) and exits 0, so the launcher can show it.

## 2. Settings list

The launcher builds the options page from a description instead of hard-coding it, so new
settings appear without a launcher update:

```
TwoTowers.exe --describe-settings      → JSON on stdout, exit 0
```
```json
{ "settings": [
  { "section": "graphics", "key": "fps", "type": "choice", "default": "60",
    "choices": ["30","60"], "label": "Frame rate",
    "help": "30 is the original speed; 60 is smoother." },
  { "section": "graphics", "key": "fov", "type": "int", "default": 100, "min": 70, "max": 130,
    "label": "Field of view (%)", "help": "..." },
  { "section": "textures", "key": "replace", "type": "bool", "default": true,
    "label": "Use texture packs", "help": "..." }
] }
```
Types: `bool`, `int` (min/max), `float` (min/max), `choice` (list of strings), `string`.
Please keep `settings.toml` to plain `[section]` + `key = value` lines (no arrays or inline
tables) so the launcher can edit it in place without a full TOML library. Unknown keys are kept.

User preferences from the ROTK side that should match: field of view default 100%;
drop-down menus for choices; faithful to the original by default.

## 3. Exit codes

The launcher waits for the port in the background and turns a non-zero code into a plain message:

| Code | Meaning | Launcher shows |
|---|---|---|
| 0 | Normal quit | nothing |
| 2 | `--game` folder missing / not The Two Towers | "The game files couldn't be found…" |
| 3 | Wrong version (title ID ≠ 0x45410010 or not the US release) | "Only the US version is supported for now." |
| 4 | Graphics card can't run Direct3D 12 (feature level too low) | "Your graphics card or driver doesn't support…" |
| 5 | settings.toml unreadable | "The settings file is damaged; reset it?" |
| other / crash | — | "The game closed unexpectedly. The log is here: …" |

## 4. Texture packs (PNG)

ROTK Fix's scheme, for consistency (players will see both in the same launcher):

- Folder: `<textures>\replace\` for packs, `<textures>\` for dumps. The launcher has an
  "Open folder" button and creates both folders.
- File name: `<width>x<height>_<16 hex digits of a 64-bit hash>.png`, lower-case hex.
  ROTK uses FNV-1a 64 over the original texture's pixel data; use whatever hash is stable
  for TT, but keep the name pattern. Sub-folders inside `replace\` are allowed (packs drop
  in as a folder).
- Settings: `[textures] replace = true`, `[textures] dump = false`.
- Logging each 2D/HUD texture name once while dumping makes packs much easier to make.

## 5. Release shape (agreed with the user)

- Plug and play: a ready-made exe in the download; players supply their own US copy. No game
  files or default.xbe in any download.
- Nothing goes to GitHub until all three games work and the user says so.
