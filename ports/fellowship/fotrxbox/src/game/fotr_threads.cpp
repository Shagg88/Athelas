// FOTR Xbox: the game's timer thread takes turns with its main thread, as on the console's one processor (#21, #25).
//
// Besides its main thread the game runs one more for good: it starts at 0x4EDF0, wakes every 30 ms (or on one of
// two events) and runs a timer queue (0x4F0C0, under the critical section 0x1EB3E8) whose entries call back into
// the game. On the Xbox both share one processor, so a tick never runs in the middle of the main thread's work. Here
// they run at the same time. Two crash reports (#21, #25; the same player, 144 fps) found an object freed at a scene
// change whose fields had been written over with floats, as by a callback still writing into an object the main
// thread had just freed and the heap had given out again.
//
// So the main thread holds a turn while it works and hands it over whenever the console's would give up its
// processor: while it waits, sleeps, reads a file or shows a frame (kernel::g_blocking); a tick waits for the turn. A tick never waits
// more than a quarter of a second (a long load without a frame), so neither thread can hold the other up for good.
// FOTR_TURNS=0 (test builds) turns it off to compare.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "core/log.h"
#include "kernel/kernel.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"

namespace {
std::timed_mutex g_turn;
thread_local bool t_main = false;   // this thread shows the frames
thread_local bool t_holds = false;  // and holds the turn
std::atomic<uint64_t> g_ticks{0}, g_late{0};
std::atomic<int> g_waiting{0};  // ticks waiting for the turn

bool On() {
    static const bool on = [] {
#ifndef _WIN32
        const char* e = std::getenv("FOTR_TURNS");
        if (e && *e == '0') return false;
#endif
        return true;
    }();
    return on;
}
}  // namespace

namespace {
thread_local int t_depth = 0;  // inside kernel calls that wait (they nest: a frame shown waits too)

// kernel::g_blocking: the main thread hands the turn over while it waits, sleeps, reads a file or shows a frame, as
// the console's would give up its processor there
void Blocking(bool done) {
    if (!t_main) return;
    if (!done) {
        if (t_depth++ == 0 && t_holds) {
            t_holds = false;
            g_turn.unlock();
        }
    } else if (--t_depth == 0 && !t_holds) {
        // a waiting tick goes first (the lock alone isn't fair: the main thread could take it straight back)
        for (int i = 0; i < 400 && g_waiting.load(std::memory_order_relaxed); i++)
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        g_turn.lock();
        t_holds = true;
    }
}

// the main thread (the game's thread 14, XAPI's main), known by 0xE5130, which it runs first as the game starts (the
// loading screen's thread runs it later too)
std::atomic<bool> g_have_main{false};
void MainThread(const char* how) {
    if (t_main || !On() || g_have_main.exchange(true)) return;  // the first thread only (the loading screen's runs it too)
    t_main = true;
    kernel::g_blocking = Blocking;
    if (t_depth == 0 && !t_holds) {
        g_turn.lock();
        t_holds = true;
    }
    LOGR("fotr: the timer thread takes turns with the main thread (the console's one processor; main thread by %s)", how);
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000E5130);
X86_FUNC(sub_000E5130) {  // a frame shown: first on the main thread as the game starts
    MainThread("its first frame");
    __imp__sub_000E5130(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_0004F0C0);
X86_FUNC(sub_0004F0C0) {  // the timer thread's tick
    if (t_main) return __imp__sub_0004F0C0(ctx, base);
    if (!On()) {
        if (++g_ticks % 2000 == 0) LOGI("fotr: %llu timer ticks", (unsigned long long)g_ticks.load());
        return __imp__sub_0004F0C0(ctx, base);
    }
    auto t0 = std::chrono::steady_clock::now();
    g_waiting++;
    bool got = g_turn.try_lock_for(std::chrono::milliseconds(250));
    g_waiting--;
    uint64_t n = ++g_ticks;
    static std::atomic<uint64_t> waited_us{0};
    waited_us += uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
    if (n % 2000 == 0)
        LOGI("fotr: %llu timer ticks, %.2f ms average wait for the turn", (unsigned long long)n, waited_us.load() / 1000.0 / double(n));
    if (!got) {
        uint64_t late = ++g_late;
        if (late <= 3 || late % 100 == 0)
            LOGR("fotr: a timer tick ran without its turn after 250 ms (%llu of %llu ticks so far)",
                 (unsigned long long)late, (unsigned long long)n);
    }
    __imp__sub_0004F0C0(ctx, base);
    if (got) g_turn.unlock();
}
