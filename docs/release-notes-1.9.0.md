# Athelas 1.9.0

Online co-op for The Return of the King (Xbox), experimental, plus fixes for The Two Towers, The Return of the King
(PC) and the launcher.

Update from 1.8.6 or later with the launcher's **Update** button. Coming from 1.8.3 or before? Extract the .7z over
your Athelas folder with 7-Zip (or Windows 11's Extract All); your settings, saves and achievements are kept.

Athelas is also on RecompLauncher, the free home for recompiled PC games: https://recomplauncher.com/game/athelas

[![Get it on RecompLauncher](https://recomplauncher.com/assets/press/launcher/badge-dark.svg)](https://recomplauncher.com/game/athelas)

## New: online co-op (The Return of the King, Xbox), experimental

Play the game's two-player co-op with a friend over the internet. Each of you runs the game on your own PC and only
your controller presses go between you, so it looks and sounds as sharp as playing alone.

1. **Player 1 (host):** on the game's **Network** tab set **Online co-op** to **Host (player 1)** and press **Play**.
   A window shows the address to send your friend, like `203.0.113.7:47100`. If Windows asks whether the game may use
   the network, allow it.
2. **Player 2:** set **Online co-op** to **Join (player 2)**, paste the address into **Friend's address** and press
   **Play**.
3. The games connect, player 1's save and settings are copied to player 2 (player 2's own saves aren't touched),
   and both games start together. Start co-op in the game as you would with two controllers.

Good to know:
- You both need Athelas 1.9.0 and your own US copy of the game, on the same kind of PC: a Windows on ARM laptop
  can't play with a regular PC.
- Player 1's router is asked to open the port by itself. If the window says it couldn't, forward **UDP port 47100**
  to player 1's PC in the router's settings. Playing in the same house? Use the home-network address the window also
  shows.
- Athelas must be in a folder you can write to (not Program Files).
- So both games stay exactly the same, widescreen is 16:9 for both (other screen shapes get bars), and the Journey
  page's virtues and Fellowship bonus are off during a session. Each of you keeps your own screen, sound, buttons,
  keyboard and mouse settings; the Graphics tab follows player 1.
- Progress is kept in player 1's save; player 2 earns achievements only for levels completed during the session.
  Set **Online co-op** back to **Off** afterwards to play as usual.
- Co-op plays at 30 or 60 frames a second (a higher setting plays at 60).
- If one of you leaves, the other plays on alone.

It's experimental: it has been tested with two copies on one PC, including a bad connection, but not yet between two
homes. If anything goes wrong, please report it with the launcher's **Report a problem** button (both players'
reports help most). Streaming with Parsec still works too; see PLAYING CO-OP ONLINE in README.txt.

## Fixed

- **The Two Towers:** the map screen's burning edges are drawn properly again (#55, #93). 1.8.7 fixed them only
  where the game draws them as point sprites; it also draws them as plain points, which were still one pixel each.
- **The Two Towers:** on the load/save screen, Enter no longer answers Yes by itself (#56, #93). The screen was
  taken for a loading picture, where Enter presses Start.
- **The Return of the King (PC) with ROTK Fix:** the glow around the ring in Perfect Mode stays on the ring with
  widescreen (#57, #93).
- **Launcher:** with the Vulkan API, the Upscaler setting (DLSS, FSR, XeSS) now says it only works with Direct3D 12.
  Before, it was quietly ignored.

## Known issues

Being worked on; if you see one, please report it with the launcher's "Report a problem" button:
the secret codes in ROTK (PC) (#89) and a graphics glitch at Helm's Deep in The Third Age (#64).

## Checksums (SHA-256)

- Athelas-1.9.0.7z: `ff300052d0e0051dce86fa825357b73166082a8980ef0ed9eee02df38a399106`
