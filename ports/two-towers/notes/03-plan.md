# Plan: getting The Two Towers running on PC

## Decision needed: base on an existing recompiler or write our own loader?

The original plan was to write our own loader. Since then, two MIT-licensed static recompilers for original Xbox games have turned up (see `02-existing-tools.md`).
One of them, **XboxXBE-Recomp**, already does most of what we planned to build ourselves, and its model (replace XDK functions found by signature, override any function in C++) matches "recompile first, decompile later".

| | A. XboxXBE-Recomp (recommended) | B. Our own native loader |
|---|---|---|
| How the game runs | Ahead-of-time x86→C++, 64-bit exe | Original x86 runs in a 32-bit process, mapped at `0x10000` |
| D3D8 / audio / input | Already exists (D3D12, XAudio2, SDL3) | Write it all, or adapt Cxbx-Reloaded code |
| What we must add | XDK 4928 signatures and layout, push-buffer recording, game hooks | Loader, kernel (118 imports), D3D8, DSOUND/XACT, XInput, `fs` handling |
| Replacing functions (fps, camera, fixes) | `X86_FUNC(sub_…)` override, `[[hook]]` | Detours on the live image |
| Risks | Young SDK; recompiler bugs (indirect calls, x87) | Much more code; tied to a 32-bit process |

**Recommendation: A.** Keep B as the fallback if the recompiler can't handle this binary.

## Phase 1: bring-up on XboxXBE-Recomp

1. **Build the toolchain on your PC:** Clang, Ninja and CMake. Build the SDK and its sample template.
2. **Create the game repo, local only and not on GitHub:**
   - Copy `templates/game` and add the SDK as `sdk/`.
   - Point `assets\` at `C:\TT\extracted`.
   - The repo holds only config, hooks and our code. Generated C++ and anything from the dump stays out of it, which their `.gitignore` already does.
3. **Add XDK 4928 support:**
   - Turn our 410 XbSymbolDatabase names into a complete `config/game.toml` `[symbols]` and `[data]` section, then run `xbe_symbols --sign` to produce `sdk/xdk/4928/signatures.toml`. Signatures store no library bytes.
   - Work out `sdk/xdk/4928/layout.toml` (D3D `CDevice` field offsets and so on) by disassembling the 4928 D3D functions, the same way their 5849 file documents each offset.
4. **Push-buffer recording:** implement `BeginPushBuffer`/`EndPushBuffer`/`RunPushBuffer`/`D3D_SetPushBufferSize` in the D3D layer as a recorded command list. Replay has to read dynamic resources at replay time.
5. **Kernel details from the analysis:**
   - Report a kernel build of 5838 or newer, so XAPI's kernel patch is skipped.
   - Make sure `fs:[0x20/0x24/0x28/0x58]` reads work.
   - Generate a per-install save-signing key.
   - Map `D:` to the dump, plus `Z:` (utility) and `U:`/`T:` folders for saves.
6. **First runs:** expect crashes at first. Compare against xemu, which runs this game, using its GDB stub, until the menus, then levels, render.

## Phase 2: the ROTK Fix-style features (decompile only what's needed)

- **Resolution and widescreen:**
  - The back buffer is fixed at 640×480 in the present parameters at `0xE07F9`.
  - The `4/3` constants are aspect-ratio candidates.
  - `640.0`/`480.0` are used by the UI layout.
  - The renderer extensions handle the render-target size.
- **High frame rate:**
  - The game runs at 30 fps, paced by a vertical-blank callback (`0xDFF80`) and `QueryPerformanceCounter`.
  - Find the game-step function and the `1/30` and `1/60` users, then decouple the simulation from rendering or scale the time step.
  - Remove the one-frame push-buffer latency.
- **Camera, bug fixes and texture packs:** override the specific functions with `X86_FUNC`, and hook the texture loading in `XGSetTextureHeader`/`D3DResource_Register`.
- **Asset formats:** document `.BFF`/`.bfa`/`.bfg`/`.bfi`/`.bfl` (nobody has published anything on them yet).
- **ROTK Fix techniques that carry over:** launcher, settings, DXVK-style Vulkan (vkd3d-proton for D3D12) and 3D audio (XAudio2 X3DAudio).

## Ground rules check

- No game code or assets in the project. The generated C++, the dump and `C:\TT\analysis` stay local.
- No DRM bypass. Nothing in this approach touches disc authentication; the game reads the owner's extracted files. Saves are signed with our own key.
- No EA branding in the launcher or splash.
- Nothing goes to GitHub until you say so.
