# FOTR Xbox port, 01: first recompile and bring-up (2026-10-20, overnight)

## The executable
- `Default.xbe`, title "The Fellowship of the Ring", title ID 0x56550004, region 1 (North America).
- Libraries, all XDK 1.0.4831: XAPILIB, D3DX8, XMV, DSOUND, LIBCMT, XBOXKRNL, XGRAPHCL, DMUSICLT, D3D8LTCG.
  - D3D8LTCG: the link-time-code-generated Direct3D; many functions take arguments in registers (see `[abi]`).
  - XMV: Microsoft's movie player (movies are .xmv), decoded by game-side library code.
  - DMUSICLT: DirectMusic (music .xpa files, Scenes/Media/Music1..3.xpa).
- Sections: .text 0x11000 (1.2 MB), D3DX, XMV, DSOUND, D3D, XGRPH, XPP, .rdata, .data, DOLBY, $$XT*.
- Game data: Scenes/<area>/<scene>/Media/{lua.bin, Resources.xpb, Sound.xpa, Textures.xpr, Dialogs_English.xpa} (Lua
  scripting), dialog/*.bin, shaders/shaders.bin, fonts/*.ttf, XboxMedia/*.xpr, Movies/*.xmv.

## Recompile
- 10,813 functions, 482,657 instructions. 5,410 overlapping decodes and junk instructions (arpl, ins/outs, les...) in
  the XMV (0x144-0x16C), DSOUND (0x17A-0x18D) and XGRPH (0x1B2-0x1B3) sections: library data decoded as code by the gap
  fill. Harmless unless executed; mark as `data_ranges` if they get in the way.

## Bring-up, in the order the game hit things (2026-10-20 night, Linux harness)

| # | What happened | Cause | Fix |
|---|---|---|---|
| 1 | "launch of  refused" (boot to dashboard) | XAPI disc authentication (0x75A28, SCSI 0x4D014) fails on PC | `[[hook]]` at 0x75B7E continues at 0x75B83 (`fotr_hooks.cpp`), as TT/ROTK |
| 2 | "D3D_DirtyFlags / SimpleRenderStateEncode unknown" | not in the signatures | `xdk_4831/manual.toml`: 0x19E024 (SetTexture ors 0x4800), 0x1CA558 (same bytes as TT's table; the game's SetRenderState 0xCF495 indexes it), IndexData 0x19E2C4 |
| 3 | Crash in 0x18FA60 under D3D_CommonSetRenderTarget 0x193AF0 | LTCG: the game calls SetRenderTarget's body directly (19 sites); it ran natively on the host's device | SDK: host `D3D_CommonSetRenderTarget` (3 stack args, ret 0xC) shares SetRenderTarget's body |
| 4 | Crash in 0x18FA60 under CDevice_SetStateVB 0x199C70 | the game flushes state itself before its own pushed draws; reads device +0x380 the host never fills | `fotr_d3d.cpp`: SetStateVB/SetStateUP do nothing (the host applies state at each draw) |
| 5 | Crash in IDirectSoundBuffer_AddRef 0x16ECFF | the native AddRef on a host buffer | SDK: host `IDirectSoundBuffer_AddRef` |
| 6 | Crash in D3D_InternalRelease (host Release called it with a stack argument) | LTCG: it takes the resource in esi | `[abi] D3D_InternalRelease = ["esi"]` (manual) |
| 7 | Crash in IDirectSoundBuffer_SetPitch (DirectMusic's voices) | not named | names the SDK knows filled from XbSymbolDatabase automatically (`merge_ltcg.py` with `SDK_DIR`): SetPitch/LFO/EG, cone, mode, CreateSoundStream... |
| 8 | Crash in IDirectSound_SetAllParameters 0x171B71 | the listener set all at once (not in the SDK) | SDK: host `IDirectSound_SetAllParameters` (DS3DLISTENER) |
| 9 | "DSound_StreamVtable2 unknown" | not found | 0x1BEBDC: CDirectSoundStream constructor 0x171AB4 stores it at +4 |
| 10 | Runs, only sky and terrain draw (6 draws a frame) | the game pushes its own draws (LTCG): Begin, ARRAY_ELEMENT16/32, DRAW_ARRAYS, INLINE_ARRAY, End, and loads vertex shader constants (0x1EA4 + 0x0B80) | SDK: the push walk runs those draws with the host's state and loads the constants |
| 11 | Bag End draws but in wrong colours | RunPushBuffer 0x190610 (push buffer on the stack, fixup in eax; 31 call sites) ran natively: the console jumps to the buffer, the host never saw it | `fotr_d3d.cpp`: the buffer's commands (fixups applied) are copied into the device's push buffer for the walk |
| 12 | "pushed inline vertices dropped" | inline vertices carry every input the shader reads, packed in register order | SDK: the walk packs a copy of the shader's inputs for inline draws |
| 13 | Common_interior: the Lua engine's protected call trapped (longjmp with no setjmp) | the CRT's `_setjmp3` (0x7A4C8) not declared | `[xbe] setjmp = [0x0007A4C8]` (`make_config.sh`) |
| 14 | Movies black (the decoder's frames fine: YUY2 640x480, two buffers 0x81068000/0x810FE000) | the movie quad's shader (B0062171, from shaders.bin) takes the screen centre and scale from c-95, set by the game's inlined SetVertexShaderConstant (0xCE0B0: pushes 0x1EA4 + 0x0B80) just before DrawVerticesUP; the walk ran only at the next push request, after the draw, so every corner landed at the screen centre | SDK: every draw walks what was pushed first (`SyncDeferred`) |
| 15 | Harness only: black dots over the movies | the reference rasterizer dropped pixels whose interpolated depth was a rounding error past the far plane (the quad sits on it) | depth within 1e-6 of the range is clamped |
| 16 | "pushed inline vertices dropped" (24-byte particle vertices read as a 36-byte mesh layout); Bree's HUD showed "NOT SET" and its lighting was off | the game pushes its own Begin / inline vertices / End (0xF69F0) and state, then calls an HLE setter (SetVertexShader for the next mesh) before the next draw; the walk ran after the setter, so the pushed draws took the new state | SDK: every device call walks what was pushed first (`DeviceLock` replaces the plain lock in d3d8.cpp) |

After 1-12: the logos/menus (movies not copied yet, skipped as missing), new game, Bag End with Frodo, Gandalf and
Bilbo, the HUD, hint text, pause menu and inventory all draw in the harness; 240 s of walking with no crash.

After 13-16 (2026-10-21): all 64 scenes start with FOTR_SCENE and run 60 s without a crash; the four logo movies play
(Vivendi/Tolkien Enterprises/Black Label...); the streaks seen before in 3_pass_trans, CombatRange1/2 and Tube_bridges
are gone (they came from the late constant loads as well). Trans_bridges is a transition scene: it returns to the menu.

## Frame pacing
- The console presents every second vertical blank (CreateDevice interval 0x80000002): 30 a second.
- The game steps by real time: its frame (app vtable 0x1C5B28 +0xC, 0xD66E0) calls 0xD6920, which reads the
  performance counter (rdtsc 0x74B56, frequency 733 MHz from 0x74B67), multiplies the ticks by 1/frequency (+0x80)
  into dt (+0x50, clamped at 1/[0x1E5CA8]) and adds it to the clocks at +0x48/+0x4C. So frame rates above 30 should
  play at normal speed (still to confirm on the PC by feel).
- Default 30 (`fotr_framerate.cpp`, `[graphics] fps`, 30-240), as on the console. The harness takes `FPS=N` (0 none).

## Field of view, difficulty, wider screens, achievements (2026-10-21)
- Views: 0xD68B0 (esi the view, arg the horizontal angle, radians) keeps the angle, its half and the half's sin/cos
  (culling); 0xD6A60 (eax the view) makes the vertical angle with the screen shape 16:9 (imm 0x3FE38E39) when the
  console's widescreen flag ([0x1ECBA8]+0x30) is set, else 4:3, and passes it to 0xD6840. Projection: 0x5843E
  (thiscall matrix; vfov, aspect, near, far), callers 0x10DBC0 (gameplay camera) and 0x9A580 only.
- The gameplay camera's tuning table (filled at 0x136B98): "Horizontal field of view" +0x5A4 (87 deg),
  "Wide Cinema FOV" +0x5A8 (100 deg). Its modes call 0xD68B0 from 0x10E1C0..0x112C8A; 0x10E3B8 copies another
  view's angle. `[graphics] fov` scales the calls returning to 0x10E1D3 0x10E21E 0x10E292 0x110767 0x110A66 0x110D86
  0x110E2E 0x110ECA 0x112C8F (`fotr_camera.cpp`). Checked: 85 -> 119 degrees at 140%, the harness picture wider.
- Wider than 16:9: every angle but the copy is widened so the vertical one stays (tan(h/2) * shape/16:9), 0xD6A60 is
  replaced (real shape), 0x5843E gets the real shape. The HUD is flat: vertex program kHudProgram through c45-c48
  (orthographic, the game lays it out 16:9 itself: x scale 9/16 of y); its x row is divided by the shape's share just
  before each such draw (draw observer) and put back after. Menus/text: program kScreenProgram, v0 in 640x480 pixels,
  squeezed by gpu::g_screen_space_hook; full-width pictures too unless a 3D draw came within 250 ms (fades). Movie
  frames left alone while gpu::g_movie is on (the title's start_menu.xmv picture is not a film: it gets bars). Checked
  in the harness at 2.37: the same health bar pixels as at 16:9 before the squeeze, more world at the sides, the main
  menu, loading, pause and inventory screens centred.
- Review fixes (same day): (1) the render thread's state copy takes changes in 64-byte pieces, so the HUD row can come
  back half new: it is put back number by number. (2) The camera sets its angle only as a level starts and when it
  changes mode (0x10E292 at load, then 0x112C8F once; none in 38 s of play), so the in-game menu's fov and a change of
  screen shape did nothing until then: CameraFrame (each frame, 0xD66E0) sets it again from the game's own angle
  (0xD68B0, 0xD6A60, 0x10DBC0 with the camera) when either changed; checked: the view and projection after a switch
  to 140% equal those of a run started at 140%. (3) "Over the world" for full-screen pictures went by a 250 ms timer:
  the pause screen's 640x480 background jumped from full width to bars a quarter second after pausing; on Windows it
  now goes by frame (GameInfo::ui counts frames shown, in order with the draws; a 3D draw earlier in the same frame),
  the timer only where ui never comes (the harness). 0x9A580 is the HUD manager's setup (orthographic 832x468 into
  +0x190, 640x480 +0x150 for 4:3, a 55-degree perspective for its 3D pieces).
- Damage: every character takes a hit through 0xBDB10 (ecx the character, arg the message: +0x04 damage, +0x50 who
  dealt it; 0xBE115 takes it from health +0x450). The player is ecx of 0xEB360 (its update; the health cheat refills
  +0x450 there). `[gameplay] damage_taken` / `enemy_damage` scale hits on / from the player (`fotr_difficulty.cpp`;
  FOTR_DMG_LOG=1 logs each hit). Checked: 10 -> 5 at 50%.
- Achievements: regions in story order (Shire, Old_Forest, Bree, Weathertop, Rivendell, Moria, Lothlorien,
  River_Anduin); a region's scene Textures.xpr opening completes the ones before (CombatRange* left out); the story
  ends with Movies\English\fmv_6.xmv (Anduin_aragorn's script). Films: fmv_3 Dell, fmv_4 Troll_shaws, fmv_5 Abyss.

## Test tools
- `FOTR_SCENE=<scene folder>` (Linux test build only, `fotr_harness_scene.cpp`): a new game starts in that scene.
- `XBE_PUSH_LOG=1` (SDK): each hardware method the push walk does not run, once.
- Work folder (not in the repo): `/home/claude/work/fotr` (`loop.sh` regenerates, builds the harness and runs once;
  `sweep.sh <scene>` runs one scene; `pad_*.txt` pad scripts).

## Open
- Music (Scenes/Media/Music1-3.xpa, DirectMusic, 300-435 MB each) and the attract/credits/trailer movies were not
  copied to the test folder; untested.
- Some pushed inline vertices are still dropped (480/1920 bytes with shader B00625D1, 960 with B00624E1): a vertex
  layout the packing does not explain yet.
- Windows build not tried yet (needs the PC).
