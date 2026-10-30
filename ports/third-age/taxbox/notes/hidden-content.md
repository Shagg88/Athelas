# The Third Age (Xbox, NTSC-U): what is in the game's files that the game doesn't show

Research of 2026-10-29: the player's default.xbe (strings, the code that uses them) and the disc's folder listing.
Nothing here is game data; names and addresses only.

## Scenes the code knows but the disc lacks
The scene table (rdata 0x243EA4) lists 13 scene files; the disc has 11 (9 levels and 2 front ends):

| scene | on the disc | what it is |
|---|---|---|
| e01c01, e01c02, e01c04 | yes | Eregion, Western Moria, Eastern Moria (quests cpp_queste1c1/c2/c4; map names map_e1c01_eregion, map_e1c02_wmoria, map_e1c04_emoria) |
| e02c01, e02c03, e02c05 | yes | Eastemnet Gullies, Plains of Rohan, Helm's Deep (cpp_queste2c1/c3/c5) |
| e02c04 | **no** | a cut Rohan scene (movies e2c4f01-06 exist, so its story stayed as films) |
| e03c01, e03c02, e03c03 | yes | Osgiliath, Minas Tirith, Pelennor Fields |
| e03c04 | **no** | a cut Gondor scene (no quest string, no movie folder) |
| e99c01 | **no** | a developer scene (99 is outside the story's numbering; likely a test level) |
| e98c03 | yes | the main menu (the only front end the game opens: rdata "Data/Game/e98c/e98c03.scx") |
| e98c02 | yes, never opened | a second front end, same structure as e98c03 (same sound banks); possibly an older menu or the Evil Mode one |

The level scenes are numbered with gaps (e01c03, e02c02 missing): chapters that exist only as movies (e1c3f has 7
films, e2c2f has 7) or were merged into neighbours.

## Movies
- attrl/attrloop.vid: the attract-mode loop (plays when the title screen is left alone).
- e1c1i/e1c1idri.vid: "IDRI", the introduction of Idrial.
- Gaps in the numbering: e1c2f17, e2c2f01, e2c3f03 missing (cut or renumbered films); e2c3f00 exists.
- Movie names the scripts use (rdata near 0x251180): FMV1/OSGI, FMV2/PAT1, GATE, MTYD, HELM, ISEN, E1C1, IDRI, LOGO;
  "ISEN" (Isengard) has no level of its own.

## Developer leftovers in the program
- `\Device\Harddisk0\Partition1\devkit\xbmovie.dat`: a dev-kit path (the Xbox dev kits' screen recorder output).
- `EA_RS_LOTR_RPG`: the project's internal name (EA Redwood Shores, LOTR RPG).
- The scripts are Lua 4.0 (tolua); the game adds SendMsg, DelayedMsg, PrintNotice, Debug_Print, GetRuntimeData,
  RegisterLuaScript, TeleportToPos (rdata 0x238CD8 table). Debug_Print / PrintNotice would show script messages
  on a debug build.
- Havok physics, evaluation build string ("Havok evaluation key has expired").
- Language table: ENG and DEU in the program (the US disc carries English only).
- Co-op (cpp_coop) and Evil Mode (cpp_evil, cpp_evil2, cpp_evilsave, titl_Evil) are real features of the retail game.

## Not done yet
The .scx/.sas scene files are chunked (SHOC/SHDR/SDAT/SONO, byte-reversed tags) and compressed; their Lua scripts and
text can't be read until that packing is decoded. e98c02's contents would tell whether it is a dev menu.
