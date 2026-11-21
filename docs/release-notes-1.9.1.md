# Athelas 1.9.1

A safety update: the launcher protects your saves, mods and game folder better when something goes wrong, plus
fixes and wishes from GitHub for The Fellowship of the Ring and The Third Age.

Update with the launcher's **Update** button. Your settings, saves and achievements are kept.

## Safer

- **Save backups:** if Windows can't read the whole saves folder, the backup now stops with a clear message instead
  of making a backup that's missing files. Restoring a backup can no longer set your current saves aside after
  backing up only part of them.
- **Updates:** if a file can't be replaced (an antivirus holding it, say), the old one is always put back. A full
  disk stops the update cleanly before anything is changed.
- **ROTK (PC), installing ROTK Fix:** if a file can't be copied (the game still running, say), the game folder is
  put back as it was, and another mod's d3d8.dll or dinput8.dll is never overwritten without being kept.
- **ROTK (PC), removing ROTK Fix:** another mod's files and your ROTK.ini backup are kept if they can't be put back
  right away (removing again finishes the job). On Wine and Proton, Athelas now only ever changes back the DLL
  settings it set itself; your own are left alone.
- **Mods:** updating a mod from a .zip unpacks the new version beside the old one first, so a damaged zip leaves
  the old version working. An Athelas update that changes only a bundled mod now reaches the game folder too.

## New

- **The Fellowship of the Ring:** the mouse turns the camera, as the right stick does (#106). It's on by default;
  switch it off with **Mouse camera** on the Controls tab. Aiming still works as before while the ranged attack is
  held.
- **The Fellowship of the Ring:** on a 16:10 screen such as the Steam Deck's, the title screen, menus and HUD now
  fill the screen instead of sitting in a 16:9 band with black bars above and below (#103).
- **All Xbox games:** new button pictures for every style (Xbox, PlayStation, Nintendo, Steam Deck, Steam Controller,
  keyboard and mouse), from Kenney's free Input Prompts. The Xbox style keeps each game's own pictures.
- **All Xbox games:** started from Athelas, the game's quit question offers **To Athelas** (back to the launcher)
  or **To desktop** (#96).
- **Report a problem** on a Steam Deck (or under Proton or Wine) also shows where the report's zip is on Linux,
  with a button to copy it (#96).

## Fixed

- **The Third Age:** with Sharp text on, thin parts of letters (the bar of an "e", for one) no longer break apart
  or vanish (#94).
- **The Fellowship of the Ring:** the new button pictures (LB/RB, and the PlayStation, Nintendo and keyboard ones)
  no longer stay the original Xbox ones for a whole session after an unlucky start (#102).
- **The Third Age:** turning Battle speed off during a battle now ends the sped-up battle properly (#105).
- **The Third Age:** a film that starts right after another (skipping the logos into the intro) no longer keeps
  the first film's zoom.
- **All Xbox games:**
  - a very short key tap is no longer missed now and then;
  - mice with smooth scrolling now move through menus;
  - the keyboard button pictures no longer leak a little memory each time they are redrawn;
  - a game that can't start one of its threads (out of memory) now says so instead of freezing;
  - graphics fixes from the XboxXBE-Recomp project: wrapped textures stay wrapped, 2D sprites and lens flares no
    longer get hard edges, a stage reading a missing texture shows the colour beneath instead of grey, and an empty
    viewport setting keeps the last one;
  - the sound queue grows when it runs dry, so fewer crackles on a busy PC;
  - draws without an alpha test let the graphics card skip hidden pixels earlier. Each game rebuilds its shader
    lists once at the first start after the update (a short stutter in the first minutes);
  - three rare processor instructions are now copied exactly (RCL/RCR flags, word POP to memory, 16-bit IDIV).
- **The Third Age:** the HUD's and menus' pictures stay with their text on wide screens (#95).
- **Launcher:** each game's **Logs** button opens that game's newest log, not the newest of any game.
- **Problem reports** now say whether the Two Towers map fix and Unlock all were at work, to help with #93.

## Known issues

Being worked on; if you see one, please report it with the launcher's "Report a problem" button:
the secret codes in ROTK (PC) (#89) and a graphics glitch at Helm's Deep in The Third Age (#64).

## Checksums (SHA-256)

- Athelas-1.9.1.7z: f67a13f318d0cf10f2715719568837536ec5dfcd525e292e3151aaf759cdcd6d
