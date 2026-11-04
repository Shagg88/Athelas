# ROTKPlus — co-op for 3 and 4 players, Versus, player names and statistics

A mod for *The Lord of the Rings: The Return of the King* (PC, `rotk.exe` 1.0, the version Athelas calls "Game v1.0").
It is developed at <https://github.com/brig-dev/ROTKPlus>: report problems there, with the session's log.

- **Co-op: N players** in the main menu: a third and a fourth hero in the same level, each with a controller of its own,
  a HUD box, a mark above the head, shared lives and a turn in the end-of-level screens. Left/right on the entry pick
  2, 3 or 4 players.
- **Versus: N players**, right below it: 2 to 4 players fight each other on any level.
- **Fellowship**: every player has a name and statistics of their own (kills by kind of enemy, combos, records, the
  last game), on pages of the main menu.

## Turning it on

In the launcher's **Mods** tab, turn on ROTKPlus. One controller per player; without one, an extra player stands still.

## Settings, statistics and logs

They are in the game folder, next to `rotk.exe`:

- `ROTKPlus.ini`: the settings. Without it the defaults are used (co-op for 3 and 4 players on). Every key is explained
  in `config/ROTKPlus.ini.example` of the ROTKPlus repository.
- `ROTKPlus_players.ini`: the names and the statistics (a copy of it is kept in `ROTKPlus_logs` at every start).
- `ROTKPlus_logs`: one log per session, never overwritten; `ROTKPlus.log` is the latest.

If ROTKPlus is also installed the older way (its own `DivxMediaLib.dll` in the game folder), only one of the two
changes the game, and the log says which.

## Building

`sh build.sh` with the 32-bit mingw-w64 g++ (`g++-mingw-w64-i686`). The same sources build the `DivxMediaLib.dll` of
the older install with MSVC.
