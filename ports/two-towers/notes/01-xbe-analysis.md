# Default.xbe analysis: The Two Towers (Xbox, NTSC-US)

Analysed 2026-10-12 from the owner's dump (`C:\TT\extracted\Default.xbe`, 2,224,128 bytes).
The raw outputs (JSON/CSV) are in `C:\TT\analysis\`. They are derived from the game binary, so **never commit them**.
The tools that produced them are in `tools/` and contain no game data.

## Header

| | |
|---|---|
| Kind | Retail XBE (entry/thunk keys = retail) |
| Base address | `0x00010000`, image size `0x9094E0` (~9.0 MB, ends at `0x9194E0`) |
| Entry point | `0x000FB71D` (in `.text`, XAPI startup) |
| Kernel thunk table | `0x0016FFA0` (start of `.rdata`) |
| Title | "The Two Towers(tm)", Title ID `0x45410010` (EA-016), version 1 |
| Region | `0x1` = North America only |
| Allowed media | `0x2` = DVD-X2 (Xbox game disc) |
| Debug path | `C:\Fusion\xbox\Game\Burn\FusionXbox.exe` |
| TLS | Index at `0x8D69B8`, 12 bytes zero-fill, no TLS data, no callbacks |
| Stack commit | 64 KB |
| Built | Certificate date 2002-11-08 |

**Library versions:** XAPILIB, D3D8, XGRAPHC, DSOUND, XACTENG, XBOXKRNL and LIBCMT are all 1.0.4928, QFE 1, approved.
There is also `CalcSig` (save-game signature helper).

## Correction to the earlier notes

**118 kernel imports, not 454.** The Xbox kernel exports only about 366 ordinals, so 454 was impossible.
All 118 are used: 95 are called, and 23 are kernel variables (for example `KeTickCount`, `LaunchDataPage`, `XboxKrnlVersion`) that are read rather than called.

- 50 are used only by game/XAPI/CRT code in `.text`.
- 33 are used by both `.text` and the XDK libraries.
- 35 are used **only by the XDK libraries** (interrupts, DPCs, `Io*`, `HalReadWritePCISpace`, `MmClaimGpuInstanceMemory`, `Av*` and so on). These go away once D3D, DSOUND, XACT and the input stack are replaced.

The full list is in `C:\TT\analysis\kernel_imports.csv`.

## Sections (40)

| Group | Sections | Notes |
|---|---|---|
| Game + XAPI + CRT | `.text` (1.01 MB) | about 287k reachable instructions. XAPI functions sit near the end (`0xF8000`–`0x100000`). |
| Graphics libs | `D3D` (75 KB), `XGRPH` | XDK D3D8 4928 and XGraphics |
| Audio libs | `DSOUND`, `XACTENG`, `WMADEC`, `DOLBY` (DSP data), `DSPImage` (inserted file) | |
| Input / devices | `XPP` | XAPI peripheral stack: XInput, USB host driver, memory units |
| Bink | `BINK` plus 14 pixel-format sections, `BINKDATA` | RAD Bink player, statically linked |
| Data | `.rdata`, `.data` (vsize 7.3 MB, mostly BSS) | |
| Inserted files | `$$XTIMAGE`, `$$XSIMAGE` (title/save icons), `RingPrp`, `RingTex`, `RingGTex`, `LdTxt{De,Es,Fr,It,Nl,Sv,Us}` | Loaded on demand with `XGetSectionHandle`/`XLoadSection` |

Every Xbox section has the executable flag set, so data sections must not be disassembled linearly.
The tools only follow real control flow.

## Code inventory

- 7,462 functions were found by recursive descent from the entry point, the XDK symbols, and code pointers in data (vtables, callbacks).
- 21,096 direct calls and 894 indirect calls.
- 10,242 switch-table targets.

## XDK library identification (Cxbx-Reloaded XbSymbolDatabase)

410 symbols were matched: D3D8 156, DSOUND 170, XAPILIB 58, XACTENG 16, XGRAPHC 5.
**Not covered:** LIBCMT, Bink, WMADEC. These are plain x86 and can run as they are, apart from the kernel calls they make.

**The game's actual API surface:** 131 library symbols are referenced from game code (`C:\TT\analysis\hle_surface.csv`).
- D3D8: 61, of which `SetRenderState_Simple` has 271 call sites, `SetTexture` 99, `SetVertexShaderConstant1` 87, `DrawVerticesUP` 45.
- XAPI: 42. DSOUND: 14. XACT: 9. XGRAPHC: 5.

## Direct hardware access

**Game code (`.text`) never touches hardware.** It has no NV2A, APU, AC97, USB or NIC register access, and no `in`/`out` instructions.
All hardware access lives in the libraries that are going to be replaced:

- **D3D:** NV2A PFIFO (`0xFD003244`, `0xFD0032A4`/`A8`), PCRTC (`0xFD600140`), `0xFD001804`, `0xFD009140`. Also `wbinvd` and port I/O in `CMiniport_InitHardware`.
- **DSOUND:** about 84 APU (`0xFE80xxxx`) and 18 AC97 accesses. **XACTENG:** 1 APU access.
- **XPP:** the USB0 OHCI base `0xFED00000`, from the USB host driver.

Privileged instructions in `.text`:
- `rdtsc` ×7: a GPU/CPU profiler at `0xE5FD0`–`0xE69B0`, plus `QueryPerformanceCounter`.
- `cpuid` ×1: CRT CPU detection.
- One `cli`/`sti` pair in XAPI's kernel-bug patch at `0xFEE2C`. It reads `XboxKrnlVersion.Build` and only patches kernels 3944, 4034, 4039 and 4817.
  **The host must report a newer kernel build (for example 5838) so this patch never runs.**

## Inlined D3D and push buffers

- Game code never reads `D3D__pDevice` and never calls `MakeSpace`/`MakeRequestedSpace`, so **it does not write push-buffer bytes inline**.
- It does write the D3D deferred-state arrays directly (`D3D_g_DeferredRenderState` ×14, `D3D_g_DeferredTextureState` ×27).
  This is normal XDK inlining. The replacement D3D has to read these arrays at draw time, which Cxbx and the recompilers already do.
- **Whole-frame push-buffer recording:**
  - `sub_000E01F0` (frame begin) inserts a GPU callback, runs **last frame's** recorded push buffer with `RunPushBuffer(pb[idx])`, kicks the GPU, flips `idx = 1-idx`, and starts `BeginPushBuffer(pb[idx])`.
  - `sub_000E0280` (frame end) ends the recording with `EndPushBuffer` and tracks the high-water size.
  - The two push buffers are 3 MB each, created at `0xE0890` and stored at `0x4B7278`/`0x4B727C`.

  **Consequence:** every frame's draws are recorded and replayed one frame later. The game only builds these buffers through D3D calls and never inspects them, so a replacement D3D can record its **own high-level command list** in place of NV2A bytes. No GPU-command interpreter is needed.
  **Watch for:** resources that change between record and replay (dynamic vertex buffers, locked textures). Replay has to read them at replay time. The one-frame latency is also worth removing later.

## Display and timing (useful for the widescreen and high-fps work)

`Direct3D_CreateDevice` is called at `0xE0862` with these presentation parameters:
- **640×480** back buffer, format `0x12`, 1 back buffer, automatic **D24S8** depth.
- **`PresentationInterval = IMMEDIATE` (`0x80000000`)**: the GPU doesn't wait for vsync.
- The game supplies its own two colour surfaces (`0x4B7298`, `0x4B72B0`) and depth surface (`0x4B7280`).

Frame pacing:
- The game paces itself through a vertical-blank callback (`SetVerticalBlankCallback(0xDFF80)` at `0xE08BF`) plus `QueryPerformanceCounter`, which has 12 call sites around `0xE3297`–`0xF0C3B`.
- `QueryPerformanceFrequency` is hard-coded to **733,333,333 Hz**.
- `Swap` is called at `0xE01C2`, `0xE01F1`, `0xE0919` and `0xE0949`.

Constants to inspect later:
- `1/60` (`0x9C3A3`, `0xD013E`, `0xE7A3F`, `0xE7A4E`)
- `30.0` and `1/30` (`0x24DDA`, `0x1706E0`)
- `640.0` and `480.0` (about 33 sites each, mostly UI around `0x21F00`–`0x22900`)
- `4/3` (8 sites, including `0x450B4`, `0xCB080`, `0xE7BE9`): **aspect-ratio candidates for widescreen**

xemu's compatibility report says the game runs at a locked 30 FPS.

## The `fs:` segment

On Xbox, `fs` points at the KPCR. On Windows it points at the TEB. The two layouts agree only at offsets `0` and `4`.

| Offset | Sites | Meaning on Xbox | Windows equivalent |
|---|---|---|---|
| `fs:[0]` | 263 | SEH chain | Same |
| `fs:[4]` | 5 | Stack base | Same |
| `fs:[0x20]` | 8 | KPRCB pointer | Differs |
| `fs:[0x24]` | 10 | IRQL | Differs |
| `fs:[0x28]` | 7 | Current KTHREAD (used for TLS and `GetLastError`) | Differs |
| `fs:[0x58]` | 2 | Used by DSOUND | Differs |

The 27 sites that differ are almost all inside XAPI, DSOUND and XACT.

## Other things the port has to handle

- **Save signing:** `XCalculateSignatureBegin` (CalcSig) reads `XboxSignatureKey`, `XboxHDKey` and `XboxAlternateSignatureKeys`.
  The PC build should sign its own saves with a key generated per install. This is not a DRM bypass: we sign our own files and don't use or extract real console keys.
- **Files:** the game reads `D:\` (the dump folder) and uses the utility drive (`XMountUtilityDrive`, `Z:`) and memory units (`XUnmountMU`).
- **Input:** `XInitDevices`/`XGetDevices`/`XGetDeviceChanges`/`XInputOpen`/`XInputGetState`/`XInputSetState`. The USB host driver in XPP must never run.
- **Exit and reboot:** `XLaunchNewImageA` ×3, `XapiBootToDash` ×4, `HalReturnToFirmware`.
