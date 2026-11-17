// The Two Towers: [graphics] alpha_edges, as The Return of the King has it (same name): alpha to coverage for the
// cut-outs (leaves, grass, fences, drawn with alpha test: about 180 draws a frame in Westfold) when anti-aliasing is
// on, the shared renderer's.
//
// ROTK's draw_distance is not here: The Two Towers' camera follows close behind the hero and its far plane is 10000
// units away (reversed depth, _33 -0.0002, _43 2.0004), with the fog off, so nothing on screen is ever cut off.
#include <cstdlib>

#include "gpu/backend.h"

namespace tt {
bool g_alpha_edges = false;  // [graphics] alpha_edges

// after the settings load (tt::Init): the pipelines are made with it, so it takes effect at the next start
void ApplyView() {
    gpu::g_alpha_edges = g_alpha_edges;
    // point sprites drawn as textured squares, as on the console: the map screen's burning edges and the selected
    // level's glow (GitHub #55: thin orange lines without). TT_POINT_SPRITES=0 draws them as single pixels again.
    const char* ps = std::getenv("TT_POINT_SPRITES");
    gpu::g_point_sprites = !(ps && *ps == '0');
}
}  // namespace tt
