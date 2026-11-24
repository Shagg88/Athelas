# Athelas 1.9.2

Fixes and wishes from GitHub and RecompLauncher players: the mouse camera in The Fellowship of the Ring, The Return
of the King on Macs, Skip any video in The Fellowship, camera invert in The Third Age, sharper fonts from texture
packs in The Return of the King (PC), ROTKPlus 1.4.2, and logs that say how each game was started.

Update with the launcher's **Update** button. Your settings, saves and achievements are kept.

## Online co-op

A reminder: since 1.9.0, **The Return of the King (Xbox)** can be played in co-op over the internet (experimental).
Both players need the same Athelas version and their own US copy of the game.

1. **Player 1:** on the game's **Network** tab set **Online co-op** to **Host (player 1)**, press **Play** and send your
   friend the address the small window shows.
2. **Player 2:** set **Online co-op** to **Join (player 2)**, paste the address and press **Play**.

If the window says the router couldn't open the port, forward **UDP port 47100** to player 1's PC. We'd like to hear
how it goes, good or bad: use **Report a problem** on both PCs and attach the zips to a GitHub issue.

## Fixed

- **The Fellowship of the Ring:** the mouse camera was upside down (moving the mouse forward looked down), and the
  Invert camera settings only changed the controller. Forward now looks up, and both Invert camera settings reverse
  the mouse too.
- **The Return of the King (PC) on a Mac:** ROTK Fix and the launcher asked Vulkan for version 1.1, and on Apple
  chips (MoltenVK) that made a Vulkan 1.4 graphics card look too old, so the game fell back to Direct3D 8. They now
  ask for the newest version on offer (#112, thanks to Marmasal for finding and testing it). When Vulkan really is too old, the
  About page and problem reports now show which version was found.

## New

- **The Fellowship of the Ring:** **Skip any video**, as the other three games have (the menus' moving background
  stays).
- **The Third Age:** **Invert camera up/down** and **left/right**, as The Fellowship has.
- **Launcher:** The Third Age's **Unlocked** frame rate can be chosen on its Graphics page (it showed as "0 fps"), and
  its **Original** preset puts the frame rate back to 30 like the other games'.
- **The Return of the King (PC):** texture packs can replace the game's fonts with sharper ones, 2x or 4x, and the
  button pictures are painted in at that size (#72, by brig-dev).
- **ROTKPlus 1.4.2** (by brig-dev): Aragorn as the third or fourth player at the Black Gate no longer stands frozen
  after the opening scene (and Sam and Frodo as extra players at the Crack of Doom get the level's own models); in Versus, a parry no longer stops fierce attacks, a combo's last blow or a fully drawn
  shot (`[PvP] HeavyUnblockable`, on by default).
- **Launcher:** a mod that comes with Athelas shows its new version on the Mods tab straight after an update (#71, by
  brig-dev).
- **Launcher:** with some games added, the hint under the buttons names the others and how to add them.
- **Logs:** each game's log says how it was started (from Athelas, or from another launcher such as RecompLauncher,
  Quiver or GameNative, Steam, or a shortcut), to help with problem reports.

## Checksum

- Athelas-1.9.2.7z: (added at release)
