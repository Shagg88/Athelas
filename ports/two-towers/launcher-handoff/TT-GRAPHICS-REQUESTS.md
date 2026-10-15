# The Two Towers port: requests from the launcher side

From the ROTK Fix chat. The first launcher test worked: the launcher started the port with the
player's settings (`C:\TT\extracted`, widescreen, 16x filtering, 60 fps), the player played about
3 minutes, and the port quit with exit 0.

Every new setting you add to `--describe-settings` appears in the launcher automatically. No
launcher update is needed. Choices show as drop-down menus, and `help` shows as the tooltip.

## 1. Bug: the HUD stays 4:3 with widescreen on

The player reports: "The HUD was still 4:3 on a 16:9 screen."

The settings were `widescreen = true` and `hud_edges = true`, at 2560x1440 borderless. The log
says `tt: world on screen (102 3D draws a frame): HUD to the screen edges` at 91 s and again at
124 s, but on screen the HUD stayed in the middle 4:3 area. Is the edge placement reaching the
HUD draws at all?

## 2. The user wants the same graphics options ROTK Fix has

These are the options ROTK Fix offers (Display and Graphics tabs). The suggested keys are in the
same style as yours. Defaults stay faithful to the original game; "ROTK Fix default" shows what
the High/Ultra presets use there.

| ROTK Fix option | Suggested TT setting | Notes |
|---|---|---|
| Monitor | `display.screen` choice: "main", "DISPLAY2"… | Which screen the game opens on. |
| Refresh rate (fullscreen) | `display.refresh` int, 0 = best | Only used with `mode = "fullscreen"`. |
| Anti-aliasing | `graphics.msaa` choice "0","2","4","8" | ROTK Fix default 4. |
| Smooth leaf edges | `graphics.alpha_to_coverage` bool | Alpha-tested foliage and fences, smoothed with MSAA. |
| Texture sharpness (LOD bias) | `graphics.lod_bias` float -1.0 to 0 | ROTK Fix default -0.5. |
| Sharpening | `graphics.sharpening` int 0–100 | A CAS-style post pass; ROTK Fix default 60. |
| Colour look | `graphics.colour_look` choice: original, film, vivid, dark | Plus `graphics.look_strength` int 0–100. |
| Brightness | `graphics.brightness` float 0.5–2.0 | Default 1.0. |
| Field of view | already there (`graphics.fov`) | ROTK Fix allows 70–150. |
| HUD sharpening | automatic, no setting | Unsharp mask on 2D/HUD textures, scaled by resolution. |
| Movie display | `graphics.movie_display` choice: auto, stretch, "4:3" | Bink FMVs. Auto stretches cutscenes to fill the width. |
| Sharper cutscenes | `graphics.movie_sharpening` int 0–100 | ROTK Fix default 70. |
| Skip opening logos | `graphics.skip_logos` bool | |
| Screenshots | **\\** key (the key above Enter) | PNG `TT_<date>_<time>.png`, plus `--screenshots "<folder>"`. |

If the original game has its own detail options (shadows, draw distance, effects), add those too.

**Presets.** ROTK Fix has a Preset menu: Original, Balanced, High, Ultra, Custom. To let the
launcher offer one for The Two Towers without hard-coding keys, please add a `presets` object to
the `--describe-settings` JSON:

```json
"presets": {
  "Original": { "graphics.msaa": "0", "graphics.sharpening": 0, "graphics.widescreen": false },
  "High":     { "graphics.msaa": "4", "graphics.sharpening": 60, "graphics.widescreen": true }
}
```

Choose the keys and values you think fit. The launcher shows "Custom" when the settings match
no preset.

**Tabs.** When the list grows, the launcher will put each section on its own tab, as ROTK Fix
does: `display` → Display, `graphics` → Graphics, `controls` → Controls, `audio` → Audio. Any
other section goes on the Graphics tab.

## 3. Folders the launcher should choose

- `--saves "<folder>"` for the `hdd\` save games.
- A shader-cache folder option, if possible.
- `--screenshots "<folder>"` (see the table above).

The launcher would put all of these in `ROTKFix\TwoTowers\`, so the port's own folder can stay
read-only.

## 4. Release copy

Which files besides `TwoTowers.exe` does a release copy need? The launcher looks for the port in a
`TwoTowers` folder next to itself.
