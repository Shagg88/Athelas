# 9. The rare startup crash (2026-10-16)

About one start in thirty, The Two Towers crashed a second or two after starting. It was caught once under Wine
(the 2026-10-16 00:30 build, `TwoTowers-mingw.exe`):

```
[1.748] exception C0000005 at 00000001407b7db9 on guest thread 1C
        read 2692871AC, guest 692871AC
        eax 006B1A08 ecx 692871A0 edx 00000000 ebx 00000000 esp D00CFF38 ebp 46D55054 esi 00000000 edi 00000000
        [esp+020] 000E0102   [esp+030] 001085E8
```

The same build started cleanly the next two times. The minidump and that exe are kept locally (not in the
repository) in `work/crash/`, with an earlier dump from 2026-10-15 22:28. That one came from a build whose layout is lost, so its
address can't be mapped.

## Where it is

The host address is in `sub_000E60B0`, one of the game's own frame-profiler functions. It starts a timing
section: `rdtsc`, then it adds the elapsed time to the current section. The profiler keeps two slots of 0x158
bytes at 0x6B18B0, and [0x6B1818] says which slot is in use. Each slot holds:

| Offset | What |
|---|---|
| +0x00 | start TSC |
| +0x10 | the current section (0 to 19) |
| +0x18 | 20 sections of 16 bytes |

The crash is the read of [ecx+0x0C] at 0xE610A, where ecx = slot + 0x18 + [slot+0x10] * 16.

The registers show slot 1 (eax 0x6B1A08), with [0x6B1A18] = 0x068BD578 (mod 2^28) instead of a section
number. That looks like a TSC value, not a section.

## What writes the field

Only two places write [slot+0x10]:
- `sub_000E60B0` writes its argument. All 55 callers push a constant from 1 to 0x11.
- The frame swap at 0xE6821 to 0xE68C4 writes 0. It toggles [0x6B1818] with `xor 1`.

Both always write values in range. So the bad value most likely comes from a stray write elsewhere, or from two
guest threads using the profiler at once. On the Xbox it ran on one core; here guest threads run in parallel on
host threads, and nothing serialises them.

The earlier sighting, in guest input code, may be the same kind of thing. The headless harness ran 80 starts
with no crash; it schedules threads differently.

## Next steps

- Read guest memory around 0x6B1A00 from the minidump, if it holds that range, to see the pattern of the bad
  value.
- Try a hardware watchpoint on 0x6B1A18 (a debug build that guards its pages, or a host thread checking the
  value every frame) to catch the writer.
- A safe stopgap: a hook at 0xE60FC that treats a section outside 0 to 19 as 0. The profiler only measures
  time, so the game would play the same. This hides the symptom, not the cause.

## The stopgap (2026-10-16)

`TT_ProfilerStart` (0xE60FC) and `TT_ProfilerEnd` (0xE65B1) in `tt_hooks.cpp` take a section outside 0 to 19
as 0 before it is used. The log says so (`tt: profiler section ... out of range`), at most three times a run.
Checked in the harness by forcing the bad value (0x068BD578) into the field: the line was logged and the game
went on. The cause is still open.
