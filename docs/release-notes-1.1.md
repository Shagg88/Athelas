# Athelas 1.1

Unzip it over your Athelas folder: your settings, saves and achievements are kept.

## New

- **Athelas tells you when a newer version is out.** When it starts, the launcher asks GitHub for the newest
  release's number and shows a note under the settings and on the About tab. Nothing about you or your games is
  sent and nothing is downloaded. Turn it off on the About tab ("Look for new versions").
- **Easier problem reports.** When a game crashes, the message now has **Open log folder** and
  **Report a problem** buttons. The second opens a bug report here with the game, versions and error filled in.
- **Windows on ARM:** the launcher is ready for native ARM64 builds of the Xbox ports (being tested in the
  [1.1-arm-test](https://github.com/Shagg88/Athelas/releases/tag/1.1-arm-test) pre-release) and starts them by
  itself on an ARM PC. On those PCs a **One processor core** setting appears for the Xbox ports, in case of random
  crashes there. The About tab says which build of each port is running.

## Fixes

- **Where ROTK's settings are** ([#4](https://github.com/Shagg88/Athelas/issues/4)): both versions of The Return
  of the King share `ROTKFix.ini`. The `settings.toml` in ROTK-Xbox now says so at its top, with the file's
  location; the launcher's ROTK Xbox About tab shows it with a **Show in folder** button; and the README has a
  "Where your settings are" section.

## Checksum

`Athelas-1.1.zip` SHA-256: `41536a89122359dcd5dfdf04888907115eab74674f6c7477e2ff9bb6f587db4a`
