// ROTK Fix Launcher - drawn ornaments (see ornament.h). Everything here is drawn
// with ImGui's shapes at the current scale; there are no pictures.
#include "ornament.h"
#include "imgui_internal.h"
#include <math.h>
#include <algorithm>

static float g_s = 1.0f;
static float O(float v) { return v * g_s; }
void orn_set_scale(float s) { g_s = s; }

static ImU32 col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

// bronze (dwarf) and stone (man) tones, dark to light
static const ImU32 BR_DEEP = col(34, 23, 10), BR_DARK = col(70, 49, 21), BR_MID = col(124, 90, 40),
                   BR_LIGHT = col(186, 146, 76), BR_HI = col(236, 204, 136);
static const ImU32 ST_DEEP = col(18, 15, 12), ST_DARK = col(44, 38, 31), ST_MID = col(78, 69, 57),
                   ST_LIGHT = col(124, 112, 94), ST_HI = col(170, 156, 130);
static const ImU32 GOLD_LINE = col(206, 166, 78, 210);

static ImVec2 V(float x, float y) { return ImVec2(x, y); }

// A band between two rectangles, lit from the top left: top and left faces light, bottom and right dark
static void bevel_band(ImDrawList *dl, ImVec2 a, ImVec2 b, float t, ImU32 top, ImU32 left, ImU32 bottom, ImU32 right)
{
    dl->AddQuadFilled(V(a.x, a.y), V(b.x, a.y), V(b.x - t, a.y + t), V(a.x + t, a.y + t), top);
    dl->AddQuadFilled(V(a.x, a.y), V(a.x + t, a.y + t), V(a.x + t, b.y - t), V(a.x, b.y), left);
    dl->AddQuadFilled(V(a.x, b.y), V(a.x + t, b.y - t), V(b.x - t, b.y - t), V(b.x, b.y), bottom);
    dl->AddQuadFilled(V(b.x, a.y), V(b.x, b.y), V(b.x - t, b.y - t), V(b.x - t, a.y + t), right);
}

// ---------------------------------------------------------------------------
// Man: the window's carved stone border

// A square corner block like a pillar's capital: bevelled stone with a carved rosette
static void stone_block(ImDrawList *dl, ImVec2 c, float h)
{
    ImVec2 a(c.x - h, c.y - h), b(c.x + h, c.y + h);
    dl->AddRectFilled(a, b, ST_MID);
    float t = O(3.5f);
    bevel_band(dl, a, b, t, ST_HI, ST_LIGHT, ST_DEEP, ST_DARK);
    dl->AddRect(a, b, ST_DEEP, 0, O(1));
    // carved rosette: a ring, four petals and a centre
    float r = h * 0.52f;
    dl->AddCircle(c, r, ST_DEEP, 24, O(1.6f));
    dl->AddCircle(V(c.x + O(0.8f), c.y + O(0.8f)), r, ST_HI, 24, O(0.7f));
    for (int i = 0; i < 4; i++) {
        float ang = i * 1.5707963f + 0.7853982f;
        ImVec2 p(c.x + cosf(ang) * r * 0.55f, c.y + sinf(ang) * r * 0.55f);
        dl->AddCircleFilled(p, r * 0.22f, ST_DARK, 12);
        dl->AddCircle(p, r * 0.22f, GOLD_LINE, 12, O(0.8f));
    }
    dl->AddCircleFilled(c, r * 0.2f, BR_LIGHT, 12);
}

void orn_window_frame(ImDrawList *dl, ImVec2 size)
{
    float m = O(3), t = O(9);
    ImVec2 a(m, m), b(size.x - m, size.y - m);
    // the stone band, bevelled, with a gold inlay line inside it
    bevel_band(dl, a, b, t, ST_LIGHT, ST_MID, ST_DEEP, ST_DARK);
    dl->AddRect(a, b, ST_DEEP, 0, O(1.2f));
    dl->AddRect(V(a.x + t, a.y + t), V(b.x - t, b.y - t), ST_DEEP, 0, O(1.2f));
    dl->AddRect(V(a.x + t * 0.5f, a.y + t * 0.5f), V(b.x - t * 0.5f, b.y - t * 0.5f), GOLD_LINE, 0, O(1));
    // a thin inner gold line, set back from the band
    dl->AddRect(V(a.x + t + O(4), a.y + t + O(4)), V(b.x - t - O(4), b.y - t - O(4)), col(170, 134, 66, 70), 0, O(1));
    // corner blocks
    float h = O(13);
    stone_block(dl, V(a.x + h - O(2), a.y + h - O(2)), h);
    stone_block(dl, V(b.x - h + O(2), a.y + h - O(2)), h);
    stone_block(dl, V(a.x + h - O(2), b.y - h + O(2)), h);
    stone_block(dl, V(b.x - h + O(2), b.y - h + O(2)), h);
    // a keystone at the top centre and a plinth at the bottom centre
    float cx = size.x * 0.5f, kw = O(22), kb = O(14), kh = O(20);
    ImVec2 k[4] = { V(cx - kw, a.y), V(cx + kw, a.y), V(cx + kb, a.y + kh), V(cx - kb, a.y + kh) };
    dl->AddConvexPolyFilled(k, 4, ST_MID);
    dl->AddQuadFilled(k[0], k[1], V(cx + kw - O(3), a.y + O(3)), V(cx - kw + O(3), a.y + O(3)), ST_HI);
    dl->AddQuadFilled(k[3], k[2], V(cx + kb - O(1), a.y + kh - O(3)), V(cx - kb + O(1), a.y + kh - O(3)), ST_DEEP);
    dl->AddPolyline(k, 4, ST_DEEP, O(1.2f), ImDrawFlags_Closed);
    dl->AddLine(V(cx, a.y + O(4)), V(cx, a.y + kh - O(4)), GOLD_LINE, O(1.2f));
    dl->AddLine(V(cx - O(5), a.y + O(8)), V(cx + O(5), a.y + O(8)), GOLD_LINE, O(1.2f));
    ImVec2 p(cx - O(30), b.y - O(7));
    dl->AddRectFilled(p, V(cx + O(30), b.y + O(1)), ST_MID);
    bevel_band(dl, p, V(cx + O(30), b.y + O(1)), O(2.5f), ST_HI, ST_LIGHT, ST_DEEP, ST_DARK);
    for (int i = -1; i <= 1; i++) dl->AddCircleFilled(V(cx + i * O(14), b.y - O(3)), O(1.8f), BR_LIGHT, 8);
}

// ---------------------------------------------------------------------------
// Dwarf: bronze frames with chamfered, stepped corners

// The eight corners of a rectangle with its corners cut by c
static void chamfer(ImVec2 a, ImVec2 b, float c, ImVec2 *o)
{
    o[0] = V(a.x + c, a.y); o[1] = V(b.x - c, a.y); o[2] = V(b.x, a.y + c); o[3] = V(b.x, b.y - c);
    o[4] = V(b.x - c, b.y); o[5] = V(a.x + c, b.y); o[6] = V(a.x, b.y - c); o[7] = V(a.x, a.y + c);
}

static void rivet(ImDrawList *dl, ImVec2 p, float r)
{
    dl->AddCircleFilled(V(p.x + r * 0.25f, p.y + r * 0.3f), r, BR_DEEP, 12);
    dl->AddCircleFilled(p, r, BR_MID, 12);
    dl->AddCircleFilled(V(p.x - r * 0.3f, p.y - r * 0.3f), r * 0.45f, BR_HI, 10);
}

static void lozenge(ImDrawList *dl, ImVec2 c, float w, float h, ImU32 fill)
{
    ImVec2 q[4] = { V(c.x, c.y - h), V(c.x + w, c.y), V(c.x, c.y + h), V(c.x - w, c.y) };
    dl->AddConvexPolyFilled(q, 4, fill);
    dl->AddTriangleFilled(q[3], q[0], c, BR_HI);
    dl->AddTriangleFilled(q[1], q[2], c, BR_DARK);
    dl->AddPolyline(q, 4, BR_DEEP, O(1), ImDrawFlags_Closed);
}

void orn_dwarf_frame(ImDrawList *dl, ImVec2 a, ImVec2 b, float t)
{
    float c = O(16);
    ImVec2 oa(a.x - t * 0.5f, a.y - t * 0.5f), ob(b.x + t * 0.5f, b.y + t * 0.5f);
    ImVec2 ia(a.x + t * 0.5f, a.y + t * 0.5f), ib(b.x - t * 0.5f, b.y - t * 0.5f);
    ImVec2 o[8], in[8];
    chamfer(oa, ob, c + t * 0.5f, o);
    chamfer(ia, ib, c - t * 0.2f, in);
    // the band, one face per side: light faces up and left, dark down and right
    ImU32 face[8] = { BR_LIGHT, BR_LIGHT, BR_MID, BR_DARK, BR_DARK, BR_DARK, BR_MID, BR_LIGHT };
    for (int i = 0; i < 8; i++) {
        int j = (i + 1) % 8;
        dl->AddQuadFilled(o[i], o[j], in[j], in[i], face[i]);
    }
    dl->AddPolyline(o, 8, BR_DEEP, O(1.4f), ImDrawFlags_Closed);
    dl->AddPolyline(in, 8, BR_DEEP, O(1.4f), ImDrawFlags_Closed);
    // an engraved groove along the middle of the band
    ImVec2 g[8];
    chamfer(a, b, c, g);
    dl->AddPolyline(g, 8, col(40, 27, 12, 200), O(1.2f), ImDrawFlags_Closed);
    for (int i = 0; i < 8; i++) g[i] = V(g[i].x + O(0.8f), g[i].y + O(0.8f));
    dl->AddPolyline(g, 8, col(236, 204, 136, 90), O(0.8f), ImDrawFlags_Closed);

    // corner brackets: an L of bronze over each corner, its arms ending in two steps, riveted at the angle
    for (int k = 0; k < 4; k++) {
        float sx = (k == 0 || k == 3) ? 1.0f : -1.0f, sy = (k < 2) ? 1.0f : -1.0f;
        ImVec2 cn((sx > 0 ? oa.x : ob.x), (sy > 0 ? oa.y : ob.y));
        float e = O(2), L = O(40), w = t + O(5), st = O(6);
        float lx[10] = { -e, L, L, L - st, L - st, w, w, w * 0.55f, w * 0.55f, -e };
        float ly[10] = { -e, -e, w * 0.55f, w * 0.55f, w, w, L - st, L - st, L, L };
        ImVec2 q[10];
        for (int i = 0; i < 10; i++) q[i] = V(cn.x + sx * lx[i], cn.y + sy * ly[i]);
        dl->AddConcavePolyFilled(q, 10, BR_MID);
        // light on the outer faces, shade on the inner ones
        dl->AddLine(q[0], q[1], BR_HI, O(1.6f));
        dl->AddLine(q[9], q[0], BR_HI, O(1.6f));
        dl->AddLine(q[4], q[5], BR_DEEP, O(1.6f));
        dl->AddLine(q[5], q[6], BR_DEEP, O(1.6f));
        dl->AddPolyline(q, 10, BR_DEEP, O(1.2f), ImDrawFlags_Closed);
        // an engraved line along each arm
        dl->AddLine(V(cn.x + sx * w * 0.3f, cn.y + sy * w * 0.3f), V(cn.x + sx * (L - st - O(3)), cn.y + sy * w * 0.3f), col(40, 27, 12, 190), O(1));
        dl->AddLine(V(cn.x + sx * w * 0.3f, cn.y + sy * w * 0.3f), V(cn.x + sx * w * 0.3f, cn.y + sy * (L - st - O(3))), col(40, 27, 12, 190), O(1));
        rivet(dl, V(cn.x + sx * w * 0.62f, cn.y + sy * w * 0.62f), O(3.4f));
        // small rivets near the arm ends
        rivet(dl, V(cn.x + sx * (L - st * 1.9f), cn.y + sy * w * 0.62f), O(2));
        rivet(dl, V(cn.x + sx * w * 0.62f, cn.y + sy * (L - st * 1.9f)), O(2));
    }
    // lozenge studs in the middle of each side
    float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
    for (int i = -1; i <= 1; i++) {
        float w = i == 0 ? O(9) : O(6), h = i == 0 ? O(7) : O(5);
        lozenge(dl, V(mx + i * O(17), a.y), w, h, BR_MID);
        lozenge(dl, V(mx + i * O(17), b.y), w, h, BR_MID);
    }
    lozenge(dl, V(a.x, my), O(6), O(9), BR_MID);
    lozenge(dl, V(b.x, my), O(6), O(9), BR_MID);
}

void orn_dwarf_fill(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 fill)
{
    ImVec2 p[8];
    chamfer(a, b, O(16), p);
    dl->AddConvexPolyFilled(p, 8, fill);
}

void orn_dwarf_plate(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 fill, bool lit)
{
    float c = std::min(O(9), (b.y - a.y) * 0.3f);
    ImVec2 p[8];
    chamfer(a, b, c, p);
    dl->AddConvexPolyFilled(p, 8, fill);
    ImVec2 in[8];
    chamfer(V(a.x + O(3), a.y + O(3)), V(b.x - O(3), b.y - O(3)), c - O(1.5f), in);
    dl->AddPolyline(p, 8, lit ? BR_LIGHT : BR_MID, O(1.6f), ImDrawFlags_Closed);
    dl->AddPolyline(in, 8, col(186, 146, 76, lit ? 150 : 80), O(0.9f), ImDrawFlags_Closed);
}

// ---------------------------------------------------------------------------
// Elf: a heading's rule that tapers and ends in a leaf scroll

static void leaf(ImDrawList *dl, ImVec2 base, float len, float ang, float wid, ImU32 fill, ImU32 vein)
{
    ImVec2 d(cosf(ang), sinf(ang)), n(-d.y, d.x);
    ImVec2 tip(base.x + d.x * len, base.y + d.y * len);
    ImVec2 m1(base.x + d.x * len * 0.45f + n.x * wid, base.y + d.y * len * 0.45f + n.y * wid);
    ImVec2 m2(base.x + d.x * len * 0.45f - n.x * wid, base.y + d.y * len * 0.45f - n.y * wid);
    dl->PathLineTo(base);
    dl->PathBezierQuadraticCurveTo(m1, tip, 10);
    dl->PathBezierQuadraticCurveTo(m2, base, 10);
    dl->PathFillConcave(fill);
    dl->AddLine(base, V(base.x + d.x * len * 0.8f, base.y + d.y * len * 0.8f), vein, O(0.8f));
}

void orn_elf_rule(ImDrawList *dl, float x0, float x1, float y)
{
    if (x1 - x0 < O(60)) return;
    ImU32 gold = col(206, 166, 78, 220), soft = col(206, 166, 78, 110);
    float curl = O(26);
    float xe = x1 - curl;
    // the main line tapers from thick at the start to fine at the scroll
    dl->AddTriangleFilled(V(x0, y - O(1.3f)), V(xe, y - O(0.3f)), V(xe, y + O(0.3f)), gold);
    dl->AddTriangleFilled(V(x0, y - O(1.3f)), V(x0, y + O(1.3f)), V(xe, y + O(0.3f)), gold);
    // a fine second line below it, fading out
    dl->AddLine(V(x0 + O(10), y + O(4)), V(xe - O(40), y + O(4)), soft, O(0.8f));
    // a small diamond at the start
    ImVec2 dm[4] = { V(x0 - O(7), y), V(x0 - O(3), y - O(3.2f)), V(x0 + O(1), y), V(x0 - O(3), y + O(3.2f)) };
    dl->AddConvexPolyFilled(dm, 4, gold);
    // the scroll: the line rises, curls back over itself and ends in a spiral
    dl->PathLineTo(V(xe, y));
    dl->PathBezierCubicCurveTo(V(xe + curl * 0.6f, y), V(xe + curl, y - curl * 0.35f), V(xe + curl * 0.8f, y - curl * 0.6f), 16);
    dl->PathBezierCubicCurveTo(V(xe + curl * 0.6f, y - curl * 0.85f), V(xe + curl * 0.2f, y - curl * 0.6f), V(xe + curl * 0.35f, y - curl * 0.35f), 16);
    dl->PathBezierCubicCurveTo(V(xe + curl * 0.45f, y - curl * 0.2f), V(xe + curl * 0.62f, y - curl * 0.3f), V(xe + curl * 0.55f, y - curl * 0.45f), 12);
    dl->PathStroke(gold, O(1.3f));
    dl->AddCircleFilled(V(xe + curl * 0.55f, y - curl * 0.45f), O(1.6f), gold, 10);
    // two leaves springing from the scroll
    leaf(dl, V(xe + curl * 0.7f, y - O(1)), O(13), 0.25f, O(3.6f), gold, col(70, 49, 21, 200));
    leaf(dl, V(xe + curl * 0.35f, y + O(0.5f)), O(10), 0.9f, O(3), soft, col(70, 49, 21, 160));
}

// ---------------------------------------------------------------------------
// Buttons

bool orn_arch_button(const char *label, ImVec2 size, ImFont *font, bool enabled)
{
    ImGuiWindow *w = ImGui::GetCurrentWindow();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::InvisibleButton(label, size);
    bool hov = enabled && ImGui::IsItemHovered(), act = enabled && ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImDrawList *dl = w->DrawList;
    float archH = size.y * 0.22f;
    ImVec2 a(p.x, p.y), b(p.x + size.x, p.y + size.y);
    // the outline: a shallow round arch over straight sides
    const int N = 24;
    ImVec2 pts[N + 3];
    float halfW = size.x * 0.5f, cx = p.x + halfW;
    float R = (halfW * halfW + archH * archH) / (2 * archH);     // circle through the arch's ends and top
    float cy = a.y + R;
    float a0 = asinf(halfW / R);
    for (int i = 0; i <= N; i++) {
        float t = -a0 + 2 * a0 * i / N;
        pts[i] = V(cx + sinf(t) * R, cy - cosf(t) * R);
    }
    pts[N + 1] = V(b.x, b.y);
    pts[N + 2] = V(a.x, b.y);
    ImU32 fill = !enabled ? col(70, 58, 40) : act ? col(124, 92, 38) : hov ? col(192, 146, 64) : col(158, 118, 48);
    ImU32 shade = !enabled ? col(52, 43, 30) : col(102, 74, 30);
    dl->AddConvexPolyFilled(pts, N + 3, fill);
    // darker lower part for depth
    dl->AddRectFilledMultiColor(V(a.x + O(2), a.y + size.y * 0.55f), V(b.x - O(2), b.y - O(2)), col(0, 0, 0, 0), col(0, 0, 0, 0),
                                (shade & 0x00FFFFFF) | 0x70000000, (shade & 0x00FFFFFF) | 0x70000000);
    dl->AddPolyline(pts, N + 3, enabled ? col(236, 204, 136) : col(110, 96, 74), O(1.6f), ImDrawFlags_Closed);
    // an inner carved line following the arch
    ImVec2 in[N + 3];
    for (int i = 0; i <= N; i++) {
        float t = -a0 + 2 * a0 * i / N;
        in[i] = V(cx + sinf(t) * (R - O(5)), cy - cosf(t) * (R - O(5)));
        in[i].x = std::max(a.x + O(5), std::min(b.x - O(5), in[i].x));
    }
    in[N + 1] = V(b.x - O(5), b.y - O(5));
    in[N + 2] = V(a.x + O(5), b.y - O(5));
    dl->AddPolyline(in, N + 3, enabled ? col(70, 49, 21, 160) : col(40, 34, 26, 160), O(1), ImDrawFlags_Closed);
    // the keystone at the top
    float kw = O(8), kb = O(5), kh = O(11);
    ImVec2 k[4] = { V(cx - kw, a.y - O(3)), V(cx + kw, a.y - O(3)), V(cx + kb, a.y + kh), V(cx - kb, a.y + kh) };
    dl->AddConvexPolyFilled(k, 4, enabled ? col(214, 172, 94) : col(90, 78, 58));
    dl->AddPolyline(k, 4, enabled ? col(70, 49, 21) : col(40, 34, 26), O(1.2f), ImDrawFlags_Closed);
    // the label, a little below the middle (the arch takes the top)
    ImGui::PushFont(font, font->LegacySize);
    ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    ImVec2 tp(cx - ts.x * 0.5f, a.y + archH * 0.5f + (size.y - archH * 0.5f - ts.y) * 0.5f);
    dl->AddText(V(tp.x + O(1), tp.y + O(1)), col(255, 236, 190, enabled ? 90 : 30), label, ImGui::FindRenderedTextEnd(label));
    dl->AddText(tp, enabled ? col(30, 20, 8) : col(30, 26, 20), label, ImGui::FindRenderedTextEnd(label));
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
    ImU32 fill = !enabled ? col(36, 30, 24) : act ? col(92, 70, 38) : hov ? col(76, 58, 34) : col(52, 40, 26);
    orn_dwarf_plate(dl, p, V(p.x + size.x, p.y + size.y), fill, enabled && hov);
    ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    ImVec2 tp(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f);
    dl->AddText(tp, enabled ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), label, ImGui::FindRenderedTextEnd(label));
    return pressed && enabled;
}
