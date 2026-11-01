#pragma once
#include <cstdint>

// Counts what may have changed the guest's draw state on the thread that draws: every HLE call there (state setters,
// locks, Present...) and every push buffer method but the draw data. Two draws with the same count between them drew
// with the same state (gpu::g_draw_same_state, D3D12's repeat path). Other threads' calls (sound, files) don't touch
// the draw state and aren't counted.
namespace core {
inline uint64_t g_state_gen = 0;
inline thread_local bool t_draw_thread = false;  // set where the title draws
inline void StateChanged() {
    if (t_draw_thread) ++g_state_gen;
}
}  // namespace core
