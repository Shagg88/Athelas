# Example Standalone Mod for Athelas / ROTKFix

This folder contains a minimal, working template for creating an external code mod (`.dll`) and its configuration (`.ini`) for *The Lord of the Rings: The Return of the King*.

## File Structure

- `mymod.c`: The C source code implementing `DllMain` and optional `ModInit()` exported entry point.
- `MyMod.ini`: Metadata declaring the mod's name, description, author, version, category, supported games, and custom settings.
- `build.sh`: Linux/WSL compilation script using `i686-w64-mingw32-gcc`.

## Building the Mod

Run the build script in WSL or Linux:

```bash
sh build.sh
```

This produces `MyMod.dll`.

## Installation

Copy both `MyMod.dll` and `MyMod.ini` into your game's `ROTKFix\mods\` directory:

```text
<GameFolder>\ROTKFix\mods\
├── MyMod.dll
└── MyMod.ini
```

Start the game or open the **Athelas Launcher**. The mod will appear in the **Mods** tab where players can view its details and toggle it on or off.
