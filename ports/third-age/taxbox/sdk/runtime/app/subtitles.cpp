// Subtitles: a line of text at the bottom of the screen, drawn by the overlay over the game. A game's translations
// use it (the line the game shows, in the player's language): Subtitle() from any thread, DrawSubtitles() in the
// renderer's ImGui frame. A line shows for its time; a newer one replaces it. Lines asked for again and again (a
// screen that looks its text up every frame) keep it showing.
//
// Movie subtitles: the games have none for their film clips, so [graphics] movie_subtitles reads them from files,
// subtitles\<movie name>.srt beside the exe (SubRip: a number, "00:01:02,500 --> 00:01:05,000", the lines, a blank
// line; UTF-8). A game says which movie starts (MovieSubtitles); the clock starts at its first frame (gpu::g_movie),
// stops while the game is paused (the in-game menu) and the subtitles end a second after its last frame.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
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

namespace {
struct Cue {
    double from, to;
    std::string text;
};
std::mutex g_movie_lock;
std::vector<Cue> g_cues;  // the movie starting's, empty for none
bool g_running = false;   // its frames have started
double g_elapsed = 0, g_last = 0, g_off_since = 0;

std::string Lower(std::string t) {
    for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
    return t;
}

// "00:01:02,500" (or with a '.') in seconds; < 0 when it isn't one
double Time(const std::string& t) {
    int h = 0, m = 0, sec = 0, ms = 0;
    char sep = 0;
    if (std::sscanf(t.c_str(), " %d:%d:%d%c%d", &h, &m, &sec, &sep, &ms) < 3) return -1;
    return h * 3600.0 + m * 60.0 + sec + ms / 1000.0;
}

std::vector<Cue> ReadSrt(const std::filesystem::path& file) {
    std::vector<Cue> cues;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    Cue cur{-1, -1, {}};
    auto done = [&] {
        if (cur.from >= 0 && !cur.text.empty()) cues.push_back(cur);
        cur = {-1, -1, {}};
    };
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);  // a BOM
        first = false;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t arrow = line.find("-->");
        if (arrow != std::string::npos) {
            done();
            cur.from = Time(line.substr(0, arrow));
            cur.to = Time(line.substr(arrow + 3));
        } else if (line.empty()) {
            done();
        } else if (cur.from >= 0) {
            // simple tags (<i>, </i>, <b>, {\an8}) left out
            std::string t;
            for (size_t i = 0; i < line.size(); i++) {
                char c = line[i];
                size_t close = c == '<' ? line.find('>', i) : c == '{' ? line.find('}', i) : std::string::npos;
                if (close != std::string::npos && close - i <= 8) {
                    i = close;
                    continue;
                }
                t += c;
            }
            if (!cur.text.empty()) cur.text += "\n";
            cur.text += t;
        }
    }
    done();
    return cues;
}

// the movie's subtitle line now, through Subtitle()
void MovieTick() {
    std::string text;
    {
        std::lock_guard lock(g_movie_lock);
        if (g_cues.empty()) return;
        double now = ImGui::GetTime();
        if (!g_running) {
            if (!gpu::g_movie) return;
            g_running = true;
            g_elapsed = 0;
            g_last = now;
            g_off_since = 0;
        }
        if (!Paused()) g_elapsed += now - g_last;
        g_last = now;
        if (gpu::g_movie) g_off_since = 0;
        else if (g_off_since == 0) g_off_since = now;
        else if (now - g_off_since > 1.0) {  // the movie is over
            g_cues.clear();
            g_running = false;
            return;
        }
        for (const Cue& c : g_cues)
            if (g_elapsed >= c.from && g_elapsed < c.to) text = c.text;
    }
    if (!text.empty()) Subtitle(text, 0.15);
}
}  // namespace

void MovieSubtitles(const std::string& name) {
    std::vector<Cue> cues;
    if (Get().movie_subtitles && !name.empty()) {
        std::error_code ec;
        std::filesystem::path dir = ExeDir() / "subtitles";
        std::string want = Lower(name) + ".srt";
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (Lower(it->path().filename().string()) == want) {
                cues = ReadSrt(it->path());
                LOGR("subtitles: %zu lines for the movie %s (%s)", cues.size(), name.c_str(), it->path().string().c_str());
                break;
            }
        if (cues.empty()) LOGR("subtitles: none for the movie %s (subtitles\\%s)", name.c_str(), want.c_str());
    }
    std::lock_guard lock(g_movie_lock);
    g_cues = std::move(cues);
    g_running = false;
}

void DrawSubtitles(float scale) {
    MovieTick();
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
