# 10. setjmp/longjmp: the Balin's Tomb crash (2026-10-16, GitHub issue #2)

A player's game stopped every time at the same place in Balin's Tomb, with this in the log:

```
indirect call to 000A056A has no recompiled function (return address 80D08830, esp D005FE10)
eax 00000001 ... [esp+068] 000A0614
```

## Cause

`sub_000A0530` is the game's protected call. It is shaped like Lua's `luaD_rawrunprotected`:
1. `setjmp(buf)` at 0xA0565, calling 0x100564, the MSVC CRT's `_setjmp3` (it writes the "VC20" magic, 0x56433230).
2. If that returns 0, `call [ebp+0x0C]` runs the protected function.
3. An error inside it calls `longjmp(buf, 1)` (0x1004E8). After the SEH unwind, `longjmp` restores ebp, ebx, edi, esi
   and esp from `buf` and ends with `jmp [buf+0x14]`, the address after the setjmp call (0xA056A), with eax 1.

The recompiler knew nothing of setjmp, so 0xA056A was not a function, and the jump trapped. Something the troll's
script does when an arrow hits it raises such an error. On the Xbox the error was simply caught.

## The fix

`[xbe] setjmp = [0x00100564]` in `config/game.toml`, and support for it in the recompiler
(`SetjmpContinuations` in `analysis.cpp`, and `codegen_emit.cpp`):
- Each call to a listed setjmp makes its return address a function and a continuation. That is the mechanism MSVC
  C++ catch blocks already use: `x86_icall` to a continuation throws `X86Resume{target, esp}`.
- The function that calls setjmp gets the same try/catch as a function with an SEH frame. It catches the resume (the
  innermost frame whose entry esp is above the resume esp), then runs the continuation with the restored registers.

TT has one setjmp call. ROTK Xbox has none.

## Checked

A test-only hook forced `longjmp(buf, 1)` from the protected call in the harness (Balin's Tomb):
- Execution resumed at the error path 0xA058E with eax 1.
- ebp was restored (buf + 0x54) and esp was exactly the frame's (ebp - 0x5C).
- It returned to 0xA0614.

Forced at the first call (the protected function skipped while the script loads), the game later crashed in
unrelated code, as expected. Forced at a later call, it played on for the rest of the run. The real error comes
from inside the protected function, as the player's log shows.
