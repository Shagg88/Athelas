// Athelas - the Xbox ports' pages (tt_*): their tabs, drawn from the settings each port lists.
#include "app.h"

static std::string cap_first(std::string s) { if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] -= 32; return s; }

static const char *tt_section_title(const std::string &s)
{
    if (s == "display") return "Screen";
    if (s == "graphics") return "Graphics";
    if (s == "textures") return "Texture packs";
    if (s == "network") return "Network";
    return nullptr;
}

// How a choice is shown in its menu
static std::string tt_choice_name(const PortSetting &ps, const std::string &c)
{
    // the port's own display names, when it gives them
    for (size_t i = 0; i < ps.choiceLabels.size(); i++) if (ps.choices[i] == c) return ps.choiceLabels[i];
    // otherwise, for older ports:
    if (ps.key == "anisotropic_filtering") return c == "0" ? "Off" : c + "x";
    if (ps.key == "fps") return c + " fps";
    if (ps.key == "msaa" || ps.key == "anti_aliasing") return c == "0" ? "Off" : c + "x";
    return cap_first(c);
}

// Screen size: one menu with the main screen's sizes (width and height are two settings in the file)
// The screen The Two Towers is set to use ("main", or "DISPLAY2" and so on)
static const Monitor &tt_monitor(const App &a)
{
    std::string scr = "main";
    if (const PortSetting *ps = tt_setting(a, "display", "screen")) scr = toml_unquote(tt_val(a, *ps));
    if (_stricmp(scr.c_str(), "main"))
        for (const Monitor &m : a.monitors)
            if (m.device.size() > 4 && !_stricmp(m.device.c_str() + 4, scr.c_str())) return m;
    for (const Monitor &m : a.monitors) if (m.primary) return m;
    return a.monitors[0];
}

// How a screen choice is shown: the monitor's own name, as on ROTK's Display tab
static std::string tt_screen_name(const App &a, const std::string &c)
{
    for (const Monitor &m : a.monitors) {
        bool match = !_stricmp(c.c_str(), "main") ? m.primary : m.device.size() > 4 && !_stricmp(m.device.c_str() + 4, c.c_str());
        if (!match) continue;
        char b[160];
        snprintf(b, sizeof b, "%s%s (%d x %d)", m.name.c_str(), m.primary ? ", main screen" : "", m.deskW, m.deskH);
        return b;
    }
    return !_stricmp(c.c_str(), "main") ? "Main screen" : c;
}

static void tt_resolution_row(App &a, const PortSetting &pw, const PortSetting &ph)
{
    int w = atoi(tt_val(a, pw).c_str()), h = atoi(tt_val(a, ph).c_str());
    const Monitor &m = tt_monitor(a);
    char cur[64];
    if (w <= 0 || h <= 0) snprintf(cur, sizeof cur, "Desktop (%d x %d)", m.deskW, m.deskH);
    else snprintf(cur, sizeof cur, "%d x %d", w, h);
    row_begin("Resolution", "The size of the picture on screen. Desktop uses the screen's own size.");
    if (ImGui::BeginCombo("##ttres", cur)) {
        char d[64]; snprintf(d, sizeof d, "Desktop (%d x %d)", m.deskW, m.deskH);
        if (ImGui::Selectable(d, w <= 0)) { a.ttFile.set(pw.section, pw.key, "0"); a.ttFile.set(ph.section, ph.key, "0"); }
        for (auto it = m.modes.rbegin(); it != m.modes.rend(); ++it) {
            if (it->w < 640 || it->h < 480) continue;
            char l[64]; snprintf(l, sizeof l, "%d x %d", it->w, it->h);
            if (ImGui::Selectable(l, it->w == w && it->h == h)) {
                a.ttFile.set(pw.section, pw.key, std::to_string(it->w));
                a.ttFile.set(ph.section, ph.key, std::to_string(it->h));
            }
        }
        ImGui::EndCombo();
    }
    row_end();
}

// Refresh rate: the rates the screen offers at the chosen size; only used in fullscreen
static void tt_refresh_row(App &a, const PortSetting &pr)
{
    const Monitor &m = tt_monitor(a);
    int w = 0, h = 0;
    if (const PortSetting *ps = tt_setting(a, "display", "width")) w = atoi(tt_val(a, *ps).c_str());
    if (const PortSetting *ps = tt_setting(a, "display", "height")) h = atoi(tt_val(a, *ps).c_str());
    if (w <= 0 || h <= 0) { w = m.deskW; h = m.deskH; }
    std::vector<int> hz;
    if (const Mode *md = m.find(w, h)) hz = md->hz;
    if (hz.empty()) hz.push_back(m.deskHz);
    int r = atoi(tt_val(a, pr).c_str());
    bool full = true;
    if (const PortSetting *pm = tt_setting(a, "display", "mode")) full = toml_unquote(tt_val(a, *pm)) == "fullscreen";
    char cur[48];
    if (r <= 0) snprintf(cur, sizeof cur, "Highest (%d Hz)", hz.back());
    else snprintf(cur, sizeof cur, "%d Hz", r);
    ImGui::BeginDisabled(!full);
    row_begin(pr.label.c_str(), full ? pr.help.c_str() : "Only used in fullscreen. Borderless and windowed use the desktop's refresh rate.");
    if (ImGui::BeginCombo("##tthz", cur)) {
        char b[48]; snprintf(b, sizeof b, "Highest (%d Hz)", hz.back());
        if (ImGui::Selectable(b, r <= 0)) a.ttFile.set(pr.section, pr.key, "0");
        for (auto it = hz.rbegin(); it != hz.rend(); ++it) {
            snprintf(b, sizeof b, "%d Hz", *it);
            if (ImGui::Selectable(b, *it == r)) a.ttFile.set(pr.section, pr.key, std::to_string(*it));
        }
        ImGui::EndCombo();
    }
    row_end();
    ImGui::EndDisabled();
}

static void tt_setting_row(App &a, const PortSetting &ps)
{
    std::string v = tt_val(a, ps);
    ImGui::PushID((ps.section + "." + ps.key).c_str());
    row_begin(ps.label.c_str(), ps.help.c_str());
    if (ps.type == "bool") {
        bool b = v == "true";
        if (ImGui::Checkbox("##b", &b)) a.ttFile.set(ps.section, ps.key, b ? "true" : "false");
    } else if (ps.type == "choice") {
        std::string cur = toml_unquote(v);
        // an older file's flag for what is now a choice (frame_generation = true): the second choice, as the game reads it
        if ((cur == "true" || cur == "false") && ps.choices.size() >= 2) cur = ps.choices[cur == "true" ? 1 : 0];
        int idx = -1;
        std::vector<std::string> names;
        for (size_t i = 0; i < ps.choices.size(); i++) {
            names.push_back(ps.section == "display" && ps.key == "screen" ? tt_screen_name(a, ps.choices[i]) : tt_choice_name(ps, ps.choices[i]));
            if (ps.choices[i] == cur) idx = (int)i;
        }
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
        if (ImGui::BeginCombo("##c", idx >= 0 ? names[idx].c_str() : cur.c_str())) {
            bool screen = ps.section == "display" && ps.key == "screen";
            for (size_t i = 0; i < names.size(); i++) {
                // the main screen also listed by its DISPLAY number: shown once, as "main"
                if (screen && (int)i != idx && _stricmp(ps.choices[i].c_str(), "main")) {
                    bool isPrimary = false;
                    for (const Monitor &m : a.monitors)
                        if (m.primary && m.device.size() > 4 && !_stricmp(m.device.c_str() + 4, ps.choices[i].c_str())) isPrimary = true;
                    if (isPrimary) continue;
                }
                if (ImGui::Selectable(names[i].c_str(), (int)i == idx)) a.ttFile.set(ps.section, ps.key, toml_quote(ps.choices[i]));
            }
            ImGui::EndCombo();
        }
    } else if (ps.type == "int") {
        int n = atoi(v.c_str()), lo = (int)ps.min, hi = (int)ps.max;
        bool changed;
        if (hi > lo && hi - lo <= 400) changed = ImGui::SliderInt("##i", &n, lo, hi, ps.key == "fov" || ps.key == "render_percent" ? "%d%%" : "%d");
        else changed = ImGui::InputInt("##i", &n, 0, 0);
        if (changed) { if (hi > lo) n = std::max(lo, std::min(hi, n)); a.ttFile.set(ps.section, ps.key, std::to_string(n)); }
    } else if (ps.type == "float") {
        float f = (float)atof(v.c_str());
        bool changed = ps.max > ps.min ? ImGui::SliderFloat("##f", &f, (float)ps.min, (float)ps.max, "%.2f") : ImGui::InputFloat("##f", &f);
        if (changed) { char b[32]; snprintf(b, sizeof b, "%.2f", f); a.ttFile.set(ps.section, ps.key, b); }
    } else {
        char b[256]; snprintf(b, sizeof b, "%s", toml_unquote(v).c_str());
        if (ImGui::InputText("##s", b, sizeof b)) a.ttFile.set(ps.section, ps.key, toml_quote(b));
    }
    row_end();
    ImGui::PopID();
}

static std::wstring tt_widen(const std::string &s) { return std::wstring(s.begin(), s.end()); }   // ASCII names

static void tt_locate_button(App &a)
{
    std::string label = std::string("Locate ") + tp().exe + "...";
    if (ImGui::Button(label.c_str())) {
        std::string f;
        std::wstring exe = tt_widen(tp().exe), title = L"Choose " + exe, type = tt_widen(tp().game) + L" port (" + exe + L")";
        if (pick_file(f, title.c_str(), type.c_str(), exe.c_str())) {
            SHCreateDirectoryExA(NULL, (exe_dir() + "\\ROTKFix").c_str(), NULL);
            WritePrivateProfileStringA("Ports", tp().iniKey, f.c_str(), ports_ini().c_str());
            tt_find(a);
        }
    }
}

void draw_tt(App &a, const LibGame &g)
{
    tp_use(a, tp_of(g.kind));
    if (!a.ttLooked) tt_find(a);
    ImGui::PushTextWrapPos(0);
    if (a.tt.exe.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(C_WARN, "%s port (%s) wasn't found.", tp().game, tp().exe);
        ImGui::Text("It is normally in the %s folder next to Athelas.exe. If you have it somewhere else, show the launcher where.", tp().dir);
        ImGui::PopTextWrapPos();
        tt_locate_button(a);
        return;
    }
    if (!tt_ready(a)) {
        ImGui::Spacing();
        ImGui::TextColored(C_BAD, "%s", a.tt.err.c_str());
        ImGui::TextColored(C_DIM, "%s", a.tt.exe.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Try again")) tt_find(a);
        ImGui::SameLine();
        tt_locate_button(a);
        return;
    }
    ImGui::PopTextWrapPos();
    if (!a.ttLoaded) tt_load_settings(a);

    tt_tabs(a, g);
}

// Which tab a section goes on (as ROTK's: Display, Graphics, Controls, Audio)
static int tt_tab_of(const std::string &sec)
{
    if (sec == "display") return 0;
    if (sec == "controls" || sec == "input") return 2;
    if (sec == "audio" || sec == "sound") return 3;
    if (sec == "network") return 4;
    if (sec == "gameplay" || sec == "cheats" || sec == "achievements" || sec == "discord" || sec == "language") return 5;
    return 1;   // graphics, textures and anything else
}

static bool tt_value_equal(const std::string &x, const std::string &y)
{
    if (x == y) return true;
    char *e1, *e2;
    double a = strtod(x.c_str(), &e1), b = strtod(y.c_str(), &e2);
    return *e1 == 0 && *e2 == 0 && e1 != x.c_str() && e2 != y.c_str() && fabs(a - b) < 1e-6;
}

// "Recommended for this PC": the button, and the box that says what the PC has and what will change before it does.
// changes: what the box lists; apply() makes them; it returns true once applied (for Undo).
bool rec_button(const char *id, const std::vector<std::string> &changes, bool canUndo, bool *undo)
{
    bool applied = false;
    const HwInfo &h = hw_info();
    // (a std::string: a long translation in a fixed buffer would cut off the "##rec" ID, or a character in two)
    std::string btn = std::string(tr("Recommended for this PC")) + "##rec" + id;
    row_begin("", "Looks at this PC's graphics card, video memory, screen and processor, and picks the settings that "
                  "should run the game smoothly. You see what changes before anything does.");
    static bool testOpen = getenv("ATHELAS_REC_OPEN") != nullptr;   // testing: the box open at once
    if (ImGui::Button(btn.c_str()) || testOpen) { testOpen = false; ImGui::OpenPopup((std::string("recpop") + id).c_str()); }
    if (canUndo) {
        ImGui::SameLine();
        if (ImGui::Button((std::string(tr("Undo")) + "##recundo" + id).c_str())) *undo = true;
    }
    if (ImGui::BeginPopup((std::string("recpop") + id).c_str())) {
        ImGui::TextColored(C_GOLD_H, "%s", tr("This PC"));
        ImGui::Text("%s%s, %u MB", h.gpu.empty() ? tr("Unknown graphics card") : h.gpu.c_str(),
                    h.integrated ? tr(" (built-in)") : "", h.vramMB);
        ImGui::Text(tr("Screen %dx%d at %d Hz, %d processor threads"), h.screenW, h.screenH, h.refresh, h.cores);
        ImGui::Text(tr("Rated: %s"), tr(hw_tier_name(h.tier)));
        ImGui::Separator();
        if (changes.empty()) ImGui::TextUnformatted(tr("Your settings already match the recommendation."));
        else {
            ImGui::TextColored(C_GOLD_H, "%s", tr("Will change"));
            for (auto &c : changes) ImGui::BulletText("%s", c.c_str());
            if (ImGui::Button(tr("Apply"))) { applied = true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
        }
        if (ImGui::Button(tr("Close"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    row_end();
    return applied;
}

// the Xbox ports: a preset by the tier (Ultra's 150% render only where the card has room for it), and the frame
// rate the screen can show, up to what the tier keeps steady
static void tt_recommend_row(App &a)
{
    const HwInfo &h = hw_info();
    static std::vector<std::pair<std::string, std::string>> before[4];   // per port: "section.key" -> value, for Undo
    std::string want = h.tier == 0 ? "Balanced" : h.tier == 3 && (h.screenW <= 2560 || h.vramMB >= 12000) ? "Ultra" : "High";
    const Port::Preset *pr = nullptr;
    for (auto &p : a.tt.presets) if (p.name == want) pr = &p;
    std::vector<std::pair<std::string, std::string>> set;   // "section.key" -> value
    if (pr)
        for (auto &kv : pr->values)
            if (kv.first != "graphics.fps") set.push_back(kv);   // the frame rate: this PC's, below
    if (const PortSetting *pf = tt_setting(a, "graphics", "fps")) {
        int cap = h.tier == 0 ? 60 : h.tier == 1 ? 120 : h.tier == 2 ? 144 : 240;
        cap = std::min(cap, h.refresh >= 50 ? h.refresh : 60);
        std::string best;
        int bestR = 0;
        for (auto &ch : pf->choices) {
            int r = atoi(ch.c_str() + (ch[0] == '"'));
            if (r > bestR && r <= cap) { bestR = r; best = ch; }
        }
        if (!best.empty()) set.push_back({ "graphics.fps", best[0] == '"' ? best : "\"" + best + "\"" });
    }
    std::vector<std::string> changes;
    std::vector<std::pair<std::string, std::string>> diff;
    for (auto &kv : set) {
        size_t dot = kv.first.find('.');
        const PortSetting *ps = tt_setting(a, kv.first.substr(0, dot).c_str(), kv.first.substr(dot + 1).c_str());
        if (!ps) continue;
        std::string cur = tt_val(a, *ps);
        if (tt_value_equal(cur, kv.second)) continue;
        diff.push_back(kv);
        std::string v = kv.second;
        v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
        changes.push_back(std::string(tr(ps->label.c_str())) + ": " + v);
    }
    if (pr && !diff.empty()) changes.insert(changes.begin(), std::string(tr("Preset")) + ": " + tr(want.c_str()));
    bool undo = false;
    if (rec_button("tt", changes, !before[a.ttWho].empty(), &undo)) {
        before[a.ttWho].clear();
        for (auto &kv : diff) {   // every old value first, then the new ones
            size_t dot = kv.first.find('.');
            const PortSetting *ps = tt_setting(a, kv.first.substr(0, dot).c_str(), kv.first.substr(dot + 1).c_str());
            before[a.ttWho].push_back({ kv.first, ps ? tt_val(a, *ps) : kv.second });
        }
        for (auto &kv : diff) {
            size_t dot = kv.first.find('.');
            a.ttFile.set(kv.first.substr(0, dot), kv.first.substr(dot + 1), kv.second);
        }
    }
    if (undo) {
        for (auto &kv : before[a.ttWho]) {
            size_t dot = kv.first.find('.');
            a.ttFile.set(kv.first.substr(0, dot), kv.first.substr(dot + 1), kv.second);
        }
        before[a.ttWho].clear();
    }
}

// Preset menu: the port's presets, or "Custom" when the settings match none
static void tt_preset_row(App &a)
{
    if (a.tt.presets.empty()) return;
    int cur = -1;
    for (size_t i = 0; i < a.tt.presets.size() && cur < 0; i++) {
        bool all = true;
        for (auto &kv : a.tt.presets[i].values) {
            size_t dot = kv.first.find('.');
            const PortSetting *ps = tt_setting(a, kv.first.substr(0, dot).c_str(), kv.first.substr(dot + 1).c_str());
            if (!ps || !tt_value_equal(tt_val(a, *ps), kv.second)) { all = false; break; }
        }
        if (all) cur = (int)i;
    }
    row_begin("Preset", "A starting point for the graphics settings. Changing any of them afterwards makes it Custom.");
    if (ImGui::BeginCombo("##ttpreset", cur >= 0 ? a.tt.presets[cur].name.c_str() : "Custom")) {
        for (size_t i = 0; i < a.tt.presets.size(); i++)
            if (ImGui::Selectable(a.tt.presets[i].name.c_str(), (int)i == cur))
                for (auto &kv : a.tt.presets[i].values) {
                    size_t dot = kv.first.find('.');
                    a.ttFile.set(kv.first.substr(0, dot), kv.first.substr(dot + 1), kv.second);
                }
        ImGui::EndCombo();
    }
    row_end();
}

static void tt_open_textures()
{
    std::string t = tt_data_dir() + "\\textures";
    SHCreateDirectoryExA(NULL, (t + "\\replace").c_str(), NULL);
    ShellExecuteA(NULL, "open", t.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

// Settings the launcher sets by itself instead of showing: the frame limit follows the
// frame rate (one "Frame rate" choice, as for ROTK)
// ([window] and [keyboard] have rows of their own on the Display and Controls tabs)
static bool tt_hidden(const PortSetting &ps)
{
    if (ps.section == "system" && ps.key == "one_core") return !native_arm64();   // shown on Windows on ARM PCs only
    return (ps.section == "display" && ps.key == "fps_cap") || ps.section == "window" || ps.section == "keyboard" ||
           (ps.section == "discord" && ps.key == "app_id") ||
           (ps.section == "network" && tp().kind != GK_ROTK_XBOX);   // system link co-op: The Return of the King's only
                                                                     // (the others have no network play, so it does nothing)
}

// The tab a setting is shown on: its section's, except the frame rate, which is on Display as in ROTK's
// (and the hints' button pictures, which are on Controls, and the fast forward settings, on Gameplay)
static int tt_tab_of_setting(const PortSetting &ps)
{
    if (ps.section == "graphics" && ps.key == "button_pictures") return 2;
    if (ps.section == "system" && (ps.key == "fast_forward_speed" || ps.key == "fast_forward_toggle"))
        return 5;   // with the other ways to play (Gameplay)
    return ps.section == "graphics" && ps.key == "fps" ? 0 : tt_tab_of(ps.section);
}

// ---- The Two Towers' Display tab, laid out exactly like ROTK's (tab_display):
// Screen (Monitor, Display mode, Resolution) and Smoothness (V-Sync, Frame rate,
// and the note under it). The refresh rate isn't shown: as in ROTK, fullscreen
// picks it from the frame rate (written to display.refresh on save).

static std::string tt_get(const App &a, const char *sec, const char *key, const char *def)
{
    const PortSetting *ps = tt_setting(a, sec, key);
    return ps ? toml_unquote(tt_val(a, *ps)) : def;
}

static int tt_geti(const App &a, const char *sec, const char *key, int def)
{
    const PortSetting *ps = tt_setting(a, sec, key);
    return ps ? atoi(toml_unquote(tt_val(a, *ps)).c_str()) : def;
}

void tt_put(App &a, const char *sec, const char *key, const std::string &v)
{
    const PortSetting *ps = tt_setting(a, sec, key);
    if (!ps) return;
    a.ttFile.set(sec, key, ps->type == "choice" || ps->type == "string" ? toml_quote(v) : v);
}

static int tt_mode_index(const App &a)   // 0 borderless, 1 fullscreen, 2 windowed (ROTK's order)
{
    std::string m = tt_get(a, "display", "mode", "borderless");
    return m == "fullscreen" ? 1 : m == "windowed" ? 2 : 0;
}

static int tt_max_refresh(const App &a)
{
    const Monitor &m = tt_monitor(a);
    if (tt_mode_index(a) == 1) {
        int w = tt_geti(a, "display", "width", 0), h = tt_geti(a, "display", "height", 0);
        if (w <= 0 || h <= 0) { w = m.deskW; h = m.deskH; }
        if (const Mode *md = m.find(w, h)) if (!md->hz.empty()) return md->hz.back();
    }
    return m.deskHz;
}

static std::vector<int> tt_refresh_list(const App &a)
{
    const Monitor &m = tt_monitor(a);
    int w = tt_geti(a, "display", "width", 0), h = tt_geti(a, "display", "height", 0);
    if (w <= 0 || h <= 0) { w = m.deskW; h = m.deskH; }
    if (const Mode *md = m.find(w, h)) if (!md->hz.empty()) return md->hz;
    return { m.deskHz };
}

// The refresh rate the port should use: in fullscreen, the best match for the frame rate
static int tt_effective_refresh(const App &a)
{
    if (tt_mode_index(a) != 1) return tt_monitor(a).deskHz;
    return pick_refresh(tt_refresh_list(a), tt_geti(a, "graphics", "fps", 30));
}

void tt_apply_refresh(App &a)
{
    if (tt_setting(a, "display", "refresh")) tt_put(a, "display", "refresh", std::to_string(tt_mode_index(a) == 1 ? tt_effective_refresh(a) : 0));
}

// "main" for the main screen, else its device name without \\.\ ("DISPLAY2")
static std::string tt_screen_value(const Monitor &m)
{
    if (m.primary) return "main";
    return m.device.rfind("\\\\.\\", 0) == 0 ? m.device.substr(4) : m.device;
}

static void tt_tab_display(App &a)
{
    const Monitor &m = tt_monitor(a);
    section("Screen");
    const PortSetting *pscr = tt_setting(a, "display", "screen");
    if (pscr && a.monitors.size() > 1) {
        row_begin("Monitor", "Which screen the game runs on.");
        if (ImGui::BeginCombo("##ttmon", m.name.c_str())) {
            for (int i = 0; i < (int)a.monitors.size(); i++) {
                const Monitor &mi = a.monitors[i];
                std::string v = tt_screen_value(mi);
                bool offered = v == "main";
                for (auto &c : pscr->choices) if (!_stricmp(c.c_str(), v.c_str())) offered = true;
                if (!offered) continue;
                char b[256];
                snprintf(b, sizeof b, "%d: %s%s  (%dx%d @ %d Hz)", i + 1, mi.name.c_str(), mi.primary ? ", main" : "", mi.deskW, mi.deskH, mi.deskHz);
                if (ImGui::Selectable(b, &mi == &m)) { tt_put(a, "display", "screen", v); tt_put(a, "display", "width", "0"); tt_put(a, "display", "height", "0"); }
            }
            ImGui::EndCombo();
        }
        row_end();
    }

    static const char *modes[] = { "Borderless", "Fullscreen", "Windowed" };
    static const char *modeVals[] = { "borderless", "fullscreen", "windowed" };
    int mode = tt_mode_index(a);
    int w = tt_geti(a, "display", "width", 0), h = tt_geti(a, "display", "height", 0);
    row_begin("Display mode", "Borderless: fills the screen and switches instantly with Alt-Tab (recommended).\n"
                              "Fullscreen: the screen switches to the chosen resolution and refresh rate.\n"
                              "Windowed: a normal window you can move around.");
    if (segmented("ttmode", &mode, modes, 3)) {
        tt_put(a, "display", "mode", modeVals[mode]);
        if (mode != 1 && (w > m.deskW || h > m.deskH)) { w = h = 0; }      // supersampling resolutions are for fullscreen only
        if (mode != 2 && w > 0 && !m.find(w, h)) { w = h = 0; }            // free window sizes are for windowed only
        tt_put(a, "display", "width", std::to_string(w)); tt_put(a, "display", "height", std::to_string(h));
    }
    row_end();

    row_begin("Resolution", "Only resolutions your monitor supports are listed.\n"
                            "In fullscreen, sizes marked \"supersampling\" are larger than your screen: the game is drawn at that size "
                            "and your graphics driver shrinks it down for a cleaner, sharper picture. They appear when NVIDIA DSR/DLDSR "
                            "or AMD VSR is turned on in the driver's control panel.");
    char cur[64];
    if (w > 0 && h > 0) snprintf(cur, sizeof cur, "%d x %d", w, h);
    else snprintf(cur, sizeof cur, "%d x %d  (screen)", m.deskW, m.deskH);
    auto setres = [&](int nw, int nh) { tt_put(a, "display", "width", std::to_string(nw)); tt_put(a, "display", "height", std::to_string(nh)); };
    if (ImGui::BeginCombo("##ttres", cur, ImGuiComboFlags_HeightLarge)) {
        char b[64];
        snprintf(b, sizeof b, "%d x %d  (screen)", m.deskW, m.deskH);
        if (ImGui::Selectable(b, w <= 0)) setres(0, 0);
        for (int i = (int)m.modes.size() - 1; i >= 0; i--) {
            const Mode &md = m.modes[i];
            bool bigger = md.w > m.deskW || md.h > m.deskH;
            if ((bigger && mode != 1) || (md.w == m.deskW && md.h == m.deskH)) continue;
            const char *tag = md.h * 16 == md.w * 9 ? "  16:9" : md.h * 16 == md.w * 10 ? "  16:10" : md.h * 4 == md.w * 3 ? "  4:3" : "";
            snprintf(b, sizeof b, "%d x %d%s%s", md.w, md.h, tag, bigger ? "  (supersampling)" : "");
            if (ImGui::Selectable(b, w == md.w && h == md.h)) setres(md.w, md.h);
        }
        if (mode == 2) {
            static const struct { int w, h; const char *tag; } extra[] = {
                { 3840, 1080, "  32:9 super ultrawide" }, { 2560, 720, "  32:9 super ultrawide" },
                { 3440, 1440, "  21:9 ultrawide" }, { 2560, 1080, "  21:9 ultrawide" }, { 2560, 1440, "  16:9" },
                { 1920, 1080, "  16:9" }, { 1600, 900, "  16:9" }, { 1280, 720, "  16:9" }, { 1440, 1080, "  4:3" } };
            ImGui::SeparatorText("Window sizes");
            for (auto &e : extra) {
                if (e.w > m.deskW || e.h > m.deskH || (e.w == m.deskW && e.h == m.deskH) || m.find(e.w, e.h)) continue;
                snprintf(b, sizeof b, "%d x %d%s", e.w, e.h, e.tag);
                if (ImGui::Selectable(b, w == e.w && h == e.h)) setres(e.w, e.h);
            }
        }
        ImGui::EndCombo();
    }
    row_end();

    // The Two Towers only: drawn at a share of the screen, then fitted (ROTK has no such setting).
    // Each choice shows the size it draws at, from the resolution above (the screen's own when 0),
    // as the port works it out: round(width * pct / 100) x round(height * pct / 100).
    if (const PortSetting *rp = tt_setting(a, "display", "render_percent")) {
        int rw = w > 0 && h > 0 ? w : m.deskW, rh = w > 0 && h > 0 ? h : m.deskH;
        auto sized = [&](const std::string &choice) {
            int pct = atoi(choice.c_str());
            char b[48];
            snprintf(b, sizeof b, "  (%d x %d)", (int)lround(rw * pct / 100.0), (int)lround(rh * pct / 100.0));
            return tt_choice_name(*rp, choice) + b;
        };
        if (rp->type == "choice") {
            std::string sel = toml_unquote(tt_val(a, *rp));
            row_begin(rp->label.c_str(), rp->help.c_str());
            ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
            if (ImGui::BeginCombo("##ttrender", sized(sel).c_str())) {
                for (auto &c : rp->choices)
                    if (ImGui::Selectable(sized(c).c_str(), c == sel)) a.ttFile.set(rp->section, rp->key, toml_quote(c));
                ImGui::EndCombo();
            }
            row_end();
        } else {   // older ports: a percentage slider
            int pct = atoi(tt_val(a, *rp).c_str());
            char fmt[64];
            snprintf(fmt, sizeof fmt, "%%d%%%%  (%d x %d)", (int)lround(rw * pct / 100.0), (int)lround(rh * pct / 100.0));
            row_begin(rp->label.c_str(), rp->help.c_str());
            if (ImGui::SliderInt("##ttrender", &pct, (int)rp->min, (int)rp->max, fmt)) a.ttFile.set(rp->section, rp->key, std::to_string(pct));
            row_end();
        }
    }

    section("Smoothness");
    bool vsync = tt_get(a, "display", "vsync", "true") == "true";
    if (tt_setting(a, "display", "vsync")) {
        row_begin("V-Sync", "Prevents tearing. The frame rate can't go above the screen's refresh rate.");
        if (ImGui::Checkbox("##ttvsync", &vsync)) tt_put(a, "display", "vsync", vsync ? "true" : "false");
        row_end();
    }

    const PortSetting *pf = tt_setting(a, "graphics", "fps");
    if (pf) {
        int maxHz = tt_max_refresh(a);
        int fps = tt_geti(a, "graphics", "fps", 30);
        row_begin("Frame rate", "The game was made for 30. Higher rates play at normal speed.\n"
                                "In fullscreen the screen switches to the matching refresh rate. In borderless and windowed, "
                                "rates that divide evenly into your screen's refresh rate are the smoothest.");
        char c[64];
        snprintf(c, sizeof c, "%d fps%s", fps, fps == 30 ? "  (original)" : "");
        if (ImGui::BeginCombo("##ttfps", c)) {
            for (auto &ch : pf->choices) {
                int r = atoi(ch.c_str());
                if (r <= 0 || (vsync && r > maxHz && r != fps)) continue;
                snprintf(c, sizeof c, "%d fps%s", r, r == 30 ? "  (original)" : "");
                if (ImGui::Selectable(c, fps == r)) tt_put(a, "graphics", "fps", ch);
            }
            ImGui::EndCombo();
        }
        row_end();
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::SetCursorPosX(g_labelW);
        ImGui::PushTextWrapPos(0);
        if (vsync && fps > maxHz)
            ImGui::TextColored(C_WARN, "Above this screen's %d Hz - the game will run at %d fps.", maxHz, maxHz);
        else if (mode == 1)
            ImGui::TextColored(C_DIM, "Screen will run at %d Hz.", tt_effective_refresh(a));
        else if (fps > 0 && maxHz % fps)
            ImGui::TextColored(C_DIM, "Your screen runs at %d Hz; %d fps won't divide evenly into it, so motion may be slightly uneven.", maxHz, fps);
        else
            ImGui::TextColored(C_DIM, "Your screen runs at %d Hz - %d fps fits it evenly.", maxHz, fps);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }
    if (const PortSetting *ps = tt_setting(a, "display", "show_fps")) tt_setting_row(a, *ps);
    if (tt_setting(a, "window", "pause_unfocused")) {
        section("In the background");
        for (const char *k : { "pause_unfocused", "mute_unfocused", "background_input" })
            if (const PortSetting *ps = tt_setting(a, "window", k)) tt_setting_row(a, *ps);
    }

    // anything else the port adds to [display] later, with its own label
    bool more = false;
    for (auto &ps : a.tt.settings) {
        if (ps.section != "display") continue;
        if (ps.key == "screen" || ps.key == "mode" || ps.key == "width" || ps.key == "height" || ps.key == "refresh" ||
            ps.key == "vsync" || ps.key == "render_percent" || ps.key == "show_fps" || tt_hidden(ps)) continue;
        if (!more) { section("More"); more = true; }
        tt_setting_row(a, ps);
    }
}

// ---- The Two Towers' Graphics tab, laid out like ROTK's (tab_graphics): Quality,
// Image, View, Texture packs, Movies. Only the settings the port really has are shown.

static bool tt_has(const App &a, const char *sec, const char *key) { return tt_setting(a, sec, key) != nullptr; }

static void tt_tab_graphics(App &a)
{
    std::vector<std::string> done;   // section.key shown here; anything else goes under "More"
    auto mark = [&](const char *sec, const char *key) { done.push_back(std::string(sec) + "." + key); };

    // the graphics API first (owner's request, 5 Nov: it was last, under "More"); it takes effect at the next start
    if (const PortSetting *r = tt_setting(a, "graphics", "renderer")) {
        if (!tt_hidden(*r)) {
            section("Renderer");
            tt_setting_row(a, *r);
            ImGui::TextColored(C_DIM, "Direct3D 12 or Vulkan. Takes effect the next time the game starts.");
        }
        mark("graphics", "renderer");
    }

    if (!a.tt.presets.empty()) {
        section("Quality");
        std::string help;
        for (auto &p : a.tt.presets) {
            const char *d = p.name == "Original" ? tp().original
                          : p.name == "Balanced" ? "widescreen, light anti-aliasing and filtering, light sharpening."
                          : p.name == "High"     ? "widescreen, 4x anti-aliasing, 16x texture filtering, sharper textures and sharpening."
                          : p.name == "Ultra"    ? "8x anti-aliasing, and drawn at 150% resolution for an even cleaner picture."
                          : "";
            help += p.name + (*d ? std::string(": ") + d : std::string()) + "\n";
        }
        help += "Custom: shown when you change individual settings.\nOnly Original changes the frame rate; the others keep the one chosen on the Display tab.";
        // reuse the preset row, with ROTK's wording
        int cur = -1;
        for (size_t i = 0; i < a.tt.presets.size() && cur < 0; i++) {
            bool all = true;
            for (auto &kv : a.tt.presets[i].values) {
                size_t dot = kv.first.find('.');
                const PortSetting *ps = tt_setting(a, kv.first.substr(0, dot).c_str(), kv.first.substr(dot + 1).c_str());
                if (!ps || !tt_value_equal(tt_val(a, *ps), kv.second)) { all = false; break; }
            }
            if (all) cur = (int)i;
        }
        row_begin("Preset", help.c_str());
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
        if (ImGui::BeginCombo("##ttpreset", cur >= 0 ? a.tt.presets[cur].name.c_str() : "Custom")) {
            for (size_t i = 0; i < a.tt.presets.size(); i++)
                if (ImGui::Selectable(a.tt.presets[i].name.c_str(), (int)i == cur))
                    for (auto &kv : a.tt.presets[i].values) {
                        size_t dot = kv.first.find('.');
                        a.ttFile.set(kv.first.substr(0, dot), kv.first.substr(dot + 1), kv.second);
                    }
            ImGui::EndCombo();
        }
        row_end();
    }

    bool image = tt_has(a, "graphics", "anti_aliasing") || tt_has(a, "graphics", "msaa") || tt_has(a, "graphics", "colour_look") ||
                 tt_has(a, "graphics", "anisotropic_filtering") || tt_has(a, "graphics", "lod_bias") ||
                 tt_has(a, "graphics", "sharpening") || tt_has(a, "graphics", "brightness");
    image = image || tt_has(a, "graphics", "upscaler");
    if (image) section("Image");
    // FSR / DLSS first: while one is on it takes the place of Render resolution and Anti-aliasing
    if (const PortSetting *up = tt_setting(a, "graphics", "upscaler")) {
        mark("graphics", "upscaler");
        tt_setting_row(a, *up);
        if (const PortSetting *q = tt_setting(a, "graphics", "upscaler_quality")) {
            mark("graphics", "upscaler_quality");
            if (toml_unquote(tt_val(a, *up)) != "0") tt_setting_row(a, *q);
        }
    }
    // Anti-aliasing first, as in ROTK
    for (const char *k : { "anti_aliasing", "msaa" }) {
        const PortSetting *ps = tt_setting(a, "graphics", k);
        if (!ps || ps->type != "choice") continue;
        mark("graphics", k);
        std::string cur = toml_unquote(tt_val(a, *ps));
        row_begin("Anti-aliasing", "Smooths jagged edges (MSAA). 8x is heavy: at high frame rates 4x looks almost the same and stays smooth.");
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
        if (ImGui::BeginCombo((std::string("##tt") + k).c_str(), tt_choice_name(*ps, cur).c_str())) {
            for (auto &c : ps->choices)
                if (ImGui::Selectable(tt_choice_name(*ps, c).c_str(), c == cur)) a.ttFile.set(ps->section, ps->key, toml_quote(c));
            ImGui::EndCombo();
        }
        row_end();
        mark("graphics", "alpha_edges");   // (removed from the games, 5 Nov: an old settings file may still list it)
        break;
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "anisotropic_filtering")) {
        mark("graphics", "anisotropic_filtering");
        std::string cur = toml_unquote(tt_val(a, *ps));
        row_begin("Texture filtering", "Anisotropic filtering: keeps ground and walls sharp at a distance.");
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
        if (ImGui::BeginCombo("##ttaf", tt_choice_name(*ps, cur).c_str())) {
            for (auto &c : ps->choices)
                if (ImGui::Selectable(tt_choice_name(*ps, c).c_str(), c == cur)) a.ttFile.set(ps->section, ps->key, toml_quote(c));
            ImGui::EndCombo();
        }
        row_end();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "lod_bias")) {
        mark("graphics", "lod_bias");
        int ts = (int)lroundf(-(float)atof(tt_val(a, *ps).c_str()) * 100.0f);
        int lo = (int)lround(-ps->max * 100), hi = (int)lround(-ps->min * 100);   // -1.00..0 -> 0..100%
        row_begin("Texture sharpness", "Shows textures at higher detail sooner. Too high can shimmer.");
        if (ImGui::SliderInt("##ttlod", &ts, lo, hi, ts == 0 ? "Game default" : "%d%%")) {
            char b[32]; snprintf(b, sizeof b, "%.2f", -ts / 100.0f); a.ttFile.set(ps->section, ps->key, b);
        }
        row_end();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "sharpening")) {
        mark("graphics", "sharpening");
        int v = atoi(tt_val(a, *ps).c_str());
        row_begin("Sharpening", "Crisper picture. 0 turns it off. While playing, [ switches it on and off to compare.");
        if (ImGui::SliderInt("##ttsharp", &v, (int)ps->min, (int)ps->max, v == 0 ? "Off" : "%d")) a.ttFile.set(ps->section, ps->key, std::to_string(v));
        row_end();
    }
    // the port's own (no ROTK Fix counterpart): right after Sharpening; presets set it
    if (const PortSetting *ps = tt_setting(a, "graphics", "smooth_2d")) {
        mark("graphics", "smooth_2d");
        bool v = tt_val(a, *ps) == "true";
        row_begin(ps->label.empty() ? "Smooth menus and HUD" : ps->label.c_str(),
                  ps->help.empty() ? "Smooths menus, text and the HUD when they are drawn larger than the console's screen." : ps->help.c_str());
        if (ImGui::Checkbox("##ttsmooth2d", &v)) a.ttFile.set(ps->section, ps->key, v ? "true" : "false");
        row_end();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "sharp_text")) {   // The Return of the King's
        mark("graphics", "sharp_text");
        tt_setting_row(a, *ps);
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "console_textures")) {
        mark("graphics", "console_textures");
        bool v = tt_val(a, *ps) == "true";
        row_begin(ps->label.empty() ? "Original texture softness" : ps->label.c_str(),
                  ps->help.empty() ? "Draws textures at the detail the Xbox used at its own resolution: softer, as on a TV." : ps->help.c_str());
        if (ImGui::Checkbox("##ttconsoletex", &v)) a.ttFile.set(ps->section, ps->key, v ? "true" : "false");
        row_end();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "colour_look")) {
        mark("graphics", "colour_look");
        std::string cur = toml_unquote(tt_val(a, *ps));
        row_begin("Colour look", "Original: the game's own colours.\n"
                                 "Film: richer contrast, cool shadows and warm highlights.\n"
                                 "Vivid: stronger colour.\n"
                                 "Dark fantasy: cooler and moodier, with less colour.\n"
                                 "While playing, ] switches between them.");
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
        if (ImGui::BeginCombo("##ttlook", tt_choice_name(*ps, cur).c_str())) {
            for (auto &c : ps->choices)
                if (ImGui::Selectable(tt_choice_name(*ps, c).c_str(), c == cur)) a.ttFile.set(ps->section, ps->key, toml_quote(c));
            ImGui::EndCombo();
        }
        row_end();
        // strength only matters with a look chosen, as in ROTK
        if (const PortSetting *pst = tt_setting(a, "graphics", "look_strength")) {
            mark("graphics", "look_strength");
            if (cur != "original") {
                int v = atoi(tt_val(a, *pst).c_str());
                row_begin("Look strength", "How strongly the colour look is applied.");
                if (ImGui::SliderInt("##ttlookpct", &v, (int)pst->min, (int)pst->max, "%d%%")) a.ttFile.set(pst->section, pst->key, std::to_string(v));
                row_end();
            }
        }
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "brightness")) {
        mark("graphics", "brightness");
        float v = (float)atof(tt_val(a, *ps).c_str());
        row_begin("Brightness", "Lifts the darker parts of the picture (dark cutscenes, caves) without washing out white.");
        if (ImGui::SliderFloat("##ttbright", &v, (float)ps->min, (float)ps->max, fabsf(v - 1.0f) < 0.005f ? "Game's own" : "%.2f")) {
            char b[32]; snprintf(b, sizeof b, "%.2f", v); a.ttFile.set(ps->section, ps->key, b);
        }
        row_end();
    }

    if (const PortSetting *ps = tt_setting(a, "graphics", "hdr")) {   // The Two Towers only
        mark("graphics", "hdr");
        bool v = tt_val(a, *ps) == "true";
        row_begin("HDR", ps->help.c_str());
        if (ImGui::Checkbox("##tthdr", &v)) a.ttFile.set(ps->section, ps->key, v ? "true" : "false");
        row_end();
    }

    const PortSetting *pws = tt_setting(a, "graphics", "widescreen");
    if (pws || tt_has(a, "graphics", "fov") || tt_has(a, "graphics", "hud_edges") || tt_has(a, "graphics", "draw_distance") ||
        tt_has(a, "graphics", "safe_area"))
        section("View");
    bool ws = pws ? tt_val(a, *pws) == "true" : false;
    if (pws) {
        mark("graphics", "widescreen");
        row_begin("Widescreen correction", "Shows more at the sides instead of stretching the 4:3 picture.");
        if (ImGui::Checkbox("##ttws", &ws)) a.ttFile.set(pws->section, pws->key, ws ? "true" : "false");
        row_end();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "hud_edges")) {
        mark("graphics", "hud_edges");
        bool v = tt_val(a, *ps) == "true";
        ImGui::BeginDisabled(!ws);
        row_begin("HUD at the screen edges", "With widescreen correction on: health bars and other HUD pieces sit at the screen's edges "
                                             "instead of in the middle 4:3 area.");
        if (ImGui::Checkbox("##tthud", &v)) a.ttFile.set(ps->section, ps->key, v ? "true" : "false");
        row_end();
        ImGui::EndDisabled();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "fov")) {
        mark("graphics", "fov");
        int v = atoi(tt_val(a, *ps).c_str());
        row_begin("Field of view", "How wide the camera sees, compared with the game's own. Wider shows more of the battle "
                                   "but makes characters look further away.");
        if (ImGui::SliderInt("##ttfov", &v, (int)ps->min, (int)ps->max, v == 100 ? "100%%  (game's own)" : "%d%%"))
            a.ttFile.set(ps->section, ps->key, std::to_string(v));
        row_end();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "draw_distance")) {   // The Return of the King's
        mark("graphics", "draw_distance");
        int v = atoi(tt_val(a, *ps).c_str());
        row_begin(ps->label.c_str(), ps->help.c_str());
        if (ImGui::SliderInt("##ttdraw", &v, (int)ps->min, (int)ps->max, v == 100 ? "100%%  (game's own)" : "%d%%"))
            a.ttFile.set(ps->section, ps->key, std::to_string(v));
        row_end();
    }
    if (const PortSetting *ps = tt_setting(a, "graphics", "safe_area")) {
        mark("graphics", "safe_area");
        tt_setting_row(a, *ps);
    }

    mark("textures", "replace"); mark("textures", "dump");   // on the Mods tab

    // Movies, in ROTK's order: Movie display, Sharper cutscenes, Skip any video, Skip opening logos, Cutscene sound delay
    {
        const PortSetting *pmd = tt_setting(a, "graphics", "movie_display"), *pms = tt_setting(a, "graphics", "movie_sharpening"),
                          *psa = tt_setting(a, "graphics", "skip_all_videos"), *psl = tt_setting(a, "graphics", "skip_logos"),
                          *pad = tt_setting(a, "graphics", "cutscene_audio_delay");
        if (pmd || pms || psa || psl || pad || tt_has(a, "graphics", "movie_subtitles")) section("Movies");
        if (pmd && pmd->type == "choice") {
            mark("graphics", "movie_display");
            std::string cur = toml_unquote(tt_val(a, *pmd));
            row_begin("Movie display", pmd->help.c_str());
            ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - S(4), S(290)));
            if (ImGui::BeginCombo("##ttmd", tt_choice_name(*pmd, cur).c_str())) {
                for (auto &c : pmd->choices)
                    if (ImGui::Selectable(tt_choice_name(*pmd, c).c_str(), c == cur)) a.ttFile.set(pmd->section, pmd->key, toml_quote(c));
                ImGui::EndCombo();
            }
            row_end();
        }
        if (pms) {
            mark("graphics", "movie_sharpening");
            int v = atoi(tt_val(a, *pms).c_str());
            row_begin("Sharper cutscenes", pms->help.c_str());
            if (ImGui::SliderInt("##ttmvsharp", &v, (int)pms->min, (int)pms->max, v == 0 ? "Off" : "%d")) a.ttFile.set(pms->section, pms->key, std::to_string(v));
            row_end();
        }
        if (psa) {
            mark("graphics", "skip_all_videos");
            bool v = tt_val(a, *psa) == "true";
            row_begin("Skip any video", psa->help.c_str());
            if (ImGui::Checkbox("##ttskipall", &v)) a.ttFile.set(psa->section, psa->key, v ? "true" : "false");
            row_end();
        }
        if (psl) {
            mark("graphics", "skip_logos");
            bool v = tt_val(a, *psl) == "true";
            row_begin("Skip opening logos", psl->help.c_str());
            if (ImGui::Checkbox("##ttlogos", &v)) a.ttFile.set(psl->section, psl->key, v ? "true" : "false");
            row_end();
        }
        if (const PortSetting *psub = tt_setting(a, "graphics", "movie_subtitles")) {
            mark("graphics", "movie_subtitles");
            tt_setting_row(a, *psub);
            row_begin("Subtitles folder", "Where the subtitle files go: one .srt file per movie, named as the movie.");
            if (ImGui::Button("Open folder##ttsubs")) {
                std::string d = a.tt.exe.substr(0, a.tt.exe.find_last_of('\\')) + "\\subtitles";
                SHCreateDirectoryExA(NULL, d.c_str(), NULL);
                ShellExecuteA(NULL, "open", d.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
            row_end();
        }
        if (pad) {
            mark("graphics", "cutscene_audio_delay");
            int v = atoi(tt_val(a, *pad).c_str());
            row_begin("Cutscene sound delay", pad->help.c_str());
            if (ImGui::SliderInt("##ttadelay", &v, (int)pad->min, (int)pad->max, v == 0 ? "Off" : "%d ms")) a.ttFile.set(pad->section, pad->key, std::to_string(v));
            row_end();
        }
    }

    // anything else on this tab that the port adds later, with its own label
    bool more = false;
    for (auto &ps : a.tt.settings) {
        if (tt_tab_of_setting(ps) != 1 || tt_hidden(ps)) continue;
        if (std::find(done.begin(), done.end(), ps.section + "." + ps.key) != done.end()) continue;
        if (!more) { section("More"); more = true; }
        tt_setting_row(a, ps);
    }
}

// ---- The Two Towers' Controls tab, laid out and worked like ROTK's: Controller, Buttons
// (click a move, press the button; it moves on by itself), Controller options.
// The port's [controls] settings say which button on the player's controller does each
// Xbox button; their values are the same names ROTK uses (A, B, X, Y, LB, RB, LT, RT, LS, RS).
static const char *const k_ttPhys[PAD_PHYS_COUNT] = { "A", "B", "X", "Y", "LB", "RB", "", "", "LS", "RS", "LT", "RT" };

static int tt_phys_of(const std::string &v)
{
    for (int i = 0; i < PAD_PHYS_COUNT; i++) if (k_ttPhys[i][0] && v == k_ttPhys[i]) return i;
    return -1;   // "none"
}

static std::vector<const PortSetting *> tt_buttons(App &a)
{
    std::vector<const PortSetting *> v;
    for (auto &ps : a.tt.settings) if (ps.section == "controls" && ps.type == "choice") v.push_back(&ps);
    return v;
}

int tt_button_count(App &a) { return (int)tt_buttons(a).size(); }

// As on ROTK: the move that had the button gets this move's old one.
bool tt_assign_button(App &a, int idx, int ph)
{
    auto b = tt_buttons(a);
    if (idx < 0 || idx >= (int)b.size() || ph < 0 || !k_ttPhys[ph][0]) return false;
    std::string nv = k_ttPhys[ph], old = toml_unquote(tt_val(a, *b[idx]));
    auto allows = [](const PortSetting *ps, const std::string &v) { return std::find(ps->choices.begin(), ps->choices.end(), v) != ps->choices.end(); };
    if (!allows(b[idx], nv)) return false;
    for (int i = 0; i < (int)b.size(); i++)
        if (i != idx && toml_unquote(tt_val(a, *b[i])) == nv) {
            // the other move gets this one's old button - or nothing, if it can't take that one
            std::string give = allows(b[i], old) ? old : allows(b[i], "none") ? std::string("none") : toml_unquote(b[i]->def);
            a.ttFile.set(b[i]->section, b[i]->key, toml_quote(give));
        }
    a.ttFile.set(b[idx]->section, b[idx]->key, toml_quote(nv));
    return true;
}

// "Speed Attack / Fire" -> "Speed attack / fire", as ROTK writes its moves; "Xbox B" stays
static std::string tt_move_name(const std::string &l)
{
    std::string r = l;
    bool first = true, afterXbox = false;
    for (size_t i = 0; i < r.size(); i++) {
        if (r[i] == ' ' || r[i] == '/') continue;
        size_t e = r.find_first_of(" /", i);
        if (e == std::string::npos) e = r.size();
        std::string w = r.substr(i, e - i);
        bool keep = first || afterXbox || w == "Xbox" || (w.size() <= 2 && isupper((unsigned char)w[0]));   // "Xbox Black" is a button's name
        afterXbox = w == "Xbox";
        if (!keep && isupper((unsigned char)w[0]) && (w.size() < 2 || islower((unsigned char)w[1]))) r[i] = (char)tolower((unsigned char)r[i]);
        first = false;
        i = e;
    }
    return r;
}

static void tt_slider_row(App &a, const char *key, const char *label, const char *help, const char *id, const char *zero)
{
    const PortSetting *ps = tt_setting(a, "input", key);
    if (!ps) return;
    int v = atoi(tt_val(a, *ps).c_str());
    row_begin(label, help);
    if (ImGui::SliderInt(id, &v, (int)ps->min, (int)ps->max, v == 0 ? zero : "%d%%")) a.ttFile.set(ps->section, ps->key, std::to_string(v));
    row_end();
}

static void tt_tab_controls(App &a)
{
    section("Controller");
    // no switch here: a controller always works, plugged in or not; this only says whether one is
    row_begin("Controller", "Any controller you plug in works, as well as the keyboard and mouse.");
    ImGui::AlignTextToFramePadding();
    if (a.controllers > 1) ImGui::TextColored(C_GOOD, "%d controllers connected", a.controllers);
    else if (a.controllers) ImGui::TextColored(C_GOOD, "Connected");
    else ImGui::TextColored(C_WARN, "None connected");
    row_end();

    auto btn = tt_buttons(a);
    int style = label_style(a);
    if (!a.captureTT) a.capture = -1;
    a.captureTT = true;
    if (a.capture >= (int)btn.size()) a.capture = -1;
    a.captureFrame = ImGui::GetFrameCount();
    if (a.capture >= 0 && (ImGui::GetTime() - a.captureAt > 8.0 || !a.controllers)) a.capture = -1;
    if (!btn.empty()) {
        section("Buttons");
        float slotW = S(170);
        ImGui::BeginGroup();
        for (int m = 0; m < (int)btn.size(); m++) {
            const PortSetting &ps = *btn[m];
            ImGui::PushID(m);
            ImGui::AlignTextToFramePadding();
            std::string name = tt_move_name(ps.label.empty() ? ps.key : ps.label);
            ImGui::TextUnformatted(name.c_str());
            ImGui::SameLine(g_labelW);
            bool waiting = a.capture == m;
            int ph = tt_phys_of(toml_unquote(tt_val(a, ps)));
            char lab[64];
            snprintf(lab, sizeof lab, "%s##slot", waiting ? "Press a button..." : ph < 0 ? "Nothing" : phys_label(ph, style));
            if (waiting) {
                float pulse = 0.5f + 0.5f * (float)sin(ImGui::GetTime() * 6.0);
                ImGui::PushStyleColor(ImGuiCol_Button, rgb(110 + (int)(40 * pulse), 82 + (int)(30 * pulse), 36));
                ImGui::PushStyleColor(ImGuiCol_Border, C_GOLD_H);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, rgb(34, 27, 20));
                ImGui::PushStyleColor(ImGuiCol_Border, C_LINE);
            }
            if (ImGui::Button(lab, ImVec2(slotW, 0))) {
                if (waiting) a.capture = -1;
                else if (a.controllers) { a.capture = m; a.captureAt = ImGui::GetTime(); }
                else ImGui::OpenPopup("pick");
            }
            ImGui::PopStyleColor(2);
            if (ImGui::BeginPopup("pick")) {
                static const int choices[] = { PH_A, PH_B, PH_X, PH_Y, PH_LB, PH_RB, PH_LT, PH_RT, PH_LS, PH_RS };
                for (int c : choices)
                    if (ImGui::Selectable(phys_label(c, style), ph == c)) tt_assign_button(a, m, c);
                if (std::find(ps.choices.begin(), ps.choices.end(), "none") != ps.choices.end() &&
                    ImGui::Selectable("Nothing", ph < 0)) a.ttFile.set(ps.section, ps.key, toml_quote("none"));
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndGroup();
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::PushTextWrapPos(0);
        if (a.capture >= 0)
            ImGui::TextColored(C_GOLD_H, "Press the button for \"%s\". Start, Back or Esc stops.",
                               tt_move_name(btn[a.capture]->label.empty() ? btn[a.capture]->key : btn[a.capture]->label).c_str());
        else if (a.controllers)
            ImGui::TextColored(C_DIM, "Click a move, then press the button you want on the controller. It moves on to the next move by itself.");
        else
            ImGui::TextColored(C_DIM, "Connect a controller to set buttons by pressing them, or click a move to pick from a list.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::BeginDisabled(!a.controllers);
        if (ImGui::Button("Set all, one by one")) { a.capture = 0; a.captureAt = ImGui::GetTime(); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        bool isDefault = true;
        for (auto *ps : btn) if (toml_unquote(tt_val(a, *ps)) != toml_unquote(ps->def)) isDefault = false;
        ImGui::BeginDisabled(isDefault);
        if (ImGui::Button("Original layout")) { for (auto *ps : btn) a.ttFile.set(ps->section, ps->key, ps->def); a.capture = -1; }
        ImGui::EndDisabled();
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_DIM, "%s pauses and skips movies, %s is Back. The D-pad and left stick move through menus. One layout is used for every controller.",
                           phys_label(PH_START, style), phys_label(PH_BACK, style));
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    }

    section("Controller options");
    tt_slider_row(a, "vibration", "Vibration", "Controller rumble from the original Xbox version. 0 turns it off.", "##ttrumble", "Off");
    tt_slider_row(a, "stick_deadzone", "Stick deadzone", "How far a stick must move before it counts. 0 keeps the game's own, as on the Xbox.", "##ttdz", "Game's own");
    tt_slider_row(a, "trigger_threshold", "Trigger sensitivity", "How far the triggers must be pulled to count as pressed. 0 keeps the game's own, as on the Xbox.", "##tttrig", "Game's own");
    // [keyboard]: the port's own list, in its order (keys.h in its SDK)
    if (const PortSetting *en = tt_setting(a, "keyboard", "enabled")) {
        static const char *const keyNames[KB_ACTION_COUNT] = {
            "move_up", "move_down", "move_left", "move_right", "walk", "a", "b", "x", "y", "black", "white",
            "left_trigger", "right_trigger", "start", "back", "left_stick", "right_stick", "dpad_up", "dpad_down",
            "dpad_left", "dpad_right" };
        static const char *const ttLabels[KB_ACTION_COUNT] = {
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            "Pause", "Back", nullptr, nullptr, "Menu up", "Menu down", "Menu left", "Menu right" };
        std::string keys[KB_ACTION_COUNT], labels[KB_ACTION_COUNT], defaults[KB_ACTION_COUNT];
        const PortSetting *ps[KB_ACTION_COUNT] = {};
        for (int k = 0; k < KB_ACTION_COUNT; k++) {
            ps[k] = tt_setting(a, "keyboard", keyNames[k]);
            if (!ps[k]) continue;
            keys[k] = toml_unquote(tt_val(a, *ps[k]));
            defaults[k] = toml_unquote(ps[k]->def);
            labels[k] = ttLabels[k] ? std::string(ttLabels[k]) : tt_move_name(ps[k]->label);
        }
        bool on = tt_val(a, *en) == "true";
        int player = 1;
        if (const PortSetting *pp = tt_setting(a, "keyboard", "player")) player = atoi(toml_unquote(tt_val(a, *pp)).c_str()) == 2 ? 2 : 1;
        if (keyboard_section(a, on, player, keys, labels, defaults, KB_ACTION_COUNT)) {
            a.ttFile.set("keyboard", "enabled", on ? "true" : "false");
            a.ttFile.set("keyboard", "player", toml_quote(player == 2 ? "2" : "1"));
            for (int k = 0; k < KB_ACTION_COUNT; k++) if (ps[k]) a.ttFile.set("keyboard", keyNames[k], toml_quote(keys[k]));
        }
    }
    // the mouse: aiming with the ranged attack held, the wheel and right click in the menus
    if (tt_setting(a, "keyboard", "mouse_aim")) {
        section("Mouse");
        for (const char *k : { "mouse_aim", "mouse_sensitivity", "mouse_invert_y", "mouse_menus" })
            if (const PortSetting *ps = tt_setting(a, "keyboard", k)) tt_setting_row(a, *ps);
    }
    // anything else the port puts on this tab
    bool more = false;
    for (auto &ps : a.tt.settings) {
        if (tt_tab_of_setting(ps) != 2 || tt_hidden(ps) || (ps.section == "controls" && ps.type == "choice")) continue;
        if (ps.section == "input" && (ps.key == "vibration" || ps.key == "stick_deadzone" || ps.key == "trigger_threshold")) continue;
        if (!more) { section("More"); more = true; }
        tt_setting_row(a, ps);
    }
}

// ---- The Two Towers' Gameplay tab (ROTK has none): difficulty adjustments, then anything else in [gameplay]
static void tt_tab_gameplay(App &a)
{
    std::vector<std::string> done;
    bool head = false;
    for (const char *key : { "damage_taken", "enemy_damage" }) {
        const PortSetting *ps = tt_setting(a, "gameplay", key);
        if (!ps) continue;
        if (!head) { section("Difficulty"); head = true; }
        done.push_back(std::string("gameplay.") + key);
        int v = atoi(tt_val(a, *ps).c_str());
        row_begin(ps->label.c_str(), ps->help.c_str());
        if (ImGui::SliderInt((std::string("##tt") + key).c_str(), &v, (int)ps->min, (int)ps->max, v == 100 ? "100%%  (game's own)" : "%d%%"))
            a.ttFile.set(ps->section, ps->key, std::to_string(v));
        row_end();
    }
    bool more = false;
    for (auto &ps : a.tt.settings) {
        if (tt_tab_of_setting(ps) != 5 || tt_hidden(ps) || ps.section == "cheats" || ps.section == "language") continue;
        if (std::find(done.begin(), done.end(), ps.section + "." + ps.key) != done.end()) continue;
        if (!more) { section(head ? "More" : "Gameplay"); more = true; }
        tt_setting_row(a, ps);
    }
    // [language]: the subtitles in another language
    bool lang = false;
    for (auto &ps : a.tt.settings) {
        if (ps.section != "language" || tt_hidden(ps)) continue;
        if (!lang) { section("Language"); lang = true; }
        tt_setting_row(a, ps);
    }
    // [cheats]: the game's own secret codes, as switches
    bool cheats = false;
    for (auto &ps : a.tt.settings) {
        if (ps.section != "cheats") continue;
        if (!cheats) { section("Cheats"); cheats = true; }
        tt_setting_row(a, ps);
    }
    achievements_section(tt_data_dir() + "\\achievements.txt");
}

// The settings on one tab, in the port's own order, grouped by section
static void tt_settings_on_tab(App &a, int tab)
{
    const PortSetting *pw = tt_setting(a, "display", "width"), *ph = tt_setting(a, "display", "height");
    std::string sec = "\x01";
    if (tab == 1 && !a.tt.presets.empty()) { section("Preset"); tt_preset_row(a); tt_recommend_row(a); }
    for (auto &ps : a.tt.settings) {
        if (tt_tab_of_setting(ps) != tab) continue;
        // the frame rate joins the Display tab's screen settings, without starting a Graphics heading there
        std::string psec = tab == 0 && ps.section == "graphics" ? std::string("display") : ps.section;
        if (psec != sec) {
            if (sec == "textures" && ImGui::Button("Open texture folder")) tt_open_textures();
            sec = psec;
            const char *t = tt_section_title(sec);
            section(t ? t : cap_first(sec).c_str());
        }
        if (pw && ph && &ps == pw) {
            tt_resolution_row(a, *pw, *ph);
            if (const PortSetting *pr = tt_setting(a, "display", "refresh")) if (pr->type == "int") tt_refresh_row(a, *pr);   // right under it
            continue;
        }
        if (pw && ph && &ps == ph) continue;
        if (tt_hidden(ps)) continue;
        if (ps.section == "display" && ps.key == "refresh" && ps.type == "int") { if (!(pw && ph)) tt_refresh_row(a, ps); continue; }
        // the HUD-edge setting only matters with widescreen on
        bool off = false;
        if (ps.key == "hud_edges") if (const PortSetting *ws = tt_setting(a, "graphics", "widescreen")) off = tt_val(a, *ws) != "true";
        ImGui::BeginDisabled(off);
        tt_setting_row(a, ps);
        ImGui::EndDisabled();
    }
    if (sec == "textures" && ImGui::Button("Open texture folder")) tt_open_textures();
}

// Mods: texture packs (the Xbox games' mods), on their own tab (Shagg, 6 Nov: About was too cluttered)
static void tt_tab_mods(App &a)
{
    const PortSetting *prep = tt_setting(a, "textures", "replace"), *pdump = tt_setting(a, "textures", "dump");
    if (prep || pdump) {
        section("Texture packs");
        std::string dir = tt_data_dir() + "\\textures";
        if (prep) {
            bool v = tt_val(a, *prep) == "true";
            std::string h = std::string("Shows sharper or remade pictures from texture packs. A pack goes in its own folder inside ") +
                            tp().dir + "\\textures\\replace; remove the folder to go back to the original pictures.";
            row_begin("Use texture packs", h.c_str());
            if (ImGui::Checkbox("##tttexrepl", &v)) a.ttFile.set(prep->section, prep->key, v ? "true" : "false");
            row_end();
        }
        if (pdump) {
            bool v = tt_val(a, *pdump) == "true";
            std::string h = std::string("For making texture packs: every picture the game uses is saved as a PNG in ") + tp().dir +
                            "\\textures\\dump as you play. Levels load a little slower the first time. Turn it off when you have what you need.";
            row_begin("Save game textures", h.c_str());
            if (ImGui::Checkbox("##tttexdump", &v)) a.ttFile.set(pdump->section, pdump->key, v ? "true" : "false");
            row_end();
        }
        std::string h = std::string("Opens ") + tp().dir + "\\textures, where the replace and dump folders are.";
        row_begin("Textures folder", h.c_str());
        if (ImGui::Button("Open folder##tttex")) tt_open_textures();
        row_end();
    }

}

static void tt_about(App &a)
{
    const LibGame &g = a.lib[a.cur];
    update_section(a);
    section("Your copy");
    {
        std::string c = std::string("Xbox, ") + game_region_name(g) + " copy, " + (g.iso ? "disc image" : "extracted files");
        status_dot(true, false, c.c_str(), g.path.c_str());
        std::string arch = exe_machine(a.tt.exe);
        std::string p = "Port " + a.tt.version + (arch.empty() ? "" : " (" + arch + ")");
        status_dot(!a.tt.exe.empty(), false, p.c_str(), a.tt.exe.c_str());
    }
    if (ImGui::Button("Settings folder")) {
        SHCreateDirectoryExA(NULL, tt_data_dir().c_str(), NULL);
        ShellExecuteA(NULL, "open", tt_data_dir().c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    tip(("Settings, texture packs and the log: " + tt_data_dir()).c_str());
    ImGui::SameLine();
    if (ImGui::Button("Screenshots")) {
        std::string d = exe_dir() + "\\Screenshots";
        SHCreateDirectoryExA(NULL, d.c_str(), NULL);
        ShellExecuteA(NULL, "open", d.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to defaults...")) ImGui::OpenPopup("Reset to defaults?");
    if (ImGui::BeginPopupModal("Reset to defaults?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextUnformatted("Put every setting of this game back to its default? Your saves are not touched.");
        if (ImGui::Button("Reset", ImVec2(S(150), 0))) {
          if (a.running) set_message(a, "Close the game first: its settings weren't reset.", true);
          else {
            a.ttFile = PortSettingsFile();
            tt_save(a);
            set_message(a, std::string(tp().game) + " settings are back to their defaults", false);
          }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(S(150), 0)) || dialog_back()) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    saves_section(a, tp().game, tt_data_dir() + "\\saves", tt_data_dir() + "\\shadercache");
    report_section(a);
    about_credits(a);
}

// The Two Towers' page when its port is ready: tabs like ROTK's
void tt_tabs(App &a, const LibGame &)
{
    // tab ids: 0 Display, 1 Graphics, 2 Controls, 3 Audio, 4 Network, 5 Gameplay; shown in the PC game's order
    // (Display, Graphics, Controls, Audio, Gameplay), then Network and About (6): the same places in every game (5 Nov)
    static const char *wide[] = { "  Display  ", "  Graphics  ", "  Controls  ", "  Audio  ", "  Network  ", "  Gameplay  ", "  About  ", "  Mods  " };
    static const char *tight[] = { "Display", "Graphics", "Controls", "Audio", "Network", "Gameplay", "About", "Mods" };
    static const int order[] = { 0, 1, 2, 3, 5, 4, 7, 6 };   // Mods before About, as the PC game's tabs
    bool has[8] = {};
    for (auto &ps : a.tt.settings) if (!tt_hidden(ps)) has[tt_tab_of_setting(ps)] = true;
    has[1] = true;   // the preset lives there
    has[6] = true;
    has[7] = tt_setting(a, "textures", "replace") || tt_setting(a, "textures", "dump");
    int count = 0;
    for (bool h : has) count += h;
    (void)wide;
    static const char *ids[] = { "###display", "###graphics", "###controls", "###audio", "###network", "###gameplay", "###about", "###mods" };
    const char *shownNames[8]; int shownN = 0;
    for (int t : order) if (has[t]) shownNames[shownN++] = tight[t];
    static int ttSel = 0;
    ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
    float tabW = tabs_begin(shownNames, shownN, ttSel);
    if (ImGui::BeginTabBar("tttabs")) {
        static int startTab = getenv("ROTKFIX_TAB") ? atoi(getenv("ROTKFIX_TAB")) : -1;
        if (int step = pad_tab_step(a)) startTab = (ttSel + step + shownN) % shownN;
        int shown = 0;
        for (int t : order) {
            if (!has[t]) continue;
            int idx = shown;
            ImGuiTabItemFlags tf = shown++ == startTab ? ImGuiTabItemFlags_SetSelected : 0;
            ImGui::SetNextItemWidth(tabW);
            if (ImGui::BeginTabItem(ids[t], nullptr, tf)) {
                ttSel = idx;
                pad_tab_focus();
                ImGui::PopFont();
                ImGui::BeginChild("tttab", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
                // settings.toml there but locked when it was read: nothing can be changed (a change would be lost when
                // the file is read again at save), until it can be read
                const bool locked = a.ttFile.unreadable && t != 6;
                if (locked) {
                    static double lastTry = -10;
                    double now = ImGui::GetTime();
                    if (now - lastTry > 2.0) { lastTry = now; settings_file_load(tt_settings_path(), a.ttFile); }
                }
                if (locked && a.ttFile.unreadable) {
                    ImGui::TextWrapped("%s", tr("The settings file is open in another program (OneDrive or antivirus, for example), so the settings can't be changed right now. Athelas tries again every few seconds."));
                    ImGui::BeginDisabled();
                }
                const bool disabledHere = locked && a.ttFile.unreadable;
                if (t == 0) tt_tab_display(a); else if (t == 1) tt_tab_graphics(a); else if (t == 2) tt_tab_controls(a);
                else if (t == 5) tt_tab_gameplay(a); else if (t < 5) tt_settings_on_tab(a, t); else if (t == 7) tt_tab_mods(a); else tt_about(a);
                if (getenv("ROTKFIX_SCROLL_END")) ImGui::SetScrollY(ImGui::GetScrollMaxY());   // testing
                else if (const char *sy = getenv("ROTKFIX_SCROLL_Y")) ImGui::SetScrollY(std::min((float)atof(sy) * g_scale, ImGui::GetScrollMaxY()));   // testing
                if (disabledHere) ImGui::EndDisabled();
                ImGui::Dummy(ImVec2(0, S(6)));
                ImGui::EndChild();
                ImGui::PushFont(F_HEAD, F_HEAD->LegacySize);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
        startTab = -1;
    }
    tabs_end();
    ImGui::PopFont();
}

// One line for the bottom bar
std::string tt_summary(const App &a)
{
    if (!tt_ready(a)) return std::string(tp().game) + "  |  port not found";
    std::string s;
    const PortSetting *p;
    int w = 0, h = 0;
    if ((p = tt_setting(a, "display", "width"))) w = atoi(tt_val(a, *p).c_str());
    if ((p = tt_setting(a, "display", "height"))) h = atoi(tt_val(a, *p).c_str());
    if (w <= 0 || h <= 0) { w = tt_monitor(a).deskW; h = tt_monitor(a).deskH; }
    s = std::to_string(w) + " x " + std::to_string(h);
    if ((p = tt_setting(a, "display", "mode"))) s += "  |  " + cap_first(toml_unquote(tt_val(a, *p)));
    if ((p = tt_setting(a, "graphics", "fps"))) s += "  |  " + std::to_string(atoi(toml_unquote(tt_val(a, *p)).c_str())) + " fps";
    // the preset, as ROTK's line ends with it
    if (!a.tt.presets.empty()) {
        std::string name = "Custom";
        for (auto &pr : a.tt.presets) {
            bool all = true;
            for (auto &kv : pr.values) {
                size_t dot = kv.first.find('.');
                const PortSetting *ps = tt_setting(a, kv.first.substr(0, dot).c_str(), kv.first.substr(dot + 1).c_str());
                if (!ps || !tt_value_equal(tt_val(a, *ps), kv.second)) { all = false; break; }
            }
            if (all) { name = pr.name; break; }
        }
        s += "  |  " + name;
    }
    return s;
}
