# XDK 4928 support for XboxXBE-Recomp: status and work list

Started 2026-10-12. The SDK ships support only for XDK 5849 (Splinter Cell: Double Agent). The Two Towers uses 4928.

## Symbol config (`config/game.toml`)

- I built the SDK's `xbe_symbols` on Linux and ran it with the 5849 signatures copied in as `4928`.
  It found **61 of 238** functions and **3 of 12** data addresses.
- `tools/merge_config.py` then merges in the Cxbx-Reloaded XbSymbolDatabase results (410 symbols) and maps the names that differ (`D3D_g_pDevice` → `D3D_pDevice` and so on).
  - Result: **148 names filled**, **102 still 0**.
  - About 50 of the remaining zeros are XNet/socket/XOnline functions. This game links no networking library, so they're correctly 0.
- **Cross-check:** where both sources found an address, they agree on 50 functions. One pair is swapped in the 5849 signature match: `IDirectSoundBuffer_SetMixBins` and `SetMixBinVolumes`.
  The thunk at `0x129F79` calls `CDirectSoundBuffer_SetMixBins`, so XbSymbolDatabase is correct. When the two disagree, prefer XbSymbolDatabase and confirm by checking the callee.
- **Still to fill:**
  - Data: `D3D_Device`, `D3D_DirtyFlags`, `D3D_IndexData`, `D3D_SimpleRenderStateEncode`, `DSound_StreamVtable`/`2`.
  - Functions: fences, `LightEnable`/`SetLight`, `SetPalette`, `Create`/`DeletePixelShader`, `DeleteVertexShader`, `GetRenderTarget2`, `GetDepthStencilSurface2`, `FlushVertexCache`, `PersistDisplay`, `SwitchTexture`, `D3DResource_IsBusy`/`BlockUntilNotBusy`, `D3D_InitDefaultState`, `D3DDevice_Release`, `DirectSoundCreateStream`, the `IDirectSound_*` 3D listener functions, the `crt_*` math functions, and `BinkGetSummary`/`NextFrame`/`Wait`.
  - Some of these may not exist in 4928. For example, `BeginPush`/`EndPush` and `SetVertexShaderConstant1Fast` may be later additions.
- **Ambiguous tiny thunks** (5-byte `jmp`s) need deciding by their jump target:
  - `IDirectSoundStream_SetLFO`/`SetEG`/`SetFilter` (`0x12A10B`/`10`/`15`)
  - `IDirectSoundBuffer_SetLFO`/`SetEG`/`SetFilter` (`0x129F09`/`25`/`41`)
  - `CDirectSoundStream_SetFilter`

## `layout.toml` for 4928: done

The file is `sdk_4928/xdk/4928/layout.toml`. All 47 keys the runtime uses are filled, and each one comes with the 4928 instruction that proves it.

- The `CDevice` layout moved a lot compared with 5849. For example, textures are at `0xB38` (5849: `0xF88`), the swap count at `0x2C10` (5849: `0x2478`), and the device size is `0x2C30` (5849: `0x24A0`).
- The display miniport moved to `+0x23C0`, but its own fields are unchanged.
- The DSOUND, XPP and stream/stage layouts are identical to 5849.
- **Render-state numbering:** all 23 render states that XbSymbolDatabase located have the same index in 4928 as in the runtime's 5849 enum (deferred states start at 92, complex states at 136). So the renderer will read states correctly.
- **All `[data]` addresses are filled.** The ones proven by hand are in `sdk_4928/manual.toml`, which `merge_config.py` applies last.

## How the SDK's D3D works (matters for this game)

The host replaces the state and draw calls. Resource creation, locking and the XG helpers stay recompiled, because they only manage memory.
**What The Two Towers calls that the SDK doesn't handle**, all from `.text`:

| Function | What happens if left as recompiled code | Plan |
|---|---|---|
| `D3DDevice_InsertCallback` ×3 | It is queued for the GPU and never fires. The game's GPU-timing callback (`0xDFFA0`) sets flags it checks (`[0x1BB3BC]`, `[0x4B7601]`). | Host version: run the callback straight away |
| `BeginPushBuffer` / `EndPushBuffer` / `RunPushBuffer` / `KickPushBuffer` | It records into a buffer that nothing executes | Host versions as no-ops. Draws then execute immediately during "recording", so frame N is presented in frame N, not N+1 (one frame less latency). Verify in bring-up. |
| `Begin`/`EndVisibilityTest`, `GetVisibilityTestResult` (1 each) | The result is never written, so the game waits or gets garbage | Host version: report "visible" at first. A real occlusion query can come later. |
| `D3DDevice_SetPixelShaderProgram` (1) | Its writes never reach the renderer | Host version that maps onto the SDK's pixel-shader path |
| DSOUND `PlayEx`, `PauseEx`, `SetPitch`, `SetLFO`, `SetEG`, plus the 3D setters `SetConeAngles`/`ConeOrientation`/`ConeOutsideVolume`/`SetMode`/`SetDistanceFactor`/`SetDopplerFactor`/`SetRolloffFactor` | Calls end up in APU register code | Host versions. The 3D setters are where X3DAudio 3D sound plugs in later. |
| XACT (`XACTENG`), run as recompiled code | It uses only public DSOUND functions (about half already replaced), but reads the APU frame counter directly at `0x137B30` (`[0xFE80200C]`) | A `[[hook]]` at that instruction that returns a host sample counter |

`D3DDevice_SetVerticalBlankCallback` can stay recompiled. It only stores the callback in `CDevice`, and the SDK's vertical-blank thread calls it from there, provided the layout offset is correct.
