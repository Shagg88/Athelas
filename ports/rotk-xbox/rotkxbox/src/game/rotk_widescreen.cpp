// ROTK Xbox: widescreen and field of view, as ROTK Fix does them on PC (graphics.c, SetTransform).
// The game builds its perspective projections for 4:3. With widescreen on and a wide display the host shows the
// frame at 16:9 (gpu BlitFrame), so the horizontal term _11 is scaled by 4:3 / 16:9: the same vertical view, more at
// the sides. FieldOfView (70 to 150, percent of the game's own angle) widens the view angle through the tangent of
// the half angle; narrow cameras (zoomed shots under 25 degrees) are left alone. Only perspective matrices
// (_34 = 1, _44 = 0) are changed, never the 2D ones, and only the renderer's copy (SDK gpu::g_projection_hook), as
// ROTK Fix changes only what reaches Direct3D. Menus, text and the HUD fill the 16:9 picture, as on PC with ROTK Fix.
#include <atomic>
#include <cmath>
#include <cstdint>

#include "app/app.h"
#include "core/log.h"
#include "gpu/vertex.h"

namespace rotk {
uint32_t g_fov_percent = 100;  // [graphics] fov, ROTKFix.ini [Graphics] FieldOfView
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
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) == 0)
        LOGR("rotk: 3D camera %.1f deg at aspect %.4f; widescreen %s, field of view %u%%",
             2.0f * std::atan(1.0f / (m[5] != 0 ? m[5] : 1.0f)) * kDeg, game_aspect, app::Widescreen() ? "on" : "off",
             pct);
}

const int kRegistered = (gpu::g_projection_hook = Projection, 0);
}  // namespace
