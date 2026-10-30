// The Third Age: [graphics] hud_edges, as Return of the King's port has it (same engine family, same placement).
//
// The game draws its 2D (HUD, text, menus) through an orthographic projection (0x1896A0, from the matrix at
// 0x2704F0), which the SDK reports to gpu::g_ortho_place with each draw's extent. Without hud_edges the 2D is
// stretched with the 16:9 frame, as now. With hud_edges, in play, HUD pieces keep their 4:3 shape and each group of
// pieces near a side moves out to that edge; groups in the middle stay centred. Pieces are grouped by touching boxes
// of the frame before (the HUD barely moves between frames), as ROTK does. Left alone: the front end, movies, fades
// and other draws across the whole screen, copies of the screen, and wide bands.
#include <algorithm>
#include <atomic>
#include <functional>
#include <vector>

#include "app/app.h"
#include "gpu/backend.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"

namespace ta {
bool g_hud_edges = false;            // [graphics] hud_edges
std::atomic<uint32_t> g_frame_no{0};  // counted at each frame's end (ta_framerate.cpp TA_FrameWait)
bool InMenus();                       // ta_state.cpp
}

namespace {
struct Box {
    float x0, x1, y0, y1;
    int side = 0;
};
struct Groups {
    uint32_t frame = ~0u;
    std::vector<Box> drawn, groups;
} g_groups;

void MakeGroups() {
    std::vector<Box>& d = g_groups.drawn;
    std::vector<int> group(d.size());
    for (size_t i = 0; i < d.size(); i++) group[i] = int(i);
    std::function<int(int)> find = [&](int i) { return group[size_t(i)] == i ? i : group[size_t(i)] = find(group[size_t(i)]); };
    constexpr float kGap = 0.04f;
    for (size_t i = 0; i < d.size(); i++)
        for (size_t j = i + 1; j < d.size(); j++)
            if (d[i].x0 <= d[j].x1 + kGap && d[j].x0 <= d[i].x1 + kGap && d[i].y0 <= d[j].y1 + kGap && d[j].y0 <= d[i].y1 + kGap)
                group[size_t(find(int(i)))] = find(int(j));
    std::vector<Box> box(d.size(), Box{1e9f, -1e9f, 1e9f, -1e9f});
    for (size_t i = 0; i < d.size(); i++) {
        Box& g = box[size_t(find(int(i)))];
        g = {std::min(g.x0, d[i].x0), std::max(g.x1, d[i].x1), std::min(g.y0, d[i].y0), std::max(g.y1, d[i].y1)};
    }
    g_groups.groups.clear();
    for (size_t i = 0; i < d.size(); i++) {
        if (find(int(i)) != int(i)) continue;
        Box g = box[i];
        g.side = g.x0 < -0.5f && g.x1 < 0.1f ? -1 : g.x1 > 0.5f && g.x0 > -0.1f ? 1 : 0;
        g_groups.groups.push_back(g);
    }
    d.clear();
}

bool Place2D(const gpu::State& s, float x0, float x1, float y0, float y1, float& a, float& b) {
    if (!ta::g_hud_edges || !app::Widescreen() || ta::InMenus() || gpu::g_movie.load()) return false;
    uint32_t w, h;
    gpu::SurfaceSize(s.rt, w, h);
    if (w != 640 || h != 480) return false;  // the screen, not a texture the game draws into
    uint32_t now = ta::g_frame_no.load();
    if (now != g_groups.frame) {
        g_groups.frame = now;
        MakeGroups();
    }
    gpu::TextureDesc d;
    if (!s.textures[0] || !gpu::Describe(s.textures[0], d)) return false;  // untextured: fades, flat overlays
    if (d.width >= 640 && d.height >= 480) return false;                    // a copy of the screen
    bool spans = x0 <= -0.98f && x1 >= 0.98f, tall = y0 <= -0.98f && y1 >= 0.98f;
    bool edge_y = y0 <= -0.98f || y1 >= 0.98f;
    bool band = (y1 - y0) < 0.5f && edge_y;
    if (spans && (tall || band)) return false;
    if (!spans && (x0 <= -0.98f || x1 >= 0.98f) && edge_y && band && (x1 - x0) >= 0.8f) return false;
    a = (4.0f / 3.0f) / app::FrameAspect();
    g_groups.drawn.push_back({x0, x1, y0, y1});
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    int side = 2;
    for (const Box& g : g_groups.groups)
        if (cx >= g.x0 && cx <= g.x1 && cy >= g.y0 && cy <= g.y1) side = g.side;
    if (side == 2)
        for (const Box& g : g_groups.groups)
            if (x0 <= g.x1 && g.x0 <= x1 && y0 <= g.y1 && g.y0 <= y1) side = g.side;
    if (side == 2) side = x0 < -0.5f && x1 < 0.1f ? -1 : x1 > 0.5f && x0 > -0.1f ? 1 : 0;
    b = side < 0 ? a - 1 : side > 0 ? 1 - a : 0;
    return true;
}
}  // namespace

namespace ta {
void InitHud() { gpu::g_ortho_place = Place2D; }
}  // namespace ta
