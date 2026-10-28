// ROTK Fix Launcher - drawn ornaments (see ornament.h). Everything is drawn with ImGui's
// shapes at the current scale; there are no pictures.
#include "ornament.h"
#include "i18n.h"
#include <string>
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

// A pointed Gothic arch rising from a baseline at y, centre cx, a trefoil carved inside
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
    // a small pinnacle knob at the point
    dl->AddCircleFilled(V(cx, y - h - O(1)), O(2.6f), g, 12);
    // a trefoil of tracery inside the arch: three carved lobes with gilded edges
    float tr = w * 0.24f, ty = y - h * 0.36f;
    ImVec2 lc[3] = { V(cx, ty - tr * 0.62f), V(cx - tr * 0.62f, ty + tr * 0.4f), V(cx + tr * 0.62f, ty + tr * 0.4f) };
    for (auto &c : lc) dl->AddCircleFilled(c, tr * 0.6f, SN_MID, 18);
    for (auto &c : lc) dl->AddCircle(c, tr * 0.6f, g, 18, O(1));
    dl->AddCircleFilled(V(cx, ty), tr * 0.22f, g, 12);
}

// ---------------------------------------------------------------------------
// Men: the window's granite border, with Gothic quatrefoils and a pointed gable

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
    float mid = t * 0.5f, R = O(15), cx = size.x * 0.5f;
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
// Elves: slender lines, notched corners, pointed arches

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
    // a slender pointed arch rising from the top edge, with a small stone
    float cx = (a.x + b.x) * 0.5f, w = O(30), h = O(15);
    ImU32 gl = PGOLD(235);
    dl->AddBezierCubic(V(cx - w, a.y), V(cx - w * 0.35f, a.y), V(cx - O(3), a.y - h * 0.5f), V(cx, a.y - h), gl, O(1.5f));
    dl->AddBezierCubic(V(cx + w, a.y), V(cx + w * 0.35f, a.y), V(cx + O(3), a.y - h * 0.5f), V(cx, a.y - h), gl, O(1.5f));
    dl->AddBezierCubic(V(cx - w * 0.6f, a.y), V(cx - w * 0.2f, a.y), V(cx - O(2), a.y - h * 0.3f), V(cx, a.y - h * 0.62f), SILVER(150), O(0.9f));
    dl->AddBezierCubic(V(cx + w * 0.6f, a.y), V(cx + w * 0.2f, a.y), V(cx + O(2), a.y - h * 0.3f), V(cx, a.y - h * 0.62f), SILVER(150), O(0.9f));
    dl->AddCircleFilled(V(cx, a.y - h), O(2), gl, 10);
    dl->AddCircleFilled(V(cx, a.y - h * 0.3f), O(2.6f), col(170, 164, 150), 12);
    dl->AddCircle(V(cx, a.y - h * 0.3f), O(4.2f), SILVER(120), 16, O(0.8f));
    // a small diamond at the bottom centre
    ImVec2 dm[4] = { V(cx - O(5), b.y), V(cx, b.y - O(4)), V(cx + O(5), b.y), V(cx, b.y + O(4)) };
    dl->AddConvexPolyFilled(dm, 4, gl);
}

void orn_plate(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 fill, bool lit)
{
    float r = std::min(O(8), (b.y - a.y) * 0.35f);
    dl->AddRectFilled(a, b, fill, r);
    dl->AddRect(a, b, lit ? PGOLD(255) : PGOLD(170), r, O(1.3f));
    dl->AddRect(V(a.x + O(3), a.y + O(3)), V(b.x - O(3), b.y - O(3)), SILVER(lit ? 110 : 60), std::max(0.0f, r - O(2)), O(0.7f));
    // small pointed clasps at the middle of the ends
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
    // a bronze boss at the top of the arch
    ImU32 fg = enabled ? col(184, 144, 78) : col(110, 100, 84);
    dl->AddCircleFilled(V(cx, a.y + O(3)), O(3), fg, 12);
    dl->AddCircle(V(cx, a.y + O(3)), O(3), col(60, 42, 16), 12, O(1));
    ImGui::PushFont(font, font->LegacySize);
    std::string shownS = tr(std::string(label, ImGui::FindRenderedTextEnd(label)));   // its translation (drawn here, not by ImGui)
    const char *shown = shownS.c_str();
    ImVec2 ts = ImGui::CalcTextSize(shown);
    ImVec2 tp(cx - ts.x * 0.5f, a.y + archH * 0.5f + (size.y - archH * 0.5f - ts.y) * 0.5f);
    dl->AddText(V(tp.x + O(1), tp.y + O(1.5f)), col(20, 12, 4, enabled ? 200 : 60), shown);
    dl->AddText(tp, enabled ? col(240, 220, 170) : col(120, 112, 100), shown);
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
    std::string shownS = tr(std::string(label, ImGui::FindRenderedTextEnd(label)));   // its translation (drawn here, not by ImGui)
    const char *shown = shownS.c_str();
    ImVec2 ts = ImGui::CalcTextSize(shown);
    ImVec2 tp(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f);
    dl->AddText(tp, enabled ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), shown);
    return pressed && enabled;
}

// A tab (elves): notched top corners, a bronze edge with a fine silver line inside, open at
// the bottom where it meets the page. The chosen tab is lit, with a bronze diamond on top.
void orn_tab(ImDrawList *dl, ImVec2 a, ImVec2 b, bool selected, bool hovered)
{
    float n = std::min(O(7), (b.y - a.y) * 0.3f);
    ImVec2 shape[6] = { V(a.x, b.y), V(a.x, a.y + n), V(a.x + n, a.y), V(b.x - n, a.y), V(b.x, a.y + n), V(b.x, b.y) };
    ImU32 fill = selected ? col(70, 53, 30, 235) : hovered ? col(56, 44, 28, 215) : col(22, 19, 15, 190);
    dl->AddConvexPolyFilled(shape, 6, fill);
    if (selected)       // a soft glow from the top
        dl->AddRectFilledMultiColor(V(a.x + n, a.y + O(1)), V(b.x - n, a.y + (b.y - a.y) * 0.55f),
                                    col(232, 196, 112, 40), col(232, 196, 112, 40), col(232, 196, 112, 0), col(232, 196, 112, 0));
    dl->AddPolyline(shape, 6, selected ? PGOLD(255) : hovered ? PGOLD(220) : PGOLD(140), O(1.3f));
    float i = O(3);
    ImVec2 inner[6] = { V(a.x + i, b.y), V(a.x + i, a.y + n + i * 0.4f), V(a.x + n + i * 0.4f, a.y + i), V(b.x - n - i * 0.4f, a.y + i),
                        V(b.x - i, a.y + n + i * 0.4f), V(b.x - i, b.y) };
    dl->AddPolyline(inner, 6, SILVER(selected ? 120 : 55), O(0.7f));
    if (selected) {
        float cx = (a.x + b.x) * 0.5f;
        ImVec2 dm[4] = { V(cx, a.y - O(4)), V(cx + O(4), a.y), V(cx, a.y + O(4)), V(cx - O(4), a.y) };
        dl->AddConvexPolyFilled(dm, 4, PGOLD(255));
    }
}
