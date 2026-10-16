# First Windows run (2026-10-12)

## Build

- `game\build.bat` with Visual Studio Community 2026 (Clang 22.1.3, CMake, Ninja) plus Git for Windows 2.56.
- The first full build took about 6 minutes. Output: `game\out\build\release\TwoTowers.exe`.
- Game data is read in place from `C:\TT\extracted` through `game\assets\redirect.txt`. That's an SDK patch (`sdk_4928\game_files_redirect.patch`).
- `game\assets\default.xbe` is a copy used by the build. It's derived from the game, so never commit it (assets\ is ignored).

## Result

- Boots, plays the logos, legal screen and `mont.bik`, and gets into the Last Alliance prologue on D3D12. That's on the RTX 3080 Ti at 2560x1440 borderless, a steady 30 fps.
- Ran for 6.4 minutes and exited cleanly (code 0). The log shows no warnings or errors.

## Problems seen on D3D12 (not in the CPU reference renderer)

The user's screenshots show these:

1. **Spiky, exploding character geometry.** That points to skinning: bone matrices taken from vertex shader constants.
   Suspect: the D3D12 HLSL translation of indexed constant reads (`a0`/`mova`), or the constant register base (the Xbox addresses c-96..c95).
2. **Ghost copies and trails of characters**, as if the frame isn't cleared.
   Suspect: an interaction with the title-supplied `BufferSurfaces` change (d3d8 patch #8). The D3D12 backend may clear or present a different target than the one the game renders to.
3. **Very dark lighting.** Could follow from 1 or 2, or from the missing `D3D_InitDefaultState`.
4. The user reported input and sound problems, but they're not characterised yet.

## Next

- Run `game\capture.bat` (it sets `XBE_CAPTURE_DIR`), press **F3** in a glitchy moment, and close the game.
- Then analyse `game\captures\` (per-draw HLSL, constants, inputs, textures) and fix the D3D12 path.
- Confirm sound and input separately.

## Gameplay fixes after the first play (2026-10-12)

**Flicker, ghosting and the "spiky" characters were one bug.** The game supplies its own two colour buffers and keeps its own back-buffer index (`[0x4B72E4]`, flipped after every `Swap`; `0xE0490` picks the surface `0x4B72B0 - index*0x18`). The SDK's `Swap` rotated the buffers' *memory* as well. As a result:
- the world was drawn into one buffer and the characters and HUD into the other, which was never cleared, so old frames piled up;
- alternate frames showed an almost empty buffer.

Fix (SDK `d3d8.cpp`): with title-owned surfaces, `Swap` rotates the surface objects and the render target follows the new back buffer. The reference renderer now shows terrain, sky, lava and characters correctly.

**Sound effects.** The game's own voice manager (48 voices, `0x6B4550`) pauses and resumes all voices around movies and cutscenes with `PauseEx`. The host's `PauseEx` turned "pause" into "stopped" and "resume" into "play", so:
- every resume restarted all 48 voices, idle ones included, with stale data;
- every pause made busy voices look free.

Fix (SDK `dsound.cpp`): `PauseEx` now sets a pause flag only, and `Play`/`Stop` clear it. The mix is also soft-limited above 0.8 instead of hard-clipped.

Diagnostics: `DSOUND_STATS=1` logs every 2 s how many voices are playing, the buffer method calls and the mix peak.

Note: in the headless harness without `mont.bik`, the movie routine pauses all sounds and returns without resuming (`0xE8C29` → `0xE8C50`). That's a quirk of the test data, not a bug.

**Missing scenery on D3D12 (ground, sky and lava black, characters fine).** The game clears only the alpha channel (`Clear` flags `0x80`) after drawing the world and before its lighting passes. The D3D12 backend used `ClearRenderTargetView`, which always writes all four channels, so it wiped the world to black. The CPU reference renderer honours the channel mask, which is why the test runs looked right.

Fix (SDK `gpu/d3d12.cpp`, `sdk_4928/d3d12_masked_clear.patch`): a clear of only some channels is drawn as a full-screen quad with a channel write mask.

## While away (2026-10-12)

- **Build check:** `gpu/d3d12.cpp` with the masked clear compiles cleanly against the MinGW Windows headers (clang `--target=x86_64-w64-windows-gnu`, imgui v1.91.0).
- **Input:** the harness confirms pad state reaches the game (`XInputGetState` → the game's pad record at `0x6B2758`). `XInputGetCapabilities` (`0x1695FB`) ran as recompiled XAPI code and answered "not connected". Fix: `src/game/tt_input.cpp` reports a standard gamepad (`manual.toml` entry).
- **Widescreen (first step):** the game renders 4:3 only, but the host shows the picture at 16:9 on a widescreen display, so the world was stretched sideways. All camera projections go through `0x4E890` (fov, aspect 4/3, near, far). `src/game/tt_widescreen.cpp` passes 16:9 instead on a widescreen display. That keeps the vertical view and shows more at the sides; culling follows. The HUD is still stretched (next step).
- `CMakeLists.txt`: the `XBE_RENDER_THREAD=0` comment blames the threaded renderer for the spikes and ghosting. The real cause was the buffer rotation (fixed). The threaded renderer can be tried again for performance.
- **Widescreen HUD:** the HUD, menus and text are 2D quads in 640x480 screen pixels, drawn with the game's pass-through vertex programs (identified by their 4928 microcode in `tt_widescreen.cpp`). The SDK gets a generic hook (`gpu::g_screen_space_hook`, `sdk_4928/vertex_screen_space_hook.patch`), and on a widescreen display their x is pulled into the middle 4:3 of the picture. Skipped: draws as wide as the screen (fades, backgrounds), targets other than 640x480, and draws that sample a screen-sized texture (the lava heat haze uses the screen copy). The HUD sits in the centre 4:3 for now; anchoring it to the screen edges, as ROTK Fix does, is a later option.
- **60 fps:** the 30 fps lock is the game's own developer setting "GRAPHICS/Cap to 30 fps" (byte `0x4B75F1`). The frame wait `0xE00E0` waits two vertical blanks when it's set and one otherwise. Gameplay and menus switch it on through the setter `0xDFFE0`. The game's timer is real-time (frame step `0x4B8BC0` = 1/30 s capped, 1/60 s uncapped), so speed is unchanged. `src/game/tt_framerate.cpp` keeps it off, except for the movie routine's own call (`0xE8CAB`). Harness: 1740 frames in 30 s instead of 1080, and gameplay frames look normal. Setting: `fps60 = true` in `settings.toml` (false gives the original 30 fps).
- **Texture packs (D3D12):** `gpu/texture_pack.{h,cpp}` plus hooks in `gpu/d3d12.cpp` (`sdk_4928/texture_packs.patch`).
  - With `texture_dump = true` in `settings.toml`, every plain 2D texture is written once to `textures\dump\<hash>.dds` as the renderer decoded it (BGRA8 or BC1-3, all mip levels).
  - Any `<hash>.dds` (or `<hash>_anything.dds`) under `textures\` (except `dump\`) is drawn instead. It can be any size, with its own mip chain, in BC1/BC2/BC3/BC7 or 32-bit BGRA/RGBA.
  - The hash covers the texture's bytes, format, size and level count (plus the palette for P8), so a name stays the same between runs and installs.
  - The DDS writer and reader are unit-tested here; PIL opens the written files.
- **Other levels in the harness:** a cloud-only test file (`src/game/tt_harness_level.cpp`, not copied to the PC) boots another level in the prologue's place (`TT_LEVEL=01WEAT1`). 01WEAT1 (Weathertop) and 17TOP1 (Helm's Deep) load and play. 21ORTH2 plays its Saruman cutscene. The level slot holds 7 characters, so 16ROHAN3 couldn't be tried this way.
- **Level sweep (harness, 50 s each, booted in the prologue's place):** 00ISUL1, 01WEAT1, 02GATE2, 04BALIN1, 13AMON1, 16ROHAN1, 16ROHAN2, 16ROHAN3, 17TOP1, 18HOLE1, 20YARD1, 21ORTH1 and 21ORTH2 all load and run with no crash or trap. Their gameplay frames look right. Not tried: 14FANG2 (its stream wave bank is 562 MB). One small oddity: a translucent rectangle in the sky in one 16ROHAN1 cutscene frame.
- **14FANG2 (Fangorn)** also loads and plays. It was tested with Amon Hen's stream bank in place of its own 562 MB one, which is over the copy limit.
  - A *missing* stream wave bank crashes the host (a segfault, not a guest trap). That doesn't matter for a full dump, but it's worth hardening later.
- **Translucent dithered rectangles** (seen in 16ROHAN1 and 14FANG2 harness frames) don't reproduce reliably: they vanished when only the one frame was drawn by the reference renderer. Likely a harness artifact. The reference renderer only draws chosen frames, so a texture the game renders on other frames is stale there. Check on D3D12 before chasing it.

## Second round on Windows (2026-10-12, evening)

- Confirmed on the PC:
  - the scenery is back (masked clear);
  - 60 fps, smooth;
  - widescreen 3D;
  - pause and resume with working sound;
  - F1 HUD toggle;
  - texture dump (about 200 DDS files in `textures\dump`);
  - copyright text centred.
- **HUD at the screen edges** (`hud_edges`, on by default):
  - Pieces within 15% of a side keep their distance to that side.
  - A draw touching the previous one goes with it, so words and bars stay whole.
  - This only applies while a 3D program has drawn in the last 100 ms. 2D programs are recognised by their microcode pattern (only mov vN to outputs, v0 to oPos first), so fades don't count as 3D.
  - The log names the program that turned the edge layout on.
- **FOV and HUD toggle** (`src/game/tt_extras.cpp`):
  - `fov_percent` scales the level's "CAMERA/Desired FOV" (`[[0x43B164]+0x18]+0x14`, 45 degrees in the prologue), so gameplay changes and cutscenes don't.
  - F1 toggles "GRAPHICS/Draw HUD" (`0x4A8C74`).
- **Shader cache** (`gpu/d3d12.cpp`, `sdk_4928/d3d12_shader_cache.patch`): compiled DXBC is kept in `shadercache\` beside the exe. This targets the stall when a cutscene ends. To be confirmed on a second launch.
- **HUD edges, final rule:** a frame counts as "world" when it has 100 or more non-2D draws. The intro movie quad uses a 43-instruction program, and the copyright screen once had 30 such draws; gameplay has 900 or more. Edges switch on after 500 ms of world frames and off after 1.5 s without them. Confirmed on the PC: the copyright text stays put.
- **Stalls (`XBE_PERF`; `capture.bat` sets it):** with the shader cache warm, the only frame over 50 ms is the first gameplay frame after the intro movie. It takes about 60 ms: 17 pipelines (8 ms), 106 new textures (5 ms upload) and about 40 ms of resource creation inside draw. Every shader comes from the cache.
- **Threaded renderer** (`XBE_RENDER_THREAD=1`, `capture_threaded.bat`): it looks the same and gives the same stall, and both run at 60 fps with about 3 ms of host renderer time a frame. The default stays off (`CMakeLists.txt`).
- **Release logs** keep only error and report lines, so diagnostics use `LOGR`.
- **Copying files to the PC:** re-committing a file from the same staging path sometimes delivered an older snapshot. Stage each update under a fresh path and read it back after committing.

## Faithful by default (2026-10-12)

Plan: get the original game working as the Xbox ran it first; enhancements come after and stay opt-in. Every enhancement is now off by default, each with one switch in `settings.toml`:

| Setting | Faithful (default) | Enhancement |
|---|---|---|
| `fps60` | `false`: the game's own 30 fps cap | `true`: 60 fps (the game's timer is real time) |
| `widescreen` | `false`: 4:3 picture with side bars, original projection and HUD | `true`: 16:9 world, HUD kept in shape |
| `hud_edges` | (only with `widescreen`) | `true`: HUD at the 16:9 edges, `false`: centred 4:3 |
| `fov_percent` | `100` | e.g. `115` for a wider gameplay camera |
| `anisotropic_filtering` | `0`: as the game asks | `16` |
| `texture_dump` | `false` | `true`: write textures for packs |
| F1 HUD toggle, texture pack files | inert unless used | — |

`widescreen` is an SDK setting (`app::Settings`). With it off, `app::Widescreen()` is false, so the frame is shown 4:3 and the game hooks (projection, HUD squeeze) stay idle. `render_percent` (internal resolution) is left at 100: it changes sharpness, not behaviour.

## First play-through, faithful settings (2026-10-12 evening)

- Played the prologue and Weathertop (01WEAT1): the upgrade screen, saving to the hard disk, the tutorial, movies and in-engine cutscenes. No crash; clean exit after 15 minutes.
- The stall after the first cutscene is gone (shader cache). In the log, the only real long frames are two at 585 s, when Weathertop first used 7 new shaders (compile 146 + 97 ms). They're cached for later runs.
- Every other long frame (116 to 550 ms) lines up with an F3 capture; writing the capture pauses the game.
- Dark Weathertop cutscenes (the ringwraith fight): the frame is drawn the same way by D3D12 and by the CPU reference renderer, which are independent implementations, and both come out equally dark (mean luma 14-21 against 25-30 in gameplay). So the darkness comes from the game's own rendering, not the port.
  - The scene fades the world before the characters with a full-screen `dst *= src alpha` pass, blends in the previous frame (motion blur), and adds letterbox bars.
  - On the Xbox it was seen through a TV (limited range, a brighter black level), so it may have looked brighter there. A side-by-side with xemu would settle it.
  - A later option could use the game's own "PICTURE CONTROL/Brightness" setting (`0x1BB394`).
