// Athelas - the launcher's look and its shared widgets: colours, fonts and scale, the settings rows, tabs, status dots.
#include "app.h"

// ---------------------------------------------------------------------------
// Theme

ImFont *F_BODY, *F_SMALL, *F_HEAD, *F_TITLE, *F_PLAY, *F_TAB;

float g_scale = 1.0f;

float g_scale0 = 1.0f;      // g_scale at the window's first size; the window can be resized (since 1.7)

int g_baseW = 880, g_baseH = 640;   // that first size, in pixels

void setup_style()
{
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 0; st.ChildRounding = 0; st.FrameRounding = S(2); st.PopupRounding = S(2);
    st.GrabRounding = S(2); st.TabRounding = 0; st.ScrollbarRounding = S(2);
    st.WindowBorderSize = 0; st.ChildBorderSize = 1; st.FrameBorderSize = 1; st.PopupBorderSize = 1;
    st.FramePadding = ImVec2(S(10), S(5)); st.ItemSpacing = ImVec2(S(10), S(8)); st.ItemInnerSpacing = ImVec2(S(8), S(6));
    st.WindowPadding = ImVec2(S(18), S(14)); st.GrabMinSize = S(14); st.ScrollbarSize = S(12);
    st.DisabledAlpha = 0.5f;
    ImVec4 *c = st.Colors;
    c[ImGuiCol_Text] = C_TEXT;                    c[ImGuiCol_TextDisabled] = C_DIM;
    c[ImGuiCol_WindowBg] = rgb(0, 0, 0, 0);       c[ImGuiCol_ChildBg] = rgb(16, 12, 9, 0.80f);
    c[ImGuiCol_PopupBg] = rgb(24, 19, 14, 0.98f); c[ImGuiCol_Border] = rgb(150, 118, 60, 0.55f);
    c[ImGuiCol_FrameBg] = rgb(34, 27, 20);        c[ImGuiCol_FrameBgHovered] = rgb(48, 38, 27);
    c[ImGuiCol_FrameBgActive] = rgb(58, 46, 32);
    c[ImGuiCol_Button] = rgb(44, 35, 25);         c[ImGuiCol_ButtonHovered] = rgb(70, 55, 34);
    c[ImGuiCol_ButtonActive] = rgb(92, 72, 40);
    c[ImGuiCol_Header] = rgb(70, 55, 34);         c[ImGuiCol_HeaderHovered] = rgb(92, 72, 40);
    c[ImGuiCol_HeaderActive] = rgb(110, 86, 46);
    c[ImGuiCol_CheckMark] = C_GOLD_H;             c[ImGuiCol_SliderGrab] = C_GOLD; c[ImGuiCol_SliderGrabActive] = C_GOLD_H;
    c[ImGuiCol_Tab] = rgb(30, 24, 18);            c[ImGuiCol_TabHovered] = rgb(80, 62, 36);
    c[ImGuiCol_TabSelected] = rgb(64, 49, 29);    c[ImGuiCol_TabSelectedOverline] = C_GOLD;
    c[ImGuiCol_TabDimmed] = rgb(30, 24, 18);      c[ImGuiCol_TabDimmedSelected] = rgb(64, 49, 29);
    c[ImGuiCol_Separator] = rgb(150, 118, 60, 0.45f);
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0.2f); c[ImGuiCol_ScrollbarGrab] = rgb(90, 72, 44);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(120, 96, 56); c[ImGuiCol_ScrollbarGrabActive] = C_GOLD;
    c[ImGuiCol_TableRowBg] = rgb(0, 0, 0, 0);     c[ImGuiCol_TableRowBgAlt] = rgb(255, 230, 180, 0.04f);
    c[ImGuiCol_NavCursor] = C_GOLD_H;             c[ImGuiCol_TextSelectedBg] = rgb(110, 86, 46);
    c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.55f);
    c[ImGuiCol_CheckboxSelectedBg] = rgb(34, 27, 20);   c[ImGuiCol_InputTextCursor] = C_TEXT;
    c[ImGuiCol_TextLink] = C_GOLD_H;                    c[ImGuiCol_TabDimmedSelectedOverline] = C_GOLD;
    c[ImGuiCol_SeparatorHovered] = C_GOLD;              c[ImGuiCol_SeparatorActive] = C_GOLD_H;
    c[ImGuiCol_ResizeGrip] = rgb(0, 0, 0, 0);           c[ImGuiCol_ResizeGripHovered] = rgb(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripActive] = rgb(0, 0, 0, 0);     c[ImGuiCol_DragDropTarget] = C_GOLD_H;
}

// ---------------------------------------------------------------------------
// Widgets

float g_labelW;

static const char *g_rowDesc;

// One setting: label on the left, control on the right; the description shows
// as a tooltip when the mouse is over the row.
void row_begin(const char *label, const char *desc)
{
    g_rowDesc = desc;
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (desc && *desc) {
        ImGui::SameLine(0, S(6));
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(rgb(150, 118, 60), "(?)");
        ImGui::PopFont();
    }
    ImGui::SameLine(g_labelW);
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
}

// The controller's (or the keyboard's) cursor is on something inside the item just drawn (a row's group)
static bool nav_cursor_in_last_item()
{
    ImGuiContext &g = *GImGui;
    if (!g.NavCursorVisible || !g.NavWindow || !g.NavId || g.NavWindow != ImGui::GetCurrentWindow()) return false;
    ImRect r = ImGui::WindowRectRelToAbs(g.NavWindow, g.NavWindow->NavRectRel[g.NavLayer]);
    return g.LastItemData.Rect.Contains(r.GetCenter());
}

void row_end()
{
    ImGui::EndGroup();
    ImGui::EndGroup();
    // the description: with the mouse over the row, or the controller's cursor on its control (no menu open)
    bool navHere = nav_cursor_in_last_item() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (g_rowDesc && *g_rowDesc && (navHere || ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled |
                                                                    ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))) {
        ImGui::BeginTooltip();
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::PushTextWrapPos(S(380));
        ImGui::TextUnformatted(g_rowDesc);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndTooltip();
    }
}

// A heading: the title, then a tapering rule ending in a leaf scroll (ornament.cpp)
void section(const char *title)
{
    ImGui::Spacing();
    float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
    ImGui::TextColored(C_GOLD, "%s", title);
    ImGui::PopFont();
    ImVec2 t0 = ImGui::GetItemRectMin(), t1 = ImGui::GetItemRectMax();
    orn_elf_rule(ImGui::GetWindowDrawList(), t1.x + S(16), right - S(4), (t0.y + t1.y) * 0.5f + S(1));
    ImGui::Dummy(ImVec2(0, S(2)));
}

// A choice between a few named options, shown as a drop-down menu.
// Options marked disabled (like "Custom") are shown when current but can't be picked.
bool segmented(const char *id, int *v, const char *const *names, int n, const bool *enabled)
{
    bool changed = false;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
    if (ImGui::BeginCombo("##c", (*v >= 0 && *v < n) ? names[*v] : "")) {
        for (int i = 0; i < n; i++) {
            if (enabled && !enabled[i]) continue;
            if (ImGui::Selectable(names[i], *v == i) && *v != i) { *v = i; changed = true; }
            if (*v == i) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

// In a dialog: Escape, or B on a controller, closes it as its Cancel button would (ImGui's modal dialogs don't)
bool dialog_back()
{
    return ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
}

bool gold_button(const char *label, ImVec2 size, ImFont *font)
{
    return orn_arch_button(label, size, font, true);
}

void set_message(App &a, const std::string &m, bool bad) { a.message = m; a.messageBad = bad; a.messageAt = ImGui::GetTime(); }

void status_dot(bool ok, bool warn, const char *label, const char *tip)
{
    ImVec4 col = ok ? C_GOOD : warn ? C_WARN : C_BAD;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImGui::BeginGroup();
    dl->AddCircleFilled(ImVec2(p.x + S(5), p.y + h * 0.55f), S(4), ImGui::GetColorU32(col));
    ImGui::SetCursorScreenPos(ImVec2(p.x + S(15), p.y));
    ImGui::TextUnformatted(label);
    ImGui::EndGroup();
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(S(380));
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// With a controller, LB and RB step to the previous and next tab (+1 / -1 here, 0 for none), unless a control is
// being changed (they speed it up or slow it down then), a menu is open or a button is being set
static int g_padTabFocus;   // frames left to put the controller's cursor on the new tab's first control

int pad_tab_step(const App &a)
{
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) || ImGui::IsAnyItemActive() || a.capture >= 0 ||
        a.kbCapture >= 0 || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return 0;
    int step = (int)ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) - (int)ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false);
    if (step) g_padTabFocus = 2;
    return step;
}

// Just after the open tab's BeginTabItem: after LB / RB, the cursor goes to the new tab itself (Down enters its page)
void pad_tab_focus()
{
    if (g_padTabFocus > 0 && --g_padTabFocus == 0) { ImGui::FocusItem(); ImGui::SetNavCursorVisible(true); }
}

// Tabs spread evenly over the width, each in a carved frame (orn_tab) with its name centred.
// ImGui's own tabs stay underneath, invisible, and only handle the clicks; 'sel' is the tab
// chosen last frame. Call before BeginTabBar, with the tab font pushed; pair with tabs_end().
float tabs_begin(const char *const *labels, int n, int sel)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float gap = S(8), h = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    float w = floorf((avail - gap * (n - 1)) / n);
    for (int i = 0; i < n; i++) {
        ImVec2 a(p.x + i * (w + gap), p.y + S(4)), b(a.x + w, p.y + h);
        bool hov = ImGui::IsMouseHoveringRect(a, b);
        orn_tab(dl, a, b, i == sel, hov);
        ImVec2 ts = ImGui::CalcTextSize(tr(labels[i]));
        dl->AddText(ImVec2(floorf(a.x + (w - ts.x) * 0.5f), floorf(a.y + (b.y - a.y - ts.y) * 0.5f)),
                    ImGui::GetColorU32(i == sel ? C_GOLD_H : hov ? C_TEXT : rgb(200, 186, 160)), tr(labels[i]));
    }
    // the line the tabs stand on, open under the chosen one
    float y = p.y + h, sx0 = p.x + sel * (w + gap), sx1 = sx0 + w;
    ImU32 line = ImGui::GetColorU32(rgb(184, 144, 78, 0.55f));
    if (sx0 > p.x) dl->AddLine(ImVec2(p.x, y), ImVec2(sx0, y), line, S(1.2f));
    if (sx1 < p.x + avail) dl->AddLine(ImVec2(sx1, y), ImVec2(p.x + avail, y), line, S(1.2f));
    // ImGui's tabs: the same places, nothing drawn
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(gap, ImGui::GetStyle().ItemInnerSpacing.y));
    ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabBarOverlineSize, 0.0f);
    for (ImGuiCol c : { ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabSelected, ImGuiCol_TabSelectedOverline,
                        ImGuiCol_TabDimmed, ImGuiCol_TabDimmedSelected, ImGuiCol_TabDimmedSelectedOverline })
        ImGui::PushStyleColor(c, ImVec4(0, 0, 0, 0));
    return w;
}

void tabs_end() { ImGui::PopStyleColor(7); ImGui::PopStyleVar(3); }
