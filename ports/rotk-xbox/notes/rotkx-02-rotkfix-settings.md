# ROTK Xbox port, 02: ROTK Fix's settings, frame rate, widescreen and FOV (2026-10-13 evening)

The launcher keeps one Return of the King page for the PC and Xbox versions. The Xbox port now reads ROTK Fix's
own settings file with the same keys. All enhancements are still off unless the file or settings.toml turns them on.

## Reading ROTKFix.ini (`src/game/rotk_settings.cpp`)

`ROTKXbox.exe --ini "<folder>\ROTKFix.ini"` reads that file after settings.toml, so the INI wins. Without `--ini`,
settings.toml is used as before. (The Linux test build reads the `ROTK_INI` environment variable instead.)

| ROTKFix.ini | Xbox port |
|---|---|
| [Graphics] Screen, DisplayMode (fullscreen/borderless/windowed), Width, Height, RefreshRate, VSync | display settings |
| [Graphics] MSAA (0/2/4/8), AnisotropicFiltering, TextureLODBias, Sharpening, Brightness | picture settings |
| [Graphics] ColourLook (original/film/vivid/dark), ColourLookStrength | colour look |
| [Graphics] WidescreenFix | widescreen |
| [Graphics] FieldOfView (70 to 150) | field of view |
| [Frame] TargetFPS, Limiter (0 = the game's own 30) | frame rate |
| [Video] MovieDisplay (auto/stretch/4:3), SharperMovies, CutsceneAudioDelay, AllowSkippingAllVideos | movies |
| [Gamepad] Vibration, StickDeadzone, TriggerThreshold | controller |
| [Textures] Dump, Replace | texture packs |

The PC-only keys are left alone: Renderer, DxvkFolder, HideRealControllers, HideMousePointer and the DirectInput
mapping. **SkipOpeningLogos** is also ignored. On the Xbox the copyright, THX, New Line and EA logos are not a separate
movie (PC has logos.avi). They are the first part of HELMFMV1.vid, the Helm's Deep intro, so
AllowSkippingAllVideos is the way past them: Start then skips the whole intro. logos.vid is never played at startup.

## Frame rate (`src/game/rotk_framerate.cpp`)

The console paces each frame at two vertical blanks:

1. At the frame end, the loop at 0xD03D3 waits until a blank has passed since the last frame.
2. 0xBF1A0, called from 0xD03FB, unbinds the four texture stages, counts the frame, then tail-jumps to
   BlockUntilVerticalBlank.

The time step at 0x9EB20 is the number of blanks times 1/59.94 s. It works the same way as ROTK PC's real clock,
so the engine already runs at any rate.

With any setting other than 30:

- The 0xD03D3 wait is skipped (a `[[hook]]`).
- 0xBF1A0 runs without the wait when the frame end calls it. Loading screens keep the wait.
- The step is the real time since the last step, clamped to 0.1 s as on ROTK PC, with the same 1200 Hz tick bookkeeping.
- The window's frame cap paces the frames.

Movies keep their own pace. In the test build the logos and movie played at normal speed with 1,600 fps uncapped.

## Widescreen and field of view (`src/game/rotk_widescreen.cpp`)

These use the same rule as ROTK Fix's SetTransform hook. With widescreen on and a wide display, the frame is shown at
16:9:

- In a perspective projection (_34 = 1, _44 = 0) that the game built for 4:3, _11 is scaled by 4:3 / 16:9. The view
  height stays the same and more is visible at the sides. A mirrored camera keeps its sign.
- The field of view changes the tangent of the half angle. Cameras narrower than 25 degrees are left alone, and the
  view is capped at 150 degrees.

Only the renderer's copy is changed. The game's own GetTransform still reads its original matrix, just as ROTK Fix
only changes what reaches Direct3D. The HUD and menus fill the 16:9 picture, as on PC with ROTK Fix.

## SDK (ROTK patch v3, `sdk_5558\sdk_rotk_vs_tt_v3.patch`, replaces v2)

- `gpu/vertex.h/.cpp`, `xdk/d3d8.cpp`: `gpu::g_projection_hook`. SetTransform(PROJECTION) passes the renderer's copy
  to the game.
- `harness/app_headless.cpp`: the test build paces frames by `fps_cap` as the window does, so the frame-rate tests
  match real play.

## Tested (Linux test build)

- INI: 9 keys read. TargetFPS 60 gave about 57 fps through the intro, and gameplay ran at about 45 fps, which is the
  test build's CPU limit. WidescreenFix and FieldOfView 120 were logged: "3D camera 56.2 deg at aspect 1.3333;
  widescreen on, field of view 120%" against 46.8 by default.
- Helm's Deep gameplay at 16:9 had correct proportions and a wider view. The pause menu and HUD were drawn.
- Default settings: unchanged, the console's 30 fps.
- Still to check on Windows: game speed at 60+ fps over a long play (animations, timers, Gandalf's moves), and how
  the HUD looks stretched to 16:9.
