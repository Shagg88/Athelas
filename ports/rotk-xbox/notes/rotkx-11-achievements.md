# 11. Achievements and the Discord status (2026-10-16)

## The progress block

The game keeps its progress in three 0x988-byte profile slots, the Xbox at 0x1C4FE0 and the PC (1.0) at 0x5BCB08.
The PC signature is at 0x47E02D (`68 08 CB 5B 00`, a push of the block). The game asks for slot 2, the profile in
play: 0x9E310 passes 2 to the stats getter 0x9A3C0, whose record lookup is 0x9A310.

Each hero has a record at slot + 0x244 + hero * 0x3C:

| Offset | What |
|---|---|
| +0x00 | levels completed alone, one bit a level |
| +0x04 | levels completed in co-op |
| +0x18 | the hero's level (float, 1 at the start) |
| +0x1C, +0x20 | experience |

Heroes are numbered by the char_* table at 0x16CF48: 0 Aragorn, 1 Legolas, 2 Gimli, 3 Gandalf, 4 Sam, 5 Frodo,
6 Pippin, 7 Merry, 8 Faramir.

Level bits come from the level table at 0x16CF00: 0 hel01, 1 pat01, 2 pat03, 3 min02, 4 pel02, 5 bla01, 6 ise01,
7 min00, 8 min04, 9 osg01, 10 she01, 11 cir00, 12 cra01, 13 fro03, 14 cir01, 15 pat02, 16 pal01, 17 pal02.
Bits 0 to 12 are the 13 levels the achievements count.

To check it in the harness, use "Debug Win" (START, DOWN x5, A, DOWN, A; the dialog defaults to No) on Helm's Deep
as Gandalf. That set bit 0 of record 3, and his level went from 1 to 2.

## Enemies defeated

The hit routine 0x2A1B0 (hooked in rotk_secrets.cpp; gameplay.c's hook_hit on PC) counts a kill when all of
these hold:
- the attacker is a player (word +0x7A = 0x63);
- the target is not a player;
- the target's health (int32 at +0x90) was above 0 before the hit and is 0 or below after it.

## How it is put together

- `rotk_achievements.cpp` defines the 29 achievements and polls slot 2 every second from a host thread, so what a
  save already holds counts too. It also takes the level and hero for the Discord status from the files the game
  opens (`Data\Game\<abc>\<abc01>.scx`, `Data\Player\Char_<abc>.scx`).
- The SDK's `app/achievements.cpp` keeps `achievements.txt` beside the log, with [unlocked], [counters] and [list]
  sections. It also draws the note and the in-game menu's page.
- `app/discord.cpp` speaks Discord's local IPC (`\\.\pipe\discord-ipc-N`, frames of op, length and JSON:
  a handshake, then SET_ACTIVITY).
- ROTK Fix's `achieve.c` does the same on PC. Its note is a GDI-drawn texture presented from post.c's
  hook_Present9, so it shows with the Vulkan renderer only.
- The launcher reads each achievements.txt on the Gameplay tab.

The Discord status needs an application ID from the Discord developer portal for each game. It stays hidden in
the launcher (`k_discordReady`) and in the ports' menus (no `discord_app_id`) until the IDs are set.
