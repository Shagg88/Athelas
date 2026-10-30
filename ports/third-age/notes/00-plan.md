# The Third Age: plan

Following docs/Static_Recompilation_Roadmap (Phases 0-5).

Phase 0
- [ ] The player's copy: C:\Emulation\Games\Xbox\Lord of the Rings, The - The Third Age (USA). Hash the xbe's
      code sections; record title, version, XDK version, region.
- [x] Others: Revan67/ThirdAge-Recomp (same release, early, not playable as of Oct 2026). We do our own.

Phase 1: foundation
- [ ] xbe_symbols on Default.xbe -> config/game.toml (XbSymbolDatabase for the XDK libraries; check each match).
- [ ] Full recompile in the headless harness; boot to the first unregistered function, fix in boot order.
- [ ] Harness: level/chapter select by env var (TA_CHAPTER), scripted pad, snapshots, WAV.
- [ ] Every stub loud; DirectSound / DirectMusic method counters.

Phase 2: fidelity
- [ ] Movies (EA titles of 2004: likely XMV), sound, saves, timing at 30/60/144.
- [ ] A full playthrough at the Original preset.

Phase 3: the Athelas options, as the other three ports
- [ ] Frame rate, widescreen (hor+ and the HUD), field of view, render scale, anti-aliasing, filtering,
      sharpening, DLSS/FSR, texture dump/replace, button pictures, controls, achievements and the Journey's deeds.
- [ ] The launcher: a fourth game kind (GK_TA_XBOX), its box, art and music from the player's copy.
- [ ] Vulkan: a second backend for the shared renderer, for all four Xbox ports (separate work).
