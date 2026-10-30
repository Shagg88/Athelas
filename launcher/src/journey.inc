// ---------------------------------------------------------------------------
// The Journey (included by main.cpp after front.inc): the three games as one road, in the spirit of The Lord of the
// Rings Online's deed log, map, minstrel, virtues and lore book. Opened from the front screen.
//   Deeds    every game's achievements (their achievements.txt), the counted ones with their progress; a title
//            chosen from the deeds earned, shown on the front screen
//   Map      the places the levels are set in, on a drawing of Middle-earth, lit as their levels are completed,
//            with the road the three games walk between them
//   Minstrel The Fellowship of the Ring's music by name, and The Two Towers' menu theme, from the player's copies
//   Virtues  opt-in, off by default: Valour (enemies defeated) adds to the damage your hits do, Fortitude (levels
//            completed) takes from the damage you take; passed to the game started (ATHELAS_VIRTUES)
//   Lore     a page for each level, opened by completing it
// Nothing is written into the games' own files; [Journey] in Athelas.ini keeps the title and the Virtues switch.

struct JDeed { std::string id, title, desc, when; };
struct JFile { std::vector<JDeed> deeds; unsigned long long kills = 0; std::map<std::string, unsigned long long> counters; bool found = false; };

// each game's file, read again at most every 2 seconds
static const JFile &j_read(const std::string &path)
{
    struct C { JFile f; double at = -10; };
    static std::map<std::string, C> cache;
    C &c = cache[path];
    if (ImGui::GetTime() - c.at < 2.0) return c.f;
    c.at = ImGui::GetTime();
    JFile f;
    if (FILE *fp = fopen(path.c_str(), "rb")) {
        f.found = true;
        std::map<std::string, std::string> when;
        char line[512], sec[32] = "";
        while (fgets(line, sizeof line, fp)) {
            std::string l = line;
            while (!l.empty() && (l.back() == '\r' || l.back() == '\n' || l.back() == ' ')) l.pop_back();
            size_t a0 = l.find_first_not_of(" \t");
            if (a0 == std::string::npos || l[a0] == ';') continue;
            l = l.substr(a0);
            if (l[0] == '[') { snprintf(sec, sizeof sec, "%s", l.c_str()); continue; }
            size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string k = l.substr(0, eq), v = l.substr(eq + 1);
            while (!k.empty() && k.back() == ' ') k.pop_back();
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
            if (!strcmp(sec, "[unlocked]")) when[k] = v;
            else if (!strcmp(sec, "[counters]")) {
                f.counters[k] = strtoull(v.c_str(), nullptr, 10);
                if (k == "enemies_defeated") f.kills = f.counters[k];
            }
            else if (!strcmp(sec, "[list]")) {
                size_t bar = v.find(" | ");
                f.deeds.push_back({ k, bar == std::string::npos ? v : v.substr(0, bar), bar == std::string::npos ? "" : v.substr(bar + 3), "" });
            }
        }
        fclose(fp);
        for (auto &d : f.deeds) { auto w = when.find(d.id); if (w != when.end()) d.when = w->second; }
    }
    c.f = f;
    return c.f;
}

// the games, in the order of the story: their key, name and achievements file
struct JGame { const char *key, *name; std::string path; };
static std::vector<JGame> j_games(const App &a)
{
    std::vector<JGame> v;
    v.push_back({ "fotr", "The Fellowship of the Ring", port_data_dir(DIR_FOTR_XBOX, nullptr) + "\\achievements.txt" });
    v.push_back({ "tt", "The Two Towers", port_data_dir(DIR_TT_XBOX, "TwoTowers") + "\\achievements.txt" });
    v.push_back({ "rotk", "The Return of the King (Xbox)", rx_data_dir() + "\\achievements.txt" });
    v.push_back({ "ta", "The Third Age", port_data_dir(DIR_TA_XBOX, nullptr) + "\\achievements.txt" });
    for (auto &g : a.lib)
        if (g.kind == GK_ROTK_PC) { v.push_back({ "rotk", "The Return of the King (PC)", g.path + "\\ROTKFix\\achievements.txt" }); break; }
    return v;
}
static bool j_has(const App &a, const char *game, const char *id)
{
    for (auto &g : j_games(a)) {
        if (strcmp(g.key, game)) continue;
        for (auto &d : j_read(g.path).deeds) if (d.id == id && !d.when.empty()) return true;
    }
    return false;
}

// ---- the places and the levels set in them
struct JPlace { const char *name; float x, y; bool above; };   // x, y: 0-1 across the map; the name above it
static const JPlace k_jPlaces[] = {
    { "The Shire", 0.100f, 0.240f, false },              // 0
    { "The Old Forest", 0.165f, 0.300f, false },         // 1
    { "Bree", 0.215f, 0.210f, true },                   // 2
    { "Weathertop", 0.280f, 0.200f, true },             // 3
    { "Rivendell", 0.420f, 0.170f, true },              // 4
    { "Moria", 0.450f, 0.420f, false },                 // 5
    { "Lothl\xc3\xb3rien", 0.530f, 0.420f, true },      // 6
    { "Amon Hen", 0.665f, 0.600f, true },               // 7
    { "Fangorn", 0.500f, 0.530f, false },                // 8
    { "Rohan", 0.530f, 0.680f, false },                  // 9
    { "Isengard", 0.390f, 0.600f, true },               // 10
    { "Helm's Deep", 0.350f, 0.735f, false },           // 11
    { "Dunharrow", 0.460f, 0.760f, false },             // 12
    { "Osgiliath", 0.750f, 0.790f, true },            // 13
    { "Minas Tirith", 0.705f, 0.830f, false },         // 14
    { "Cirith Ungol", 0.835f, 0.800f, true },          // 15
    { "The Black Gate", 0.800f, 0.580f, true },        // 16
    { "Mount Doom", 0.910f, 0.720f, false },             // 17
    { "Eregion", 0.395f, 0.330f, false },               // 18 (The Third Age)
};
struct JLevel { const char *game, *id, *name; int place; bool road; const char *lore; };
// in the order the story walks them (road: on the road drawn between the places; the Two Towers' prologue is the
// war of long ago, so it stands apart)
static const JLevel k_jLevels[] = {
    { "fotr", "region_shire", "The Shire", 0, true,
      "Bag End stands quiet under the Hill, but the Black Riders are already asking after Baggins. Frodo leaves with "
      "the Ring, Sam at his side, before the road can be closed to him." },
    { "fotr", "region_old_forest", "The Old Forest", 1, true,
      "The trees of the Old Forest remember older things than hobbits, and do not love them. Beyond lie the "
      "Barrow-downs, where the dead of a lost kingdom keep their gold." },
    { "fotr", "region_bree", "Bree", 2, true,
      "At the Prancing Pony the hobbits meet a ranger the Bree-folk call Strider. He knows the wild roads, and the "
      "Riders are close behind." },
    { "fotr", "region_weathertop", "Weathertop", 3, true,
      "On the ruined watchtower of Amon S\xc3\xbbl the Nazg\xc3\xbbl find them. Frodo is struck by a Morgul blade, "
      "and the race to Rivendell begins." },
    { "fotr", "region_rivendell", "Rivendell", 4, true,
      "In the house of Elrond the Council decides the Ring must be destroyed. Nine walkers set out south, against "
      "the Nine riders." },
    { "fotr", "region_moria", "The Mines of Moria", 5, true,
      "The dwarf-kingdom of Khazad-d\xc3\xbbm lies dark and full of goblins. In its deeps something older wakes, and "
      "on the bridge Gandalf stands against it." },
    { "fotr", "region_lothlorien", "Lothl\xc3\xb3rien", 6, true,
      "The Golden Wood of Galadriel gives the Fellowship rest and gifts. In her mirror Frodo sees what will be if "
      "he fails." },
    { "tt", "level_prologue", "Prologue", 17, false,
      "Long ago, on the slopes of Orodruin, the Last Alliance of Elves and Men broke Sauron's host, and Isildur took "
      "the Ring from his hand." },
    { "tt", "level_weathertop", "Weathertop", 3, false,
      "The Two Towers tells the Fellowship's road again in Aragorn's memory: the night on Weathertop against the "
      "Ringwraiths." },
    { "tt", "level_gates_of_moria", "Gates of Moria", 5, false,
      "Before the Doors of Durin the Watcher in the Water stirs, and the Fellowship has no road but into the dark." },
    { "tt", "level_balins_tomb", "Balin's Tomb", 5, false,
      "In the Chamber of Mazarbul lies Balin, Lord of Moria, and the last pages of his people's book. Drums sound in "
      "the deep." },
    { "tt", "level_amon_hen", "Amon Hen", 7, true,
      "At the Seat of Seeing the Fellowship breaks. Uruk-hai of Isengard take Merry and Pippin, and Boromir falls "
      "defending them." },
    { "tt", "level_fangorn", "Fangorn Forest", 8, true,
      "Under the eaves of Fangorn the hunters find no hobbits, but a white rider. The forest itself is waking." },
    { "tt", "level_plains_of_rohan", "Plains of Rohan", 9, true,
      "The Riddermark burns at its borders as Saruman's orcs raid its villages. The Three Hunters ride for Edoras." },
    { "tt", "level_westfold", "The Westfold", 9, true,
      "The folk of the Westfold flee towards the Hornburg, and the wolf-riders of Isengard follow them." },
    { "tt", "level_gap_of_rohan", "Gap of Rohan", 10, true,
      "Between the mountains Isengard's army marches south, ten thousand strong, towards Helm's Deep." },
    { "tt", "level_deeping_wall", "Helm's Deep: The Deeping Wall", 11, true,
      "Night, rain and ladders on the Deeping Wall. Elves of L\xc3\xb3rien stand with the men of Rohan." },
    { "tt", "level_breached_wall", "Helm's Deep: Breached Wall", 11, true,
      "Fire from Isengard breaks the culvert, and the Uruk-hai pour through the breach." },
    { "tt", "level_hornburg_courtyard", "Helm's Deep: Hornburg Courtyard", 11, true,
      "At dawn the king rides out, and on the hill to the east a white rider appears with the Rohirrim." },
    { "rotk", "level_helms_deep", "Helm's Deep", 11, true,
      "The battle at the Hornburg ends as Gandalf comes down the slope with the light behind him." },
    { "rotk", "level_road_to_isengard", "Road to Isengard", 10, true,
      "Isengard is drowned, the Ents keep watch, and Saruman is shut in his tower. The road ahead turns east." },
    { "rotk", "level_osgiliath", "Escape from Osgiliath", 13, true,
      "Faramir's men hold the ruined city on the river until they can hold it no longer." },
    { "rotk", "level_paths_of_the_dead", "Paths of the Dead", 12, true,
      "Under the mountain an oath-breaker host waits for the heir of Isildur to call it to its oath." },
    { "rotk", "level_king_of_the_dead", "King of the Dead", 12, true,
      "Their king will not bow until the sword that was broken is shown to him." },
    { "rotk", "level_southern_gate", "The Southern Gate", 14, true,
      "Minas Tirith, the city of kings, is besieged. Gandalf holds its southern gate." },
    { "rotk", "level_top_of_the_wall", "Top of the Wall", 14, true,
      "Siege towers rise against the walls of the White City, and its defenders hold the battlements." },
    { "rotk", "level_courtyard", "The Courtyard", 14, true,
      "The gate is broken, and the fight comes into the city itself." },
    { "rotk", "level_pelennor_fields", "Pelennor Fields", 14, true,
      "The Rohirrim come at dawn, and under the walls the greatest battle of the age is fought." },
    { "rotk", "level_shelobs_lair", "Shelob's Lair", 15, true,
      "In the pass above Minas Morgul, Gollum leads the hobbits into the dark of an ancient spider." },
    { "rotk", "level_cirith_ungol", "Cirith Ungol", 15, true,
      "Sam, alone with the Ring, climbs the tower of Cirith Ungol to bring his master out." },
    { "rotk", "level_black_gate", "The Black Gate", 16, true,
      "The Captains of the West march to the Morannon so that the Eye looks at them and not at the mountain." },
    { "rotk", "level_crack_of_doom", "The Crack of Doom", 17, true,
      "At the end of all things, on the fire of Orodruin, the Quest is decided." },
    // The Third Age: a company of its own walking beside the Fellowship (road: false, the drawn road is the
    // Fellowship's); the last area's deed is reaching it
    { "ta", "area_eregion", "Eregion", 18, false,
      "Berethor of Gondor rides north on a quest of his own, and in the ruins of the elven-smiths' land he meets "
      "the companions he will fight beside." },
    { "ta", "area_western_moria", "Western Moria", 5, false,
      "Following the Fellowship's trail, the company goes down into the dark halls of Khazad-d\xc3\xbbm." },
    { "ta", "area_eastern_moria", "Eastern Moria", 5, false,
      "Deeper in the mines, the drums of Moria sound, and the way out is through the orcs that answer them." },
    { "ta", "area_eastemnet_gullies", "Eastemnet Gullies", 9, false,
      "In the gullies of the East Emnet, the company fights beside the Riders of Rohan." },
    { "ta", "area_plains_of_rohan", "Plains of Rohan", 9, false,
      "Across the open grass of the Riddermark, Saruman's hosts are on the march." },
    { "ta", "area_helms_deep", "Helm's Deep", 11, false,
      "Behind the Deeping Wall the people of Rohan make their stand, and the company stands with them." },
    { "ta", "area_osgiliath", "Osgiliath", 13, false,
      "The ruined city on the Anduin is all that holds the river crossing against Mordor." },
    { "ta", "area_minas_tirith", "Minas Tirith", 14, false,
      "The White City prepares for war, and the company comes to defend it." },
    { "ta", "area_pelennor_fields", "Pelennor Fields", 14, false,
      "On the fields before the city, the company joins the great battle of the age." },
};
static const int k_jLevelCount = (int)(sizeof k_jLevels / sizeof k_jLevels[0]);
static bool j_done(const App &a, const JLevel &l) { return j_has(a, l.game, l.id); }

// a counter summed over every game's file
static unsigned long long j_count(const App &a, const std::string &name)
{
    unsigned long long n = 0;
    for (auto &g : j_games(a)) {
        const JFile &f = j_read(g.path);
        auto it = f.counters.find(name);
        if (it != f.counters.end()) n += it->second;
    }
    return n;
}
// how many of these deeds are earned, each once (the kill deeds are in every game: earning one in two games is
// still one)
static int j_deeds_of(const App &a, std::initializer_list<const char *> ids)
{
    int n = 0;
    for (const char *id : ids) {
        bool got = false;
        for (auto &g : j_games(a))
            for (auto &d : j_read(g.path).deeds)
                if (!d.when.empty() && d.id == id) got = true;
        n += got;
    }
    return n;
}

// ---- Quests: three a day, two a week, picked from the date, counted from the games' counters
enum { QK_KILLS, QK_KILLS_AT, QK_LEVELS, QK_LEVEL_AT };
struct JQuest { int kind, place; unsigned long long target; std::string text; };
static long j_today()
{
    time_t t = time(nullptr);
    struct tm l = *localtime(&t), g = *gmtime(&t);
    long off = (long)difftime(mktime(&l), mktime(&g));
    return (long)((t + off) / 86400);
}
static long j_week() { return (j_today() + 3) / 7; }   // weeks start on Monday (1 January 1970 was a Thursday)
static std::vector<int> j_quest_places(const App &a)   // places of levels in the games the player has played
{
    std::vector<int> v;
    for (int i = 0; i < k_jLevelCount; i++) {
        bool has = false;
        for (auto &g : j_games(a)) if (!strcmp(g.key, k_jLevels[i].game) && j_read(g.path).found) has = true;
        if (has && std::find(v.begin(), v.end(), k_jLevels[i].place) == v.end()) v.push_back(k_jLevels[i].place);
    }
    return v;
}
static std::vector<JQuest> j_quests(const App &a, bool weekly)
{
    std::vector<JQuest> q;
    uint32_t seed = weekly ? (uint32_t)j_week() * 7919u : (uint32_t)j_today() * 104729u;   // (unsigned: no overflow)
    auto rnd = [&seed]() { seed = (seed * 1103515245u + 12345u) & 0x7FFFFFFFu; return (int)(seed >> 8); };
    std::vector<int> places = j_quest_places(a);
    if (weekly) {
        q.push_back({ QK_KILLS, -1, 1000, "Defeat 1,000 enemies in any of the games" });
        q.push_back({ QK_LEVELS, -1, 5, "Complete 5 levels" });
        return q;
    }
    q.push_back({ QK_KILLS, -1, 150, "Defeat 150 enemies in any of the games" });
    if (!places.empty()) {
        int p = places[rnd() % places.size()];
        q.push_back({ QK_KILLS_AT, p, 60, std::string("Defeat 60 enemies at ") + k_jPlaces[p].name });
        int r = places[rnd() % places.size()];
        q.push_back({ QK_LEVEL_AT, r, 1, std::string("Complete a level at ") + k_jPlaces[r].name });
    } else q.push_back({ QK_LEVELS, -1, 1, "Complete any level" });
    return q;
}
static unsigned long long j_quest_value(const App &a, const JQuest &q)
{
    if (q.kind == QK_KILLS) return j_count(a, "enemies_defeated");
    if (q.kind == QK_LEVELS) return j_count(a, "levels_completed");
    unsigned long long n = 0;
    for (int i = 0; i < k_jLevelCount; i++)
        if (k_jLevels[i].place == q.place)
            n += j_count(a, std::string(q.kind == QK_KILLS_AT ? "kills@" : "completed@") + k_jLevels[i].id);
    return n;
}
static int j_quests_done() { return atoi(ini_get("Quests", "Done", "0").c_str()); }
// a new day or week takes the counters as they are now; a quest reached is counted once
static void j_quests_tick(const App &a)
{
    for (int w = 0; w < 2; w++) {
        const char *P = w ? "W" : "D";
        long period = w ? j_week() : j_today();
        std::vector<JQuest> qs = j_quests(a, w != 0);
        char key[16];
        bool fresh = atol(ini_get("Quests", P, "0").c_str()) != period;
        if (fresh) ini_put("Quests", P, std::to_string(period));
        for (size_t i = 0; i < 3; i++) {
            // each slot keeps which quest it counts for: a new day, or a different quest in it (the list changes when
            // a game is played for the first time), starts it again from the counters as they are now
            std::string id = i < qs.size() ? std::to_string(qs[i].kind) + ":" + std::to_string(qs[i].place) : "-";
            snprintf(key, sizeof key, "%sk%zu", P, i);
            if (!fresh && ini_get("Quests", key, "") == id) continue;
            ini_put("Quests", key, id);
            snprintf(key, sizeof key, "%sb%zu", P, i); ini_put("Quests", key, i < qs.size() ? std::to_string(j_quest_value(a, qs[i])) : "0");
            snprintf(key, sizeof key, "%sd%zu", P, i); ini_put("Quests", key, "0");
        }
        for (size_t i = 0; i < qs.size(); i++) {
            snprintf(key, sizeof key, "%sd%zu", P, i);
            if (ini_get("Quests", key, "0") == "1") continue;
            char bk[16]; snprintf(bk, sizeof bk, "%sb%zu", P, i);
            unsigned long long base = strtoull(ini_get("Quests", bk, "0").c_str(), nullptr, 10), now = j_quest_value(a, qs[i]);
            if (now >= base + qs[i].target) {
                ini_put("Quests", key, "1");
                ini_put("Quests", "Done", std::to_string(j_quests_done() + 1));
            }
        }
    }
}
struct JQuestTitle { int n; const char *title; };
static const JQuestTitle k_jQuestTitles[] = { { 10, "Trusted Errand-rider" }, { 25, "Friend of the Free Peoples" }, { 50, "Hero of the West" } };

// ---- Virtues, as in The Lord of the Rings Online: each ranked by a kind of deed, a few slotted for their full
// bonus, the rest giving a little. Our games let a virtue change only two things: the damage your hits do (dealt) and
// the damage you take (taken), so each virtue is a mix of those two.
struct JVirtue { const char *key, *name, *from; int max; float dealt, taken; };   // per rank, slotted, percent
static const JVirtue k_jVirtues[] = {
    { "valour", "Valour", "the kill deeds and Fate's deeds", 7, 2.0f, 0.5f },
    { "fortitude", "Fortitude", "levels completed (a rank for every 3)", 10, 0.0f, 2.0f },
    { "determination", "Determination", "the great deeds: a whole story, every hero, the secret mission", 5, 1.0f, 1.0f },
    { "zeal", "Zeal", "the heroes of The Return of the King", 10, 1.5f, 0.0f },
    { "fidelity", "Fidelity", "co-op and quests (a rank for every 3 quests)", 10, 0.5f, 1.0f },
};
static const int k_jVirtueCount = 5, k_jSlots = 3;
static int j_virtue_rank(const App &a, int v)
{
    int r = 0;
    switch (v) {
    case 0: r = j_deeds_of(a, { "kills_100", "kills_250", "kills_500", "kills_1000", "kills_5000", "fate_one", "fate_all" }); break;
    case 1: { int n = 0; for (int i = 0; i < k_jLevelCount; i++) n += j_done(a, k_jLevels[i]); r = n / 3; break; }
    case 2: r = j_deeds_of(a, { "story_complete", "every_level", "secret_mission", "every_level_every_hero" }); break;
    case 3: r = j_deeds_of(a, { "hero_aragorn", "hero_legolas", "hero_gimli", "hero_gandalf", "hero_sam", "hero_frodo",
                                "hero_pippin", "hero_merry", "hero_faramir", "hero_level_10" }); break;
    case 4: r = (j_deeds_of(a, { "coop" }) ? 1 : 0) + j_quests_done() / 3; break;
    }
    return std::min(r, k_jVirtues[v].max);
}
static bool j_slotted(int v)
{
    std::string s = "," + ini_get("Journey", "Slots", "valour,fortitude,determination") + ",";
    return s.find(std::string(",") + k_jVirtues[v].key + ",") != std::string::npos;
}
static void j_set_slot(int v, bool on)
{
    std::string out;
    for (int i = 0; i < k_jVirtueCount; i++)
        if (i == v ? on : j_slotted(i)) out += (out.empty() ? "" : ",") + std::string(k_jVirtues[i].key);
    ini_put("Journey", "Slots", out);
}
struct JVirtues { int dealt = 100, taken = 100; };
static JVirtues j_virtues(const App &a)
{
    float d = 0, t = 0;
    for (int v = 0; v < k_jVirtueCount; v++) {
        int r = j_virtue_rank(a, v);
        if (j_slotted(v)) { d += r * k_jVirtues[v].dealt; t += r * k_jVirtues[v].taken; }
        else d += r * 0.2f;   // unslotted: a little, as LOTRO's unslotted virtues give
    }
    JVirtues out;
    out.dealt = 100 + (int)std::min(30.0f, d + 0.5f);   // at most +30%
    out.taken = 100 - (int)std::min(30.0f, t + 0.5f);   // at most -30%
    return out;
}
static bool j_virtues_on() { return ini_get("Journey", "Virtues", "0") == "1"; }
static bool j_fate_on() { return ini_get("Journey", "Fate", "0") == "1"; }
static bool j_fellowship_on() { return ini_get("Journey", "Fellowship", "0") == "1"; }
// what the game started next is told (every frame; the files are read every 2 seconds at most)
static void journey_env(const App &a)
{
    static double at = -10;
    if (ImGui::GetTime() - at >= 1.0) { at = ImGui::GetTime(); j_quests_tick(a); }
    // the switches every frame (the files behind them are cached): Play pressed right after a change starts the
    // game with it
    static std::string last = "-";
    std::string want;
    if (j_virtues_on() || j_fate_on()) {
        JVirtues v = j_virtues_on() ? j_virtues(a) : JVirtues();
        if (j_fate_on()) { v.dealt = v.dealt * 80 / 100; v.taken = v.taken * 150 / 100; }   // Fate: tougher enemies
        want = std::to_string(v.dealt) + "," + std::to_string(v.taken);
    }
    if (want != last) { last = want; SetEnvironmentVariableA("ATHELAS_VIRTUES", want.empty() ? NULL : want.c_str()); }
    SetEnvironmentVariableA("ATHELAS_FATE", j_fate_on() ? "1" : NULL);
    SetEnvironmentVariableA("ATHELAS_FELLOWSHIP", j_fellowship_on() ? "1" : NULL);
}

// the title chosen, for the front screen ("" for none)
static std::string journey_title() { return ini_get("Journey", "Title", ""); }

// ---- the tabs
static void j_deeds(App &a)
{
    std::string title = journey_title();
    section("Title");
    {
        std::vector<std::string> earned;
        for (auto &g : j_games(a))
            for (auto &d : j_read(g.path).deeds)
                if (!d.when.empty() && std::find(earned.begin(), earned.end(), d.title) == earned.end()) earned.push_back(d.title);
        for (auto &qt : k_jQuestTitles) if (j_quests_done() >= qt.n) earned.push_back(qt.title);
        row_begin("Your title", "Shown under Athelas on the front screen. Every deed you've earned in any of the games "
                                "gives you its name as a title.");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##title", title.empty() ? "(none)" : title.c_str())) {
            if (ImGui::Selectable("(none)", title.empty())) ini_put("Journey", "Title", "");
            for (auto &t : earned) if (ImGui::Selectable(t.c_str(), t == title)) ini_put("Journey", "Title", t);
            ImGui::EndCombo();
        }
        row_end();
        if (earned.empty()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(C_DIM, "Earn a deed in any of the games to take its name as your title.");
            ImGui::PopTextWrapPos();
        }
    }
    for (auto &g : j_games(a)) {
        const JFile &f = j_read(g.path);
        int got = 0;
        for (auto &d : f.deeds) got += !d.when.empty();
        char head[128];
        if (f.deeds.empty()) snprintf(head, sizeof head, "%s", g.name);
        else snprintf(head, sizeof head, "%s  -  %d of %d", g.name, got, (int)f.deeds.size());
        section(head);
        ImGui::PushTextWrapPos(0);
        if (f.deeds.empty()) {
            ImGui::TextColored(C_DIM, "Play the game once and its deeds appear here.");
            ImGui::PopTextWrapPos();
            continue;
        }
        if (f.kills) ImGui::TextColored(C_DIM, "Enemies defeated: %llu", f.kills);
        for (auto &d : f.deeds) {
            bool on = !d.when.empty();
            ImGui::TextColored(on ? C_GOLD_H : C_DIM, "%s %s", on ? "+" : "-", d.title.c_str());
            if (on) { ImGui::SameLine(); ImGui::PushFont(F_SMALL, F_SMALL->LegacySize); ImGui::TextColored(C_DIM, "  %s", d.when.c_str()); ImGui::PopFont(); }
            ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
            ImGui::Indent(S(18));
            ImGui::TextColored(C_DIM, "%s", d.desc.c_str());
            // the counted deeds: how far along
            unsigned long long target = d.id == "kills_100" ? 100 : d.id == "kills_250" ? 250 : d.id == "kills_500" ? 500
                                     : d.id == "kills_1000" ? 1000 : d.id == "kills_5000" ? 5000 : 0;
            if (target && !on) {
                char ov[48];
                snprintf(ov, sizeof ov, "%llu / %llu", std::min(f.kills, target), target);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, C_GOLD);
                ImGui::ProgressBar((float)std::min(f.kills, target) / (float)target, ImVec2(S(260), S(20)), ov);
                ImGui::PopStyleColor();
            }
            ImGui::Unindent(S(18));
            ImGui::PopFont();
        }
        ImGui::PopTextWrapPos();
    }
}

// ---- the map: parchment made here, The Two Towers' own map pieces laid on it where they belong (read from the
// player's copy: UI\mainmenu.BFF, the Misty Mountains to Lorien and Rohan to the White Mountains), our ink around
static SDL_Renderer *g_jRen = nullptr;          // set by draw_journey
struct JTile { unsigned long long hdr; float x0, y0, w, h; };   // where a piece lies on the map (0-1)
static const JTile k_jTiles[] = {
    { 0x119F80, 0.32f, 0.27f, 0.42f, 0.41f },    // the Misty Mountains, Lorien, Fangorn, the Wold
    { 0x12AF80, 0.257f, 0.515f, 0.42f, 0.41f },  // Rohan and the White Mountains
};
static bool g_jHasTiles = false;
static bool j_under_tile(float x, float y)        // the game's own drawing is there
{
    if (!g_jHasTiles) return false;
    for (auto &t : k_jTiles)
        if (x > t.x0 + t.w * 0.08f && x < t.x0 + t.w * 0.92f && y > t.y0 + t.h * 0.10f && y < t.y0 + t.h * 0.90f) return true;
    return false;
}
// the coast, west and south: the sea lies to its left / below
static const float k_jCoast[][2] = { { 0.07f, 0.00f }, { 0.04f, 0.10f }, { 0.07f, 0.22f }, { 0.05f, 0.34f }, { 0.09f, 0.46f },
                                     { 0.13f, 0.58f }, { 0.17f, 0.70f }, { 0.25f, 0.82f }, { 0.36f, 0.90f }, { 0.50f, 0.93f },
                                     { 0.60f, 0.97f }, { 0.64f, 1.00f } };
static const int k_jCoastN = (int)(sizeof k_jCoast / sizeof k_jCoast[0]);
static bool j_sea(float x, float y)
{
    for (int i = 0; i + 1 < k_jCoastN; i++) {
        float y0 = k_jCoast[i][1], y1 = k_jCoast[i + 1][1];
        if (y >= y0 && y <= y1) {
            float t = y1 > y0 ? (y - y0) / (y1 - y0) : 0, cx = k_jCoast[i][0] + (k_jCoast[i + 1][0] - k_jCoast[i][0]) * t;
            return x < cx;
        }
    }
    return false;
}
// (unsigned all through: an int that overflows is undefined, and the compiler made a loop of it never end)
static float j_hash(int x, int y, int s)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)s * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}
static float j_noise(float x, float y, int s)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = j_hash(xi, yi, s), b = j_hash(xi + 1, yi, s), c = j_hash(xi, yi + 1, s), d = j_hash(xi + 1, yi + 1, s);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float j_fbm(float x, float y, int s) { float v = 0, amp = 0.5f; for (int o = 0; o < 5; o++) { v += amp * j_noise(x, y, s + o); x *= 2.03f; y *= 2.03f; amp *= 0.5f; } return v; }

static SDL_Texture *j_map_texture(App &a)
{
    static SDL_Texture *tex = nullptr;
    static std::string builtFor = "-";
    int tt = fr_first(a, FR_TT, GK_TT_XBOX);
    std::string ttDir = tt >= 0 ? a.lib[tt].path : std::string();
    if (const char *t = getenv("ATHELAS_MAP_TT")) ttDir = t;   // testing: a Two Towers folder
    if (tex && builtFor == ttDir) return tex;
    if (!g_jRen) return nullptr;
    builtFor = ttDir;
    if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
    const int W = 1024, H = 512;
    std::vector<uint8_t> px((size_t)W * H * 4);
    // the game's pieces, if there's a copy of The Two Towers
    struct Pic { unsigned char *p = nullptr; int w = 0, h = 0; float mr = 0, mg = 0, mb = 0; };
    Pic pics[2];
    g_jHasTiles = false;
    if (!ttDir.empty())
        for (int i = 0; i < 2; i++) {
            std::string entry;
            DWORD at = GetFileAttributesA(ttDir.c_str());
            if (at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_DIRECTORY)) {
                unsigned long long off = 0; uint32_t sz = 0;
                if (iso_locate(ttDir, "UI\\mainmenu.BFF", off, sz)) entry = "\002tt:" + std::to_string(off + k_jTiles[i].hdr) + ":" + ttDir;
            } else entry = "\002tt:" + std::to_string(k_jTiles[i].hdr) + ":" + ttDir + "\\UI\\mainmenu.BFF";
                    if (!entry.empty()) pics[i].p = tt_picture_load(entry, pics[i].w, pics[i].h);
                    if (pics[i].p) {
                double r = 0, g = 0, b = 0; long n = 0;   // its colour, away from the burnt edges
                for (int y = pics[i].h / 4; y < pics[i].h * 3 / 4; y += 2)
                    for (int x = pics[i].w / 4; x < pics[i].w * 3 / 4; x += 2) {
                        const unsigned char *q = pics[i].p + ((size_t)y * pics[i].w + x) * 4;
                        r += q[0]; g += q[1]; b += q[2]; n++;
                    }
                pics[i].mr = (float)(r / n); pics[i].mg = (float)(g / n); pics[i].mb = (float)(b / n);
                g_jHasTiles = true;
                        }
        }
    const float pr = 116, pg = 91, pb = 54;   // the parchment's own colour, near the game's
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
            float n = j_fbm(u * 9, v * 4.5f, 1), fib = j_noise(u * 220, v * 30, 7);
            float stain = j_fbm(u * 3, v * 1.5f, 11);
            float k = 0.80f + 0.32f * n + 0.05f * (fib - 0.5f) - (stain > 0.62f ? (stain - 0.62f) * 0.9f : 0);
            float r = pr * k, g = pg * k, b = pb * k;
            if (j_sea(u, v)) {   // the sea: darker, a little grey-green
                r = r * 0.62f + 14; g = g * 0.66f + 18; b = b * 0.70f + 18;
            }
            // the game's pieces, their burnt edges left out, their colour brought to the parchment's
            for (int i = 0; i < 2; i++) {
                if (!pics[i].p) continue;
                const JTile &t = k_jTiles[i];
                float tu = (u - t.x0) / t.w, tv = (v - t.y0) / t.h;
                if (tu <= 0 || tu >= 1 || tv <= 0 || tv >= 1) continue;
                float sx = tu * (pics[i].w - 1), sy = tv * (pics[i].h - 1);
                int ix = (int)sx, iy = (int)sy;
                float fx = sx - ix, fy = sy - iy;
                float c[3];
                for (int ch = 0; ch < 3; ch++) {
                    auto P2 = [&](int xx, int yy) { xx = std::min(xx, pics[i].w - 1); yy = std::min(yy, pics[i].h - 1); return (float)pics[i].p[((size_t)yy * pics[i].w + xx) * 4 + ch]; };
                    c[ch] = (P2(ix, iy) * (1 - fx) + P2(ix + 1, iy) * fx) * (1 - fy) + (P2(ix, iy + 1) * (1 - fx) + P2(ix + 1, iy + 1) * fx) * fy;
                }
                float lum = 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
                float edge = std::min(std::min(tu, 1 - tu) / 0.24f, std::min(tv, 1 - tv) / 0.30f);
                edge = edge * edge * (3 - 2 * std::min(1.0f, edge));
                edge = std::min(1.0f, edge) * (0.85f + 0.3f * j_noise(u * 40, v * 20, 3));
                float keep = lum < 35 ? 0 : lum < 60 ? (lum - 35) / 25 : 1;   // the black burn
                float al = std::max(0.0f, std::min(1.0f, edge)) * keep * 0.92f;
                // its ink and its light on our parchment: each pixel against the piece's own colour, a little stronger
                auto rel = [](float v, float m) { float q = v / std::max(1.0f, m); return q < 1 ? 1.0f + (q - 1.0f) * 1.3f : 1.0f + (q - 1.0f) * 0.45f; };
                float cr = r * rel(c[0], pics[i].mr), cg = g * rel(c[1], pics[i].mg), cb = b * rel(c[2], pics[i].mb);
                r = r + (cr - r) * al; g = g + (cg - g) * al; b = b + (cb - b) * al;
            }
            // the burnt, torn edge all round
            float d = std::min(std::min(u, 1 - u) * 2.0f, std::min(v, 1 - v)) + (j_fbm(u * 14, v * 7, 21) - 0.5f) * 0.05f;
            float burn = d < 0.045f ? std::max(0.0f, d) / 0.045f : 1;
            float alpha = d < 0.012f ? std::max(0.0f, d / 0.012f) : 1;
            r *= 0.25f + 0.75f * burn; g *= 0.22f + 0.78f * burn; b *= 0.2f + 0.8f * burn;
            uint8_t *o = &px[((size_t)y * W + x) * 4];
            o[0] = (uint8_t)std::max(0.0f, std::min(255.0f, r)); o[1] = (uint8_t)std::max(0.0f, std::min(255.0f, g));
            o[2] = (uint8_t)std::max(0.0f, std::min(255.0f, b)); o[3] = (uint8_t)(255 * alpha);
        }
    for (auto &p : pics) free(p.p);
    tex = SDL_CreateTexture(g_jRen, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, W, H);
    if (tex) {
        SDL_UpdateTexture(tex, NULL, px.data(), W * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    }
    return tex;
}

static void j_map(App &a)
{
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "The road of the three games. Places glow when you've completed a level there, and the road "
                              "you've walked is marked in red; point at a place to see its levels.");
    ImGui::PopTextWrapPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float w = avail.x, h = w * 0.515f;
    if (h > avail.y - S(6) && avail.y > S(200)) {   // the whole map in view: narrower, centred
        h = avail.y - S(6); w = h / 0.515f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - w) * 0.5f);
    }
    // its own window, so its own draw list (a whole map of ink is more than one list's 65,536 vertices)
    ImGui::BeginChild("##mapwin", ImVec2(w, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    ImVec2 p0 = ImGui::GetCursorScreenPos(), p1 = p0 + ImVec2(w, h);
    ImGui::InvisibleButton("##map", ImVec2(w, h));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    auto P = [&](float x, float y) { return ImVec2(p0.x + x * w, p0.y + y * h); };
    SDL_Texture *tex = j_map_texture(a);
    if (tex) dl->AddImage((ImTextureID)(intptr_t)tex, p0, p1);
    else dl->AddRectFilled(p0, p1, IM_COL32(116, 91, 54, 255), S(6));
    const ImU32 ink = IM_COL32(52, 33, 18, 215), inkSoft = IM_COL32(52, 33, 18, 120);
    const float u = w / 1000.0f;   // a unit of ink, with the map's size
    // the coast
    {
        ImVec2 pts[16];
        for (int i = 0; i < k_jCoastN; i++) pts[i] = P(k_jCoast[i][0], k_jCoast[i][1]);
        dl->AddPolyline(pts, k_jCoastN, ink, std::max(1.0f, 1.6f * u));
        for (int i = 0; i + 1 < k_jCoastN; i++)   // a second, fainter line off shore, as old maps draw it
            dl->AddLine(pts[i] - ImVec2(6 * u, 0), pts[i + 1] - ImVec2(6 * u, 0), inkSoft, std::max(1.0f, 0.8f * u));
        for (int k = 0; k < 6; k++) {   // waves
            ImVec2 c = P(0.025f + 0.012f * (k % 2), 0.55f + 0.07f * k);
            if (!j_sea((c.x - p0.x) / w + 0.01f, (c.y - p0.y) / h)) continue;
            dl->AddBezierQuadratic(c, c + ImVec2(5 * u, -4 * u), c + ImVec2(10 * u, 0), inkSoft, std::max(1.0f, u));
            dl->AddBezierQuadratic(c + ImVec2(10 * u, 0), c + ImVec2(15 * u, -4 * u), c + ImVec2(20 * u, 0), inkSoft, std::max(1.0f, u));
        }
    }
    // mountains, hand-drawn: a peak, its shaded side hatched
    auto peak = [&](float x, float y, float s) {
            if (j_under_tile(x, y)) return;
        ImVec2 c = P(x, y);
        float hh = s * u, ww = s * 0.75f * u;
        ImVec2 l = c + ImVec2(-ww, hh * 0.45f), t = c + ImVec2(0, -hh * 0.55f), r = c + ImVec2(ww, hh * 0.45f);
        dl->AddTriangleFilled(l, t, r, IM_COL32(150, 122, 80, 120));
        dl->AddLine(l, t, ink, std::max(1.0f, 1.3f * u));
        dl->AddLine(t, r, ink, std::max(1.0f, 1.3f * u));
        for (int k = 1; k <= 3; k++) {
            float f = k / 4.0f;
            ImVec2 a0 = t + (r - t) * f;
            dl->AddLine(a0, a0 + ImVec2(-ww * 0.35f * (1 - f), hh * 0.5f * (1 - f) + 2 * u), inkSoft, std::max(1.0f, 0.8f * u));
        }
    };
    auto range = [&](float x0, float y0, float x1, float y1, int n, float s) {
        for (int i = 0; i <= n; i++) {
            float t = (float)i / n, jx = (j_hash(i, n, 5) - 0.5f) * 0.012f, jy = (j_hash(n, i, 6) - 0.5f) * 0.02f;
            peak(x0 + (x1 - x0) * t + jx, y0 + (y1 - y0) * t + jy, s * (0.8f + 0.4f * j_hash(i, i, 9)));
        }
    };
    range(0.455f, 0.03f, 0.475f, 0.30f, 9, 19);     // the Misty Mountains (the game's piece goes on from here),
    range(0.485f, 0.06f, 0.50f, 0.29f, 7, 15);      // two ridges wide
    range(0.40f, 0.36f, 0.36f, 0.62f, 9, 15);       // (when there's no piece: the rest of them)
    range(0.30f, 0.78f, 0.70f, 0.80f, 18, 14);      // the White Mountains
    range(0.29f, 0.04f, 0.36f, 0.08f, 4, 11);       // the mountains of Angmar
    range(0.78f, 0.56f, 0.98f, 0.555f, 10, 14);     // Ered Lithui
    range(0.835f, 0.60f, 0.845f, 0.95f, 10, 14);    // Ephel Duath
    // Mount Doom
    {
        ImVec2 c = P(0.91f, 0.72f);
        float s = 13 * u;
        dl->AddTriangleFilled(c + ImVec2(-s, s * 0.6f), c + ImVec2(-s * 0.25f, -s * 0.7f), c + ImVec2(s, s * 0.6f), IM_COL32(110, 50, 30, 150));
        dl->AddLine(c + ImVec2(-s, s * 0.6f), c + ImVec2(-s * 0.25f, -s * 0.7f), ink, 1.4f * u);
        dl->AddLine(c + ImVec2(s * 0.25f, -s * 0.7f), c + ImVec2(s, s * 0.6f), ink, 1.4f * u);
        dl->AddLine(c + ImVec2(-s * 0.25f, -s * 0.7f), c + ImVec2(s * 0.25f, -s * 0.7f), ink, 1.4f * u);
    }
    // forests: little crowns, as Tolkien drew them
    auto forest = [&](float cx, float cy, float rx, float ry, int n) {
        for (int i = 0; i < n; i++) {
            float ang = j_hash(i, 1, 13) * 6.283f, rr = sqrtf(j_hash(i, 2, 14));
            float x = cx + cosf(ang) * rx * rr, y = cy + sinf(ang) * ry * rr;
            if (j_under_tile(x, y)) continue;
            ImVec2 c = P(x, y);
            float s = (3.2f + 1.5f * j_hash(i, 3, 15)) * u;
            dl->AddCircleFilled(c, s, IM_COL32(90, 98, 52, 140), 8);
            dl->AddCircle(c, s, ink, 8, std::max(1.0f, 0.9f * u));
            dl->AddLine(c + ImVec2(0, s), c + ImVec2(0, s + 2.5f * u), ink, std::max(1.0f, 0.9f * u));
        }
    };
    forest(0.165f, 0.27f, 0.025f, 0.035f, 14);      // the Old Forest
    forest(0.70f, 0.15f, 0.06f, 0.13f, 60);         // Mirkwood
    forest(0.50f, 0.53f, 0.035f, 0.05f, 22);        // Fangorn
    forest(0.53f, 0.41f, 0.025f, 0.035f, 12);       // Lothlorien
    // the Anduin, wavering a little
    {
        static const float an[][2] = { { 0.560f, 0.00f }, { 0.570f, 0.14f }, { 0.585f, 0.30f }, { 0.635f, 0.48f }, { 0.67f, 0.60f },
                                       { 0.72f, 0.70f }, { 0.745f, 0.79f }, { 0.725f, 0.90f }, { 0.66f, 0.985f } };
        const int n = (int)(sizeof an / sizeof an[0]);
        for (int i = 0; i + 1 < n; i++) {
            ImVec2 a0 = P(an[i][0], an[i][1]), a1 = P(an[i + 1][0], an[i + 1][1]);
            for (int k = 0; k < 8; k++) {
                float t0 = k / 8.0f, t1 = (k + 1) / 8.0f;
                float mx = an[i][0] + (an[i + 1][0] - an[i][0]) * (t0 + t1) * 0.5f, my = an[i][1] + (an[i + 1][1] - an[i][1]) * (t0 + t1) * 0.5f;
                if (j_under_tile(mx, my)) continue;
                ImVec2 q0 = a0 + (a1 - a0) * t0 + ImVec2(sinf((i * 8 + k) * 1.7f) * 1.5f * u, 0);
                ImVec2 q1 = a0 + (a1 - a0) * t1 + ImVec2(sinf((i * 8 + k + 1) * 1.7f) * 1.5f * u, 0);
                dl->AddLine(q0, q1, IM_COL32(44, 52, 60, 200), (1.2f + 1.2f * i / n) * u);
            }
        }
    }
    // the lands' names, in the old hand, spaced out
    auto land = [&](float x, float y, const char *name, float size) {
        if (j_under_tile(x, y)) return;
        std::string spaced;
        for (const char *c = name; *c; c++) { spaced += *c; if (c[1] && *c != ' ') spaced += ' '; }
        float sz = size * u;
        ImVec2 ts = F_TITLE->CalcTextSizeA(sz, FLT_MAX, 0, spaced.c_str());
        ImVec2 at = P(x, y) - ts * 0.5f;
        dl->AddText(F_TITLE, sz, at, IM_COL32(70, 42, 22, 150), spaced.c_str());
    };
    land(0.32f, 0.10f, "ANGMAR", 26);
    land(0.19f, 0.40f, "ERIADOR", 30);
    land(0.20f, 0.56f, "ENEDWAITH", 20);
    land(0.30f, 0.65f, "DUNLAND", 18);
    land(0.47f, 0.66f, "ROHAN", 34);
    land(0.56f, 0.90f, "GONDOR", 30);
    land(0.79f, 0.68f, "ITHILIEN", 14);
    land(0.93f, 0.86f, "MORDOR", 26);
    land(0.70f, 0.10f, "MIRKWOOD", 18);
    land(0.86f, 0.30f, "RHOVANION", 20);
    land(0.86f, 0.46f, "THE BROWN LANDS", 13);
    land(0.07f, 0.74f, "THE SEA", 14);
    // a compass, in the sea
    {
        ImVec2 c = P(0.055f, 0.88f);
        float s = 22 * u;
        for (int k = 0; k < 4; k++) {
            float an = k * 1.5708f;
            ImVec2 d(cosf(an), sinf(an)), q(-d.y, d.x);
            dl->AddTriangleFilled(c + d * s, c + q * (s * 0.22f), c, k == 3 ? IM_COL32(140, 40, 28, 220) : ink);
            dl->AddTriangle(c + d * s, c - q * (s * 0.22f), c, ink, std::max(1.0f, u));
        }
        dl->AddCircle(c, s * 0.45f, ink, 24, std::max(1.0f, u));
        ImVec2 ns = F_TITLE->CalcTextSizeA(12 * u, FLT_MAX, 0, "N");
        dl->AddText(F_TITLE, 12 * u, c + ImVec2(-ns.x * 0.5f, -s - ns.y), ink, "N");
    }
    // the road: each level on it in the story's order, red ink where it's been walked
    int prev = -1; bool prevDone = true;
    for (int i = 0; i < k_jLevelCount; i++) {
        const JLevel &l = k_jLevels[i];
        if (!l.road) continue;
        bool done = j_done(a, l);
        if (prev >= 0 && prev != l.place) {
            ImVec2 a0 = P(k_jPlaces[prev].x, k_jPlaces[prev].y), a1 = P(k_jPlaces[l.place].x, k_jPlaces[l.place].y);
            bool walked = prevDone && done;
            ImU32 col = walked ? IM_COL32(150, 28, 20, 235) : IM_COL32(60, 40, 24, 130);
            ImVec2 d = a1 - a0;
            float len = sqrtf(d.x * d.x + d.y * d.y), step = (walked ? 6.0f : 4.0f) * u;
            for (float t = 0; t < len; t += step * 2) {
                float t1 = std::min(len, t + step);
                dl->AddLine(a0 + d * (t / len), a0 + d * (t1 / len), col, (walked ? 2.4f : 1.4f) * u);
            }
        }
        prev = l.place; prevDone = done;
    }
    // the places
    int hover = -1;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    const int np = (int)(sizeof k_jPlaces / sizeof k_jPlaces[0]);
    float fs = std::max(S(13), 15.5f * u);
    for (int p = 0; p < np; p++) {
        int total = 0, done = 0;
        for (int i = 0; i < k_jLevelCount; i++) if (k_jLevels[i].place == p) { total++; done += j_done(a, k_jLevels[i]); }
        ImVec2 c = P(k_jPlaces[p].x, k_jPlaces[p].y);
        float r = 5.5f * u;
        bool lit = done > 0, all = done == total;
        if (lit) {
            dl->AddCircleFilled(c, r * 2.6f, IM_COL32(255, 214, 120, all ? 70 : 40));
            dl->AddCircleFilled(c, r, all ? IM_COL32(170, 30, 22, 255) : IM_COL32(190, 120, 40, 255));   // a seal of red wax
            dl->AddCircle(c, r, IM_COL32(240, 200, 110, 255), 0, 1.4f * u);
        } else {
            dl->AddCircleFilled(c, r * 0.8f, IM_COL32(150, 122, 80, 200));
            dl->AddCircle(c, r * 0.8f, ink, 0, 1.3f * u);
        }
        const char *nm = k_jPlaces[p].name;
        ImVec2 ts = F_BODY->CalcTextSizeA(fs, FLT_MAX, 0, nm);
        ImVec2 tp = c + ImVec2(-ts.x * 0.5f, k_jPlaces[p].above ? -r - 2 * u - ts.y : r + 2 * u);
        ImU32 halo = IM_COL32(140, 112, 70, 170);
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) dl->AddText(F_BODY, fs, tp + ImVec2((float)dx, (float)dy), halo, nm);
        dl->AddText(F_BODY, fs, tp, lit ? IM_COL32(40, 22, 10, 255) : IM_COL32(60, 42, 26, 200), nm);
        ImVec2 dm = mouse - c;
        if (ImGui::IsItemHovered() && dm.x * dm.x + dm.y * dm.y < r * r * 9) hover = p;
    }
    ImGui::EndChild();
    if (hover >= 0) {
        ImGui::BeginTooltip();
        ImGui::TextColored(C_GOLD_H, "%s", k_jPlaces[hover].name);
        for (int i = 0; i < k_jLevelCount; i++) {
            const JLevel &l = k_jLevels[i];
            if (l.place != hover) continue;
            bool d = j_done(a, l);
            const char *gn = !strcmp(l.game, "fotr") ? "Fellowship" : !strcmp(l.game, "tt") ? "Two Towers" : "Return of the King";
            ImGui::TextColored(d ? C_GOOD : C_DIM, "%s %s  (%s)", d ? "+" : "-", l.name, gn);
        }
        ImGui::EndTooltip();
    }
    if (!g_jHasTiles) {
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::TextColored(C_DIM, "Add The Two Towers and its own map of Rohan and the Misty Mountains is drawn in.");
        ImGui::PopFont();
    }
}

static std::string j_pretty(const std::string &file)
{
    std::string s = file;
    size_t dot = s.rfind('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    if (s.size() > 4 && !_strnicmp(s.c_str(), "MSX_", 4)) s = s.substr(4);
    for (char &c : s) if (c == '_') c = ' ';
    return s;
}

static void j_minstrel(App &a)
{
    std::string fx = fr_music_copies(a, FR_FOTR, GK_FOTR_XBOX), tt = fr_music_copies(a, FR_TT, GK_TT_XBOX),
                ta = fr_music_copies(a, FR_TA, GK_TA_XBOX);
    static std::vector<std::string> tracks;
    static std::string tracksFor = "-";
    if (tracksFor != fx) { tracksFor = fx; tracks = fx.empty() ? std::vector<std::string>() : music_fotr_list(fx); }
    std::string now = music_now();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "The games' own music, played from your copies of them. Choose a piece to play it; it "
                              "loops until you choose another or stop it.");
    ImGui::PopTextWrapPos();
    if (orn_plate_button("Stop##minstrel", ImVec2(S(110), S(30)), true)) music_play(MUSIC_NONE, std::string());
    section("The Two Towers");
    if (tt.empty()) ImGui::TextColored(C_DIM, "Add The Two Towers to hear its music.");
    else if (ImGui::Selectable("Main menu theme##ttmenu")) { music_enable(true); music_play(MUSIC_TT, tt); }
    section("The Third Age");
    if (ta.empty()) ImGui::TextColored(C_DIM, "Add The Third Age to hear its music.");
    else if (ImGui::Selectable("Main menu theme##tamenu")) { music_enable(true); music_play(MUSIC_TA, ta); }
    section("The Fellowship of the Ring");
    if (fx.empty()) ImGui::TextColored(C_DIM, "Add The Fellowship of the Ring to hear its music.");
    else if (tracks.empty()) ImGui::TextColored(C_DIM, "Its music archive (Scenes\\Media\\Music2.xpa) couldn't be read from your copy.");
    for (auto &t : tracks) {
        std::string lbl = j_pretty(t) + "##" + t;
        bool playing = t == now;
        if (playing) ImGui::PushStyleColor(ImGuiCol_Text, C_GOLD_H);
        if (ImGui::Selectable(lbl.c_str(), playing)) { music_enable(true); music_play_named(fx, t); }
        if (playing) ImGui::PopStyleColor();
    }
}

static void j_virtues_tab(App &a)
{
    JVirtues v = j_virtues(a);
    bool on = j_virtues_on();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "As in The Lord of the Rings Online, your deeds raise your virtues. Each virtue has ranks, "
                              "earned by a kind of deed. Slot up to three for their full bonus; the others still give a "
                              "little. Off by default, so each game plays as it was made.");
    ImGui::PopTextWrapPos();
    section("Virtues");
    row_begin("Use my virtues", "On: the game you start next gets your virtues, on top of its Damage settings. "
                                "Off: no change to any game.");
    if (ImGui::Checkbox("##virtues", &on)) ini_put("Journey", "Virtues", on ? "1" : "0");
    row_end();
    ImGui::TextColored(C_GOLD_H, "Now: your hits do %d%% of their damage, you take %d%%.", v.dealt, v.taken);
    int slotted = 0;
    for (int i = 0; i < k_jVirtueCount; i++) slotted += j_slotted(i);
    for (int i = 0; i < k_jVirtueCount; i++) {
        const JVirtue &vt = k_jVirtues[i];
        int r = j_virtue_rank(a, i);
        bool sl = j_slotted(i);
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::PushID(i);
        ImGui::BeginDisabled(!sl && slotted >= k_jSlots);
        if (ImGui::Checkbox("##slot", &sl)) j_set_slot(i, sl);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextColored(sl ? C_GOLD_H : C_TEXT, "%s", vt.name);
        ImGui::SameLine();
        ImGui::TextColored(C_DIM, "  rank %d of %d", r, vt.max);
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::Indent(S(30));
        char ov[16]; snprintf(ov, sizeof ov, "%d / %d", r, vt.max);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, C_GOLD);
        ImGui::ProgressBar((float)r / vt.max, ImVec2(S(220), S(20)), ov);
        ImGui::PopStyleColor();
        std::string give;
        char b[96];
        if (vt.dealt > 0) { snprintf(b, sizeof b, "+%g%% damage dealt", vt.dealt); give += b; }
        if (vt.taken > 0) { snprintf(b, sizeof b, "%s-%g%% damage taken", give.empty() ? "" : ", ", vt.taken); give += b; }
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C_DIM, "Each rank, slotted: %s. Unslotted: +0.2%% damage dealt a rank.", give.c_str());
        ImGui::TextColored(C_DIM, "Ranked by %s.", vt.from);
        ImGui::PopTextWrapPos();
        ImGui::Unindent(S(30));
        ImGui::PopFont();
        ImGui::PopID();
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "%d of %d slotted. All together at most +30%% damage dealt and -30%% damage taken.", slotted, k_jSlots);
    ImGui::PopTextWrapPos();

    section("Fate");
    bool fate = j_fate_on();
    row_begin("Fate", "A harder road, like LOTRO's Hard Mode: enemies hit 50% harder and take 25% more to bring down. "
                      "Every enemy counts twice towards the kill deeds, and two deeds of its own: Fated and Doom-sworn.");
    if (ImGui::Checkbox("##fate", &fate)) ini_put("Journey", "Fate", fate ? "1" : "0");
    row_end();
    section("Fellowship");
    bool fel = j_fellowship_on();
    row_begin("Fellowship bonus", "The Return of the King, co-op: while both players are fighting (each has landed a hit in "
                                  "the last 5 seconds), both players' hits do 10% more. As War in the North rewards "
                                  "fighting together.");
    if (ImGui::Checkbox("##fellowship", &fel)) ini_put("Journey", "Fellowship", fel ? "1" : "0");
    row_end();
}

static void j_quests_tab(App &a)
{
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "Three quests a day and two a week, as LOTRO's missions. Play any of the games with the "
                              "launcher open; progress counts from the start of the day or week.");
    ImGui::PopTextWrapPos();
    for (int w = 0; w < 2; w++) {
        section(w ? "This week" : "Today");
        const char *P = w ? "W" : "D";
        std::vector<JQuest> qs = j_quests(a, w != 0);
        for (size_t i = 0; i < qs.size(); i++) {
            char key[16];
            snprintf(key, sizeof key, "%sb%zu", P, i);
            unsigned long long base = strtoull(ini_get("Quests", key, "0").c_str(), nullptr, 10), now = j_quest_value(a, qs[i]);
            unsigned long long got = now > base ? std::min(now - base, qs[i].target) : 0;
            snprintf(key, sizeof key, "%sd%zu", P, i);
            bool done = ini_get("Quests", key, "0") == "1";
            ImGui::TextColored(done ? C_GOOD : C_TEXT, "%s %s", done ? "+" : "-", qs[i].text.c_str());
            ImGui::Indent(S(18));
            char ov[48]; snprintf(ov, sizeof ov, "%llu / %llu", done ? qs[i].target : got, qs[i].target);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, done ? C_GOOD : C_GOLD);
            ImGui::ProgressBar(done ? 1.0f : (float)got / (float)qs[i].target, ImVec2(S(260), S(20)), ov);
            ImGui::PopStyleColor();
            ImGui::Unindent(S(18));
        }
    }
    section("Quests completed");
    int n = j_quests_done();
    ImGui::Text("%d", n);
    for (auto &qt : k_jQuestTitles)
        ImGui::TextColored(n >= qt.n ? C_GOLD_H : C_DIM, "%s %d quests: the title %s", n >= qt.n ? "+" : "-", qt.n, qt.title);
}

// ---- Records: each level completed, from every game's records.txt
struct JRun { std::string when, id, game; long secs; unsigned long long kills; bool fate; };
static std::vector<JRun> j_runs(const App &a)
{
    std::vector<JRun> v;
    for (auto &g : j_games(a)) {
        std::string dir = g.path.substr(0, g.path.find_last_of('\\'));
        FILE *f = fopen((dir + "\\records.txt").c_str(), "rb");
        if (!f) continue;
        char line[256];
        while (fgets(line, sizeof line, f)) {
            if (line[0] == ';') continue;
            char when[24] = "", id[64] = "";
            long secs = -1; unsigned long long kills = 0; int fate = 0;
            if (sscanf(line, "%23[^|]| %63[^ |] | %ld | %llu | %d", when, id, &secs, &kills, &fate) >= 4) {
                std::string w = when;
                while (!w.empty() && w.back() == ' ') w.pop_back();
                v.push_back({ w, id, g.name, secs, kills, fate != 0 });
            }
        }
        fclose(f);
    }
    std::sort(v.begin(), v.end(), [](const JRun &x, const JRun &y) { return x.when > y.when; });
    return v;
}
static const char *j_level_name(const std::string &id)
{
    for (int i = 0; i < k_jLevelCount; i++) if (id == k_jLevels[i].id) return k_jLevels[i].name;
    if (id == "secret_mission") return "Tower of Orthanc";
    if (id == "region_river_anduin") return "The River Anduin";
    return id.c_str();
}
static std::string j_time(long s) { if (s < 0) return "-"; char b[24]; snprintf(b, sizeof b, "%ld:%02ld", s / 60, s % 60); return b; }
static void j_records(App &a)
{
    static std::vector<JRun> runs;
    static double at = -10;
    if (ImGui::GetTime() - at > 3.0) { at = ImGui::GetTime(); runs = j_runs(a); }
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "Each level you complete: how long it took and how many enemies you defeated, as "
                              "Conquest's battle results.");
    ImGui::PopTextWrapPos();
    if (runs.empty()) { ImGui::TextColored(C_DIM, "Complete a level in any of the games and it appears here."); return; }
    section("Best");
    if (ImGui::BeginTable("best", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Level"); ImGui::TableSetupColumn("Fastest"); ImGui::TableSetupColumn("Most enemies"); ImGui::TableSetupColumn("Times");
        ImGui::TableHeadersRow();
        std::vector<std::string> ids;
        for (auto &r : runs) if (std::find(ids.begin(), ids.end(), r.id) == ids.end()) ids.push_back(r.id);
        for (auto &id : ids) {
            long fast = -1; unsigned long long most = 0; int times = 0;
            for (auto &r : runs) if (r.id == id) { times++; if (r.secs >= 0 && (fast < 0 || r.secs < fast)) fast = r.secs; most = std::max(most, r.kills); }
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(j_level_name(id));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(j_time(fast).c_str());
            ImGui::TableNextColumn(); ImGui::Text("%llu", most);
            ImGui::TableNextColumn(); ImGui::Text("%d", times);
        }
        ImGui::EndTable();
    }
    section("Latest");
    if (ImGui::BeginTable("runs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("When"); ImGui::TableSetupColumn("Level"); ImGui::TableSetupColumn("Time"); ImGui::TableSetupColumn("Enemies");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < runs.size() && i < 25; i++) {
            const JRun &r = runs[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextColored(C_DIM, "%s", r.when.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%s%s", j_level_name(r.id), r.fate ? "  (Fate)" : "");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(j_time(r.secs).c_str());
            ImGui::TableNextColumn(); ImGui::Text("%llu", r.kills);
        }
        ImGui::EndTable();
    }
}

static void j_lore(App &a)
{
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(C_DIM, "A page for every level, written as you complete it.");
    const char *was = "";
    for (int i = 0; i < k_jLevelCount; i++) {
        const JLevel &l = k_jLevels[i];
        if (strcmp(was, l.game)) {
            was = l.game;
            section(!strcmp(l.game, "fotr") ? "The Fellowship of the Ring" : !strcmp(l.game, "tt") ? "The Two Towers" : "The Return of the King");
        }
        bool d = j_done(a, l);
        ImGui::TextColored(d ? C_GOLD_H : C_DIM, "%s", l.name);
        ImGui::PushFont(F_SMALL, F_SMALL->LegacySize);
        ImGui::Indent(S(18));
        if (d) ImGui::TextColored(C_TEXT, "%s", l.lore);
        else ImGui::TextColored(C_DIM, "Complete this level to read its page.");
        ImGui::Unindent(S(18));
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, S(4)));
    }
    ImGui::PopTextWrapPos();
}

// the whole page, in place of the front screen
static void draw_journey(App &a, ImVec2 size, SDL_Renderer *ren)
{
    g_jRen = ren;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float ts = F_TITLE->LegacySize * 0.5f * ImGui::GetStyle().FontScaleMain;
    ImVec2 tp = ImGui::GetWindowPos() + ImVec2(S(28), S(16));
    dl->AddText(F_TITLE, ts, tp + ImVec2(S(1.5f), S(1.5f)), IM_COL32(0, 0, 0, 110), "The Journey");
    dl->AddText(F_TITLE, ts, tp, ImGui::GetColorU32(C_GOLD_H), "The Journey");

    const float panelW = std::min(size.x - S(60), S(760)), panelX = (size.x - panelW) * 0.5f, top = S(84);
    const float panelH = size.y - top - S(80);
    ImVec2 panelA = ImGui::GetWindowPos() + ImVec2(panelX, top), panelB = panelA + ImVec2(panelW, panelH);
    orn_panel_fill(dl, panelA, panelB, ImGui::GetColorU32(rgb(15, 14, 13, 0.78f)));
    const float inset = S(9) * 0.5f + S(5);
    ImGui::SetCursorPos(ImVec2(panelX + inset, top + inset));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("journey", ImVec2(panelW - inset * 2, panelH - inset * 2), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
    g_labelW = std::max(S(200), ImGui::GetContentRegionAvail().x * 0.32f);
    static const char *names[] = { "Deeds", "Quests", "Map", "Records", "Virtues", "Lore", "Minstrel" };
    const int N = 7;
    static int sel = 0;
    ImGui::PushFont(F_TAB, F_TAB->LegacySize);
    float tabW = tabs_begin(names, N, sel);
    if (ImGui::BeginTabBar("jtabs")) {
        static int startTab = getenv("ATHELAS_JOURNEY_TAB") ? atoi(getenv("ATHELAS_JOURNEY_TAB")) : -1;   // testing
        if (int step = pad_tab_step(a)) startTab = (sel + step + N) % N;
        static const char *ids[] = { "###deeds", "###quests", "###map", "###records", "###virtues", "###lore", "###minstrel" };
        for (int t = 0; t < N; t++) {
            ImGui::SetNextItemWidth(tabW);
            if (ImGui::BeginTabItem(ids[t], nullptr, t == startTab ? ImGuiTabItemFlags_SetSelected : 0)) {
                sel = t;
                pad_tab_focus();
                ImGui::PopFont();
                ImGui::BeginChild("jtab", ImVec2(0, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
                if (t == 0) j_deeds(a); else if (t == 1) j_quests_tab(a); else if (t == 2) j_map(a);
                else if (t == 3) j_records(a); else if (t == 4) j_virtues_tab(a); else if (t == 5) j_lore(a);
                else j_minstrel(a);
                ImGui::Dummy(ImVec2(0, S(6)));
                ImGui::EndChild();
                ImGui::PushFont(F_TAB, F_TAB->LegacySize);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
        startTab = -1;
    }
    tabs_end();
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    orn_panel_frame(dl, panelA, panelB);

    // back to the games: the button, Escape, or B on a controller
    bool modal = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    ImGui::SetCursorPos(ImVec2(S(40), size.y - S(60)));
    bool back = orn_plate_button("\xe2\x80\xb9  Games##jback", ImVec2(S(130), S(36)), true);
    if (!modal && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))) back = true;
    if (back) {
        a.journey = false;
        a.front = true;
        music_enable(front_get("Music") != "0");   // the front screen's music again, as it was set
        music_play(MUSIC_NONE, std::string());
    }
}
