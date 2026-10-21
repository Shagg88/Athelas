// Subtitles: a line of text at the bottom of the screen, drawn by the overlay over the game. A game's translations
// use it (the line the game shows, in the player's language): Subtitle() from any thread, DrawSubtitles() in the
// renderer's ImGui frame. A line shows for its time; a newer one replaces it. Lines asked for again and again (a
// screen that looks its text up every frame) keep it showing.
#include <algorithm>
#include <mutex>
#include <string>

#include "app/app.h"
#include "imgui.h"

namespace app {
namespace {
std::mutex g_lock;
std::string g_text;
double g_until = 0, g_since = 0;  // ImGui time; g_until 0: not drawn yet (the time is set at the first draw)
double g_seconds = 0;
}  // namespace

void Subtitle(const std::string& utf8, double seconds) {
    std::lock_guard lock(g_lock);
    if (utf8 == g_text && g_until > 0) {  // the same again: kept showing, without fading in anew
        g_seconds = seconds;
        g_until = -1;  // the next draw sets its end from now
        return;
    }
    g_text = utf8;
    g_seconds = seconds;
    g_until = 0;
}

void ClearSubtitle() {
    std::lock_guard lock(g_lock);
    g_text.clear();
}

void DrawSubtitles(float scale) {
    std::lock_guard lock(g_lock);
    if (g_text.empty()) return;
    double now = ImGui::GetTime();
    if (g_until == 0) {
        g_since = now;
        g_until = now + g_seconds;
    } else if (g_until < 0) {
        g_until = now + g_seconds;
    }
    if (now > g_until) {
        g_text.clear();
        return;
    }
    float alpha = float(std::min({1.0, (now - g_since) / 0.2, (g_until - now) / 0.4}));
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float pad = std::max(12.0f, vp->Size.y / 22.0f);
    float width = vp->Size.x * 0.8f;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x / 2, vp->Pos.y + vp->Size.y - pad), ImGuiCond_Always,
                            ImVec2(0.5f, 1));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(width, vp->Size.y / 3));
    ImGui::SetNextWindowBgAlpha(0.6f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##subtitle", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::SetWindowFontScale(scale * 1.15f);
    float wrap = width - ImGui::GetStyle().WindowPadding.x * 2;
    float textw = ImGui::CalcTextSize(g_text.c_str()).x;
    if (textw > wrap) ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(g_text.c_str());
    if (textw > wrap) ImGui::PopTextWrapPos();
    ImGui::End();
    ImGui::PopStyleVar(2);
}

}  // namespace app
