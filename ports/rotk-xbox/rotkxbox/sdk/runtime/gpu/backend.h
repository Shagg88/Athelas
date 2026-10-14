#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

#include "gpu/state.h"

namespace gpu {

// D3DCLEAR flags as the XDK defines them
enum : uint32_t {
    kClearTarget = 0xF0,  // ARGB channels
    kClearZ = 0x01,
    kClearStencil = 0x02,
};

class Backend {
public:
    virtual ~Backend() = default;
    // the title's back buffer size, the output window may differ
    virtual bool Init(uint32_t width, uint32_t height) = 0;
    // rects: count D3DRECTs (4 x int32), null for the whole viewport
    virtual void Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
                       uint32_t stencil) = 0;
    // indices: the title's 16 bit indices, null for non indexed draws, then start is the first vertex
    virtual void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) = 0;
    virtual void Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects_va,
                      uint32_t dst_points_va) = 0;
    virtual void Present(const Surface& front) = 0;
    // the CPU is about to touch this resource's memory: write GPU results back first
    virtual void Sync(uint32_t data_va, uint32_t size) = 0;
    // a lock that does not wait for the GPU (NOOVERWRITE): a result a frame or two old is what the console gives too
    virtual void SyncLate(uint32_t data_va) = 0;
    // SyncLate in two for the render thread: DeliverLate (any thread) writes the newest finished copy to guest memory,
    // false when there is none recent; RecordLate (in order with the draws) starts a copy for a later lock
    virtual bool DeliverLate(uint32_t) { return false; }
    virtual void RecordLate(uint32_t) {}
    virtual void Flush() = 0;
    virtual void SetGamma(const uint8_t* ramp /*3 x 256*/) = 0;

    // the XDK's visibility tests: the pixels drawn that pass the depth and stencil tests between Begin and End, which
    // names the test. QueryResult gives the newest finished count for the index, in the title's pixels; false while
    // none has finished. Read from the title's thread while the draws go on.
    virtual void BeginQuery() {}
    virtual void EndQuery(uint32_t /*index*/) {}
    virtual bool QueryResult(uint32_t /*index*/, uint32_t& /*pixels*/) { return false; }
    virtual bool Queries() const { return false; }  // counts them at all

    // the XDK's fences and busy checks against the render thread; a backend that draws at once is never behind
    virtual uint32_t Fence() { return ++fence_; }
    virtual bool FencePending(uint32_t) { return false; }
    virtual void WaitFence(uint32_t) {}
    virtual bool Busy(uint32_t /*resource*/) { return false; }
    virtual void WaitResource(uint32_t /*resource*/) {}

private:
    uint32_t fence_ = 0;
};

std::unique_ptr<Backend> CreateBackend();
// the same backend driven from a thread of its own: calls return once recorded
std::unique_ptr<Backend> CreateThreadedBackend(std::unique_ptr<Backend> inner);

// perf diagnostic: the title's thread recording for the render thread, and waiting on it
inline std::atomic<uint64_t> g_perf_capture_ns{0}, g_perf_wait_ns{0}, g_perf_waits{0};
inline std::atomic<uint64_t> g_perf_wait_kind_ns[6];  // read-back, resource, fence, flush, copy, next frame
inline std::atomic<uint64_t> g_perf_sync_late{0}, g_perf_sync_block{0};  // read-backs the title waited for, by cause

// F3: save the next frame, every draw and target, under diag::CaptureDir
inline std::atomic<bool> g_capture{false};
// \ (the key above Enter): the next frame as shown, a PNG in app::ScreenshotDir
inline std::atomic<bool> g_screenshot{false};
// [ : sharpening off while held off, to compare; a press switches it back on
inline std::atomic<bool> g_sharpen_off{false};
// the game shows a full-screen movie (its frame fills the back buffer): presented by [graphics] movie_display,
// movie_sharpening and cutscene_audio_delay instead of as a game frame
inline std::atomic<bool> g_movie{false};
// set by a game: how movie_display "fit" shows the movie on screen. kMovieFit: film bars above and below, its middle
// 16:9 is shown; kMovieWide: a widescreen film squeezed into the 4:3 frame (anamorphic), the whole frame shown 16:9;
// kMovie43: a 4:3 picture, shown 4:3
constexpr uint32_t kMovieFit = 0, kMovieWide = 1, kMovie43 = 2;
inline std::atomic<uint32_t> g_movie_shape{kMovieFit};
// set by a game that shows its 4:3 frame wider (app::FrameAspect): render targets get that much more width, so the
// frame is drawn at the screen's own resolution instead of stretched sideways. Off: targets keep the guest's shape.
inline std::atomic<bool> g_wide_targets{false};
// set by a game: called with each draw's state before it is drawn (to look at what the game draws). Null for none.
using DrawObserver = void (*)(const State& s);
inline DrawObserver g_draw_observer = nullptr;
// set by a game: alpha to coverage for cut-outs (leaves, grass, fences) when anti-aliasing is on
inline std::atomic<bool> g_alpha_edges{false};
// set by a game: fixed function fog distances scaled by this (a longer draw distance), density divided by it
inline std::atomic<float> g_fog_scale{1.0f};
// ] : the colour look shown, from [graphics] colour_look at start, the key steps through them while playing
inline std::atomic<uint32_t> g_colour_look{0};
constexpr uint32_t kColourLooks = 4;

}  // namespace gpu
