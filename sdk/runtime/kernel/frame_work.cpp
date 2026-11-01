// What the game's threads spent a frame on, for the renderer's hitch lines (gpu LogHitch): file reads, waits on
// objects and sleeps, on the frame's own thread (the one that presents) and on all the others. A frame that took
// long with none of the renderer's own work in it says here whether the game was reading, waiting on another
// thread, or busy itself.
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "kernel/kernel.h"

namespace kernel {

namespace {
struct Bucket {
    std::atomic<int64_t> read_ns{0}, wait_ns{0}, sleep_ns{0};
    std::atomic<uint64_t> reads{0}, read_bytes{0};
};
Bucket g_mine, g_others;
std::atomic<size_t> g_frame_thread{0};
std::mutex g_slow_lock;
std::string g_slow_name;
int64_t g_slow_ns = 0;

size_t ThisThread() {
    static thread_local size_t id = std::hash<std::thread::id>()(std::this_thread::get_id()) | 1;
    return id;
}
Bucket& Here() { return ThisThread() == g_frame_thread.load(std::memory_order_relaxed) ? g_mine : g_others; }
void Take(Bucket& b, FrameWork& out) {
    out.read_ns = b.read_ns.exchange(0);
    out.wait_ns = b.wait_ns.exchange(0);
    out.sleep_ns = b.sleep_ns.exchange(0);
    out.reads = b.reads.exchange(0);
    out.read_bytes = b.read_bytes.exchange(0);
}
}  // namespace

int64_t WorkNow() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void CountRead(int64_t ns, uint32_t bytes, const std::string& name) {
    Bucket& b = Here();
    b.read_ns += ns;
    b.reads++;
    b.read_bytes += bytes;
    std::lock_guard<std::mutex> lock(g_slow_lock);
    if (ns > g_slow_ns) {
        g_slow_ns = ns;
        g_slow_name = name;
    }
}

void CountWait(int64_t ns) { Here().wait_ns += ns; }
void CountSleep(int64_t ns) { Here().sleep_ns += ns; }

void MarkFrameThread() { g_frame_thread.store(ThisThread(), std::memory_order_relaxed); }

void TakeFrameWork(FrameWork& mine, FrameWork& others, std::string& slowest, double& slowest_ms) {
    Take(g_mine, mine);
    Take(g_others, others);
    std::lock_guard<std::mutex> lock(g_slow_lock);
    slowest.swap(g_slow_name);
    g_slow_name.clear();
    slowest_ms = double(g_slow_ns) / 1e6;
    g_slow_ns = 0;
}

}  // namespace kernel
