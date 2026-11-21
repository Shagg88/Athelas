// Lockstep mode: one participant at a time, the turn in a fixed cyclic order, virtual time (lockstep.h)
#include "kernel/lockstep.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

namespace kernel {
bool AtDispatchLevel();  // kernel.h
}

namespace kernel::ls {

void ResetFloatState();  // lockstep.h

#ifdef XBE_LOCKSTEP
const bool kSafepoints = true;
#else
const bool kSafepoints = false;
#endif
// replays (xdk/xapi.cpp) need the game to run the same each time, so recording or playing one turns it on too, in a
// build with the safepoints only (without them a thread polling memory would never pass the turn: the game would stop)
bool g_on = std::getenv("XBE_LOCKSTEP") || (kSafepoints && (std::getenv("XBE_PAD_RECORD") || std::getenv("XBE_PAD_REPLAY")));

namespace {

constexpr int64_t kQuantumNs = 20000;  // virtual time a pass at a safepoint counts for (20 us)

}  // namespace

struct Part {
    std::string name;
    size_t index = 0;
    bool alive = true;
    int64_t deadline = INT64_MAX;  // while blocked with a timeout
};

namespace {

std::mutex g_m;  // guards g_holder and the hand over; the rest is only touched by the participant holding the turn
std::condition_variable g_cv;
std::vector<Part*> g_parts;  // registration order (never shrinks: index is the place in the cycle)
Part* g_holder = nullptr;
std::atomic<int64_t> g_now{0};  // changed only by the holder; read by host threads too (frame pacing)
uint32_t g_idle = 0;  // turns passed on in a row by blocked participants, with nothing done in between
std::vector<std::function<void(int64_t)>> g_hooks;
std::vector<std::function<int64_t()>> g_deadlines;
thread_local Part* t_part = nullptr;

size_t Alive() {
    size_t n = 0;
    for (Part* p : g_parts) n += p->alive;
    return n;
}

void RunHooks() {
    for (auto& h : g_hooks) h(g_now.load(std::memory_order_relaxed));
}

// the next participant in the cycle that is alive (me when there is none)
Part* Next(Part* me) {
    size_t n = g_parts.size();
    for (size_t i = 1; i <= n; i++) {
        Part* p = g_parts[(me->index + i) % n];
        if (p->alive) return p;
    }
    return me;
}

// with g_m held: hand the turn to the next participant
void HandOn(Part* me) {
    Part* next = Next(me);
    g_holder = next->alive ? next : nullptr;  // nobody left: the next one registered takes it
    g_cv.notify_all();
}

// with g_m held: wait until the turn is mine again (a long wait is logged: a host lock held across a pass, or a
// participant that blocks outside the scheduler)
void WaitTurn(Part* me, std::unique_lock<std::mutex>& l) {
    while (g_holder != me) {
        if (!g_cv.wait_for(l, std::chrono::seconds(5), [&] { return g_holder == me; }))
            LOGR("lockstep: %s has waited 5 s for its turn; %s holds it", me->name.c_str(),
                 g_holder ? g_holder->name.c_str() : "nobody");
    }
    ResetFloatState();
}

}  // namespace

// the SSE rounding and denormal state every participant runs with (round to nearest, no flush to zero, exceptions
// masked: 0x1F80, the default): a driver or overlay that changed it on a thread (a hook in Present) would make that PC's
// sums differ. Set again whenever a participant gets the turn (and after each present, d3d8.cpp). x86 only.
void ResetFloatState() {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    if (_mm_getcsr() != 0x1F80) _mm_setcsr(0x1F80);
#endif
}

Part* Register(const char* name) {
    auto* p = new Part;
    p->name = name;
    std::lock_guard<std::mutex> l(g_m);
    // the order must be the same on every PC: participants are only registered by the one holding the turn (or before
    // any runs)
    if (g_holder && g_holder != t_part) LOGR("lockstep: %s registered by a thread without the turn", name);
    p->index = g_parts.size();
    g_parts.push_back(p);
    if (!g_holder) g_holder = p;
    LOGI("lockstep: participant %zu is %s", p->index, name);
    return p;
}

void Enter(Part* p) {
    t_part = p;
    std::unique_lock<std::mutex> l(g_m);
    WaitTurn(p, l);
    g_idle = 0;
}

void Leave() {
    Part* me = t_part;
    if (!me) return;
    t_part = nullptr;
    std::lock_guard<std::mutex> l(g_m);
    me->alive = false;
    if (g_holder == me) HandOn(me);
}

bool InTurn() { return t_part != nullptr; }

int64_t Now() { return g_now.load(std::memory_order_relaxed); }

void OnTime(std::function<void(int64_t)> fn) { g_hooks.push_back(std::move(fn)); }
void OnDeadline(std::function<int64_t()> fn) { g_deadlines.push_back(std::move(fn)); }

void Pass() {
    Part* me = t_part;
    if (!me || t_keep_turn) return;  // (a host lock held: lockstep.h t_keep_turn)
    g_idle = 0;
    g_now.fetch_add(kQuantumNs, std::memory_order_relaxed);
    RunHooks();
    std::unique_lock<std::mutex> l(g_m);
    HandOn(me);
    WaitTurn(me, l);
}

void BackedgePass() {
    g_backedges = 0;
    // at DISPATCH_LEVEL nothing else ran on the console's one processor: the turn stays until the IRQL comes down
    if (::kernel::AtDispatchLevel() || t_keep_turn) return;
    Pass();
}

bool Block(const std::function<bool()>& cond, int64_t deadline_ns) {
    Part* me = t_part;
    if (!me) {
        // not a participant (a host thread nobody registered): it can't be scheduled, so it polls; logged once
        static bool told = false;
        if (!told) LOGR("lockstep: a thread outside the scheduler waits (its result may differ between PCs)");
        told = true;
        while (!cond() && g_now < deadline_ns) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return cond();
    }
    for (;;) {
        if (cond()) {
            g_idle = 0;
            return true;
        }
        if (g_now >= deadline_ns) {
            g_idle = 0;
            return false;
        }
        if (++g_idle > Alive()) {
            // a whole round and nobody could run: time jumps to the earliest deadline
            int64_t next = deadline_ns;
            for (Part* p : g_parts)
                if (p->alive && p != me && p->deadline < next) next = p->deadline;
            for (auto& d : g_deadlines) next = std::min(next, d());
            g_idle = 0;
            if (next == INT64_MAX) {
                static int told = 0;
                if (told++ < 5) LOGR("lockstep: every participant waits and none has a deadline (%s last)", me->name.c_str());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                // a later deadline: time moves on to it; one already due (a timer set to fire at once, after the last
                // look): its hooks run now, at the same time
                if (next > g_now) g_now = next;
                RunHooks();
            }
            continue;  // check my own condition at the new time before passing on
        }
        me->deadline = deadline_ns;
        {
            std::unique_lock<std::mutex> l(g_m);
            HandOn(me);
            WaitTurn(me, l);
        }
        me->deadline = INT64_MAX;
    }
}

}  // namespace kernel::ls
