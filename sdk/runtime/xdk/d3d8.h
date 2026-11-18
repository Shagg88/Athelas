#pragma once
#include <mutex>

#include "gpu/state.h"

namespace d3d {

gpu::State& State();
std::recursive_mutex& Lock();
// Blocks until the next vertical blank has run (its count, event and the title's callback), or max_ms. For titles
// that wait for a blank by calling Sleep(0) in a loop: the thread sleeps instead of spinning a core.
void WaitVBlank(int max_ms);
// every draw the game makes, with the guest's own state, on the game's thread before the renderer sees it (the same on
// every PC and with either API, unlike the renderer's gpu::g_draw_observer): for game hooks that must decide the same
// on both PCs in lockstep (ROTK's movie frames, rotk_framerate.cpp)
extern void (*g_guest_draw)(const gpu::State& s);
// the transforms a title stored in the device itself (inlined SetTransform), into the renderer: what the
// library's state flush would load (a title's own override of that flush calls it)
void TakeDeviceTransforms();

}  // namespace d3d
