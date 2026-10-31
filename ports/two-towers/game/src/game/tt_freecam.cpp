// The Two Towers: a free camera TEST (Shagg, 31 Oct 2026), on only with the environment variable TT_FREECAM=1 (no
// setting yet). The right stick turns the picture: the 3D is turned in the vertex shader (SDK gpu::g_orbit_*), about
// the camera itself (TT_FREECAM_PIVOT=0, the default) or about a point that many units in front of it (the hero's
// distance, for a third-person orbit). Let go of the stick and the view eases back to the game's own.
//
// What this tests: how much of each level is missing or broken when seen from angles the designers didn't plan
// (the game still culls with its own camera, so things outside its view can be missing). The game's movement and
// aim don't follow the turned view yet.
//
// The projection comes from 0x4E890 (this, fov, aspect, near, far; fov vertical, logged once to confirm its unit).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"

namespace tt {
namespace {
bool Enabled() {
    static const bool on = [] {
        const char* e = std::getenv("TT_FREECAM");
        bool v = e && *e && *e != '0';
        if (v) {
            const char* p = std::getenv("TT_FREECAM_PIVOT");
            gpu::g_orbit_pivot = p ? float(std::atof(p)) : 0.0f;
            LOGR("tt: free camera test on, pivot %.0f units in front of the camera", gpu::g_orbit_pivot.load());
        }
        return v;
    }();
    return on;
}
}  // namespace

// from the 0x4E890 hook (tt_widescreen.cpp), with the aspect the picture is drawn at
void FreeCamProjection(float fov, float aspect, float zn, float zf) {
    if (!Enabled()) return;
    static std::atomic<int> logged{0};
    if (logged < 6) {
        logged++;
        LOGR("tt: free camera: projection fov %g aspect %g near %g far %g", fov, aspect, zn, zf);
    }
    if (!(fov > 0.05f) || !(zf > zn) || !(zn > 0)) return;
    float rad = fov > 3.2f ? fov * 3.14159265f / 180.0f : fov;  // degrees or radians
    float p22 = 1.0f / std::tan(rad / 2);
    gpu::g_orbit_p22 = p22;
    gpu::g_orbit_p11 = p22 / aspect;
    gpu::g_orbit_a = -zn / (zf - zn);  // reversed depth: 1 at near, 0 at far
    gpu::g_orbit_b = zn * zf / (zf - zn);

    // the right stick, once per frame-ish (by time, so the speed doesn't depend on the frame rate)
    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now();
    float dt = std::chrono::duration<float>(clock::now() - last).count();
    if (dt < 0.004f) return;
    last = clock::now();
    dt = std::min(dt, 0.1f);
    input::Pad pad = input::Raw(0);
    auto axis = [](int16_t v) {
        float f = v / 32767.0f;
        return std::fabs(f) < 0.25f ? 0.0f : (f - std::copysign(0.25f, f)) / 0.75f;
    };
    float sx = axis(pad.rx), sy = axis(pad.ry);
    float yaw = gpu::g_orbit_yaw, pitch = gpu::g_orbit_pitch;
    const float speed = 2.0f, back = 3.0f;  // radians a second; how fast it eases back
    if (sx != 0) yaw += sx * speed * dt;
    else yaw -= yaw * std::min(1.0f, back * dt);
    if (sy != 0) pitch += sy * speed * dt;
    else pitch -= pitch * std::min(1.0f, back * dt);
    yaw = std::clamp(yaw, -3.1f, 3.1f);
    pitch = std::clamp(pitch, -0.8f, 0.8f);
    gpu::g_orbit_yaw = yaw;
    gpu::g_orbit_pitch = pitch;
    gpu::g_orbit_on = std::fabs(yaw) > 1e-4f || std::fabs(pitch) > 1e-4f;
}
}  // namespace tt
