# Athelas 1.8.4

A release of fixes: two reported problems, and what a full review of the code turned up.

Update from 1.8.3 with the launcher's **Update** button. Coming from 1.8.2 or before? Extract the .7z over your
Athelas folder with 7-Zip (or Windows 11's Extract All); your settings, saves and achievements are kept.

Athelas is also on RecompLauncher, the free home for recompiled PC games: https://recomplauncher.com/game/athelas

[![Get it on RecompLauncher](https://recomplauncher.com/assets/press/launcher/badge-dark.svg)](https://recomplauncher.com/game/athelas)

## New

- **An installer**, `Athelas-Setup-1.8.4.exe`, next to the .7z: click Install and it's done. It needs no
  administrator rights, adds Start menu and desktop shortcuts, and an uninstaller that keeps your settings and saves.
- **Folders and user names in any alphabet** (Cyrillic, Japanese, accented letters...) work in the launcher, on
  Windows 10 1903 and later. Settings saved by an older version are converted once.
- **A new in-game menu** for the Xbox games, in the launcher's style, with larger text. On a controller it now
  opens with **Back + Start** (Share + Options on a PlayStation pad) instead of both stick clicks; F10 still opens it.
- **"Before you play":** on the first start, and once after each update, Athelas asks you to confirm that the game
  files you use are your own (it includes none and provides none). Tick "Don't ask me again" with Accept to end the
  question for good; Decline closes Athelas.
- **A clean folder after every update:** files an older Athelas left behind that the new version doesn't use are
  moved into a folder named "Old files - safe to delete" (before, only unchanged ones were removed). Delete it
  whenever you like. Your games, saves, settings, texture packs and anything you added are never touched.
- **Sharp box art:** "Get box art" now downloads the full-size box scans (about 1500 x 2150) from the LaunchBox
  games database, and The Return of the King's PC box too. Already have the small ones? Press it again.
- **The Third Age** is marked as an early version in the launcher.
- **Problem reports:** a switch to leave the crash file out of the zip (a memory snapshot, which unlike the logs
  can't have your Windows user name taken out).

## Fixed

- **The Two Towers:** the keyboard works on the title screen again: Enter presses Start (#77).
- **Return of the King (PC):** the sound no longer loops a short beat while the game is minimized; it goes silent
  and comes back when you return (#78).
- **Return of the King (PC):** a borderless resolution of another shape than the monitor (1920x1200 on a 16:9
  screen, say) keeps its shape, with black bars, instead of being stretched. (`[Window] KeepShape = 0` in
  ROTKFix.ini brings the stretching back.)
- **Return of the King (PC):** the in-game brightness works when post-processing is off; removing ROTK Fix keeps
  the key bindings and settings you changed since installing it.
- **Updates:** an update interrupted half way (the launcher closed, the PC shut down) is undone at the next start
  instead of leaving a mix of two versions; the download no longer has to fit in memory.
- **Xbox games:** the settings are no longer reset when the settings file was briefly locked (OneDrive, antivirus)
  as Play was pressed.
- **Xbox games, frame generation:** V-Sync stays on in menus, films and the pause menu with DLSS frame generation;
  frame pacing data is sent from the game's own thread.
- **Xbox games:** a 16:10 screen (the Steam Deck's 1280x800, say) uses the whole picture instead of 16:9 with black
  bars; The Fellowship of the Ring's menus keep their shape on it (#80).
- **Xbox games, frame generation:** the game and the renderer agree on which frame is which, for steadier pacing and
  latency.
- **The Fellowship of the Ring:** characters' lips move in the cutscenes again (#79), and the save and load
  pictures show the moment you saved instead of black (#81).
- **Xbox games:** pause and fast forward now also hold and speed up the games' internal timers; a rare crash when a
  game closes is gone.
- **The Fellowship of the Ring:** a cheat switched off in the in-game menu stops at once.
- **The Two Towers:** the damage sliders no longer add up from level to level.
- **Launcher:** save backups work for Windows user names in any alphabet; opening a game's page is faster (the
  game's settings list is remembered until the game's program changes); a game in a folder at the top of a drive
  (`E:\`) starts.
- **Launcher:** B on a controller goes back from a game's page to the games, as Y does (#83).
- **ARM64:** The Third Age's ARM64 build is packed with the others.

## Known issues

Being worked on; if you see one, please report it with the launcher's "Report a problem" button:
the Perfect Mode ring in ROTK (PC) (#57), the
Two Towers map screen's glow, ROTK (PC) stopping behind the death menu, a ghost image in the ROTK (PC) credits, and
a graphics glitch at Helm's Deep in The Third Age.

## Checksums (SHA-256)

- Athelas-1.8.4.7z: `5f8f7e4b5779ba6cd6089f4405160c7369b56bd12755ac1a0ea97de710a85308`
- Athelas-Setup-1.8.4.exe: `42bd93a626f8ceab5a071418ef02de0b28d5b19b42482c929bd6333231b104f1`
