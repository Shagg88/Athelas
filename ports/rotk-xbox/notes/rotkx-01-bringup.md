# ROTK Xbox port, 01: XDK 5558 support and first bring-up (2026-10-13, overnight)

The Return of the King, Xbox, NTSC-US (title 4541003E), on XboxXBE-Recomp.

**Result:** in the SDK's Linux headless harness the recompiled `default.xbe` boots, plays the THX/New Line/LOTR
logos and the Helm's Deep intro movie with correct pictures, loads Helm's Deep and runs gameplay: Gandalf, the
Uruk-hai, fire, smoke, sparks and the HUD all draw correctly (`helms_deep_9300.png`). It has not been built or
run on Windows yet; that is the next step.

## Baseline

- SDK: upstream XboxXBE-Recomp `65e959d4` plus The Two Towers' complete patch
  `C:\TT\project\sdk_4928\sdk_vs_upstream_65e959d4.patch`. Rebased 2026-10-13 12:30 onto its newer version
  (224,046 bytes, md5 `c92b2f7b...`: static C runtime, settings list in the harness, shader changes).
- ROTK's own SDK changes are in `sdk_5558\sdk_rotk_vs_tt_v2.patch` (7 files), applied on top of that baseline.
  They applied cleanly over the new Two Towers patch.
  Nothing in `C:\TT` was changed.

## The executable

- Libraries all XDK 5558: XAPILIB, LIBC, D3D8, D3DX8, XGRAPHC, DSOUND, XBOXKRNL. No XACT, DirectMusic, WMA or Bink.
- 113 kernel imports, 5,357 functions found by the scan (8,564 after the recompiler's gap fill).
- Game code touches no hardware. One `wbinvd` (0x11A94) and the XAPI kernel-patch `cli/sti` are already handled
  by the recompiler (the host reports kernel 5838).
- The executable has an `.XTLID` section (XDK export ids and addresses), which named several functions directly.
- Game API surface: 160 library functions called from game code, all named (`analysis/surface.txt` locally).

## XDK 5558 support

- `sdk\xdk\5558\layout.toml`: all 47 keys, each with the instruction that proves it. 5558 is close to 5849: most
  CDevice fields are 4 to 0x10 lower (textures 0xF78, swap count 0x2468, miniport at +0x1C18, size 0x2490).
  Render-state numbering matches the runtime's enum (deferred from 92, complex from 136).
- `sdk\xdk\5558\signatures.toml`: signed from the completed ROTK config with `xbe_symbols --sign`
  (124 functions, 12 data). From the image alone the SDK now finds the same config ROTK uses.
- `sdk_5558\manual.toml`: the hand-proven extras (D3D_DirtyFlags 0x13A428, D3D_IndexData 0x13A424, BeginPush,
  XInputGetCapabilities, and the push-buffer calls the port replaces).

## Problems found and fixed, in boot order

| # | What happened | Cause | Fix |
|---|---|---|---|
| 1 | Boot asked to launch the dashboard, then a black screen | XAPI's disc authentication (0x121C84, SCSI 0x4D014) fails on PC and XapiInitProcess boots to the dashboard | `[[hook]]` at 0x121EBE continues at 0x121EC3 (`rotk_hooks.cpp`), as TT does |
| 2 | Stuck after opening `LoadOnce.scx` | `NtQueryInformationFile` class 6 (FileInternalInformation) unsupported | SDK `files.cpp`: returns a per-file index |
| 3 | Crash in the loader callback 0xF29D0 | The game queues a read with an event and also handles a synchronous completion; the SDK completed synchronously *and* signalled, so the request completed twice | SDK `files.cpp`: handles opened without FILE_SYNCHRONOUS_IO report STATUS_PENDING, as the console's drivers always queue |
| 4 | Characters drawn as rainbow shapes | Skinning programs are loaded at instruction 12 (`LoadVertexShader`) and chosen per mesh (`SelectVertexShader`); the SDK did not support either | SDK `d3d8.cpp`: vertex program memory, Load/Select build a shader from the declaration and the loaded program |
| 5 | Particle quads dropped (`EndPush primitive 8 dropped`) | The game pushes raw NV2A commands with its own vertex formats (0x1760..) | SDK `d3d8.cpp` EndPush: draws with the formats the title pushed |
| 6 | Movies drawn as offset coloured layers (harness only) | The CPU reference renderer ignored TEXCOORDINDEX; the movie samples Y with set 1 and U/V with set 0 | `harness/reference_renderer.cpp`. The D3D12 path already honoured it |

## Notes for next steps

- **Movies:** EA `.vid` with DivX frames, decoded by game code into three LIN_AL8 planes (Y 768x544, U/V
  384x272, 32/16-pixel borders) and drawn with a YUV pixel shader. No SDK video support needed.
- **RunPushBuffer** is linked and replaced with a reporting no-op; it was never called in testing.
- **Input:** the pad is opened (port 0). Pad-script presses didn't skip the intro movie in the harness; check on
  Windows whether movies can be skipped (TT saw the same in its harness).
- **Missing movie hangs:** a run without `HELMFMV1.vid` stalls after trying `blakfmv1.vid`, so a full dump is needed.
- **Frame timing / 60 fps:** not looked at yet. ROTK Fix found the PC version uses delta time with a 0.1 s clamp;
  expect the same here.
- Enhancements (widescreen, fps, FOV, HUD edges, texture packs) come after the faithful game works on Windows,
  all off by default, as in TT.

## Building on Windows

Same as The Two Towers: run `rotkxbox\build.bat` (Visual Studio 2022/2026 with Clang and CMake tools).
`config\game.toml` is already written, so the build goes straight to recompiling. The game data is read from
`C:\ROTKX\extracted` through `assets\redirect.txt`; `assets\default.xbe` is a copy the build uses (never commit
it). The exe is `rotkxbox\out\build\release\ROTKXbox.exe`. `capture.bat` runs it with F3 captures and timing logs.
