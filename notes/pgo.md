# Profile-guided optimisation (PGO), all four Xbox ports

How it fits together (2026-10-30):

- **SDK** (`sdk/cmake/XbeRecomp.cmake`, all four copies): `-DXBE_PGO=GEN` makes a measuring build
  (`-fprofile-instr-generate`, `XBE_PGO_GEN`). `runtime/app/main.cpp` then writes the counts every 20 s and at exit
  to `LLVM_PROFILE_FILE`, else `<exe>-<pid>.profraw` (a killed run keeps what it measured). Without `XBE_PGO`, a
  port's `pgo/<target>.profdata` is compiled in (`-fprofile-instr-use`, mismatch warnings off): the mingw, mingw-arm64
  and release presets all pick it up, so BUILD_RELEASE needs nothing extra. `XBE_PGO=OFF` ignores it.
- **Kit** (`tools/kit/make_kit.ps1`): the profile in the compile flags goes into the kit as `pgo/<exe>.profdata` and
  the flag becomes `-fprofile-instr-use=@KIT@/pgo/<exe>.profdata`; a measuring build refuses to make a kit.
- **Player's PC** (`launcher/src/builder.cpp`): `@KIT@` is replaced by the kit's folder, so the recompiled game
  code a player builds is optimised with the same profile. A new profile changes kit.json, so the launcher
  rebuilds once.
- **Training** (`scripts/windows/PGO_TRAIN.ps1`, gameplay): measuring build plays a scripted route, the profile is
  merged with llvm-profdata (shipped in the release's tools), the normal build is rebuilt with it, then the same
  route is played without and with the profile and the fps lines are printed.
- The profile holds counts per function of our runtime and of the recompiled code (names like `sub_001F4E30`),
  no game code or data, so it is committed with the port and shipped in the kit.
- Re-train after big changes to a port's code: stale counts are ignored, so a stale profile only loses the gain.

Not covered: ROTK Fix (a thin layer around the PC game's own rotk.exe, built with gcc; the game's time is spent in
code we don't compile) and the launcher (not performance-bound).

Status: plumbing done; The Third Age's measuring build made (job 129). Training waits for the GPU to be free.
