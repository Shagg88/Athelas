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

// pale stone (the white city), silver and pale gold (elven work)
static const ImU32 SN_DEEP = col(30, 29, 28), SN_DARK = col(86, 82, 76), SN_MID = col(150, 144, 133),
                   SN_LIGHT = col(196, 190, 176), SN_HI = col(234, 229, 216);
static ImU32 SILVER(int a = 230) { return col(214, 212, 204, a); }
static ImU32 PGOLD(int a = 230) { return col(218, 192, 128, a); }

// ---------------------------------------------------------------------------
// Men: the window's pale stone border

// a band along one side: a soft gradient from the lit outer edge to the shaded inner edge
static void stone_side(ImDrawList *dl, ImVec2 a, ImVec2 b, bool vertical, bool flip)
{
    ImU32 c0 = SN_LIGHT, c1 = SN_MID;
    if (flip) std::swap(c0, c1);
    if (vertical) dl->AddRectFilledMultiColor(a, b, c0, c1, c1, c0);
    else          dl->AddRectFilledMultiColor(a, b, c0, c0, c1, c1);
}

// classical moulding lines running round the band: an outer fillet, a raised bead and a groove
static void mouldings(ImDrawList *dl, ImVec2 a, ImVec2 b, float t)
{
    dl->AddRect(a, b, SN_DEEP, 0, O(1.2f));
    dl->AddRect(V(a.x + O(2), a.y + O(2)), V(b.x - O(2), b.y - O(2)), SN_HI, 0, O(0.9f));
    dl->AddRect(V(a.x + t * 0.45f, a.y + t * 0.45f), V(b.x - t * 0.45f, b.y - t * 0.45f), SN_DARK, 0, O(1.4f));
    dl->AddRect(V(a.x + t * 0.45f + O(1.3f), a.y + t * 0.45f + O(1.3f)), V(b.x - t * 0.45f - O(1.3f), b.y - t * 0.45f - O(1.3f)), SN_HI, 0, O(0.8f));
    dl->AddRect(V(a.x + t, a.y + t), V(b.x - t, b.y - t), SN_DEEP, 0, O(1.2f));
}

// a square corner block like the base of a column, with a carved eight-petal rosette
static void stone_block(ImDrawList *dl, ImVec2 c, float h)
{
    ImVec2 a(c.x - h, c.y - h), b(c.x + h, c.y + h);
    dl->AddRectFilledMultiColor(a, b, SN_HI, SN_LIGHT, SN_MID, SN_LIGHT);
    dl->AddRect(a, b, SN_DEEP, 0, O(1.2f));
    dl->AddRect(V(a.x + O(2.5f), a.y + O(2.5f)), V(b.x - O(2.5f), b.y - O(2.5f)), SN_DARK, 0, O(0.9f));
    float r = h * 0.55f;
    for (int i = 0; i < 8; i++) {
        float ang = i * 0.7853982f;
        ImVec2 d(cosf(ang), sinf(ang));
        ImVec2 tip(c.x + d.x * r, c.y + d.y * r), side(-d.y * r * 0.22f, d.x * r * 0.22f);
        ImVec2 mid(c.x + d.x * r * 0.5f, c.y + d.y * r * 0.5f);
        ImVec2 q[4] = { c, V(mid.x + side.x, mid.y + side.y), tip, V(mid.x - side.x, mid.y - side.y) };
        dl->AddConvexPolyFilled(q, 4, (i % 2) ? SN_MID : SN_DARK);
    }
    dl->AddCircle(c, r + O(1.5f), SN_DARK, 32, O(1));
    dl->AddCircleFilled(c, r * 0.22f, SN_HI, 16);
}

void orn_window_frame(ImDrawList *dl, ImVec2 size)
{
    float m = O(3), t = O(10);
    ImVec2 a(m, m), b(size.x - m, size.y - m);
    stone_side(dl, a, V(b.x, a.y + t), false, false);                 // top
    stone_side(dl, V(a.x, b.y - t), b, false, true);                  // bottom
    stone_side(dl, V(a.x, a.y + t), V(a.x + t, b.y - t), true, false);  // left
    stone_side(dl, V(b.x - t, a.y + t), V(b.x, b.y - t), true, true);   // right
    mouldings(dl, a, b, t);
    // a faint pale-gold line set back inside the stone
    dl->AddRect(V(a.x + t + O(5), a.y + t + O(5)), V(b.x - t - O(5), b.y - t - O(5)), PGOLD(60), 0, O(1));
    float h = O(13);
    stone_block(dl, V(a.x + h - O(2), a.y + h - O(2)), h);
    stone_block(dl, V(b.x - h + O(2), a.y + h - O(2)), h);
    stone_block(dl, V(a.x + h - O(2), b.y - h + O(2)), h);
    stone_block(dl, V(b.x - h + O(2), b.y - h + O(2)), h);
    // the keystone at the top centre, with a small carved arch inside it
    float cx = size.x * 0.5f, kw = O(20), kb = O(13), kh = O(24);
    ImVec2 k[4] = { V(cx - kw, a.y - O(1)), V(cx + kw, a.y - O(1)), V(cx + kb, a.y + kh), V(cx - kb, a.y + kh) };
    dl->AddConvexPolyFilled(k, 4, SN_LIGHT);
    dl->AddQuadFilled(k[0], k[1], V(cx + kw - O(2), a.y + O(3)), V(cx - kw + O(2), a.y + O(3)), SN_HI);
    dl->AddPolyline(k, 4, SN_DEEP, O(1.2f), ImDrawFlags_Closed);
    dl->PathArcTo(V(cx, a.y + kh * 0.62f), O(5.5f), 3.14159f, 6.28318f, 12);
    dl->PathLineTo(V(cx + O(5.5f), a.y + kh - O(4)));
    dl->PathLineTo(V(cx - O(5.5f), a.y + kh - O(4)));
    dl->PathStroke(SN_DARK, O(1.1f), ImDrawFlags_Closed);
    // a plinth at the bottom centre
    ImVec2 p0(cx - O(34), b.y - O(8)), p1(cx + O(34), b.y + O(1));
    dl->AddRectFilledMultiColor(p0, p1, SN_HI, SN_HI, SN_MID, SN_MID);
    dl->AddRect(p0, p1, SN_DEEP, 0, O(1.1f));
    dl->AddLine(V(p0.x + O(4), p0.y + O(3)), V(p1.x - O(4), p0.y + O(3)), SN_DARK, O(0.9f));
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
    ImU32 g = PGOLD(235), gs = PGOLD(150), sv = SILVER(200);
    auto P = [&](float x, float y) { return V(c.x + sx * x, c.y + sy * y); };
    // the curl at the corner
    dl->PathLineTo(P(O(4), O(4)));
    dl->PathBezierCubicCurveTo(P(O(14), O(2)), P(O(18), O(14)), P(O(10), O(14)), 12);
    dl->PathBezierCubicCurveTo(P(O(5), O(14)), P(O(5), O(8)), P(O(9), O(8)), 10);
    dl->PathStroke(g, O(1.4f));
    dl->AddCircleFilled(P(O(9), O(8)), O(1.5f), g, 8);
    // the stem along the horizontal edge, waving gently, with leaves
    dl->PathLineTo(P(O(4), O(4)));
    dl->PathBezierCubicCurveTo(P(len * 0.3f, O(0)), P(len * 0.55f, O(9)), P(len, O(3)), 24);
    dl->PathStroke(g, O(1.3f));
    // and along the vertical edge
    dl->PathLineTo(P(O(4), O(4)));
    dl->PathBezierCubicCurveTo(P(O(0), len * 0.3f), P(O(9), len * 0.55f), P(O(3), len), 24);
    dl->PathStroke(g, O(1.3f));
    // leaves: alternate sides along each stem
    float base = (sx > 0) ? 0.0f : 3.14159f, vb = (sy > 0) ? 1.5708f : -1.5708f;
    for (int i = 1; i <= 3; i++) {
        float f = i / 4.0f;
        float tilt = (i % 2) ? 0.6f : -0.6f;
        // along the top/bottom stem: leaves point onward and slightly in or out
        ImVec2 ph = P(len * f, O(3) + O(4) * sinf(f * 3.14159f));
        leaf(dl, ph, O(11) - i * O(1.2f), base + sy * sx * tilt * (sx > 0 ? 1 : -1), O(3.2f), (i % 2) ? g : sv);
        // along the side stem
        ImVec2 pv = P(O(3) + O(4) * sinf(f * 3.14159f), len * f);
        leaf(dl, pv, O(11) - i * O(1.2f), vb - sx * sy * tilt * (sy > 0 ? 1 : -1), O(3.2f), (i % 2) ? g : sv);
    }
    // a tendril at each stem's end
    ImVec2 e1 = P(len, O(3)), e2 = P(O(3), len);
    dl->AddBezierCubic(e1, P(len + O(8), O(3)), P(len + O(10), O(-3)), P(len + O(5), O(-4)), gs, O(1));
    dl->AddBezierCubic(e2, P(O(3), len + O(8)), P(O(-3), len + O(10)), P(O(-4), len + O(5)), gs, O(1));
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
    float len = std::min(O(110), std::min(b.x - a.x, b.y - a.y) * 0.28f);
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
    dl->AddCircleFilled(V(cx, a.y - h * 0.3f), O(2.6f), col(236, 240, 246), 12);
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
    ImU32 fill = !enabled ? col(74, 68, 58) : act ? col(176, 146, 82) : hov ? col(226, 196, 124) : col(204, 172, 102);
    dl->AddConvexPolyFilled(pts, N + 3, fill);
    // a soft light from above
    dl->AddRectFilledMultiColor(V(a.x + O(2), a.y + archH), V(b.x - O(2), b.y - O(2)), col(255, 250, 230, 0), col(255, 250, 230, 0),
                                col(60, 40, 10, 70), col(60, 40, 10, 70));
    dl->AddPolyline(pts, N + 3, enabled ? col(246, 236, 210) : col(120, 112, 98), O(1.5f), ImDrawFlags_Closed);
    dl->AddPolyline(in, N + 3, enabled ? col(110, 84, 36, 170) : col(50, 46, 40, 160), O(1), ImDrawFlags_Closed);
    // the keystone, in white stone
    float kw = O(8), kb = O(5.5f), kh = O(12);
    ImVec2 k[4] = { V(cx - kw, a.y - O(3)), V(cx + kw, a.y - O(3)), V(cx + kb, a.y + kh), V(cx - kb, a.y + kh) };
    dl->AddConvexPolyFilled(k, 4, enabled ? SN_HI : SN_MID);
    dl->AddPolyline(k, 4, SN_DARK, O(1.1f), ImDrawFlags_Closed);
    ImGui::PushFont(font, font->LegacySize);
    ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    ImVec2 tp(cx - ts.x * 0.5f, a.y + archH * 0.5f + (size.y - archH * 0.5f - ts.y) * 0.5f);
    dl->AddText(V(tp.x + O(1), tp.y + O(1)), col(255, 248, 225, enabled ? 110 : 30), label, ImGui::FindRenderedTextEnd(label));
    dl->AddText(tp, enabled ? col(40, 28, 10) : col(34, 30, 26), label, ImGui::FindRenderedTextEnd(label));
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
