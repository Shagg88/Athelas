# 07: Launcher roadmap

These are the end goals, recorded on 2026-10-12. The plan is one launcher, from the ROTK Fix project, that detects and runs three games:

- ROTK PC
- The Two Towers Xbox (this port)
- later, ROTK Xbox, built on the same recompiler with XDK 5558 added

Nothing goes on GitHub until all three work.

The order of work doesn't change: the faithful game comes first. Every enhancement is a setting in `settings.toml` and is off by default.

Status markers below: **[have]** is done, **[part]** is partly done, **[todo]** is not started.

## 1. The launcher drives the port (high priority)

- **[todo]** Start-up arguments:
  - the game-files location (a disc image or an extracted folder);
  - the `settings.toml` path.
  
  Today the game path comes from the settings and the settings file sits beside the exe.
- **[part]** `settings.toml` is the only place settings live, with stable key names. **[todo]** Write a key reference listing each key's type, allowed values, default and a one-line description. The launcher builds the Two Towers options page from it.
- **[part]** Every enhancement is a plain setting. `fps60`, `widescreen`, `hud_edges`, `fov_percent`, `texture_dump` and `render_percent` already are. Keys like F1 (HUD toggle) stay as extras only.
- **[todo]** Clear, documented exit codes. `logs\tt.log` is readable already; check that start-up failures are logged in plain words.

## 2. Display settings matching the ROTK Fix launcher

- **[part]** These settings exist today:
  - `display_mode`: fullscreen, borderless or windowed
  - `width` and `height`
  - `vsync`
  - `fps_cap`
  - `render_percent`
- **[todo]** Which monitor to use, remembered by its Windows display name.
- **[todo]** Refresh rate.
- **[todo]** Frame cap using a high-resolution timer plus a short spin, and correct above 60 fps.
- **[todo]** Read resolutions and refresh rates from the monitor the game is actually on. ROTK Fix read the wrong monitor; avoid that bug.
- **[todo]** Anti-aliasing. **[have]** Internal resolution scale (`render_percent`).

## 3. One shared texture-pack format (high priority)

- **[have]** DDS packs named `<16-hex-hash>.dds`, plus dumping.
- **[todo]** Also accept PNG. ROTK Fix names its files `<width>x<height>_<16-hex-hash>.png`.
- **[todo]** Use the shared folder layout: `textures\replace\<pack name>\` and `textures\dump\`. Today packs sit in `textures\` with `dump` skipped.
- **[todo]** While dumping, log which textures each screen draws in 2D, so HUD pieces are easy to find.

## 4. Controllers and prompts

- **[todo]** Per-player button remapping, using launcher-friendly names: A/B/X/Y, LB/RB, LT/RT and so on.
- **[todo]** On-screen prompts follow player 1's controller type (Xbox, PlayStation or Switch), with coloured face buttons always on. If player 1 has no pad, use another player's style.

## 5. Keep the recompiler work reusable for ROTK Xbox (XDK 5558)

- **[have]** SDK changes are kept as separate patches in `sdk_4928/*.patch`. Keep doing this.
- **[todo]** Write up how 4928 support was added: the steps, the tools, and how each layout entry was proved. ROTK Xbox's 5558 support will follow the same steps.
- **[have]** Generic hooks stay game-neutral in the SDK, such as the screen-space HUD hook and visibility tests. Game-specific code lives only in `src/game`.

## 6. Release basics

- No game data or generated code in any repository.
- **[todo]** Check the XboxXBE-Recomp licence, since it decides how the port can be shared.
- Neutral name: no official names or logos in the program name, window title or icons.
  - **[todo]** The window title is currently "The Two Towers (PC port)". Revisit this before release.
