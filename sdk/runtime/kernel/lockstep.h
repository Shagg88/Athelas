// Lockstep mode (online co-op research, notes/lockstep/design.md): the guest runs the same on every PC given the same
// input. On with XBE_LOCKSTEP=1; off, nothing here does anything.
//
// One participant (a guest thread, or a host thread that runs guest code: DPCs, vertical blanks, the DirectSound
// pump, the audio mixer) runs at a time. The turn passes in a fixed cyclic order (the order participants were
// registered) at:
//  - a safepoint: generated code counts backward jumps (X86_BARRIER, XBE_LOCKSTEP builds) and every kQuantum of them
//    the thread passes the turn on, so a thread polling memory that another thread fills never starves it;
//  - a wait (Block) whose condition is false. A blocked participant checks its condition again when the turn comes
//    back to it; no host wake up decides anything.
// Time is virtual (Now, nanoseconds): it moves on by kQuantumNs at each safepoint pass and, when a whole round finds
// nobody able to run, jumps to the earliest deadline. Every clock the guest reads follows it (GuestNs, Now100ns,
// rdtsc), and timers fire as it passes them (OnTime). Host timing never decides what the guest sees.
#pragma once
#include <cstdint>
#include <functional>

namespace kernel::ls {

extern bool g_on;  // XBE_LOCKSTEP set, or online co-op (xdk/netplay.cpp) before the game starts
// built with the safepoints in generated code (XBE_LOCKSTEP): without them a thread that polls memory never passes the
// turn, so lockstep can't run
extern const bool kSafepoints;

struct Part;
// a new participant, registered by the thread that creates it while that thread holds the turn (or before any guest
// code runs), so the order is the same on every run
Part* Register(const char* name);
// on the participant's own host thread, before its first guest code: waits for its turn
void Enter(Part* p);
// the participant ends: the turn passes on and it is never scheduled again
void Leave();
// true when the calling host thread is a participant
bool InTurn();
// gives the turn to the next participant that can run and waits for it to come back (a yield)
void Pass();
// a backward jump in generated code (XBE_LOCKSTEP builds): counts, and passes every kQuantum
// Off (normal play), one test of a flag that never changes, which the processor predicts; on, only one thread runs
// at a time, so one plain counter serves them all (the thread that passes resets it, the next starts from 0).
constexpr uint32_t kQuantum = 20000;
inline uint32_t g_backedges = 0;
// above 0 while this thread holds a host lock another participant may take (the D3D device lock: guest code runs under
// it, D3D_InternalRelease): its safepoints don't pass the turn, or the next participant would wait on the lock while
// holding the turn, and both PCs would stop for good (review, 18 Nov 2026). The same on every PC.
inline thread_local int t_keep_turn = 0;
void BackedgePass();
inline void Backedge() {
    if (__builtin_expect(g_on, 0) && ++g_backedges >= kQuantum) BackedgePass();
}
// waits until cond() is true (checked only while holding the turn) or virtual time reaches deadline_ns
// (INT64_MAX: none). True when cond() became true.
bool Block(const std::function<bool()>& cond, int64_t deadline_ns = INT64_MAX);
// virtual nanoseconds since the start
int64_t Now();
// the floating-point control state participants run with, set again (x86: MXCSR 0x1F80); after anything that may
// have run foreign code on this thread (a present: overlays, drivers)
void ResetFloatState();
// called with the turn held whenever virtual time has moved on (timers firing, the kernel clock in guest memory)
void OnTime(std::function<void(int64_t now)> fn);
// a source of deadlines besides the participants' own (timers): when everyone waits, time may jump to the earliest
void OnDeadline(std::function<int64_t()> fn);

}  // namespace kernel::ls
