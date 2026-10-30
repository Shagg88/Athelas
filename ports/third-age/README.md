# The Third Age (Xbox) - PC port

The Lord of the Rings: The Third Age (original Xbox, 2004, NTSC-U), statically recompiled to a native PC
program, for the Athelas launcher, on the same runtime and renderer as The Fellowship of the Ring, The Two
Towers and The Return of the King.

Rules, as for the other ports:

- No game data, extracted files or generated code in this repository or in any release. The player supplies
  their own disc (a disc image or its files); the launcher reads it at run time.
- The US release only for now (title 4541005F); other releases get a warning with "Start anyway".
- Faithful by default: every enhancement is opt-in and the Original preset plays as the console did.

Status: being set up. `taxbox/` is the port (CMake, build scripts, the shared SDK); `notes/` keeps one note
per problem found.
