# 07: Launcher roadmap

These are the end goals, recorded on 2026-10-12 and updated on 2026-10-13. The plan is one launcher, from the ROTK Fix project, that detects and runs three games:

- ROTK PC
- The Two Towers Xbox (this port)
- later, ROTK Xbox, built on the same recompiler with XDK 5558 added

Nothing goes on GitHub until all three work.

The order of work doesn't change: the faithful game comes first. Every enhancement is a setting in `settings.toml` and is off by default.

Status markers below:

- **[have]**: done and tested on Windows.
- **[built]**: written and checked on Linux, waiting for its first Windows test.
- **[todo]**: not started.

The full key list, exit codes and folder rules are in `launcher-handoff\TT-PORT-INTERFACE.md`. The launcher chat reports that everything listed there is wired up on its side.

## 1. The launcher drives the port

- **[have]** Start-up arguments:
  - the game files: an extracted folder or a disc image;
  - `--config`, `--textures`, `--log`, `--saves`, `--shader-cache` and `--screenshots`;
  - `--no-dialogs`, `--version` and `--describe-settings`.
- **[have]** Exit codes, documented, with `Exit (N): reason` as the log's last line.
- **[have]** `settings.toml` in sections, with stable keys. `--describe-settings` lists every setting's type, choices, `choice_labels`, range, label, help and default, plus presets: Original, Balanced, High and Ultra. Only Original sets the frame rate.
- **[built]** The Visual C++ runtime is built into the exe (`/MT`), so players don't need the Redistributable.
- **[built]** `release.bat` makes `out\build\release\TwoTowers\` holding only `TwoTowers.exe` and `THIRD_PARTY.txt`, the layout the launcher expects.
- **[have]** Disc images, tested in the Linux harness: both a trimmed XISO made by extract-xiso and a full-dump style image with the video partition in front. Not yet tried on Windows with a real image.

## 2. Display and graphics, matching ROTK Fix

**Tested on Windows:**

- **[have]** Display mode, Monitor, Resolution, V-Sync and Render resolution, a dropdown from 50% to 300%.
- **[have]** Widescreen correction, with the HUD at the screen edges, checked in 16:9. Menus and the title screen fill the screen: their art sat inside a TV safe area with black around it, and is now enlarged 8% to the edges, with the menu text moving with it.
- **[have]** Smooth menus and HUD (`smooth_2d`, on except in Original): pictures the game draws unfiltered, including the full-screen 640 x 480 menu backgrounds, are upscaled bicubic instead of turning into blocks at high render resolutions.
- **[have]** Texture filtering, Texture sharpness, Brightness, Field of view and Skip opening logos.
- **[have]** Sharpening, with the `[` key, and screenshots with `\`.

**Waiting for the first Windows test:**

- **[have]** Frame rate: 30 (the Xbox's), 60, 90, 120, 144, 165, 180 and 240. Played at 180 on Windows.
- **[built]** Refresh rate, used in fullscreen.
- **[built]** Anti-aliasing (MSAA): Off, 2x, 4x or 8x, on the frame's own targets.
- **[built]** Colour look (Original, Film, Vivid, Dark fantasy) with Look strength and the `]` key. These use ROTK Fix's own numbers.
- **[built]** HUD sharpening, automatic while Sharpening is on, as in ROTK Fix.
- **[built]** HDR, one on/off setting, for HDR screens with Windows HDR on. It can't be tested on the player's monitor, which has no HDR.
- **[have]** Movies: Movie display (Fit, Stretch or 4:3), Sharper cutscenes, Cutscene sound delay and Skip any video. Fit is chosen per movie: the story clips, intro, logos and idle trailer fill the width; the making-of, interviews and game-over Eye stay 4:3.
- **[have]** Screenshots show the picture as on screen: brightness, sharpening, colour look, and movies placed as shown, at the full screen size.

**Not planned:**

- Smooth leaf edges (alpha to coverage), dropped by the player.
- **[have]** Ultrawide: with widescreen on, the picture takes the screen's shape from 16:9 up to 32:9: a wider camera, the HUD at the wide edges, and screenshots at that shape. Menus show as on 16:9 with bars at the sides, as filling the width would stretch the art or cut off the menu. Checked at 21:9 (3840 x 1620) on Windows: the world fills the screen and the HUD sits at its edges.
  - **[have]** The frame is drawn at the screen's width as well as its height (it used to be drawn at the height's scale and widened, 1.33x softer across at 16:9 and 1.78x at 21:9). This costs that many more pixels to draw: 1.33x at 16:9, 1.78x at 21:9. Checked on Windows at 21:9: as sharp across as down, shadows, lava haze and the HUD in place; about 150 fps where it was 180.

## 3. Texture packs

- **[have]** PNG and DDS packs anywhere under `textures\replace\`, named `<w>x<h>_<16 hex>.png` as ROTK Fix names them.
- **[have]** Dumps go to `textures\dump\`. While dumping, the log names each texture first drawn in 2D (HUD, menus and text).

## 4. Controllers

- **[built]** `[controls]` remapping: for each Xbox button, which controller button presses it. The labels are the game's own controls screen's: A Speed Attack / Fire, B Knock Back, X Parry, Y Fierce Attack, Black Jump Back, left trigger Ranged Weapon, right trigger Killing Move. White does nothing in this game and keeps its Xbox name.
- **[built]** `[input]` Vibration, Stick deadzone and Trigger sensitivity. The game's rumble reaches the controller.
- **[have]** PlayStation, Switch and other pads work through SDL, by button position: Cross acts as A, Circle as B, Square as X and Triangle as Y.
- Not planned: PlayStation button pictures. The player prefers the Xbox ones, and the button textures are scattered through the game files.

## 5. Keep the recompiler work reusable for ROTK Xbox (XDK 5558)

- **[have]** `sdk_4928\sdk_vs_upstream_65e959d4.patch` is the complete set of SDK changes against upstream. It's refreshed with every SDK change.
- **[have]** `notes\08-handoff-sdk-harness.md` covers the SDK, the harness and the lessons learned.
- **[have]** Generic hooks stay game-neutral in the SDK, and game-specific code lives only in `src\game`.

## 6. Release basics

- No game data or generated code in any repository.
- **[todo]** Check the XboxXBE-Recomp licence, since it decides how the port can be shared.
- Neutral name: no official names or logos in the program name, window title or icons.
  - **[todo]** The window title is currently "The Two Towers (PC port)". Revisit it before release.
