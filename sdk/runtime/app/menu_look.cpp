// The in-game menu's look: see menu_look.h. The launcher's palette and ornaments (launcher/src/ornament.cpp), at the
// scale of the game's screen. Our code (MIT).
#include "app/menu_look.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/menu_fonts.h"

namespace app::look {

namespace {

ImU32 Rgb(int r, int g, int b, float a = 1) { return IM_COL32(r, g, b, int(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f)); }
ImVec4 Rgb4(int r, int g, int b, float a = 1) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }
ImVec2 V(float x, float y) { return ImVec2(x, y); }

// stone: weathered granite; gold: antique bronze-gold; silver for the fine inner lines (ornament.cpp)
ImU32 Stone(float a = 1) { return Rgb(20, 17, 13, a); }
ImU32 StoneLit(float a = 1) { return Rgb(54, 44, 30, a); }
ImU32 Silver(float a = 1) { return Rgb(150, 150, 144, a); }

void Notched(ImDrawList* dl, ImVec2 a, ImVec2 b, float r) {
    dl->PathArcTo(V(a.x, a.y), r, 1.5708f, 0.0f, 8);
    dl->PathArcTo(V(b.x, a.y), r, 3.14159f, 1.5708f, 8);
    dl->PathArcTo(V(b.x, b.y), r, 4.71239f, 3.14159f, 8);
    dl->PathArcTo(V(a.x, b.y), r, 6.28318f, 4.71239f, 8);
}

void Diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    ImVec2 p[4] = {V(c.x, c.y - r), V(c.x + r, c.y), V(c.x, c.y + r), V(c.x - r, c.y)};
    dl->AddConvexPolyFilled(p, 4, col);
}

ImVec2 TextAt(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* text, float wrap = 0, bool shadow = true) {
    ImVec2 s = f->CalcTextSizeA(size, FLT_MAX, wrap, text);
    p = V(std::round(p.x), std::round(p.y));
    if (shadow) dl->AddText(f, size, V(p.x + size * 0.05f, p.y + size * 0.07f), IM_COL32(0, 0, 0, 170), text, nullptr, wrap);
    dl->AddText(f, size, p, col, text, nullptr, wrap);
    return s;
}

// a controller button or a key, as a small picture; its width (draw false: only measured)
float Glyph(ImDrawList* dl, const Look& L, ImVec2 left_mid, float h, const std::string& name, bool draw = true) {
    ImFont* f = L.fonts.body;   // (Cinzel Decorative's swash L made "LB" hard to read)
    const float fs = h * 0.50f;
    const bool face = name.size() == 1 && std::strchr("ABXY", name[0]);
    if (face) {
        if (!draw) return h;
        const float r = h * 0.5f;
        const ImVec2 c(left_mid.x + r, left_mid.y);
        // the Xbox colours, darkened to sit with the gold
        ImU32 k = name[0] == 'A' ? Rgb(70, 140, 60) : name[0] == 'B' ? Rgb(170, 50, 44) : name[0] == 'X' ? Rgb(44, 96, 168) : Rgb(196, 156, 40);
        dl->AddCircleFilled(V(c.x + L.u, c.y + 1.5f * L.u), r, IM_COL32(0, 0, 0, 140), 28);
        dl->AddCircleFilled(c, r, k, 28);
        dl->AddCircle(c, r - L.u, Rgb(255, 240, 210, 0.35f), 28, std::max(1.f, L.u));
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, name.c_str());
        dl->AddText(f, fs, V(std::round(c.x - ts.x * 0.5f), std::round(c.y - ts.y * 0.52f)), IM_COL32(255, 250, 240, 255), name.c_str());
        return h;
    }
    ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, name.c_str());
    const float w = std::max(h * 1.1f, ts.x + h * 0.7f);
    if (!draw) return w;
    const ImVec2 a(left_mid.x, left_mid.y - h * 0.5f), b(left_mid.x + w, left_mid.y + h * 0.5f);
    const float r = h * 0.22f;
    dl->AddRectFilled(V(a.x + L.u, a.y + 1.5f * L.u), V(b.x + L.u, b.y + 1.5f * L.u), IM_COL32(0, 0, 0, 140), r);
    dl->AddRectFilledMultiColor(a, b, Rgb(58, 50, 38), Rgb(58, 50, 38), Rgb(30, 26, 20), Rgb(30, 26, 20));
    dl->AddRect(a, b, Gold(0.75f), r, 0, std::max(1.f, L.u));
    dl->AddText(f, fs, V(std::round(a.x + (w - ts.x) * 0.5f), std::round(left_mid.y - ts.y * 0.52f)), Text(), name.c_str());
    return w;
}

}  // namespace

ImU32 Gold(float a) { return Rgb(206, 166, 78, a); }
ImU32 GoldLight(float a) { return Rgb(236, 204, 128, a); }
ImU32 Text(float a) { return Rgb(236, 226, 204, a); }
ImU32 Dim(float a) { return Rgb(170, 154, 126, a); }

Fonts AddFonts(ImFontAtlas* atlas, ImFont* fallback) {
    Fonts f;
    ImFontConfig c;
    c.FontDataOwnedByAtlas = false;  // the data is static (menu_fonts.h)
    c.OversampleH = 2;
    f.title = atlas->AddFontFromMemoryTTF((void*)menu_font_uncial, int(sizeof menu_font_uncial), 64.f, &c);
    f.head = atlas->AddFontFromMemoryTTF((void*)menu_font_cinzel, int(sizeof menu_font_cinzel), 32.f, &c);
    // the text: the digits first (EB Garamond's plain figures: IM Fell's old-style 1 and 0 read as I and o in "100"
    // or "Player 1"), then IM Fell English merged in for everything else (the first font to have a letter keeps it)
    static const ImWchar kDigits[] = {0x0025, 0x0025, 0x0030, 0x0039, 0};
    ImFontConfig dc = c;
    dc.GlyphRanges = kDigits;
    f.body = atlas->AddFontFromMemoryTTF((void*)menu_font_digits, int(sizeof menu_font_digits), 32.f, &dc);
    if (f.body) {
        ImFontConfig mc = c;
        mc.MergeMode = true;
        mc.DstFont = f.body;
        atlas->AddFontFromMemoryTTF((void*)menu_font_garamond, int(sizeof menu_font_garamond), 32.f, &mc);
    } else {
        f.body = atlas->AddFontFromMemoryTTF((void*)menu_font_garamond, int(sizeof menu_font_garamond), 32.f, &c);
    }
    // letters these fonts don't have (Cinzel has no Cyrillic, say): from the menu's earlier font, merged in behind
    if (fallback && fallback->Sources.Size) {
        for (ImFont* to : {f.head, f.body}) {
            if (!to) continue;
            // a fresh config with the earlier font's data (a copy of its own would carry the loader's state, which
            // AddFont refuses, and its 48 px size: merged letters 1.5 times the text's - review)
            const ImFontConfig* src = fallback->Sources[0];
            ImFontConfig m;
            m.FontData = src->FontData;
            m.FontDataSize = src->FontDataSize;
            m.GlyphRanges = src->GlyphRanges;
            m.OversampleH = 2;
            m.MergeMode = true;
            m.DstFont = to;
            m.FontDataOwnedByAtlas = false;
            atlas->AddFont(&m);
        }
    }
    if (!f.title) f.title = f.head;
    if (!f.head) f.head = fallback;
    if (!f.body) f.body = fallback;
    return f;
}

Look MakeLook(ImVec2 display, const Fonts& fonts, bool pad) {
    Look L;
    L.display = display;
    L.u = std::max(0.5f, display.y / 1080.f);
    const float w = std::min(display.x, display.y * 16.f / 9.f);
    L.c0 = V(std::floor((display.x - w) * 0.5f), 0.f);
    L.c1 = V(L.c0.x + w, display.y);
    L.pad = pad;
    L.fonts = fonts;
    return L;
}

void PushStyle(const Look& L) {
    const float u = L.u;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, V(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, V(14 * u, 8 * u));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, V(10 * u, 6 * u));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * u);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4 * u);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 10 * u);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(1.f, u));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, std::max(1.f, u));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10 * u);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 22 * u);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Rgb4(18, 15, 11, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Text, Rgb4(236, 226, 204));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, Rgb4(120, 110, 94));
    ImGui::PushStyleColor(ImGuiCol_Border, Rgb4(184, 144, 78, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Rgb4(22, 19, 15, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Rgb4(48, 40, 28));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Rgb4(64, 52, 34));
    ImGui::PushStyleColor(ImGuiCol_Button, Rgb4(40, 34, 26));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Rgb4(70, 56, 34));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Rgb4(110, 80, 36));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, Rgb4(232, 196, 112));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, Rgb4(222, 186, 104));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, Rgb4(240, 210, 140));
    ImGui::PushStyleColor(ImGuiCol_Header, Rgb4(110, 84, 40, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Rgb4(140, 104, 50, 0.75f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, Rgb4(166, 124, 60));
    ImGui::PushStyleColor(ImGuiCol_Separator, Rgb4(184, 144, 78, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, Rgb4(90, 72, 44));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, Rgb4(150, 118, 60));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, Rgb4(206, 166, 78));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));  // the rows and buttons show focus themselves
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.55f));
}
void PopStyle() {
    ImGui::PopStyleColor(25);
    ImGui::PopStyleVar(13);
}

void Backdrop(ImDrawList* dl, const Look& L) {
    const ImVec2 d = L.display;
    dl->PushClipRectFullScreen();
    // the game stays in sight, dimmed: darker at the top and bottom, where the title and the hints are
    const ImU32 edge = Rgb(6, 5, 4, 0.94f), mid = Rgb(8, 6, 4, 0.72f);
    dl->AddRectFilledMultiColor(V(0, 0), V(d.x, d.y * 0.22f), edge, edge, mid, mid);
    dl->AddRectFilled(V(0, d.y * 0.22f), V(d.x, d.y * 0.78f), mid);
    dl->AddRectFilledMultiColor(V(0, d.y * 0.78f), V(d.x, d.y), mid, mid, edge, edge);
    if (L.c0.x > 1.f) {  // ultrawide: the sides darker still, softly
        const ImU32 side = IM_COL32(0, 0, 0, 120), none = IM_COL32(0, 0, 0, 0);
        const float e = 48 * L.u;
        dl->AddRectFilled(V(0, 0), V(L.c0.x - e, d.y), side);
        dl->AddRectFilledMultiColor(V(L.c0.x - e, 0), V(L.c0.x, d.y), side, none, none, side);
        dl->AddRectFilled(V(L.c1.x + e, 0), V(d.x, d.y), side);
        dl->AddRectFilledMultiColor(V(L.c1.x, 0), V(L.c1.x + e, d.y), none, side, side, none);
    }
    dl->PopClipRect();
}

void ElfRule(ImDrawList* dl, const Look& L, float x0, float x1, float y) {
    const float u = L.u;
    if (x1 - x0 < 80 * u) return;
    const ImU32 gold = Gold(0.9f), soft = Silver(0.45f);
    const float curl = 26 * u, xe = x1 - curl;
    // tapering from a small diamond to the scroll
    dl->AddTriangleFilled(V(x0, y - 1.2f * u), V(xe, y - 0.35f * u), V(xe, y + 0.35f * u), gold);
    dl->AddTriangleFilled(V(x0, y - 1.2f * u), V(x0, y + 1.2f * u), V(xe, y + 0.35f * u), gold);
    dl->AddLine(V(x0 + 12 * u, y + 4.5f * u), V(xe - 60 * u, y + 4.5f * u), soft, std::max(1.f, 0.7f * u));
    Diamond(dl, V(x0 - 4 * u, y), 4 * u, gold);
    dl->PathLineTo(V(xe, y));
    dl->PathBezierCubicCurveTo(V(xe + curl * 0.6f, y), V(xe + curl, y - curl * 0.35f), V(xe + curl * 0.8f, y - curl * 0.6f), 16);
    dl->PathBezierCubicCurveTo(V(xe + curl * 0.6f, y - curl * 0.85f), V(xe + curl * 0.2f, y - curl * 0.6f),
                               V(xe + curl * 0.35f, y - curl * 0.35f), 16);
    dl->PathBezierCubicCurveTo(V(xe + curl * 0.45f, y - curl * 0.2f), V(xe + curl * 0.62f, y - curl * 0.3f),
                               V(xe + curl * 0.55f, y - curl * 0.45f), 12);
    dl->PathStroke(gold, 0, std::max(1.f, 1.3f * u));
    dl->AddCircleFilled(V(xe + curl * 0.55f, y - curl * 0.45f), 1.6f * u, gold, 10);
}

float Header(ImDrawList* dl, const Look& L, const char* game, const char* right) {
    const float u = L.u, x = L.c0.x + 64 * u, x1 = L.c1.x - 64 * u, y = 30 * u;
    const float ts = 62 * u;
    ImVec2 t = TextAt(dl, L.fonts.title, ts, V(x, y), GoldLight(), "Athelas");
    const float base = y + ts * 0.78f;  // the title's baseline, to stand the other words on
    if (game && *game) {
        const float gs = 30 * u;
        Diamond(dl, V(x + t.x + 22 * u, base - gs * 0.35f), 4 * u, Gold());
        TextAt(dl, L.fonts.head, gs, V(x + t.x + 38 * u, base - gs * 0.86f), Dim(), game);
    }
    if (right && *right) {
        const float rs = 27 * u;
        ImVec2 s = L.fonts.body->CalcTextSizeA(rs, FLT_MAX, 0, right);
        TextAt(dl, L.fonts.body, rs, V(x1 - s.x, base - rs * 0.86f), Dim(), right);
    }
    const float rule = y + ts * 1.06f + 8 * u;
    ElfRule(dl, L, x, x1, rule);
    return rule;
}

void Panel(ImDrawList* dl, ImVec2 a, ImVec2 b, const Look& L) {
    const float u = L.u, r = 12 * u;
    // a soft shadow, the stone with light from above, then the gold edge and a silver hairline inside it
    dl->AddRectFilled(V(a.x + 4 * u, a.y + 6 * u), V(b.x + 4 * u, b.y + 6 * u), IM_COL32(0, 0, 0, 90), r);
    dl->AddRectFilled(V(a.x + r, a.y), V(b.x - r, b.y), Stone(0.90f));
    dl->AddRectFilled(V(a.x, a.y + r), V(a.x + r, b.y - r), Stone(0.90f));
    dl->AddRectFilled(V(b.x - r, a.y + r), V(b.x, b.y - r), Stone(0.90f));
    dl->AddRectFilledMultiColor(V(a.x + r, a.y + u), V(b.x - r, a.y + (b.y - a.y) * 0.18f), Rgb(232, 196, 112, 0.05f),
                                Rgb(232, 196, 112, 0.05f), Rgb(232, 196, 112, 0), Rgb(232, 196, 112, 0));
    Notched(dl, a, b, r);
    dl->PathStroke(Gold(0.85f), ImDrawFlags_Closed, std::max(1.f, 1.6f * u));
    const float g = 5 * u;
    Notched(dl, V(a.x + g, a.y + g), V(b.x - g, b.y - g), r);
    dl->PathStroke(Silver(0.30f), ImDrawFlags_Closed, std::max(1.f, 0.8f * u));
    // a small pointed arch on the top edge and a diamond on the bottom, as the launcher's panels
    const float cx = (a.x + b.x) * 0.5f, w = 28 * u, h = 13 * u;
    dl->AddBezierCubic(V(cx - w, a.y), V(cx - w * 0.35f, a.y), V(cx - 3 * u, a.y - h * 0.5f), V(cx, a.y - h), Gold(0.9f), 1.4f * u);
    dl->AddBezierCubic(V(cx + w, a.y), V(cx + w * 0.35f, a.y), V(cx + 3 * u, a.y - h * 0.5f), V(cx, a.y - h), Gold(0.9f), 1.4f * u);
    dl->AddCircleFilled(V(cx, a.y - h), 2 * u, Gold(), 10);
    Diamond(dl, V(cx, b.y), 4.5f * u, Gold(0.9f));
}

float Heading(ImDrawList* dl, const Look& L, ImVec2 p, const char* text, float size, float max_w) {
    size *= 1.35f;   // (Shagg: bigger text throughout)
    float s = L.px(size);
    if (max_w > 0) {   // a long name in a narrow panel (4:3): smaller, to stay inside it
        float w = L.fonts.head->CalcTextSizeA(s, FLT_MAX, 0, text).x + 18 * L.u;
        if (w > max_w) s = std::max(s * 0.6f, s * max_w / w);
    }
    Diamond(dl, V(p.x + 5 * L.u, p.y + s * 0.55f), 4.5f * L.u, Gold());
    TextAt(dl, L.fonts.head, s, V(p.x + 18 * L.u, p.y), GoldLight(), text);
    return s * 1.25f;
}

int Tabs(const Look& L, ImVec2 a, ImVec2 b, const std::vector<std::string>& names, int current, const char* prev,
         const char* next) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float u = L.u, h = b.y - a.y, gh = h * 0.58f, gap = 8 * u;
    float x0 = a.x, x1 = b.x;
    if (prev && *prev) x0 += Glyph(dl, L, V(a.x, (a.y + b.y) * 0.5f), gh, prev) + 16 * u;
    if (next && *next) {
        const float w = Glyph(dl, L, V(0, 0), gh, next, false);
        Glyph(dl, L, V(b.x - w, (a.y + b.y) * 0.5f), gh, next);
        x1 -= w + 16 * u;
    }
    const int n = int(names.size());
    if (!n) return -1;
    const float tw = std::min((x1 - x0 - gap * (n - 1)) / n, 300 * u);
    const float start = x0 + ((x1 - x0) - (tw * n + gap * (n - 1))) * 0.5f;
    int clicked = -1;
    for (int i = 0; i < n; ++i) {
        const ImVec2 ta(std::round(start + i * (tw + gap)), a.y), tb(std::round(start + i * (tw + gap) + tw), b.y);
        ImGui::SetCursorScreenPos(ta);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tab", V(tb.x - ta.x, tb.y - ta.y))) clicked = i;
        const bool hov = ImGui::IsItemHovered(), sel = i == current;
        ImGui::PopID();
        // notched top corners, open at the bottom where the tab meets the line (ornament.cpp's orn_tab)
        const float c = std::min(9 * u, h * 0.3f);
        ImVec2 shape[6] = {V(ta.x, tb.y), V(ta.x, ta.y + c), V(ta.x + c, ta.y), V(tb.x - c, ta.y), V(tb.x, ta.y + c), V(tb.x, tb.y)};
        dl->AddConvexPolyFilled(shape, 6, sel ? StoneLit(0.96f) : hov ? Rgb(40, 33, 24, 0.92f) : Rgb(18, 15, 12, 0.82f));
        if (sel)
            dl->AddRectFilledMultiColor(V(ta.x + c, ta.y + u), V(tb.x - c, ta.y + h * 0.6f), Rgb(232, 196, 112, 0.16f),
                                        Rgb(232, 196, 112, 0.16f), Rgb(232, 196, 112, 0), Rgb(232, 196, 112, 0));
        dl->AddPolyline(shape, 6, sel ? Gold() : hov ? Gold(0.8f) : Gold(0.45f), 0, std::max(1.f, 1.3f * u));
        if (sel) Diamond(dl, V((ta.x + tb.x) * 0.5f, ta.y), 4.5f * u, GoldLight());
        std::string up = names[size_t(i)];
        for (char& ch : up)
            if (ch >= 'a' && ch <= 'z') ch = char(ch - 32);
        const float fs = 29 * u;
        ImVec2 ts = L.fonts.head->CalcTextSizeA(fs, FLT_MAX, 0, up.c_str());
        TextAt(dl, L.fonts.head, fs, V(ta.x + (tb.x - ta.x - ts.x) * 0.5f, ta.y + (h - ts.y) * 0.5f + u),
               sel ? GoldLight() : hov ? Text() : Dim(), up.c_str());
    }
    dl->AddLine(V(a.x, b.y), V(b.x, b.y), Gold(0.55f), std::max(1.f, 1.3f * u));
    return clicked;
}

bool Button(const Look& L, const char* label, ImVec2 size, bool primary) {
    ImGui::PushID(label);
    const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + size.x, a.y + size.y);
    const bool pressed = ImGui::InvisibleButton("##b", size, ImGuiButtonFlags_EnableNav);
    const bool hov = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && GImGui->NavCursorVisible);
    const bool held = ImGui::IsItemActive();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float u = L.u, r = 6 * u;
    dl->AddRectFilled(V(a.x + 3 * u, a.y + 4 * u), V(b.x + 3 * u, b.y + 4 * u), IM_COL32(0, 0, 0, 110), r);
    ImU32 top, bot;
    if (primary) {
        top = held ? Rgb(110, 80, 36) : hov ? Rgb(178, 136, 66) : Rgb(150, 110, 50);
        bot = held ? Rgb(80, 56, 24) : hov ? Rgb(120, 88, 38) : Rgb(98, 70, 30);
    } else {
        top = held ? Rgb(36, 30, 22) : hov ? Rgb(62, 52, 38) : Rgb(40, 34, 26);
        bot = held ? Rgb(24, 20, 15) : hov ? Rgb(38, 32, 24) : Rgb(24, 21, 17);
    }
    // rounded fill with a vertical shade: a filled rounded rect, shaded by its vertices
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, IM_COL32_WHITE, r);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, a, V(a.x, b.y), top, bot);
    dl->AddRect(a, b, hov ? GoldLight() : primary ? Gold(0.95f) : Gold(0.6f), r, 0, std::max(1.f, (hov ? 1.8f : 1.3f) * u));
    dl->AddRect(V(a.x + 3 * u, a.y + 3 * u), V(b.x - 3 * u, b.y - 3 * u), Silver(hov ? 0.4f : 0.18f), r * 0.6f, 0, std::max(1.f, 0.7f * u));
    // small pointed clasps at the ends, as the launcher's plate buttons
    const float my = (a.y + b.y) * 0.5f, cl = 4 * u;
    dl->AddTriangleFilled(V(a.x - cl, my), V(a.x + 2 * u, my - cl), V(a.x + 2 * u, my + cl), hov ? GoldLight() : Gold(0.85f));
    dl->AddTriangleFilled(V(b.x + cl, my), V(b.x - 2 * u, my + cl), V(b.x - 2 * u, my - cl), hov ? GoldLight() : Gold(0.85f));
    const float fs = 29 * u;
    const char* end = ImGui::FindRenderedTextEnd(label);
    std::string text(label, end);
    ImVec2 ts = L.fonts.head->CalcTextSizeA(fs, FLT_MAX, 0, text.c_str());
    TextAt(dl, L.fonts.head, fs, V(a.x + (size.x - ts.x) * 0.5f, a.y + (size.y - ts.y) * 0.5f + (held ? u : 0)),
           primary ? Rgb(252, 240, 210) : hov ? Text() : Dim(), text.c_str());
    return pressed;
}

bool Toggle(const Look& L, const char* id, bool* v, ImVec2 size) {
    const float u = L.u, th = std::min(size.y, 38 * u), tw = th * 2.0f;
    const char* word = *v ? "On" : "Off";
    const float ww = L.fonts.body->CalcTextSizeA(34 * u, FLT_MAX, 0, "Off").x;
    const float w = std::min(size.x, tw + 14 * u + ww + 10 * u);
    const ImVec2 c = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(V(c.x + size.x - w, c.y));
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, V(w, size.y), ImGuiButtonFlags_EnableNav);
    if (clicked) *v = !*v;
    const bool hov = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && GImGui->NavCursorVisible);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 t0(a.x + w - tw, a.y + (size.y - th) * 0.5f), t1(t0.x + tw, t0.y + th);
    // the track: fully round ends, shaded top to bottom (the shading on the round shape itself: a square gradient under
    // it showed its corners outside the curve - Shagg, 12 Nov)
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(t0, t1, IM_COL32_WHITE, th * 0.5f);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, t0, V(t0.x, t1.y),
                                                  *v ? Rgb(186, 144, 66) : Rgb(26, 22, 17), *v ? Rgb(122, 90, 38) : Rgb(14, 12, 9));
    dl->AddRect(t0, t1, *v ? GoldLight() : hov ? Gold(0.9f) : Gold(0.5f), th * 0.5f, 0, std::max(1.f, 1.2f * u));
    const float r = th * 0.5f - 4 * u;
    const ImVec2 kc(*v ? t1.x - th * 0.5f : t0.x + th * 0.5f, (t0.y + t1.y) * 0.5f);
    dl->AddCircleFilled(V(kc.x + u, kc.y + 1.5f * u), r, IM_COL32(0, 0, 0, 110), 24);
    dl->AddCircleFilled(kc, r, *v ? Rgb(250, 240, 214) : Rgb(130, 120, 104), 24);
    const float fs = 34 * u;
    ImVec2 ws = L.fonts.body->CalcTextSizeA(fs, FLT_MAX, 0, word);
    TextAt(dl, L.fonts.body, fs, V(t0.x - 12 * u - ws.x, a.y + (size.y - ws.y) * 0.5f), *v ? Text() : Dim(), word);
    return clicked;
}

void Hints(ImDrawList* dl, const Look& L, const std::vector<Hint>& hints) {
    const float u = L.u, gh = 38 * u, y = L.display.y - 36 * u - gh * 0.5f, gap = 40 * u, fs = 30 * u;
    float total = 0;
    for (const Hint& h : hints)
        total += Glyph(dl, L, V(0, 0), gh, h.glyph, false) + 10 * u + L.fonts.body->CalcTextSizeA(fs, FLT_MAX, 0, h.label.c_str()).x + gap;
    total -= gap;
    float x = std::max(L.c0.x + 64 * u, (L.c0.x + L.c1.x - total) * 0.5f);
    for (const Hint& h : hints) {
        x += Glyph(dl, L, V(x, y), gh, h.glyph) + 10 * u;
        ImVec2 s = L.fonts.body->CalcTextSizeA(fs, FLT_MAX, 0, h.label.c_str());
        TextAt(dl, L.fonts.body, fs, V(x, y - s.y * 0.55f), Dim(), h.label.c_str());
        x += s.x + gap;
    }
}

}  // namespace app::look
