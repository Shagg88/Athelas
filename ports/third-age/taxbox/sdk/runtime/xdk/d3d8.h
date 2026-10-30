#pragma once
#include <mutex>

#include "gpu/state.h"

namespace d3d {

gpu::State& State();
std::recursive_mutex& Lock();
// Blocks until the next vertical blank has run (its count, event and the title's callback), or max_ms. For titles
// that wait for a blank by calling Sleep(0) in a loop: the thread sleeps instead of spinning a core.
void WaitVBlank(int max_ms);
// the transforms a title stored in the device itself (inlined SetTransform), into the renderer: what the
// library's state flush would load (a title's own override of that flush calls it)
void TakeDeviceTransforms();

}  // namespace d3d
