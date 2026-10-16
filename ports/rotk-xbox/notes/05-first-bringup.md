# First bring-up in the headless harness (2026-10-12)

**Result: GO.** In the SDK's Linux headless harness, the recompiled `Default.xbe` does all of this without crashing:
- boots and runs XAPI startup
- shows the loading screen and the legal screen
- plays the `mont.bik` intro movie (about 3 minutes)
- loads `Worlds\00isul1` and runs the Last Alliance prologue with its HUD

The frames come from the SDK's CPU reference renderer. They're not yet D3D12 on Windows.

## How the build works

- `game/` is the game repo, made from the SDK's `templates/game`, with `sdk/` as a patched copy of XboxXBE-Recomp.
  It's local only. Nothing from `generated/` or `assets/` is ever kept in it.
- `config/make_config.sh` builds `config/game.toml` from four inputs:
  - the `xbe_symbols` output (5849 signatures)
  - the XbSymbolDatabase symbols
  - `sdk_4928/manual.toml` (hand-proven addresses)
  - `config/hooks.toml`
- `xbe_recomp` generates 8,701 functions (about 983k lines of C++). The generated code compiles in a few minutes.

## Problems found and fixed, in boot order

| # | What happened | Cause | Fix |
|---|---|---|---|
| 1 | Crash in D3D `0x116F20` from the game's frame start `0xE01E0` | The original `BeginPushBuffer` (frame recording) touched device state the host owns | `tt_d3d.cpp`: Begin/End/Run/KickPushBuffer are no-ops (draws go straight to the renderer); `InsertCallback` runs the callback immediately |
| 2 | Crash at XACT `0x137B30` | It read the APU sample counter `[0xFE80200C]` | `[[hook]]` `TT_ApuSampleCounter`: 48 kHz samples since start |
| 3 | Divide error at `0xF0D55` after "missing U:\" | XAPI's disc authentication check (`0xFE36E`, SCSI page 0x3E) fails on PC; the error path skipped the `T:`/`U:`/`Z:` setup | `[[hook]]` `TT_DiscCheckFailed` at `0xFE4C4` continues XAPI startup at `0xFE4C9` |
| 4 | Trap: call to `0xFFFF0021` | `IDirectSound_CreateSoundStream` forwarded to `DirectSoundCreateStream`, which isn't linked in this game | SDK `dsound.cpp`: shared `NewStream()` used by both entry points |
| 5 | Crash in D3D `0x116F20` from `0x118300` | `D3DDevice_DrawIndexedVerticesUP` (5 args) has no host version | SDK `d3d8.cpp`: new host version using the UP vertex ring and an indexed draw |
| 6 | Crash in `CMcpxStream_GetStatus` | `CDirectSoundStream_GetStatus` was unnamed (XbSymbolDatabase calls it `…__r2`) | `manual.toml`: `0x129802` |
| 7 | Trap: call to `0xFFFF0002` (`BinkWait`) | The game paces movies itself and doesn't link `BinkWait` | SDK `bink.cpp`: the movie hold is used only when `HAS_BinkWait` |
| 8 | 3D frames never shown (screen frozen on the last 2D image) | `CreateDevice` ignored the title's own `BufferSurfaces`/`DepthStencilSurface` (present params `+0x34`/`+0x40`) | SDK `d3d8.cpp`: the device uses the title's surfaces when it supplies them |

Also added for this game, so no console sound code runs:
- `PlayEx`, `PauseEx`, `SetPitch`, `SetLFO`, `SetEG` and the 3D cone/mode/distance/doppler/rolloff setters
- the stream `SetPitch`/`SetLFO`/`SetEG`/`SetOutputBuffer` methods
- `IDirectSound_CreateSoundStream`

The SDK changes are kept as patches in `sdk_4928/*.patch`, so they can be offered upstream later.

## Known gaps

- The game never calls `D3D_InitDefaultState`, and the address is still unknown. The SDK logs "default states were not applied". Nothing looks wrong yet.
- The 3D sound settings are accepted but not applied. That's where X3DAudio fits later.
- The visibility test always reports "visible" (640×480 pixels).
- About 50 names remain at 0. Most are networking functions this game doesn't contain, and the rest are unused.
- Input isn't verified yet. START/A presses from a pad script didn't skip the intro movie.
- The movie phase is slow in the harness. That's the CPU reference renderer, not the game.
