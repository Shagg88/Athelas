# Athelas 1.8.5

Vulkan is here: a second renderer for the Xbox games, now complete and open to everyone, plus better support for
Steam Deck, Linux (Wine / Proton) and Mac.

Update from 1.8.4 with the launcher's **Update** button. Coming from 1.8.3 or before? Extract the .7z over your
Athelas folder with 7-Zip (or Windows 11's Extract All); your settings, saves and achievements are kept.

Athelas is also on RecompLauncher, the free home for recompiled PC games: https://recomplauncher.com/game/athelas

[![Get it on RecompLauncher](https://recomplauncher.com/assets/press/launcher/badge-dark.svg)](https://recomplauncher.com/game/athelas)

## New

- **Vulkan renderer** for The Two Towers, The Return of the King, The Fellowship of the Ring and The Third Age. Pick
  it under **Renderer** in a game's settings. It draws the whole game with the same picture and settings as
  Direct3D 12: render scale, anti-aliasing (MSAA, SMAA, alpha to coverage), HDR, colour looks, sharpening, the water
  and glow effects, movies, screenshots, texture packs and F3 captures. Direct3D 12 stays the default.
  - It's fast: in a busy Third Age battle on our test PC (RTX 3080 Ti), Vulkan ran at about 78-84 fps where
    Direct3D 12 ran at about 51 (with V-Sync off).
  - Shaders are saved after they're first made and prepared in the background at the next start, so there's less
    stutter the second time you play.
  - The upscalers and frame generation (DLSS, FSR, XeSS) are Direct3D 12 only for now.
  - Vulkan is new: if a game looks or runs differently on it than on Direct3D 12, please tell us with the launcher's
    **Report a problem** button.
- **Steam Deck, Linux and Mac:**
  - The readme has a section for each: Steam entries with launch options (`--play fotr`, `tt`, `rotk-xbox`,
    `rotk-pc`, `ta`), plain Wine, Lutris and Bottles, and Macs.
  - In Steam Deck Game Mode, Fullscreen at another size or refresh rate no longer changes the screen mode (gamescope
    scales the picture), so the game isn't minimized.
  - Under Wine, the launcher's Chinese, Japanese and Korean text uses the system's fonts, and folders open in the
    Linux or Mac file manager. The Shortcuts section explains Steam / Lutris launch options instead of making Windows
    shortcuts inside the Wine prefix.
  - Save backups under Wine and Proton go to a Backups folder beside Athelas. The Windows desktop there is inside
    the prefix, which Proton deletes with the game's data.
  - If no renderer can start under Wine, the message points to vkd3d-proton (or D3DMetal on a Mac) instead of
    "update your graphics driver".

## Fixed

- **Xbox games, Direct3D 12:** at most 2 frames are queued instead of 3 (when frame generation is off), for lower
  input lag.
- **The Fellowship of the Ring:** at 60 fps, characters' lips move at the console's speed instead of twice as fast
  (#91).
- **Return of the King (PC) under Wine, Lutris and Bottles:** ROTK Fix uses DXVK's Direct3D 9 as it should, instead
  of falling back to Wine's slower Direct3D 8. One controller no longer shows up as two players.
- **Launcher:** the Vulkan check in **About** no longer shows green on systems where ROTK Fix can't use it (Macs).
  In the background the launcher uses far less CPU.
- **Launcher:** when a game can't start any renderer, the message now says what the game itself says, including the
  Wine advice.
- **Problem reports:** your Linux or Mac user name is taken out of the paths in the logs too, not only the Windows
  one.
- **The Third Age:** when the game runs out of memory (#85), the log now records what it was making and how full
  each of its memory areas was, so the cause can be found from your report. This doesn't fix the crash yet.

## Known issues

Being worked on; if you see one, please report it with the launcher's "Report a problem" button:
The Third Age running out of memory late in the game (#85), the Perfect Mode ring in ROTK (PC) (#57), the Two
Towers map screen's glow, ROTK (PC) stopping behind the death menu, a ghost image in the ROTK (PC) credits, and a
graphics glitch at Helm's Deep in The Third Age.

## Checksums (SHA-256)

- Athelas-1.8.5.7z: `72e8705db060cc604b9fad568fa1f6606b9342f5bf332d6090c7f2985ea91ee8`
