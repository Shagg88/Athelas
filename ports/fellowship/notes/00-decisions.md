# Decisions log (the single plan; one or two lines each, newest last)

- 2026-10-20: Port The Fellowship of the Ring (original Xbox, USA, title 56550004, XDK 4831, D3D8LTCG) the same way as
  The Two Towers and The Return of the King: XboxXBE-Recomp, our fork of it (ROTK's `sdk/`, TT patch + ROTK patch).
  Why: both shipped on it; FOTR's XDK (4831) is next to TT's (4928). No public recomp of FOTR found (searched 2026-10-20).
- 2026-10-20: The renderer is the SDK's own Direct3D 8 -> Direct3D 12 layer (HLE, no NV2A emulation), as in TT/ROTK. The
  user's "native-port playbook" (Gears 2) assumes an emulated GPU to replace; here that end state is the starting point.
  Its frame record/replay tool and estimate-vs-actual table are worth adding later.
- 2026-10-20: XDK symbols: xbe_symbols with the 5849 signatures (no 4831 set exists) + XbSymbolDatabase (420 names,
  92 of them D3D8LTCG with their register conventions) merged by `tools/merge_ltcg.py`, which writes `[abi]` from
  XbSymbolDatabase's `_<pops>__LTCG_<reg><arg>` names.
- 2026-10-20: `sdk/xdk/4831/layout.toml` started as 4928's; the CDevice keys checked so far match 4928 exactly.
- 2026-10-20: Rules carried over: local commits only, never pushed; no game data (xbe, extracted files, generated code,
  analysis dumps) in the repo; faithful defaults, enhancements opt-in; credits "By Shagg, with Claude (Anthropic)".
- 2026-10-21: Frame rate 30 by default (the console's interval two), 60-240 opt-in: the game steps by real time.
- 2026-10-21: SDK change kept in the FOTR copy until TT/ROTK sweeps pass: every draw first walks the title's own
  pushed commands (before, they ran at the next push request, after the draw); later every device call does
  (`DeviceLock`).
