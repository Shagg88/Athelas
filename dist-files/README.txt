Athelas
=======
By Shagg, with Claude (Anthropic).

An unofficial, free fan project that lets you play your own copies of three
Lord of the Rings games on a modern PC, all from one launcher, Athelas
(Athelas.exe - named after kingsfoil, the healing herb):

- The Return of the King (PC, 2003), through ROTK Fix: any screen size and
  refresh rate, smooth high frame rates, sharper graphics, and modern Xbox,
  PlayStation and Switch controllers.
- The Return of the King (Xbox), through its native PC port, ROTKXbox.exe.
- The Two Towers (Xbox), through its native PC port, TwoTowers.exe.

Not affiliated with or endorsed by Electronic Arts, New Line Cinema, Warner
Bros. or the Tolkien Estate. No game files are included: you need your own
copy of each game you play (the PC game installed, or your own Xbox disc as
an .iso or extracted files). ROTK Fix does not change or bypass the PC
game's copy protection. The Xbox ports skip the console's check for an
original Xbox disc, which a PC can't answer as it has no Xbox drive; they
don't decrypt or change the game's files.


REQUIREMENTS
------------
- Windows 10 or 11 (64-bit), or Linux / Steam Deck through Proton (see STEAM
  DECK AND LINUX).
- The Return of the King (PC): your own US copy, installed. A graphics card
  with a Vulkan 1.3 driver is recommended (most cards from 2016 on, with a
  current driver). Without one the game still runs through Windows' own
  Direct3D 8, but sharpening, colour looks and texture filtering are off;
  the launcher's About tab shows which one you have.
- The Two Towers and The Return of the King (Xbox): your own US Xbox copy,
  and a DirectX 12 graphics card.
- ROTK (PC) 3D sound and EAX need Microsoft's free "Visual C++ Redistributable
  (x86)", which most PCs already have from other games. Without it the game
  plays its original sound, and the launcher's sound setting says so.
- A controller is optional: Xbox, PlayStation, Switch and most others work.
- Laptops and integrated graphics: on first start the launcher picks the
  lighter Balanced preset for you.


ANTIVIRUS WARNINGS
------------------
ROTK Fix works the way game mods have always worked: it puts its own
d3d8.dll and dinput8.dll next to the game and adjusts the game in memory
while it runs. Some antivirus programs distrust exactly that, and the files
aren't code-signed (signing costs money every year), so Windows SmartScreen
may say the launcher is "unrecognised" (click "More info", then "Run anyway").
- Each download has a .sha256 file next to it. To check your download: in
  PowerShell, run  Get-FileHash Athelas-<version>.zip  and compare the result
  with the .sha256 file.
- If your antivirus removes a file, restore it and add an exception for the
  game folder, or report the false positive to your antivirus maker.
ROTK Fix never connects to the internet and only changes files in the game
folder (and the game's settings file in Documents, with a backup).


INSTALL
-------
1. Unzip everything into a folder of its own, for example C:\Games\Athelas
   (not into a game folder).
2. Run Athelas.exe (the launcher) and show it where your games are:
   - The Return of the King (PC): choose your installed game and press
     "Set up ROTK Fix here". This puts ROTK Fix's files (ROTKFix.dll,
     d3d8.dll, dinput8.dll, SDL3.dll and a ROTKFix folder) into the game's
     folder, next to rotk.exe - the game's own files aren't changed. If
     another mod's d3d8.dll or dinput8.dll is there, it is kept and put back
     when you remove ROTK Fix. A game in Program Files asks for Windows'
     permission.
   - The Xbox games: choose the folder you extracted the game to, or its disc
     image. Nothing is copied or changed there.
3. Choose your settings and press PLAY. Always start the games from the
   launcher from now on: it is where every setting lives, and it saves them
   for the game each time.
4. Optional: the About tab can put a shortcut to Athelas on the desktop and
   in the Start menu.

The first time you press Save or Play, the launcher adds the controller layout
to the game's Input.ini (the original is kept as Input.ini.original) and
updates the game's settings file in Documents (a copy of yours is kept).


UNINSTALL
---------
Open the launcher, go to About, and press "Remove ROTK Fix". It removes every
ROTK Fix file from the game folder and puts the game's own files back. Saved
games and screenshots are kept. Then delete the launcher's folder.


WHILE PLAYING
-------------
  \  or Print Screen   Screenshot (saved to the Screenshots folder)
  [                    Sharpening on/off, to compare
  ]                    Next colour look


THE TWO TOWERS (XBOX) - port 1.0.1
----------------------------------
An unofficial native PC port of the original Xbox game (US release),
recompiled for Windows, in the TT-Xbox folder and started from Athelas.
It plays as on the Xbox by default. Options include widescreen up to 32:9
ultrawide, frame rates up to 240, render resolution up to 300%,
anti-aliasing, texture filtering, sharpening and HDR, menus that fill the
screen with smooth upscaling, movie display modes, controller remapping and
adjustable damage for each side.
- Supported: the US Xbox release, as a disc image (.iso) or the extracted
  files. Other releases are recognised and you're warned before they start;
  they will most likely crash.
- Keyboard and mouse or controller (see KEYBOARD, MOUSE AND THE IN-GAME
  MENU below). The game's hints still show controller buttons.
- Settings: on the launcher's Two Towers page (or in TT-Xbox\settings.toml);
  the ones that take effect at once are also in the in-game menu (F10).
  Its saves are in TT-Xbox\saves.
- The game's own files are only read, never changed.
- Needs Windows 10 or 11 (64-bit) and a Direct3D 12 graphics card.

Keys while playing:
  F10                  Settings menu (the game pauses)
  \                    Screenshot (saved to the Screenshots folder)
  F1                   Hide or show the HUD
  [                    Sharpening off/on
  ]                    Next colour look

Reporting a problem: see REPORTING A PROBLEM below. The log is TT-Xbox\tt.log,
and after a crash the newest Game.<date>_<time>.dmp in TT-Xbox\CrashDumps.


THE RETURN OF THE KING (XBOX) - port 1.0.1
------------------------------------------
An unofficial native PC port of the Xbox version, in the ROTK-Xbox folder and
started from Athelas. It uses the same settings page as the PC version, with
frame rates up to 240, widescreen, and fixes for slowdowns and vanishing
enemies at high frame rates.
- Needs your own Xbox disc dump: the extracted folder or an .iso (US
  release). Nothing in it is changed.
- Sharper text and menus: if you also have The Return of the King (PC), its
  larger fonts and menu pictures are used (Graphics tab). Not required.
- Needs Windows 10 or 11 (64-bit) and a Direct3D 12 graphics card.
- Co-op: two players on one screen, with two controllers (or a controller
  and the keyboard); it can also be played online (see PLAYING CO-OP ONLINE).
- Keyboard and mouse, an in-game settings menu (F10), and pausing in the
  background: see KEYBOARD, MOUSE AND THE IN-GAME MENU below. With no
  controller plugged in, the game's hints show the keys.
- Gameplay tab (shared with the PC version): damage taken, enemy damage, and
  every level and character unlocked. All off by default.
- Known issue: the picture is darker than the PC version because it keeps the
  Xbox's brightness curve: raise Brightness to about 1.2-1.3 to match.

Its saves are in ROTK-Xbox\saves.

Reporting a problem: see REPORTING A PROBLEM below. The log is
ROTK-Xbox\rotkx.log, and after a crash the newest .dmp in ROTK-Xbox\CrashDumps.


KEYBOARD, MOUSE AND THE IN-GAME MENU (both Xbox ports)
------------------------------------------------------
Keyboard and mouse work as well as a controller, as player 1 or player 2
(Controls tab). The keys are the PC version of The Return of the King's:
  W A S D              Move (hold Left Alt to walk)
  Left mouse / Enter   Speed attack; in the menus, Enter goes on (and "press
                       start")
  Right mouse          Fierce attack
  Space / middle mouse Killing move
  Q                    Parry
  Left Ctrl / Bksp     Physical attack (Knock back), and back in menus
  Left Shift           Ranged attack
  Tab                  Jump back
  E                    Action (The Return of the King)
  F5                   Special ability (The Return of the King)
  Esc                  Pause; in the menus, back
  Arrow keys           Menus
Each key can be changed on the launcher's Controls tab: click a move, then
press the key or mouse button.

F10, or both stick clicks together on a controller, opens the settings menu
over the game, which stands still behind it. It holds the settings that take
effect at once (picture, controls, field of view, damage) and saves them when
it closes (F10, Esc, B or Start). LB and RB, or Page Up and Page Down, change
its page.

In the background: "Pause in the background" (Display tab) stops the game,
picture and sound, while another window is in front, and it carries on where
it was when you come back. "Mute in the background" only silences it. Both
are off by default.


ACHIEVEMENTS (all three games)
------------------------------
Each game has a set of achievements of its own: The Two Towers 14 (every
level, and the secret mission), The Return of the King 29 on both PC and Xbox
(every level, every hero, co-op, a hero at level 10, enemies defeated). They
are earned as you play, and what your saves already hold counts too, so an
old save earns the levels it has finished the first time you play.

A note shows in the bottom right corner when you earn one ("Achievement
notes", Gameplay tab; on the PC version with the Vulkan renderer, the
default). The launcher's Gameplay tab lists them, earned and not, and the
in-game menu (F10) has an Achievements page in the Xbox ports. They are kept
in achievements.txt: in the ROTKFix folder in the game folder for the PC
version, and in the TT-Xbox and ROTK-Xbox folders for the ports. They are
only kept on this PC; nothing is sent anywhere.


REPORTING A PROBLEM
-------------------
Open an issue at https://github.com/Shagg88/Athelas/issues (a free GitHub
account is needed) and choose "Bug report". Say which game and version
(PC or Xbox), what happened and what you did just before, and attach the
log: drag the file into the box.
  The Return of the King (PC)    ROTKFix.log in the game folder
  The Return of the King (Xbox)  ROTK-Xbox\rotkx.log
  The Two Towers                 TT-Xbox\tt.log
After an Xbox port crashes, also the newest .dmp file in its CrashDumps folder.
GitHub doesn't take .dmp files as they are: right-click the file, choose
"Compress to ZIP file" (Windows 11) or Send to > Compressed (zipped)
folder (Windows 10), and attach the zip.
The logs hold your settings, your graphics card and Windows version, and the
folder names where your games are, which can include your Windows user name.


TEXTURE PACKS
-------------
ROTK Fix can swap any of the game's textures for a PNG of your own. It
includes no game art: packs are made from your own copy.
- Making a pack: on the launcher's Graphics tab, tick Save game textures, and
  play. Every texture is saved once as a PNG in ROTKFix\textures\dump as the game
  loads it, named <width>x<height>_<16-character code>.png. A level loads all
  its textures when it starts, so starting each level once is enough.
  Untick it when you have what you need. While saving, the log lists the
  pictures each screen draws in 2D (HUD, menus), to find them in the dump.
- Using a pack: put the edited or upscaled PNGs in ROTKFix\textures\replace
  (subfolders are fine; Open folder on the Graphics tab goes there). The game
  uses them the next time it starts, while Use texture packs is ticked. A file
  can be any size, but should keep the original's shape, and its name must
  end with the original's 16-character code; anything before it is free,
  for example hud_health_256x64_0123456789abcdef.png.
Text, the movie picture and the screen effects cannot be replaced.


PLAYING CO-OP ONLINE
--------------------
Both Return of the King games have two-player co-op on one PC (The Two Towers
is single-player). A friend can join over the internet by streaming it: only
your PC needs the game, and your friend's controller arrives as a second
controller and plays player 2.
- Parsec (parsec.app): both of you install it. Your friend connects to your
  PC, and you allow their controller. Works with both games.
- Steam Remote Play Together: in Steam, add Athelas.exe as a non-Steam game
  ("Add a Game" > "Add a Non-Steam Game"), start it from Steam, then invite a
  friend from the Steam overlay (Shift+Tab). Your friend needs Steam, not the
  game. Steam only promises this for its own games, so if the invite option
  doesn't appear, use Parsec.
Then pick Co-op in the game's menu. Your friend sees your screen with a short
delay. For the PC game, check that "Player 2 (co-op)" on the launcher's
Controls tab is set to "Second controller" (the default).


STEAM DECK AND LINUX
--------------------
All three games run through Proton, Steam's Windows layer. This has been tested
on Linux in a setup like the Deck's (1280x800, Proton's graphics layers), not
yet on a real Deck: reports are welcome.
1. In Desktop Mode, unzip Athelas into a folder of its own, for example
   /home/deck/Games/Athelas.
2. In Steam: Games > "Add a Non-Steam Game to My Library", choose Athelas.exe.
   Then its Properties > Compatibility: tick "Force the use of a specific Steam
   Play compatibility tool" and pick the newest Proton.
3. Start it from Steam (Game Mode works too) and show it your games:
   - The Xbox games: your disc images (.iso) or extracted folders, anywhere on
     the Deck or an SD card.
   - The Return of the King (PC): a folder with your installed game, for
     example copied from a Windows PC. Athelas sets ROTK Fix up there and tells
     Proton to use its files; no launch options are needed. The original disc
     release's copy protection can't run under Proton, so that copy won't start.
- Controls: the Deck's controls work as an Xbox controller in all three games
  and in Athelas (the touchscreen and trackpads also work in Athelas).
- The Balanced preset suits the Deck; 60 fps is a good target for battery life.


WIDE AND ULTRAWIDE SCREENS
--------------------------
The game world fills any screen shape, from 4:3 to 32:9: you see more at the
sides instead of a stretched picture.
Menus, text and the HUD fill the screen up to 16:9. On wider screens they stay
16:9 in the middle, so text is not stretched; full-screen shading, fades and
the pause screen still reach the edges.
The trailer keeps its 4:3 shape with black bars. Story cutscenes and the
opening logos fill the width on 16:9, and the height on wider screens.
To try an ultrawide shape on a normal monitor, set Display mode to Windowed:
the Resolution list then also offers window sizes such as 2560 x 1080 (21:9).


SUPPORTED GAME VERSION
----------------------
The launcher checks your rotk.exe and shows "Game v1.0" when it is supported.
If it says "Unknown game version", please report it with your rotk.exe's size.
ROTK Fix does not change or bypass the game's copy protection.

The Xbox ports were made from the US releases. Another release (another
revision of the disc, or a modified copy) makes the launcher ask whether to
start it anyway: it may well work, but it hasn't been tested and could crash.

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
  Screen                 Which screen the game uses: main, or DISPLAY2 and so on (default main)
  DisplayMode            borderless, fullscreen or windowed (default borderless)
  Width                  Resolution; 0 = the screen's own (default 0)
  Height                 (default 0)
  RenderScale            Render resolution: % of that resolution the game is drawn at, then fitted to the
                         screen (50-200; above 100 = supersampling, costs speed). Fullscreen plays
                         borderless when this isn't 100 (default 100)
  RefreshRate            Fullscreen only; 0 = the screen's own (default 0)
  VSync                  1 = no tearing (default 1)
  Renderer               dxvk = Vulkan (recommended), native = Windows' own Direct3D 8 (default dxvk)
  DxvkFolder             Which DXVK inside the ROTKFix folder draws the game (default dxvk)
  MSAA                   Anti-aliasing: 0, 2, 4 or 8 (default 4)
  AnisotropicFiltering   Texture filtering: 0, 2, 4, 8 or 16 (default 16)
  TextureLODBias         Texture sharpness: 0 = game's own, down to -1.0 = sharpest (default -0.50)
  Sharpening             0-100, 0 = off (also turns off the automatic HUD sharpening). [ switches it on and off while playing (default 60)
  Brightness             Added to the game's own brightness option (0.5-2.0) (default 1.00)
  ColourLook             original, film, vivid or dark. ] switches while playing (default original)
  ColourLookStrength     0-100 (default 100)
  WidescreenFix          1 = see more at the sides instead of a stretched picture (default 1)
  FieldOfView            Percent of the game's own view angle (70-150) (default 100)
  DrawDistance           Percent of the game's own (50-800) (default 100)
  HUDEdges               Xbox version: 1 = HUD pieces at the screen edges, with WidescreenFix=1 (default 0)
  SharperText            Xbox version: 1 = text, title and main menu with the PC version's larger fonts and
                         pictures (needs the PC game's Data\Game files in this folder); other menu
                         pictures scaled up sharply (default 0)
  UseGameBrightness      1 = the game's own brightness option works in every display mode

[Frame]  Frame rate
  TargetFPS              Frames per second (the game was made for 60) (default 60)
  Limiter                1 = ROTK Fix's smooth limiter, 0 = the game's own (62 fps, busy CPU) (default 1)
  ShowFPS                1 = a frame rate counter in the corner of the screen (default 0)
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
  ButtonNames            Button pictures in on-screen hints: auto, xbox, playstation, nintendo; original = the game's own text; keyboard = key caps (Xbox version) (default auto)
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

[Gameplay]  Gameplay (both versions of The Return of the King)
  DamageTaken            Damage you take, % of the game's (10-500) (default 100)
  EnemyDamage            Damage your hits do to enemies, % of the game's (10-500) (default 100)
  UnlockAll              1 = every level and character can be chosen (default 0)

[Window]  In the background (Xbox version)
  PauseInBackground      1 = the game stops while another window is in front (default 0)
  MuteInBackground       1 = no sound while another window is in front (default 0)

[Keyboard]  Keyboard and mouse (Xbox version; on PC the game's own keyboard controls are used)
  Enabled                1 = play with the keyboard and mouse as well as a controller (default 1)
  Player                 1 or 2: which player the keyboard plays as (default 1)
  MoveForward ... MenuRight  The keys for each move, comma separated: A-Z, 0-9, F1-F12 (not F3 or F10),
                         Space, Enter, Esc, Tab, Backspace, CapsLock, LShift, RShift, LCtrl, RCtrl, LAlt, RAlt,
                         Up, Down, Left, Right, Insert, Delete, Home, End, PageUp, PageDown, Numpad0-9,
                         NumpadStar, NumpadPlus, NumpadMinus, NumpadDot, NumpadSlash, Minus, Equals, Comma,
                         Period, Slash, Semicolon, Apostrophe, Grave, Mouse1-Mouse5 (left, right, middle, 4, 5)
                         Moves: MoveForward MoveBack MoveLeft MoveRight Walk SpeedAttack PhysicalAttack Parry
                         FierceAttack Action JumpBack RangedAttack KillingMove Pause Back Special RightStick
                         MenuUp MenuDown MenuLeft MenuRight

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


INCLUDED OPEN-SOURCE SOFTWARE (licences in the licenses folder, and in
ROTKFix\licenses in the game folder once ROTK Fix is set up there)
-----------------------------------------------------------
  DXVK 3.1.1 (zlib)    Vulkan renderer          ROTKFix\dxvk
  SDL3 (zlib)          controllers, launcher    SDL3.dll
  DSOAL, OpenAL Soft (LGPL)  3D sound and reverb  ROTKFix\sound
  Dear ImGui (MIT)     launcher interface
  stb_image, stb_image_write (MIT / public domain)
  Uncial Antiqua, Cinzel, EB Garamond fonts (SIL Open Font License)  launcher
  Liberation Sans (SIL Open Font License)  letters in the button pictures
  XboxXBE-Recomp (MIT, (c) 2026 TGP482)  runtime in TwoTowers.exe and ROTKXbox.exe
  toml++ (MIT)         settings files in the Xbox ports
  (the Xbox ports' own lists are in TT-Xbox\THIRD_PARTY.txt and ROTK-Xbox\THIRD_PARTY.txt)
ROTK Fix and Athelas are MIT licensed, (c) 2026 Shagg.
