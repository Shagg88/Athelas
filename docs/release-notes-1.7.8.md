# Athelas 1.7.8

Press **Update** in the launcher, or unzip it over your Athelas folder: your settings, saves and achievements are
kept.

## Fixed

### The Return of the King (PC)

- **Video options flickering, and settings that wouldn't stick** (#45, likely #44): ROTK Fix changed the game's own
  record of its screen settings to "windowed", so the game thought it was in a window: each change in Video options
  made it resize the window (the flicker), and every choice came out as 1024x768 in 16-bit colour. ROTK Fix now
  leaves the game's settings alone, and a change that doesn't change the screen no longer resets it.
- **The resolution list showed 1920x1080 as 1920x1440** (#45): it now shows the real height.
- **"Skip opening logos" didn't skip them** (#48): the game marks its opening logos as never skippable. With the
  setting on, the logos (only) can now be skipped, and are.
- **The spinning blue "busy" cursor while loading** (#46): the game's own cursor is kept while it loads.
- **Jagged edges on the loading ring** (#46): the game switched anti-aliasing off on its first screens; with
  ROTK Fix's anti-aliasing on, it now stays on.
- **Texture packs stuttering** (#47): your own replacement textures are now prepared while the level loads instead of
  when they first appear, they're kept between levels, and DDS files (DXT1, DXT3, DXT5, A8R8G8B8, with mipmaps) are
  used next to or instead of PNGs: they load in a millisecond or two.

### The Xbox games

- **Linux / Wine** (#42): the games start without DirectX 12 and use Vulkan instead, rather than failing to start.
- **Another version of the game** (another region or printing): the launcher now says so in plain words, in your
  language, instead of showing the game's technical log line.
- **Controllers on the Steam Deck**: the launcher lets go of the controllers while a game is running, so it can't
  keep them from the game.

## Checksums (SHA-256)

- Athelas-1.7.8.zip: `b64eaeb8f7cb9d7ac1c38ec52148abb627aa0fd99e57954ccb47fe205229729d`
