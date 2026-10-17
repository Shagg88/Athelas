# Athelas 1.0.1

A bug-fix update. Unzip it over your Athelas folder: your settings, saves and achievements are kept.

## Fixes

- **The Two Towers: the crash in Balin's Tomb** when the cave troll is hit by an arrow
  ([#2](../../issues/2)). The game's script engine recovers from an error there, as it did on the Xbox; the port
  now does the same instead of closing.
- **The Two Towers: a rare crash at startup**, in the game's own frame timing, is prevented.
- **Copies of the Xbox games that were turned away** ([#3](../../issues/3)):
  - A copy whose game code and data are the same as the US release's, and that differs only in its certificate or
    signature (another printing or dump), is now recognised as the US release and starts normally.
  - A genuinely different release can now be started from the launcher: it asks whether to start it anyway, as the
    ports do on their own. These copies aren't tested and may crash.
  - The log names each copy's version and build date, which helps when you report a problem with one.
- **Starting a port's exe directly** (without the launcher) now uses the launcher's settings, saves and achievements:
  ROTKXbox.exe reads the same ROTKFix.ini the launcher writes, and both ports use the `saves` folder and
  achievements file beside them ([#3](../../issues/3)).
- **Launcher:** the achievement list sits under one Achievements heading.

## Notes

- At 4K, turn on **Smooth menus and HUD** (Graphics tab) for The Two Towers: its menus were drawn for a 640x480 TV,
  and the Original look keeps their hard pixels on purpose.
- Windows on ARM (Snapdragon laptops) runs Athelas through Windows' own translation layer, which costs a lot of speed
  in the Xbox ports. A native ARM build is planned.

## Checksum

`Athelas-1.0.1.zip` SHA-256: `7c41eadf83477bdd0e1bec16b6fddd45d33934da5043e5503b987dc9274e961f`
