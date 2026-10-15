# TT port: working folder

- `notes/01-xbe-analysis.md`: what's inside `Default.xbe`.
- `notes/02-existing-tools.md`: existing Xbox recomp and decomp tools and how they fit.
- `notes/03-plan.md`: recommended plan and the decision still open.
- `tools/`: Python analysis scripts (need `pip install capstone`). They contain no game data.
  - `xbe_info.py <xbe> [--json out]`: headers, sections, libraries, kernel imports.
  - `xbe_cfg.py <xbe> <symbols.txt> <kernel_names.json> <outdir>`: recursive disassembly, kernel and XDK cross-references, hardware access, `fs:` use.
  - `xbe_scan.py`: first linear-sweep scan. Superseded by `xbe_cfg.py` and kept only for reference.
  - `xbe_dis.py <xbe> <hexVA> [n]`: quick disassembly at an address.

Inputs that `xbe_cfg.py` needs:
- `symbols.txt` comes from `XbSymbolDatabaseCLI Default.xbe` (https://github.com/Cxbx-Reloaded/XbSymbolDatabase).
- `kernel_names.json` comes from Cxbx-Reloaded's `KernelThunk.cpp`.

Generated results are in `C:\TT\analysis\`. They are derived from the game, so never commit them.
