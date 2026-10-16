# ROTK Xbox port, 03: launcher requests (2026-10-13 evening)

## Read-only port folder: `--config` and `--shader-cache`

These already work, in the shared SDK with The Two Towers (`app/main.cpp` Args, `app/settings.cpp` SettingsFile,
ShaderCacheDir). With `--config "<folder>\settings.toml"`, settings are read from and written to that file, never
beside the exe. With `--shader-cache "<folder>"`, compiled shaders go there.

The other output folders follow `--log` (logs, CrashDumps and F3 captures), `--saves` (the Xbox hard disk),
`--screenshots` and `--textures`. ROTKFix.ini (`--ini`) is only read, never written.

## Exit codes

These are the same SDK code as The Two Towers:

| Code | Meaning |
|---|---|
| 0 | quit |
| 1 | boot failure |
| 2 | wrong or missing files |
| 3 | not the US release |
| 4 | no Direct3D 12 |
| 5 | bad settings file |

Every failure writes `Exit (N): reason` as the last line of the log. Code 2 and code 3 test against ROTK's title ID
(0x4541003E).

## HUD in widescreen

The Xbox port already matches ROTK Fix as it is today, so nothing was changed:

- ROTK Fix build 78 (source 2026-10-12b, `graphics.c` `hud_aspect`) lets menus and the HUD fill the screen up to
  16:9. It only holds them at 16:9 on wider screens.
- The PC game itself lays the HUD out in fractions of the screen.
- "KeepHUDProportions" is in the launcher's list of retired keys.
- The user's own PC screenshot at 2560x1440 (ROTK-Test, 2026-10-12 10:30) shows the HUD stretched: the target ring is
  wider than tall.
- The Xbox port draws the same layout. The SDK shows 16:9 at most, so ultrawide screens get the same 16:9 cap.

The 4:3 shape and edge logic in ROTK-ENGINE-NOTES section 5 describes ROTK Fix's ultrawide case. A 4:3 HUD with edge
pieces (like the Two Towers' `hud_edges`) would be a new option for both versions.

## More ROTKFix.ini keys

| Key | Xbox port |
|---|---|
| [Graphics] DrawDistance (50 to 800) | Far clip plane of the 3D cameras times the percent, by the same rule as ROTK Fix. The Xbox cameras use the same reversed depth as PC (_33 = -2.2e-5, _43 = 0.09, far at about 4100 units), so _33 is divided by the percent. Fixed-function fog distances are scaled with it (SDK `gpu::g_fog_scale`). |
| [Graphics] AlphaEdgeSmoothing | With MSAA, cut-outs (alpha test greater or greater-equal, no blending) use alpha to coverage. The edge's alpha is sharpened to a one-pixel ramp around the reference (SDK `gpu::g_alpha_edges`, `PsoKey::a2c`, `alphatest()`). |
| [Gamepad] SecondController | 0 leaves player 2's port empty. |
| [Gamepad] Enabled | Not used. This is the PC's DirectInput layer; on the Xbox version the controller is the game's own input. |
| [GamepadButtons], [GamepadButtons2] | Each action slot is driven by the chosen physical button, as in ROTK Fix's virtual pad (`rotk_controls.cpp`). The slots are A, B, X, Y, LB (white), RB (black), LS, LT and RT. Player 2 without a section of its own uses player 1's layout. The game's own prompt shows "A Speed", so the slot names match the Xbox layout. |

Settings.toml has matching options for standalone use: `draw_distance` and `alpha_edges`.

## Release

`release.bat` builds, then writes `release\ROTKXbox\` with only ROTKXbox.exe and THIRD_PARTY.txt (plus a zip when
7-Zip is installed).

## SDK: ROTK patch v4 (`sdk_5558\sdk_rotk_vs_tt_v4.patch`, replaces v3)

v4 adds `gpu/backend.h` (g_alpha_edges, g_fog_scale), `gpu/d3d12.cpp` (the PSO alpha-to-coverage key, the surf.w
constant and scaled fog) and `gpu/shaders.cpp` (`alphatest` returns the alpha, sharpened under alpha to coverage).

## Tested (Linux test build)

- INI with DrawDistance 200, AlphaEdgeSmoothing 1, SecondController 0, SpeedAttack=X and Parry=A: all read, and
  gameplay draws correctly.
- The test build has no MSAA, so alpha to coverage is checked only by a Windows syntax check. The HLSL is compiled on
  the PC.
- Still to check on Windows:
  - leaf edges with MSAA 4x or 8x and AlphaEdgeSmoothing on
  - the view at a long draw distance
  - a swapped button in play
  - a long play at 60 fps or higher
