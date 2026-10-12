ROTK Fix
========
An unofficial, free fan project for The Lord of the Rings: The Return of the
King (PC, 2003). It lets you play your own copy on a modern PC: any screen
size and refresh rate, smooth high frame rates, sharper graphics, and modern
Xbox, PlayStation and Switch controllers.

Not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner
Bros. or the Tolkien Estate. No game files are included: you need your own
copy of the game, already installed and able to run.


INSTALL
-------
1. Unzip everything into your game folder (the folder with rotk.exe).
   If Windows asks to replace d3d8.dll or dinput8.dll, those files belong to
   another mod. Copy them somewhere safe first, then let ROTK Fix replace them.
2. Run ROTKFixLauncher.exe. Choose your settings and press PLAY.
   Always start the game from the launcher from now on: it is where every
   setting lives, and it saves them for the game each time.

The first time you press Save or Play, the launcher adds the controller layout
to the game's Input.ini (the original is kept as Input.ini.original) and
updates the game's settings file in Documents (a copy of yours is kept).


UNINSTALL
---------
Open the launcher, go to About, and press "Remove ROTK Fix". It removes every
ROTK Fix file and puts the game's own files back. Saved games and screenshots
are kept.


WHILE PLAYING
-------------
  \  or Print Screen   Screenshot (saved to the Screenshots folder)
  [                    Sharpening on/off, to compare
  ]                    Next colour look


TEXTURE PACKS
-------------
ROTK Fix can swap any of the game's textures for a PNG of your own. It
includes no game art: packs are made from your own copy.
- Making a pack: in ROTKFix.ini set Dump = 1 under [Textures] and play.
  Every texture is saved once as a PNG in ROTKFix\textures\dump as the game
  loads it, named <width>x<height>_<16-character code>.png. A level loads all
  its textures when it starts, so starting each level once is enough.
  Set Dump back to 0 when you have what you need.
- Using a pack: put the edited or upscaled PNGs in ROTKFix\textures\replace
  (subfolders are fine). The game uses them the next time it starts. A file
  can be any size, but should keep the original's shape, and its name must
  end with the original's 16-character code; anything before it is free,
  for example hud_health_256x64_0123456789abcdef.png.
Text, the movie picture and the screen effects cannot be replaced.


PLAYING CO-OP ONLINE
--------------------
The game's co-op is for two players on one PC, but a friend can join over the
internet by streaming it. Only your PC needs the game; your friend's
controller arrives as a second controller and plays player 2.
- Steam Remote Play Together: in Steam, add rotk.exe as a non-Steam game
  ("Add a Game" > "Add a Non-Steam Game"), start it from Steam, then invite a
  friend from the Steam overlay (Shift+Tab). Your friend needs Steam, not the
  game.
- Parsec (parsec.app): both of you install it. Your friend connects to your
  PC, and you allow their controller.
Start the game as usual from ROTKFixLauncher.exe or Steam, then pick Co-op.
Your friend sees your screen with a short delay. In the launcher's Controls
tab, set "Player 2 (co-op)" to "Second controller".


WIDE AND ULTRAWIDE SCREENS
--------------------------
The game world fills any screen shape, from 4:3 to 32:9: you see more at the
sides instead of a stretched picture.
Menus, text and the HUD fill the screen up to 16:9. On wider screens they stay
16:9 in the middle, so text is not stretched; full-screen shading, fades and
the pause screen still reach the edges.
Logos and the trailer keep their 4:3 shape with black bars. Story cutscenes
are film: they fill the width on 16:9, and the height on wider screens.
To try an ultrawide shape on a normal monitor, set Display mode to Windowed:
the Resolution list then also offers window sizes such as 2560 x 1080 (21:9).


SUPPORTED GAME VERSION
----------------------
The launcher checks your rotk.exe and shows "Game v1.0" when it is supported.
If it says "Unknown game version", please report it with your rotk.exe's size.
ROTK Fix does not change or bypass the game's copy protection.

Installed from the original disc? Its SafeDisc copy protection needs an old
Windows driver that Windows 10 and 11 block, so the game will not start on its
own. Install SafeDiscShim (https://github.com/RibShark/SafeDiscShim), a free
tool that stands in for that driver, put the disc in the drive, and press
Play. The disc version asks to run as administrator: allow the Windows prompt.
If you play from an image of your own disc, mount it in a virtual drive before
pressing Play. The launcher shows "Disc version (protected)" for this copy.


ROTKFIX.INI SETTINGS
--------------------
The launcher writes ROTKFix.ini in the game folder. You can also edit it in Notepad.

[Graphics]  Screen and picture
  DisplayMode            borderless, fullscreen or windowed (default borderless)
  Width                  Resolution; 0 = the screen's own (default 0)
  Height                 (default 0)
  RefreshRate            Fullscreen only; 0 = the screen's own (default 0)
  VSync                  1 = no tearing (default 1)
  Renderer               dxvk = Vulkan (recommended), native = Windows' own Direct3D 8 (default dxvk)
  DxvkFolder             Which DXVK inside the ROTKFix folder draws the game (default dxvk)
  MSAA                   Anti-aliasing: 0, 2, 4 or 8 (default 4)
  AlphaEdgeSmoothing     1 = also smooth leaves, grass and fences (needs MSAA; costs speed) (default 1)
  AnisotropicFiltering   Texture filtering: 0, 2, 4, 8 or 16 (default 16)
  TextureLODBias         Texture sharpness: 0 = game's own, down to -1.0 = sharpest (default -0.50)
  Sharpening             0-100, 0 = off (also turns off the automatic HUD sharpening). [ switches it on and off while playing (default 60)
  Brightness             Added to the game's own brightness option (0.5-2.0) (default 1.00)
  ColourLook             original, film, vivid or dark. ] switches while playing (default original)
  ColourLookStrength     0-100 (default 100)
  WidescreenFix          1 = see more at the sides instead of a stretched picture (default 1)
  FieldOfView            Percent of the game's own view angle (70-150) (default 100)
  DrawDistance           Percent of the game's own (50-800) (default 100)
  UseGameBrightness      1 = the game's own brightness option works in every display mode

[Frame]  Frame rate
  TargetFPS              Frames per second (the game was made for 60) (default 60)
  Limiter                1 = ROTK Fix's smooth limiter, 0 = the game's own (62 fps, busy CPU) (default 1)
  SpinMicroseconds       Fine timing window; leave as it is (default 1000)

[Video]  Movies
  MovieDisplay           auto = cutscenes fill the width, stretch, or 4:3 (default auto)
  SharperMovies          Cutscene sharpening 0-100, 0 = off (default 70)
  CutsceneAudioDelay     Holds the picture back (ms) for Bluetooth sound, 0 = off (default 0)
  AllowSkippingAllVideos 1 = every movie can be skipped (default 1)
  SkipOpeningLogos       1 = go straight to the menus (default 0)

[Sound]  Sound
  Mode                   3D sound and EAX reverb: speakers, headphones, or off (default speakers)

[Gamepad]  Controllers
  Enabled                1 = use modern controllers (as "ROTK Fix Gamepad") (default 1)
  SecondController       1 = a second controller plays player 2 in co-op (default 1)
  ButtonNames            Button pictures in on-screen hints: auto, xbox, playstation, nintendo; original = the game's own text (default auto)
  HideMousePointer       1 = hide the game's menu pointer while a controller is used (moving the mouse shows it) (default 1)
  Vibration              Rumble strength 0-100, 0 = off (default 100)
  StickDeadzone          Percent (default 12)
  TriggerThreshold       How far a trigger is pulled to count as pressed, percent (default 30)
  HideRealControllers    1 = the game only sees the ROTK Fix controllers (default 1)
  LogInput               1 = log controller details

[GamepadButtons]  Player 1 buttons (A B X Y LB RB LT RT LS RS, by Xbox position)
  SpeedAttack            (default A)
  PhysicalAttack         (default B)
  Parry                  (default X)
  FierceAttack           (default Y)
  JumpBack               (default LB)
  Action                 (default RB)
  Special                (default LS)
  RangedAttack           (default LT)
  KillingMove            (default RT)

[GamepadButtons2]  Player 2 buttons (co-op)
  SpeedAttack            (default A)
  PhysicalAttack         (default B)
  Parry                  (default X)
  FierceAttack           (default Y)
  JumpBack               (default LB)
  Action                 (default RB)
  Special                (default LS)
  RangedAttack           (default LT)
  KillingMove            (default RT)

[Textures]  Texture packs
  Dump                   1 = save every texture the game uses as a PNG in ROTKFix\textures\dump (for making texture packs) (default 0)
  Replace                1 = use replacement PNGs from ROTKFix\textures\replace (texture packs) (default 1)

[Extras]  Extras
  DeveloperButtons       1 = show the hidden developer menu buttons (testing) (default 0)

[Log]  Log (ROTKFix.log)
  Enabled                (default 1)
  Detailed               1 = much more detail, for tracking down a problem (default 0)
  IntervalSeconds        Seconds between frame-rate lines

[Launcher]  Launcher
  Monitor                (default 0)
  CreatedGameSettings    1 = the launcher made the game's ROTK.ini (removed again when uninstalling)


INCLUDED OPEN-SOURCE SOFTWARE (licences in ROTKFix\licenses)
-----------------------------------------------------------
  DXVK 3.1.1 (zlib)    Vulkan renderer          ROTKFix\dxvk
  SDL3 (zlib)          controllers, launcher    SDL3.dll
  DSOAL, OpenAL Soft (LGPL)  3D sound and reverb  ROTKFix\sound
  Dear ImGui (MIT)     launcher interface
  stb_image, stb_image_write (MIT / public domain)
  Cinzel, EB Garamond fonts (SIL Open Font License)  launcher
  Liberation Sans (SIL Open Font License)  letters in the button pictures
ROTK Fix itself is MIT licensed.
