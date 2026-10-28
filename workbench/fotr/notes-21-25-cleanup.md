# FOTR #21 / #25: the scene-change clean-up finds an object written over (time-boxed hunt, Oct 24)

Symptom: both reports come from one player (i5-13600K, RTX 5070, fps 144). The crash is in 0x1249B0, the destructor
called by the clean-up pass (0x113960 -> 0x8C490 -> 0x8C100 -> 0x2AEB0). At 0x1249F6 it runs `mov ecx,[eax]` with
eax = obj+0x20, the owned object, because obj+0x18 == 1. That field held a float: 3F398AB2 (0.725) in #21 and
3A509C07 (0.0008) in #25. The objects were at 01E7FE60 and 01878560, both in the game heap. 1.5.1's guard
(fotr_cleanup.cpp) clears exactly this field, so 1.5.1 plays on.

What was tried:
- A freed-block checker (Linux test build, FOTR_HEAPCHECK=1) on the game's statically linked RtlFreeHeap (0x7416F)
  and RtlAllocateHeap (0x739B8). Freed blocks of 4 KB or less are filled with FEEEFEEE and held back (1 MB), then
  checked when they are really freed. Scenes run 200 s at 144 fps: Bywater, Hobbiton, Bag End, Green Hill Country,
  Weathertop (including a death and a new game), Gates of Moria. About 600,000 blocks were checked. No block was
  written after its free, and nothing crashed.
- Host-side writers ruled out: renderer read-backs (DeliverLate and Sync) write only into surfaces the game is
  locking at that moment, and in the 0x80000000 window, never the heap. Sound packet status words are integers,
  not floats.

Where the evidence points (not proven):
- A second game thread (20, entry 0x4EDF0) wakes every 30 ms or on two events and runs a timer queue (0x4F0C0)
  under critical section 0x1EB3E8, calling callbacks through [obj+8](obj[0xC], obj+0x10). On the console this thread
  and the main thread share one core (and the runtime ignores thread priority). Here they run at the same time. A
  callback writing a float (a fade or volume, a position) into an object the main thread has just freed would leave
  exactly this kind of damage. A scene change is when most objects are freed.
- Next steps when there's a save near Bywater: log the callback addresses at 0x4F1EF (needs an instruction hook);
  run with [system] one_core = true at 144 fps to see whether the crash stops; and try FOTR_HEAPCHECK on a real
  Hobbiton -> Bywater walk (the scripted pad never crosses a scene exit).
