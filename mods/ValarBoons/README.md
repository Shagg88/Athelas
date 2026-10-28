# Valar Boons — Standalone Mod for ROTK

A **Mythological Blessings and Combat Powers Mod** for *The Lord of the Rings: The Return of the King (PC)*.

This mod is an **external standalone DLL** that interfaces dynamically with Athelas (`ROTKFix`). It requires **zero changes** to the core game files and no hardcoded dependencies in Athelas.

---

## ⚡ Default Mythological Blessings (Configurable via INI)

The mod includes 3 default mythological boons out-of-the-box. Every boon, effect, duration, cooldown progression, key binding, and visual color is **fully configurable and extensible** via `ValarBoons.ini`:

| Valar | Aspect | Default Buff Effects |
| :--- | :--- | :--- |
| **Manwë** *(Lord of the Breath of Arda)* | **Wind Cleave** | Outgoing damage **+60%** + **15 flat wind damage**. Cyan aura glow. |
| **Varda** *(Elentári, Queen of the Stars)* | **Starlight Ward** | **75% incoming damage absorbed** + **Retribution Holy Shock (45 dmg)** dealt back to attackers + **+40 flat outgoing holy damage**. Golden aura glow. |
| **Aulë** *(The Smith & Maker of Mountains)* | **Mithril Poise** | **Clamps max incoming damage to 10** per blow + **2.2x Heavy Shattering Strike**. Amber aura glow. |
| **Avatar of the Valar** | **Divine Synergy** | Activates all active boons simultaneously for devastating combat synergy. |

> [!NOTE]
> These are the default configurations. You can adjust values, add more boons (`[Boon4]`, `[Boon5]`, etc.), adjust cooldown curves, or alter combat scaling directly in `ValarBoons.ini`.

---

## 🎮 Controls (In-Game)

When the game window is active, the default hotkeys are:

| Action | Default Keys | Effect |
| :--- | :--- | :--- |
| **Gift of Manwë** | <kbd>1</kbd>, <kbd>F1</kbd>, <kbd>Num 1</kbd> | Toggle Wind Cleave |
| **Gift of Varda** | <kbd>2</kbd>, <kbd>F2</kbd>, <kbd>Num 2</kbd> | Toggle Starlight Ward |
| **Gift of Aulë** | <kbd>3</kbd>, <kbd>F3</kbd>, <kbd>Num 3</kbd> | Toggle Mithril Poise |
| **Awaken All Boons** | <kbd>4</kbd>, <kbd>F4</kbd>, <kbd>Num 4</kbd> | Activate all boons together |
| **Relinquish Blessings** | <kbd>0</kbd>, <kbd>F10</kbd>, <kbd>Num 0</kbd> | Deactivate and reset all boons |

All hotkeys are dynamically registered with ROTKFix's input router (`RF_RegisterHotkey`), ensuring hotkey presses are processed reliably without interfering with native game controls. Any key can be rebound via `ValarBoons.ini`.

---

## 📁 Architecture & Source Organization

The mod is cleanly divided into specialized modules for maintainability and scalability:

```text
mods/ValarBoons/
├── valar_boons.h       # Shared data models, constants, and ROTKFix API prototypes
├── valar_main.c        # Lifecycle management, DLL entry points, logging, and API resolution
├── valar_config.c      # INI configuration parser, key binding parser, and progression curve loader
├── valar_logic.c       # Hit filter hooks, cooldown progression, and combat math calculations
├── valar_render.c      # Direct3D9 rendering hooks, HUD badges, and screen aura overlays
├── ValarBoons.ini      # Fully customizable mod and boon configuration file
├── build.sh            # Standalone MinGW compilation script
└── README.md           # Documentation and usage guide
```

---

## ⚙️ Configuration (`ValarBoons.ini`)

All aspects of the mod can be customized inside `ValarBoons.ini`:

### `[Mod]` Section
Metadata parsed by the **Athelas Launcher GUI**:
* `Name`, `Description`, `Author`, `Version`, `Category`, `Games`: Launcher catalog entries.
* `Hotkeys`: Human-readable control summary displayed under the Launcher's **Mods** tab.
* `Enabled`: Default mod activation switch (`1` = enabled, `0` = disabled).

### `[General]` Section
* `BoonCount`: Total number of active boon definitions (`[Boon1]` .. `[BoonN]`).
* `EnableAllKey`: Comma-separated keys to activate all boons simultaneously (e.g. `4, F4, Num4`).
* `ResetKey`: Comma-separated keys to deactivate all boons (e.g. `0, F10, Num0`).
* `ShowHud`: `1` to display active boon badges and timers in the top-left corner; `0` to hide.
* `ShowAura`: `1` to display screen-edge aura glow and hit-flash effects; `0` to disable.

### Logging
Logging is automatically governed by ROTKFix's global `[Log] Enabled` setting in `ROTKFix.ini`. When logging is enabled, mod events are written to `ValarBoons.log` in the game directory and to `ROTKFix.log`.

### `[BoonN]` Sections
Each boon can be fully customized:
* `Name`: Display name shown in HUD badges.
* `Title` / `Description`: Text pushed to ROTKFix in-game banner notifications (`RF_ShowNotificationEx`).
* `Key`: Activation keys (e.g., `1, F1, Num1`).
* `Group`: Group ID (`0` = independent; `> 0` = shared group cooldown & mutual exclusivity).
* `Color`: `R, G, B` color values (0–255) for HUD badges and screen-edge auras.
* `Duration`: Active duration in seconds (`0.0` = indefinite toggle until pressed again).
* `Cooldown`: Cooldown seconds before reactivation. Supports escalating progression curves (e.g., `5, 10, 20` where subsequent uses increase cooldown up to the final limit).
* `DamageMultiplier`: Outgoing damage percentage (e.g., `160` = +60% damage, `100` = base damage).
* `FlatDamage`: Bonus flat damage added to every outgoing hit.
* `DamageReduction`: Incoming damage reduction percentage (e.g., `75` = 75% incoming damage absorbed).
* `MaxDamageClamped`: Maximum cap on incoming damage per blow (e.g., `10`; `0` = disabled).
* `HolyShockDamage`: Retribution shock damage dealt directly back to attackers upon hit (`0` = disabled).

---

## 🚀 Installation

Copy the built `ValarBoons.dll` and `ValarBoons.ini` into your game's mod directory:

```text
<ROTK Game Directory>\
├── rotk.exe
├── d3d8.dll (or ROTKFix.dll)
├── ROTKFix.ini
└── ROTKFix\
    └── mods\
        ├── ValarBoons.dll
        └── ValarBoons.ini
```

Start the game or toggle the mod in the **Athelas Launcher** under the **Mods** tab.

---

## 🛠️ Building

### Compile Standalone DLL
Using WSL or Linux with `i686-w64-mingw32-gcc`:
```bash
cd mods/ValarBoons
sh build.sh
```
