// ROTK Fix Launcher - drawn ornaments (see ornament.h). Everything is drawn with ImGui's
// shapes at the current scale; there are no pictures.
#include "ornament.h"
#include "imgui_internal.h"
#include <math.h>
#include <algorithm>

static float g_s = 1.0f;
static float O(float v) { return v * g_s; }
void orn_set_scale(float s) { g_s = s; }

static ImU32 col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
static ImVec2 V(float x, float y) { return ImVec2(x, y); }

// weathered granite (the old stone of Gondor), pewter and antique bronze-gold
static const ImU32 SN_DEEP = col(16, 15, 14), SN_DARK = col(24, 23, 21), SN_MID = col(62, 58, 52),
                   SN_LIGHT = col(84, 79, 71), SN_HI = col(122, 114, 100);
static ImU32 SILVER(int a = 230) { return col(150, 150, 144, a); }
static ImU32 PGOLD(int a = 230) { return col(184, 144, 78, a); }

// ---------------------------------------------------------------------------
// Leaves (shared): a two-tone leaf with a midrib, like gilded carving

static void leaf2(ImDrawList *dl, ImVec2 base, float len, float ang, float wid, ImU32 fill, ImU32 hi)
{
    ImVec2 d(cosf(ang), sinf(ang)), n(-d.y, d.x);
    ImVec2 tip(base.x + d.x * len, base.y + d.y * len);
    // a full almond leaf: swelling quickly from the stalk, tapering to a fine point
    auto P = [&](float along, float side) { return V(base.x + d.x * len * along + n.x * wid * side, base.y + d.y * len * along + n.y * wid * side); };
    dl->PathLineTo(base);
    dl->PathBezierCubicCurveTo(P(0.12f, 1.25f), P(0.62f, 1.35f), tip, 14);
    dl->PathBezierCubicCurveTo(P(0.62f, -1.35f), P(0.12f, -1.25f), base, 14);
    dl->PathFillConcave(fill);
    dl->PathLineTo(base);
    dl->PathBezierCubicCurveTo(P(0.12f, 1.25f), P(0.62f, 1.35f), tip, 14);
    dl->PathLineTo(base);
    dl->PathFillConcave(hi);
    dl->AddLine(base, V(base.x + d.x * len * 0.85f, base.y + d.y * len * 0.85f), col(40, 30, 14, 170), O(0.7f));
}

// A rinceau: a gilded vine scrolling along a straight run from p0 to p1. The stem waves;
// from each crest a tendril curls into the hollow, and leaves spring from the stem.
static void rinceau(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float amp, float wave, float leafLen,
                    ImU32 stem, ImU32 lf, ImU32 lfHi)
{
    ImVec2 d(p1.x - p0.x, p1.y - p0.y);
    float L = sqrtf(d.x * d.x + d.y * d.y);
    if (L < wave) return;
    d.x /= L; d.y /= L;
    ImVec2 n(-d.y, d.x);
    float ang = atan2f(d.y, d.x);
    auto at = [&](float x, float off) { return V(p0.x + d.x * x + n.x * off, p0.y + d.y * x + n.y * off); };
    // the stem
    const int N = (int)(L / O(3)) + 2;
    for (int i = 0; i <= N; i++) {
        float x = L * i / N;
        dl->PathLineTo(at(x, amp * sinf(6.28318f * x / wave)));
    }
    dl->PathStroke(stem, O(1.7f));
    // tendrils and leaves at each crest
    int crests = (int)(L / (wave * 0.5f));
    for (int k = 0; k < crests; k++) {
        float x = wave * (0.25f + 0.5f * k);
        if (x > L - wave * 0.2f) break;
        float sgn = (k % 2) ? -1.0f : 1.0f;              // which side the crest is on
        ImVec2 c = at(x, sgn * amp);
        // a tendril curling back into the hollow and ending in a small spiral
        ImVec2 e = at(x + wave * 0.22f, -sgn * amp * 0.15f);
        dl->AddBezierCubic(c, at(x + wave * 0.12f, sgn * amp * 1.25f), at(x + wave * 0.3f, sgn * amp * 0.6f), e, stem, O(1.1f));
        dl->AddCircle(V(e.x - n.x * sgn * O(1.6f), e.y - n.y * sgn * O(1.6f)), O(1.8f), stem, 10, O(1));
        // a leaf springing forward and outward from the crest, and a smaller one back and inward
        leaf2(dl, c, leafLen, ang + sgn * 0.6f, leafLen * 0.34f, lf, lfHi);
        leaf2(dl, at(x - wave * 0.08f, sgn * amp * 0.7f), leafLen * 0.7f, ang + 3.14159f - sgn * 0.7f, leafLen * 0.3f, lf, lfHi);
    }
}

// A Gothic quatrefoil medallion: a stone roundel with four carved lobes and a gilded centre
static void quatrefoil(ImDrawList *dl, ImVec2 c, float R)
{
    dl->AddCircleFilled(V(c.x + O(1), c.y + O(1.5f)), R, col(0, 0, 0, 90), 32);
    dl->AddCircleFilled(c, R, SN_LIGHT, 32);
    dl->AddCircle(c, R, SN_DARK, 32, O(1.3f));
    dl->AddCircle(c, R - O(2.2f), SN_HI, 32, O(0.8f));
    float r = R * 0.36f, o = R * 0.34f;
    for (int i = 0; i < 4; i++) {
        float a = i * 1.5708f;
        dl->AddCircleFilled(V(c.x + cosf(a) * o, c.y + sinf(a) * o), r, SN_MID, 20);
    }
    for (int i = 0; i < 4; i++) {
        float a = i * 1.5708f;
        dl->AddCircle(V(c.x + cosf(a) * o, c.y + sinf(a) * o), r, PGOLD(230), 20, O(1.1f));
    }
    // small pointed cusps between the lobes
    for (int i = 0; i < 4; i++) {
        float a = i * 1.5708f + 0.7854f;
        ImVec2 tip(c.x + cosf(a) * R * 0.62f, c.y + sinf(a) * R * 0.62f);
        dl->AddLine(V(c.x + cosf(a) * R * 0.38f, c.y + sinf(a) * R * 0.38f), tip, PGOLD(200), O(1));
    }
    dl->AddCircleFilled(c, R * 0.16f, PGOLD(255), 16);
}

// A pointed Gothic arch rising from a baseline at y, centre cx: leafy crockets climbing
// both sides and a leaf finial on top, a trefoil carved inside
static void gothic_crest(ImDrawList *dl, float cx, float y, float w, float h)
{
    ImU32 g = PGOLD(240);
    // the arch's two curves meet in a point
    ImVec2 L0(cx - w, y), R0(cx + w, y), top(cx, y - h);
    ImVec2 cl1(cx - w, y - h * 0.5f), cl2(cx - w * 0.4f, y - h * 0.78f);
    ImVec2 cr1(cx + w, y - h * 0.5f), cr2(cx + w * 0.4f, y - h * 0.78f);
    // stone face inside the arch
    dl->PathLineTo(L0);
    dl->PathBezierCubicCurveTo(cl1, cl2, top, 16);
    dl->PathBezierCubicCurveTo(cr2, cr1, R0, 16);
    dl->PathFillConvex(SN_LIGHT);
    dl->AddBezierCubic(L0, cl1, cl2, top, SN_DARK, O(2.2f));
    dl->AddBezierCubic(R0, cr1, cr2, top, SN_DARK, O(2.2f));
    dl->AddBezierCubic(L0, cl1, cl2, top, g, O(1.2f));
    dl->AddBezierCubic(R0, cr1, cr2, top, g, O(1.2f));
    // crockets: small leaves climbing the outside of each curve
    for (int i = 1; i <= 3; i++) {
        float t = i / 4.0f, u = 1 - t;
        auto bez = [&](ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3) {
            return V(u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x, u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y); };
        ImVec2 pl = bez(L0, cl1, cl2, top), pr = bez(R0, cr1, cr2, top);
        leaf2(dl, pl, O(8), 3.14159f + 0.9f + t * 0.5f, O(2.6f), g, col(206, 168, 98));
        leaf2(dl, pr, O(8), -0.9f - t * 0.5f, O(2.6f), g, col(206, 168, 98));
    }
    // the finial: three leaves on a short stem
    dl->AddLine(top, V(cx, y - h - O(5)), g, O(1.4f));
    leaf2(dl, V(cx, y - h - O(4)), O(9), -1.5708f, O(3), g, col(206, 168, 98));
    leaf2(dl, V(cx, y - h - O(2)), O(7), -1.5708f - 0.9f, O(2.4f), g, col(206, 168, 98));
    leaf2(dl, V(cx, y - h - O(2)), O(7), -1.5708f + 0.9f, O(2.4f), g, col(206, 168, 98));
    // a trefoil of tracery inside the arch: three carved lobes with gilded edges
    float tr = w * 0.24f, ty = y - h * 0.36f;
    ImVec2 lc[3] = { V(cx, ty - tr * 0.62f), V(cx - tr * 0.62f, ty + tr * 0.4f), V(cx + tr * 0.62f, ty + tr * 0.4f) };
    for (auto &c : lc) dl->AddCircleFilled(c, tr * 0.6f, SN_MID, 18);
    for (auto &c : lc) dl->AddCircle(c, tr * 0.6f, g, 18, O(1));
    dl->AddCircleFilled(V(cx, ty), tr * 0.22f, g, 12);
}

// ---------------------------------------------------------------------------
// Men: the window's pale stone border, with a gilded rinceau running round it

void orn_window_frame(ImDrawList *dl, ImVec2 size)
{
    float m = O(2), t = O(17);
    ImVec2 a(m, m), b(size.x - m, size.y - m);
    // the stone band: lit along its outer edge, softly shaded towards the inside
    ImU32 lo = col(40, 38, 35), hiS = col(66, 62, 56);
    dl->AddRectFilledMultiColor(a, V(b.x, a.y + t), hiS, hiS, lo, lo);
    dl->AddRectFilledMultiColor(V(a.x, b.y - t), b, lo, lo, hiS, hiS);
    dl->AddRectFilledMultiColor(V(a.x, a.y + t), V(a.x + t, b.y - t), hiS, lo, lo, hiS);
    dl->AddRectFilledMultiColor(V(b.x - t, a.y + t), V(b.x, b.y - t), lo, hiS, hiS, lo);
    // mouldings: a fine outer fillet and an inner bead
    dl->AddRect(a, b, SN_DARK, 0, O(1.2f));
    dl->AddRect(V(a.x + O(2), a.y + O(2)), V(b.x - O(2), b.y - O(2)), SN_HI, 0, O(0.8f));
    dl->AddRect(V(a.x + t, a.y + t), V(b.x - t, b.y - t), SN_DARK, 0, O(1.3f));
    dl->AddRect(V(a.x + t - O(1.5f), a.y + t - O(1.5f)), V(b.x - t + O(1.5f), b.y - t + O(1.5f)), PGOLD(200), 0, O(1));
    // the gilded vine along each side, between the corner medallions
    ImU32 stem = col(124, 90, 40), lf = col(142, 104, 46), lfHi = col(190, 150, 82);
    float mid = t * 0.5f, R = O(15), gap = R + O(4);
    float amp = t * 0.2f, wave = O(44), leafLen = O(8);
    float cx = size.x * 0.5f, crestW = O(26);
    rinceau(dl, V(a.x + gap + O(2), a.y + mid), V(cx - crestW - O(4), a.y + mid), amp, wave, leafLen, stem, lf, lfHi);
    rinceau(dl, V(b.x - gap - O(2), a.y + mid), V(cx + crestW + O(4), a.y + mid), amp, wave, leafLen, stem, lf, lfHi);
    rinceau(dl, V(a.x + gap + O(2), b.y - mid), V(cx - O(20), b.y - mid), amp, wave, leafLen, stem, lf, lfHi);
    rinceau(dl, V(b.x - gap - O(2), b.y - mid), V(cx + O(20), b.y - mid), amp, wave, leafLen, stem, lf, lfHi);
    rinceau(dl, V(a.x + mid, a.y + gap + O(2)), V(a.x + mid, b.y - gap - O(2)), amp, wave, leafLen, stem, lf, lfHi);
    rinceau(dl, V(b.x - mid, a.y + gap + O(2)), V(b.x - mid, b.y - gap - O(2)), amp, wave, leafLen, stem, lf, lfHi);
    // Gothic quatrefoil medallions at the corners
    quatrefoil(dl, V(a.x + mid, a.y + mid), R);
    quatrefoil(dl, V(b.x - mid, a.y + mid), R);
    quatrefoil(dl, V(a.x + mid, b.y - mid), R);
    quatrefoil(dl, V(b.x - mid, b.y - mid), R);
    // a pointed arch crest at the top centre, a small medallion at the bottom centre
    gothic_crest(dl, cx, a.y + t + O(24), O(24), O(36));
    quatrefoil(dl, V(cx, b.y - mid), O(11));
}

// ---------------------------------------------------------------------------
// Elves: slender lines, tendrils and leaves

static void leaf(ImDrawList *dl, ImVec2 base, float len, float ang, float wid, ImU32 fill)
{
    ImVec2 d(cosf(ang), sinf(ang)), n(-d.y, d.x);
    ImVec2 tip(base.x + d.x * len, base.y + d.y * len);
    ImVec2 m1(base.x + d.x * len * 0.45f + n.x * wid, base.y + d.y * len * 0.45f + n.y * wid);
    ImVec2 m2(base.x + d.x * len * 0.45f - n.x * wid, base.y + d.y * len * 0.45f - n.y * wid);
    dl->PathLineTo(base);
    dl->PathBezierQuadraticCurveTo(m1, tip, 10);
    dl->PathBezierQuadraticCurveTo(m2, base, 10);
    dl->PathFillConcave(fill);
    dl->AddLine(base, V(base.x + d.x * len * 0.8f, base.y + d.y * len * 0.8f), col(20, 18, 16, 140), O(0.6f));
}

// A vine growing from a corner along both edges: a curl at the corner, slender stems
// with leaves. sx, sy = +1/-1 say which way the edges run from the corner.
static void corner_vine(ImDrawList *dl, ImVec2 c, float sx, float sy, float len)
{
    ImU32 g = PGOLD(245), gs = PGOLD(170), lf = col(156, 118, 56), lfHi = col(200, 162, 92);
    ImU32 sl = col(96, 98, 96), slHi = col(146, 148, 142);
    auto P = [&](float x, float y) { return V(c.x + sx * x, c.y + sy * y); };
    float base = (sx > 0) ? 0.0f : 3.14159f;          // the horizontal stem's direction
    float vb = (sy > 0) ? 1.5708f : -1.5708f;         // the vertical stem's direction
    // a full spiral curl at the corner
    dl->PathLineTo(P(O(5), O(5)));
    dl->PathBezierCubicCurveTo(P(O(22), O(1)), P(O(26), O(22)), P(O(13), O(22)), 14);
    dl->PathBezierCubicCurveTo(P(O(5), O(22)), P(O(5), O(11)), P(O(12), O(11)), 12);
    dl->PathBezierCubicCurveTo(P(O(16), O(11)), P(O(17), O(16)), P(O(14), O(16)), 8);
    dl->PathStroke(g, O(1.9f));
    dl->AddCircleFilled(P(O(14), O(16)), O(2), g, 10);
    // the two stems, waving along the edges
    auto stemPt = [&](float f, bool horiz) {
        float w = O(5) * sinf(f * 6.28318f * 1.5f);
        return horiz ? P(O(5) + (len - O(5)) * f, O(4) + w) : P(O(4) + w, O(5) + (len - O(5)) * f);
    };
    for (int h = 0; h < 2; h++) {
        for (int i = 0; i <= 40; i++) dl->PathLineTo(stemPt(i / 40.0f, h == 0));
        dl->PathStroke(g, O(1.8f));
    }
    // leaves along both stems, alternating sides, gold and silver, getting smaller
    for (int i = 1; i <= 6; i++) {
        float f = i / 7.0f, size = O(17) - i * O(1.5f);
        float side = (i % 2) ? 1.0f : -1.0f;
        bool gold = (i % 3) != 0;
        ImVec2 ph = stemPt(f, true), pv = stemPt(f, false);
        float ah = base + side * 0.75f * (sx > 0 ? 1 : -1) * (sy > 0 ? 1 : -1);
        float av = vb - side * 0.75f * (sx > 0 ? 1 : -1) * (sy > 0 ? 1 : -1);
        leaf2(dl, ph, size, ah, size * 0.3f, gold ? lf : sl, gold ? lfHi : slHi);
        leaf2(dl, pv, size, av, size * 0.3f, gold ? lf : sl, gold ? lfHi : slHi);
        // small buds between the leaves
        if (i % 2 == 0) {
            dl->AddCircleFilled(stemPt(f + 0.07f, true), O(1.8f), g, 10);
            dl->AddCircleFilled(stemPt(f + 0.07f, false), O(1.8f), g, 10);
        }
    }
    // a curling tendril at each stem's end
    ImVec2 e1 = stemPt(1, true), e2 = stemPt(1, false);
    dl->AddBezierCubic(e1, P(len + O(12), O(4)), P(len + O(14), O(-5)), P(len + O(6), O(-5)), gs, O(1.3f));
    dl->AddBezierCubic(e2, P(O(4), len + O(12)), P(O(-5), len + O(14)), P(O(-5), len + O(6)), gs, O(1.3f));
}

// the panel's outline: a rectangle whose corners are cut in by a quarter circle
static void notched_path(ImDrawList *dl, ImVec2 a, ImVec2 b, float r)
{
    dl->PathArcTo(V(a.x, a.y), r, 1.5708f, 0.0f, 8);          // top-left notch (concave)
    dl->PathArcTo(V(b.x, a.y), r, 3.14159f, 1.5708f, 8);      // top-right
    dl->PathArcTo(V(b.x, b.y), r, 4.71239f, 3.14159f, 8);     // bottom-right
    dl->PathArcTo(V(a.x, b.y), r, 6.28318f, 4.71239f, 8);     // bottom-left
}

void orn_panel_fill(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 fill)
{
    float r = O(10);
    // a concave outline can't be filled as one convex shape: fill the cross and the rounded corners
    dl->AddRectFilled(V(a.x + r, a.y), V(b.x - r, b.y), fill);
    dl->AddRectFilled(V(a.x, a.y + r), V(a.x + r, b.y - r), fill);
    dl->AddRectFilled(V(b.x - r, a.y + r), V(b.x, b.y - r), fill);
}

void orn_panel_frame(ImDrawList *dl, ImVec2 a, ImVec2 b)
{
    float r = O(10);
    // the outer line: silver-gold, notched at the corners
    notched_path(dl, a, b, r);
    dl->PathStroke(PGOLD(230), O(1.7f), ImDrawFlags_Closed);
    // an inner hairline, set in
    float g = O(4.5f);
    notched_path(dl, V(a.x + g, a.y + g), V(b.x - g, b.y - g), r);
    dl->PathStroke(SILVER(120), O(0.8f), ImDrawFlags_Closed);
    // vines growing from each corner
    float len = std::min(O(165), std::min(b.x - a.x, b.y - a.y) * 0.36f);
    corner_vine(dl, V(a.x, a.y), 1, 1, len);
    corner_vine(dl, V(b.x, a.y), -1, 1, len);
    corner_vine(dl, V(a.x, b.y), 1, -1, len);
    corner_vine(dl, V(b.x, b.y), -1, -1, len);
    // a slender pointed arch rising from the top edge, with a leaf finial and a small bright stone
    float cx = (a.x + b.x) * 0.5f, w = O(30), h = O(15);
    ImU32 gl = PGOLD(235);
    dl->AddBezierCubic(V(cx - w, a.y), V(cx - w * 0.35f, a.y), V(cx - O(3), a.y - h * 0.5f), V(cx, a.y - h), gl, O(1.5f));
    dl->AddBezierCubic(V(cx + w, a.y), V(cx + w * 0.35f, a.y), V(cx + O(3), a.y - h * 0.5f), V(cx, a.y - h), gl, O(1.5f));
    dl->AddBezierCubic(V(cx - w * 0.6f, a.y), V(cx - w * 0.2f, a.y), V(cx - O(2), a.y - h * 0.3f), V(cx, a.y - h * 0.62f), SILVER(150), O(0.9f));
    dl->AddBezierCubic(V(cx + w * 0.6f, a.y), V(cx + w * 0.2f, a.y), V(cx + O(2), a.y - h * 0.3f), V(cx, a.y - h * 0.62f), SILVER(150), O(0.9f));
    leaf(dl, V(cx, a.y - h), O(9), -1.5708f, O(3), gl);
    dl->AddCircleFilled(V(cx, a.y - h * 0.3f), O(2.6f), col(170, 164, 150), 12);
    dl->AddCircle(V(cx, a.y - h * 0.3f), O(4.2f), SILVER(120), 16, O(0.8f));
    // a pair of leaves hanging from the bottom centre
    leaf(dl, V(cx, b.y), O(10), 1.5708f - 0.5f, O(3), gl);
    leaf(dl, V(cx, b.y), O(10), 1.5708f + 0.5f, O(3), gl);
    dl->AddCircleFilled(V(cx, b.y), O(2), gl, 10);
}

void orn_plate(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 fill, bool lit)
{
    float r = std::min(O(8), (b.y - a.y) * 0.35f);
    dl->AddRectFilled(a, b, fill, r);
    dl->AddRect(a, b, lit ? PGOLD(255) : PGOLD(170), r, O(1.3f));
    dl->AddRect(V(a.x + O(3), a.y + O(3)), V(b.x - O(3), b.y - O(3)), SILVER(lit ? 110 : 60), std::max(0.0f, r - O(2)), O(0.7f));
    // small leaf points at the middle of the ends, like elven clasps
    float my = (a.y + b.y) * 0.5f;
    ImVec2 l[3] = { V(a.x - O(3), my), V(a.x + O(2), my - O(3)), V(a.x + O(2), my + O(3)) };
    ImVec2 rr[3] = { V(b.x + O(3), my), V(b.x - O(2), my + O(3)), V(b.x - O(2), my - O(3)) };
    dl->AddTriangleFilled(l[0], l[1], l[2], PGOLD(lit ? 255 : 190));
    dl->AddTriangleFilled(rr[0], rr[1], rr[2], PGOLD(lit ? 255 : 190));
}

void orn_elf_rule(ImDrawList *dl, float x0, float x1, float y)
{
    if (x1 - x0 < O(60)) return;
    ImU32 gold = PGOLD(220), soft = SILVER(110);
    float curl = O(26);
    float xe = x1 - curl;
    // the line tapers from the heading to the scroll
    dl->AddTriangleFilled(V(x0, y - O(1.1f)), V(xe, y - O(0.3f)), V(xe, y + O(0.3f)), gold);
    dl->AddTriangleFilled(V(x0, y - O(1.1f)), V(x0, y + O(1.1f)), V(xe, y + O(0.3f)), gold);
    dl->AddLine(V(x0 + O(10), y + O(4)), V(xe - O(40), y + O(4)), soft, O(0.7f));
    ImVec2 dm[4] = { V(x0 - O(7), y), V(x0 - O(3), y - O(3)), V(x0 + O(1), y), V(x0 - O(3), y + O(3)) };
    dl->AddConvexPolyFilled(dm, 4, gold);
    // the scroll: the line rises, curls back over itself and ends in a spiral
    dl->PathLineTo(V(xe, y));
    dl->PathBezierCubicCurveTo(V(xe + curl * 0.6f, y), V(xe + curl, y - curl * 0.35f), V(xe + curl * 0.8f, y - curl * 0.6f), 16);
    dl->PathBezierCubicCurveTo(V(xe + curl * 0.6f, y - curl * 0.85f), V(xe + curl * 0.2f, y - curl * 0.6f), V(xe + curl * 0.35f, y - curl * 0.35f), 16);
    dl->PathBezierCubicCurveTo(V(xe + curl * 0.45f, y - curl * 0.2f), V(xe + curl * 0.62f, y - curl * 0.3f), V(xe + curl * 0.55f, y - curl * 0.45f), 12);
    dl->PathStroke(gold, O(1.2f));
    dl->AddCircleFilled(V(xe + curl * 0.55f, y - curl * 0.45f), O(1.5f), gold, 10);
    leaf(dl, V(xe + curl * 0.7f, y - O(1)), O(13), 0.25f, O(3.4f), gold);
    leaf(dl, V(xe + curl * 0.35f, y + O(0.5f)), O(10), 0.9f, O(3), SILVER(170));
}

// ---------------------------------------------------------------------------
// Buttons

bool orn_arch_button(const char *label, ImVec2 size, ImFont *font, bool enabled)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::InvisibleButton(label, size);
    bool hov = enabled && ImGui::IsItemHovered(), act = enabled && ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float archH = size.y * 0.24f;
    ImVec2 a(p.x, p.y), b(p.x + size.x, p.y + size.y);
    // a round arch over straight sides, like a doorway of the white city
    const int N = 28;
    ImVec2 pts[N + 3], in[N + 3];
    float halfW = size.x * 0.5f, cx = p.x + halfW;
    float R = (halfW * halfW + archH * archH) / (2 * archH);
    float cy = a.y + R, a0 = asinf(halfW / R);
    for (int i = 0; i <= N; i++) {
        float t = -a0 + 2 * a0 * i / N;
        pts[i] = V(cx + sinf(t) * R, cy - cosf(t) * R);
        in[i] = V(std::max(a.x + O(5), std::min(b.x - O(5), cx + sinf(t) * (R - O(5)))), cy - cosf(t) * (R - O(5)));
    }
    pts[N + 1] = b; pts[N + 2] = V(a.x, b.y);
    in[N + 1] = V(b.x - O(5), b.y - O(5)); in[N + 2] = V(a.x + O(5), b.y - O(5));
    ImU32 fill = !enabled ? col(74, 68, 58) : act ? col(110, 80, 36) : hov ? col(166, 124, 60) : col(140, 102, 46);
    dl->AddConvexPolyFilled(pts, N + 3, fill);
    // a soft light from above
    dl->AddRectFilledMultiColor(V(a.x + O(2), a.y + archH), V(b.x - O(2), b.y - O(2)), col(255, 250, 230, 0), col(255, 250, 230, 0),
                                col(60, 40, 10, 70), col(60, 40, 10, 70));
    dl->AddPolyline(pts, N + 3, enabled ? col(206, 168, 98) : col(90, 84, 74), O(1.5f), ImDrawFlags_Closed);
    dl->AddPolyline(in, N + 3, enabled ? col(70, 50, 20, 200) : col(50, 46, 40, 160), O(1), ImDrawFlags_Closed);
    // a gilded leaf finial at the top of the arch
    ImU32 fg = enabled ? col(184, 144, 78) : col(110, 100, 84), fh = enabled ? col(214, 176, 106) : col(130, 120, 104);
    leaf2(dl, V(cx, a.y + O(2)), O(12), -1.5708f, O(3.6f), fg, fh);
    leaf2(dl, V(cx, a.y + O(3)), O(10), -1.5708f - 1.0f, O(3), fg, fh);
    leaf2(dl, V(cx, a.y + O(3)), O(10), -1.5708f + 1.0f, O(3), fg, fh);
    dl->AddCircleFilled(V(cx, a.y + O(3)), O(2.2f), fg, 10);
    ImGui::PushFont(font, font->LegacySize);
    ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    ImVec2 tp(cx - ts.x * 0.5f, a.y + archH * 0.5f + (size.y - archH * 0.5f - ts.y) * 0.5f);
    dl->AddText(V(tp.x + O(1), tp.y + O(1.5f)), col(20, 12, 4, enabled ? 200 : 60), label, ImGui::FindRenderedTextEnd(label));
    dl->AddText(tp, enabled ? col(240, 220, 170) : col(120, 112, 100), label, ImGui::FindRenderedTextEnd(label));
    ImGui::PopFont();
    return pressed && enabled;
}

bool orn_plate_button(const char *label, ImVec2 size, bool enabled)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::InvisibleButton(label, size);
    bool hov = enabled && ImGui::IsItemHovered(), act = enabled && ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 fill = !enabled ? col(30, 28, 26, 220) : act ? col(70, 62, 46, 235) : hov ? col(54, 48, 38, 235) : col(28, 26, 24, 225);
    orn_plate(dl, p, V(p.x + size.x, p.y + size.y), fill, enabled && hov);
    ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    ImVec2 tp(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f);
    dl->AddText(tp, enabled ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), label, ImGui::FindRenderedTextEnd(label));
    return pressed && enabled;
}
