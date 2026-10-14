// ROTK Xbox: widescreen and field of view, as ROTK Fix does them on PC (graphics.c, SetTransform).
// The game builds its perspective projections for 4:3. With widescreen on and a wide display the host shows the
// frame at 16:9 (gpu BlitFrame), so the horizontal term _11 is scaled by 4:3 / 16:9: the same vertical view, more at
// the sides. FieldOfView (70 to 150, percent of the game's own angle) widens the view angle through the tangent of
// the half angle; narrow cameras (zoomed shots under 25 degrees) are left alone. Only perspective matrices
// (_34 = 1, _44 = 0) are changed, never the 2D ones, and only the renderer's copy (SDK gpu::g_projection_hook), as
// ROTK Fix changes only what reaches Direct3D. Menus, text and the HUD fill the 16:9 picture, as on PC with ROTK Fix
// (its HUD fills the screen up to 16:9 and is held at 16:9 only on wider screens; the SDK shows 16:9 at most).
//
// DrawDistance (50 to 800, percent of the game's own): the far clip plane of the 3D cameras moves out, and the fog
// distances with it (gpu::g_fog_scale), as ROTK Fix does. AlphaEdgeSmoothing: with anti-aliasing, cut-out textures
// (leaves, grass, fences) get smooth edges (alpha to coverage, gpu::g_alpha_edges).
//
// hud_edges (off by default; ROTKFix.ini [Graphics] HUDEdges): with widescreen, HUD pictures keep their 4:3 shape and
// the pieces near a side move out to that side of the 16:9 picture, the rest stays centred. The game draws its 2D
// through an orthographic projection (FVF XYZ|DIFFUSE|TEX1), so each draw is placed by its extent in clip space
// (SDK gpu::g_ortho_place), with ROTK Fix's tests for what keeps the full width: untextured draws (fades), screen
// copies, small textures stretched across, and bands along the top or bottom. A full-screen picture keeps its shape.
#include <atomic>
#include <cmath>
#include <cstdint>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/state.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"

namespace rotk {
uint32_t g_fov_percent = 100;   // [graphics] fov, ROTKFix.ini [Graphics] FieldOfView
uint32_t g_draw_percent = 100;  // [graphics] draw_distance, ROTKFix.ini [Graphics] DrawDistance
bool g_alpha_edges = false;     // [graphics] alpha_edges, ROTKFix.ini [Graphics] AlphaEdgeSmoothing
bool g_hud_edges = false;       // [graphics] hud_edges, ROTKFix.ini [Graphics] HUDEdges

// after the settings load
void InitCamera() {
    gpu::g_alpha_edges = g_alpha_edges;
    gpu::g_fog_scale = float(g_draw_percent) / 100.0f;
    if (g_draw_percent != 100) LOGR("rotk: draw distance %u%%", g_draw_percent);
    if (g_alpha_edges) LOGR("rotk: smooth cut-out edges with anti-aliasing");
    if (g_hud_edges) LOGR("rotk: HUD at 4:3 with its pieces at the screen edges (with widescreen)");
}
}

namespace {
constexpr float kDeg = 57.29578f;

void Projection(float* m) {
    // row major: _11 m[0], _22 m[5], _34 m[11], _44 m[15]
    bool persp = m[11] > 0.99f && m[11] < 1.01f && std::fabs(m[15]) < 0.001f;
    if (!persp || m[0] == 0.0f || m[5] <= 0.0f) return;
    // the gameplay camera can be mirrored (_11 negative): magnitudes, sign kept
    float game_aspect = std::fabs(m[5] / m[0]);
    if (app::Widescreen() && game_aspect > 1.30f && game_aspect < 1.36f) {
        constexpr float screen = 16.0f / 9.0f;
        m[0] *= game_aspect / screen;
    }
    uint32_t pct = rotk::g_fov_percent;
    if (pct != 100) {
        float vfov = 2.0f * std::atan(1.0f / m[5]);
        if (vfov > 25.0f / kDeg) {
            float nv = std::fmin(vfov * float(pct) / 100.0f, 150.0f / kDeg);
            float k = std::tan(vfov * 0.5f) / std::tan(nv * 0.5f);
            m[0] *= k;
            m[5] *= k;
        }
    }
    // draw distance: the far plane f out to f * percent. Standard depth: _33 = f/(f-n), _43 = -n*f/(f-n); reversed
    // depth (z/w = _43/z + _33, 1 at the camera, 0 at f = -_43/_33): _33 divided by the percent
    uint32_t dd = rotk::g_draw_percent;
    float raw33 = m[10], raw43 = m[14];
    if (dd != 100) {
        float k = float(dd) / 100.0f;
        if (m[10] < 0.0f && m[10] > -0.01f && m[14] > 0.0f) {
            m[10] /= k;
        } else if (m[10] > 1.0f && m[14] < 0.0f) {
            float n = -m[14] / m[10], f = m[14] / (1.0f - m[10]);
            if (f > n) {
                float f2 = f * k;
                m[10] = f2 / (f2 - n);
                m[14] = -n * f2 / (f2 - n);
            }
        }
    }
    // each distinct camera once (the first 8)
    static float seen[8][2];
    static std::atomic<int> nseen{0};
    float vf = 2.0f * std::atan(1.0f / (m[5] != 0 ? m[5] : 1.0f)) * kDeg;
    int n = nseen;
    for (int i = 0; i < n && i < 8; i++)
        if (std::fabs(seen[i][0] - vf) < 0.5f && std::fabs(seen[i][1] - raw43) <= std::fabs(raw43) * 0.01f) return;
    if (n >= 8) return;
    seen[n][0] = vf;
    seen[n][1] = raw43;
    nseen = n + 1;
        LOGR("rotk: 3D camera %.1f deg at aspect %.4f (_33 %g _43 %g); widescreen %s, field of view %u%%, draw distance %u%%",
             vf, game_aspect, raw33, raw43,
             app::Widescreen() ? "on" : "off", pct, dd);
}

// hud_edges: where a 2D draw goes. The last draw's box and placement: text is drawn a letter at a time and bars a
// piece at a time, so a draw touching the one before (side by side, same rows) goes where that one went.
struct Chain {
    float x0 = 0, x1 = -1, y0 = 0, y1 = -1, b = 0;
} g_chain;

bool HudPlace(const gpu::State& s, float x0, float x1, float y0, float y1, float& a, float& b) {
    if (!rotk::g_hud_edges || !app::Widescreen()) return false;
    uint32_t w, h;
    gpu::SurfaceSize(s.rt, w, h);
    if (w != 640 || h != 480) return false;  // the screen, not a texture the game draws into
    gpu::TextureDesc d;
    if (!s.textures[0] || !gpu::Describe(s.textures[0], d)) return false;  // untextured: fades, flat overlays
    if (d.width >= 640 && d.height >= 480) return false;                  // a copy of the screen
    bool spans = x0 <= -0.98f && x1 >= 0.98f;
    bool edge_y = y0 <= -0.98f || y1 >= 0.98f;
    bool band = (y1 - y0) < 0.5f && edge_y;
    if (spans && (d.width <= 128 || d.height <= 128 || band)) return false;     // gradients, shading bands
    if (!spans && (x0 <= -0.98f || x1 >= 0.98f) && edge_y && band && (x1 - x0) >= 0.8f) return false;  // a band in pieces
    a = (4.0f / 3.0f) / (16.0f / 9.0f);
    if (spans) {  // a full-screen picture: its shape, centred
        b = 0;
        g_chain = {};
        return true;
    }
    constexpr float kGap = 0.05f, kRows = 0.035f, kEdge = 0.7f, kWide = 0.8f;
    bool touches = x0 <= g_chain.x1 + kGap && x1 >= g_chain.x0 - kGap && y0 <= g_chain.y1 + kRows && y1 >= g_chain.y0 - kRows;
    if (touches) {
        b = g_chain.b;
    } else if (x0 < -kEdge && x1 < 0) {
        b = a - 1;  // its left side stays at the screen's left
    } else if (x1 > kEdge && x0 > 0) {
        b = 1 - a;
    } else {
        b = 0;
    }
    if (x1 - x0 > kWide) g_chain = {};
    else g_chain = {x0, x1, y0, y1, b};
    return true;
}

const int kRegistered = (gpu::g_projection_hook = Projection, gpu::g_ortho_place = HudPlace, 0);
}  // namespace
