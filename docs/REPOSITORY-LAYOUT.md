# Repository layout

One repository for the whole Athelas project (from 26 October 2026; before that the launcher and each port had a
repository of its own, and their histories are all here).

| Folder | What it is |
| --- | --- |
| `launcher/` | Athelas.exe, the launcher (C++, Dear ImGui, SDL3); `launcher/build.sh` |
| `src/` | ROTK Fix, the fix for The Return of the King on PC (ROTKFix.dll and the d3d8/dinput8 stand-ins); `build.sh` |
| `mods/`, `examples/` | Valar Boons, and the starter mod |
| `ports/two-towers/` | The Two Towers port (`game/`: build.bat, release.bat, build_arm64.bat), its notes and SDK patches |
| `ports/rotk-xbox/` | The Return of the King (Xbox) port (`rotkxbox/`) |
| `ports/fellowship/` | The Fellowship of the Ring port (`fotrxbox/`) |
| `scripts/package.sh` | Builds the release zips (on Linux, from the ports' release builds) |
| `scripts/windows/` | `BUILD_RELEASE.bat` (the three ports, x64 and ARM64) and `UPDATE_SOURCE.bat` |
| `dist-files/` | README.txt and the other files that go into the release zip |
| `docs/` | Release notes for every version, the launcher spec, engine notes, this file |
| `workbench/` | Claude's Linux test and research scripts |
| `third_party/`, `licenses/` | Dear ImGui, miniz, DSOAL, the licences |

Branches: `main` is the project. `archive/<port>/<branch>` are the ports' old side branches; `public-github` is
GitHub's public repository as last pushed (1.5.1). Tags: `1.0` … `1.6.1` are Athelas releases; `tt-*`, `rotkx-*`,
`fotr-*` are the ports' own.

Each port keeps its own copy of the shared SDK (`ports/*/…/sdk`); they are kept identical by hand.

Private and never published: `ports/*/HANDOFF.md`, the ports' `notes/`, `workbench/`. The public repository on GitHub
is a separate snapshot without them.
