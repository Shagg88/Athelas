# Existing original-Xbox recomp and decomp tools (surveyed 2026-10-12)

## Static recompilers (x86 XBE to native PC executable)

These are the closest match to "recompile first, decompile later". Both are MIT-licensed, and both keep game code and generated code out of the repository. Users build from their own dump, which is the same model as our ground rules.

### TGP482 / XboxXBE-Recomp: best fit

https://github.com/TGP482/XboxXBE-Recomp

Built for the Splinter Cell: Double Agent port (https://github.com/TGP482/SCDA-XBERecomp).

What it does:
- Translates x86 to C++ ahead of time and produces a 64-bit Windows executable.
- Finds XDK functions with **signatures** (hashes with addresses masked, no library bytes stored) and replaces them at the API level:
  - D3D8 on a native **D3D12** renderer, including vertex programs and pixel combiners translated to HLSL, plus renderer extension hooks.
  - DirectSound on **XAudio2**, including ADPCM, mix bins and the effects image.
  - XAPI, XNet and the kernel.
  - **SDL3** controllers with rumble.
- `X86_FUNC(sub_XXXXXXXX)` replaces any recompiled function, and `__imp__sub_…` calls the original. This is exactly the "decompile only what we change" workflow.
- `[[hook]]` runs code at any instruction.
- `xbe2pe.py` writes a flat PE file for Ghidra.
- Also handles x87 precision, the 733 MHz timer, and the Bink-faster-than-30-fps fix.
- Has a headless Linux test harness.

**Gaps for The Two Towers:**
1. It only has signatures and a D3D/device data layout for **XDK 5849**. We need `xdk/4928/signatures.toml` and `layout.toml`.
   It has a `--sign` mode that generates signatures from a title whose config is complete. Our XbSymbolDatabase results (410 named symbols) give us most of that config.
2. **No push-buffer recording support** (`BeginPushBuffer`/`EndPushBuffer`/`RunPushBuffer`). Our game records every frame this way, so this needs adding. A high-level command-list recorder is the cleanest way.
3. The project is young (78 commits, 1 star). Expect to fix things in the SDK itself.

### sp00nznet / xboxrecomp: broader but less mature

https://github.com/sp00nznet/xboxrecomp

- Python lifter (Capstone) that translates x86 to C. Each function becomes `void f(void)`, with guest registers as thread-local globals and a simulated guest stack.
- Runtime libraries:
  - Kernel.
  - D3D8 on D3D11, with an OpenGL backend for Linux/macOS.
  - DSOUND on a software mixer.
  - An APU and NV2A register layer taken from xemu (**LGPL-2.1**). It includes NV2A push-buffer parsing and replay.
- Mirrors Xbox memory with `CreateFileMapping`. Has a conformance suite that compares lifted C against real x86 execution.

Status:
- **No game is playable yet.** Its own README retired the "Burnout 3 playable" claim, and Burnout 3 now reaches engine init.
- The Xbox Dashboard port renders an orb but not the UI.
- Wreckless and Blood Wake are at early bring-up.

Useful reference code, especially the conformance tests and push-buffer replay, but riskier as a base.

## Emulators (behaviour references, not bases)

- **xemu** (https://xemu.app): low-level emulation, the most accurate. It rates **The Two Towers as Playable** with a locked 30 FPS, and only some cutscene audio/video desync (tested April 2025).
  Use it as **ground truth**: compare behaviour, capture frames, and use its GDB stub to trace a function before replacing it.
- **Cxbx-Reloaded**: high-level emulation. We already use its XbSymbolDatabase. Its D3D8 and DSOUND patches are a reference for 4928-era behaviour.
- MattKC's **Reburn 3** plan (Burnout 3) starts from Cxbx-Reloaded and injects PC code into the running game, then decompiles step by step.
  It's the "native execution" alternative to a static recompiler (https://forum.mattkc.com/viewtopic.php?p=871).

## Analysis tools

- **ghidra-xbe** (https://github.com/XboxDev/ghidra-xbe): Ghidra loader for XBE files that names XDK functions through XbSymbolDatabase. Use it for the decompile phase.
- **XbSymbolDatabase** (https://github.com/Cxbx-Reloaded/XbSymbolDatabase): already built and used. Its CLI produced our 410 symbols.

## Decompilation projects (reference for later)

- **Jet Set Radio Future decomp** (https://codeberg.org/KeybadeBlox/JSRF-Decompilation): delinks the XBE into object files, then does matching decompilation with Visual C++ 7.0 and re-links with `cxbe` from nxdk.
  It's a full matching decomp, which is more than we need, but it shows how delinking works for XBEs. **It bans contributions written with LLMs**, so we shouldn't contribute to it from here.

## Engine-specific

Searches found no prior work on The Two Towers' Fusion formats (`.bfa`/`.bfg`/`.bfi`/`.bfl`/`.BFF`). We'll be the first to document them.
Stormfront's later **Forgotten Realms: Demon Stone** (2004) also had a PC release. If it's the same Fusion engine, it's a legitimate reference for how the engine is structured. We'd use it only to understand the engine, never copy its code.
