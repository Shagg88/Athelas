# ROTKPlus — co-op for 3 and 4 players, Versus, player names and statistics

A mod for *The Lord of the Rings: The Return of the King* (PC, `rotk.exe` 1.0, the version Athelas calls "Game v1.0").
It is developed at <https://github.com/brig-dev/ROTKPlus>: report problems there, with the session's log.

- **Multiplayer** in the main menu, in place of Co-op: a page where you pick the game (campaign or Versus) and who plays.
  - **Campaign**: a third and a fourth hero in the same level, each with a controller of its own, a HUD box, a mark
    above the head, shared lives and a turn in the end-of-level screens.
  - **Versus**: 2 to 4 players, everybody against everybody or 2 against 2 (the teams of the Sun and of the Moon), with
    the level's enemies or the heroes only, last one standing or timed, a single game or a tournament for a cup, in
    three arenas (the Southern Gate, the Courtyard of Minas Tirith, the Black Gate).
- **Fellowship**: every player has a name and statistics of their own (kills by kind of enemy, combos, Versus games,
  who knocked down whom, records, the last game), on pages of the main menu.

## Turning it on

In the launcher's **Mods** tab, turn on ROTKPlus. One controller per player; without one, an extra player stands still.
Players 1 and 2 use ROTK Fix's controllers; ROTKPlus opens the next free ones for players 3 and 4.

## Settings, statistics and logs

They are in the game folder, next to `rotk.exe`:

- `ROTKPlus.ini`: the settings. Without it the defaults are used (co-op for 3 and 4 players on). Every key is explained
  in `ROTKPlus.ini.example`, in the mod's folder of the Athelas sources (`mods/ROTKPlus`) and in the ROTKPlus repository
  as [`config/ROTKPlus.ini.example`](https://github.com/brig-dev/ROTKPlus/blob/main/config/ROTKPlus.ini.example) (also in
  the zip of every ROTKPlus release): copy it into the game folder as `ROTKPlus.ini` and change what you need.
- `ROTKPlus_players.ini`: the names and the statistics (a copy of it is kept in `ROTKPlus_logs` at every start).
- `ROTKPlus_logs`: one log per session, never overwritten; `ROTKPlus.log` is the latest.

If ROTKPlus is also installed the older way (its own `DivxMediaLib.dll` in the game folder), only one of the two
changes the game, and the log says which.

## What it writes into the game's ROTK.ini

The game has controls for four players but sets up only two. For the third and fourth player ROTKPlus adds these
sections to the game's own `ROTK.ini` (the one in the game's settings folder under Documents, the file ROTK Fix also
reads), the first time it starts and only if they are missing:

- `[input2]` and `[input3]`: `map=ROTKDefault` and `mode=KB`, the same kind of entry the game keeps for players 1 and 2
  in `[input0]` and `[input1]`;
- `[input_KB_BASE_2]` and `[input_KB_BASE_3]`: a copy of `[input_KB_BASE_1]` (the game's keyboard keys).

Nothing else in `ROTK.ini` is changed. A controller of player 3 or 4 overrides those keys every frame, so they only
matter without one. Removing the sections is harmless: they are written again at the next start.

## Building

`sh build.sh` with the 32-bit mingw-w64 g++ (`g++-mingw-w64-i686`). The same sources build the `DivxMediaLib.dll` of
the older install with MSVC.
