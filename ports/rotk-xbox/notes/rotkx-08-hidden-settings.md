# ROTK Xbox port, 08: hidden settings and debug features (2026-10-14)

How it was searched:

- Strings in default.xbe (ASCII and UTF-16), and the unpacked GlobScen, fro03 and hel01 streams.
- References found by searching for pushes and moves of each string's address.
- ROTK Fix's notes on the PC version, for comparison.

The tools and text dumps are in `work/hidden/`: `strs.py`, `xref.py`, `callers.py`, `unpack.py`, `tables.py`,
`cheats.py`, `xbe_strings.tsv`, `xb_tables.txt`, `cheat_table.txt`.

## No settings list like TT's

ROTK has no named-setting registry: no "SECTION/Name" strings, no tweak menu. Every function was ranked by how many
different strings it's given; only UI-element and assert helpers came up. The tuning is in data tables loaded from
GlobScen through 0x113D60: `abil`, `npc `, `plyr`, `upgd`, `weap`, plus `terr` per level. The Xbox and PC tables are
byte-for-byte identical.

## Findings

| Switch | Where | Shipped | Test build result | Player setting? |
|---|---|---|---|---|
| Difficulty damage multipliers (`upgd` "DIFF") | copied at load by 0x29530 to floats 0x17F550..0x17F57C, used at 0x2A1FB | see the table below | Logged as listed. ROTK_DAMAGE=300 scales all twelve; health was lower at the same point of Helm's Deep (fights vary, so not exact) | Yes: a damage-taken percent, like TT's |
| Developer buttons on the pause page | 0x10AA40 hides "Debug Game", "Debug Win", "Try Again" every frame (word +0x74 of each element) | hidden | ROTK_DEVBUTTONS: "Debug Win" and "Try Again" appear under the pause panel | Maybe: Debug Win skips the level, a cheat |
| Unlock all levels and characters | byte 0x1DFD40, read by 0x10AA10 (used by 0x9E560 and 0x9E870); the front end's boot script sets it to 0 | 0 | Not reached: the test pad script didn't get past the save screen | Likely yes, once tested |
| Debug respawn | byte 0x1DFD41; its Options row is hidden by calls at 0x10385E..0x103896 | 0 | not tested | Maybe |
| Secret codes accepted before the final level is beaten | 0x9E4D0 asks progress flag 0xC | final level required | not tested | Maybe |
| God-mode check | 0xF9660 (`xor al,al; ret`), called only by the player-damage function 0x2A1E0 | "no" | Answering "yes" crashes 11 s into a fight: an indirect call to garbage from 0x4D5EB. The code behind it was compiled out | No. The "Invulnerable" code (player +0x634 bit 4) is the working route |
| Cheat-code table | 0x178138, 98 codes. Hold LT+RT on the pause page and press four buttons (A=1, B=2, Y=4, X=8, Up=0x10, Down=0x20) | works after beating the game | not tested | Already in the game |
| Global cheat flags | dword 0x1DA24C. 0x10 HUD flag; 0x40, 0x80, 0x100 co-op only; 0x200 projectiles; bits 1/2/4/8 = secret characters 5/7/6/8 | 0 | not tested | Maybe |
| Co-op 1P/2P markers | byte 0x1DB338, toggled with Back | 1 | not tested | Already in the game |
| Hidden main-menu items | Online, Exit, PlayTrailer, hidden at 0x102D50 | hidden | Exit and PlayTrailer show as empty slots: no text ("mainMenuExit", "mainMenuPlayTrailer" missing from GlobScen) and no working action | Exit: always on in the port (below). PlayTrailer: not wanted. Online is dead |
| Shadows / alternate cameras | The Xbox render loop 0xC7F80 has no gate for them | none | n/a | No (PC only) |
| Frame rate, FOV, safe area, draw distance | already handled by the port | | | done |

**Difficulty multipliers** (damage the player takes). Within a difficulty the value rises from min to max as more
level/character pairs are completed (byte 0x16D5B0, 1/117 per pair):

| | Single player | Co-op |
|---|---|---|
| Easy | 0.75–1.00 | 1.00–1.25 |
| Normal | 1.00–1.40 | 1.50–2.00 |
| Hard | 1.50–2.00 | 2.00–3.00 |

**Cheat codes.** The seven codes the Codes screen shows, in order:

| Code | Effect |
|---|---|
| X X B B | Restore Health |
| B Down Y A | Perfect Mode |
| Y Up Y Down | Always Devastating |
| Up Down Y X | All Upgrades |
| X X Down B | Infinite Missiles |
| X B X Up | No damage |
| Down B Up X | Flag 0x200 (projectiles) |

All 98, including the 86 per-character ones, are in `work/hidden/cheat_table.txt`.

**Port correctness.** The game's rdtsc waits assume 733.33 MHz (float 0x155944). The SDK's x86_rdtsc already ticks
at that rate.

## In the port: `src/game/rotk_secrets.cpp`

These switches are all off. In the test build they're turned on from the environment:

- ROTK_UNLOCK
- ROTK_DEVBUTTONS
- ROTK_CODES
- ROTK_DAMAGE=percent

The damage table is logged at each load. None of them is a player setting yet.

## Main menu Exit (always on)

The user wants Exit as part of the game, not an option. `rotk_secrets.cpp`:

- After 0x102D50 hides its items, 0x10A550(0, "MainMenuScreen.Exit", 1) shows Exit again. It sits below Credits,
  as on PC.
- Label: GlobScen's string table ("key\0text\0" pairs, loaded once into guest memory) has the unused
  "mainMenuOnline\0Online"; it is renamed in place to "mainMenuExit\0\0\0Exit\0\0" (same length).
- Action: 0xFE010(event) is the menus' input handler (jump table 0xFE0B8: 2/3 down, 6 START, 7 accept). The focus
  is [0x1DB2E0]. Accept runs the element's script at +0x8C; Exit's is an "rpn!" program whose quit does nothing on
  Xbox. When event 6 or 7 arrives with the focus on Exit, the port ends with exit code 0.
- Harness: START, START, then three DOWNs (co-op is skipped without a second pad) and A gives "Exit (0)". The
  capture shows "exit" under credits.

## RenderScale (launcher)

ROTKFix.ini `[Graphics] RenderScale` (50 to 300) maps to `render_percent`: the share of the chosen resolution that's
drawn, fitted to the screen. The SDK's own setting list goes up to 300, so the launcher can offer 250 and 300. With
`g_wide_targets` the targets are already as wide as the screen, so 200% at 2560x1440 draws 5120x2880.
