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

## The cause (2026-10-17)

The profiler also keeps a stack of section numbers: the count at 0x6B181C and up to 32 entries from 0x6B1820.
`sub_000E6080` pushes the current section, and `sub_000E6AC0` pops one and starts it again through
`sub_000E60B0`. Neither is atomic (`mov eax, [0x6B181C]` / `dec eax` / `mov [0x6B181C], eax`).

Two guest threads use them: thread 1C (started at 0xE7AE0, using the profiler from 0.1 s) and the main thread 14
(from about 1.2 s), so they overlap during startup, which is when the crash came. Thread 1C is the one in the
crash report above. On the Xbox's single core they almost never met inside these routines. Here guest threads run
side by side, so an update to the count could be lost and the count could go below 0. A pop then read its
"section" from below the stack, at 0x6B181C and the data before it: a time stamp (0x068BD578, the crash above) or
text ("ga-1", 0x312D6167, which the guard caught once on the simulated ARM run).

A start-up loop found it with a debug harness that logs each new thread to call the routines; 400 plain starts of
the headless harness never hit the race.

The fix, in `tt_hooks.cpp`: host versions of the four profiler routines (push, pop, start, and end with the frame
swap) take one recursive mutex around the originals, so one thread at a time is inside the profiler. The pop also
treats an empty or overfull stack as section 0, and the push skips when the stack is full, each logged
(`tt: profiler stack ...`). The guard from the stopgap stays as a last check.

Checked on the simulated ARM processor (qemu, which times threads very differently): without the fix, 4 of the 13
sweep levels crashed at startup; with it, all 13 ran their 300 s. In 4 of them the pop still found the stack empty
once, at about 1.7 s on thread 1C, which logs `profiler stack empty (0), section 0 started` and goes on. So thread
1C can also pop a section another thread pushed; the routines are now safe either way, and the profiler only
measures time.
