// The Third Age: [gameplay] battle_speed, the game sped up 2-4x in its turn-based battles only, back to normal for
// exploring, the menus outside battle and the movies.
//
// How a battle is told apart (no battle flag in the game's memory is known yet, so this is read from what the game
// does; to be confirmed on a PC with the log lines "ta: battle start" / "ta: battle end"):
// - the game has no fighting outside its battles, so a blow (an HP loss through 0x1F4E30, ta_gameplay.cpp) where the
//   one hit or the one hitting is not in the party starts a battle;
// - every enemy hit is remembered, and once each of them has been dealt its killing blow (0x1F4E30's "kill" byte) the
//   battle is over, after a short wait so the last blow plays out (an enemy never hit is unknown: a battle can then
//   end early, and the next blow starts it again);
// - AP spent by the party (0x1EEC40) keeps a battle going but never starts one (it may be spent from the menus too);
// - no blow or AP for kQuietMs, a new scene, a movie or the front end also end it.
// If the PC's logs show a battle ending or starting at the wrong time, a hook where the game itself starts and ends
// its battles should replace these guesses (SetInBattle below is the one place that changes the speed).
//
// The sound is off while sped up, as with fast forward (app::Muted): the mixer plays at the PC's own pace, so the
// game's sounds, made faster, would fall out of step with the picture and its streamed music would run short.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <set>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"

namespace ta {
uint32_t g_battle_speed = 1;  // [gameplay] battle_speed: 1 (the game's own pace), 2, 3 or 4
bool InMenus();               // ta_state.cpp: the front end
}

namespace {
constexpr int64_t kQuietMs = 30000;  // no blow or AP spent this long (real time): the battle is taken as over
constexpr int64_t kLastBlowMs = 1500;  // after the last enemy hit falls, its fall plays out before normal speed

std::mutex g_lock;
bool g_in = false;                       // in a battle (under g_lock)
std::set<uint32_t> g_hit, g_down;        // enemies hit, and those of them dealt a killing blow (under g_lock)
int64_t g_last = 0;                      // NowMs of the last blow or AP spent (under g_lock)
int64_t g_all_down = 0;                  // NowMs when every enemy hit was down, 0 while one still stands (under g_lock)
std::atomic<bool> g_active{false};       // g_in, read each frame without the lock

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// the one place a battle starts or ends (under g_lock)
void SetInBattle(bool in, const char* why) {
    if (g_in == in) return;
    g_in = in;
    g_active = in;
    if (!in) {
        LOGR("ta: battle end (%s; %u enemies hit, %u down)", why, unsigned(g_hit.size()), unsigned(g_down.size()));
        g_hit.clear();
        g_down.clear();
        g_all_down = 0;
    } else {
        LOGR("ta: battle start (%s)", why);
    }
}
}  // namespace

namespace ta {
// 0x1F4E30, an HP loss (change < 0): who was hit, who hit, and whether it was a killing blow; party: the one hit is
// in the party, by_party: the one hitting is
void BattleBlow(uint32_t target, bool party, uint32_t source, bool by_party, bool kill) {
    if (g_battle_speed <= 1) return;
    std::lock_guard lock(g_lock);
    bool enemy_hit = target && !party;
    bool enemy_hits = source && !by_party;
    if (!enemy_hit && !enemy_hits) return;  // a party member hurting themself (a trap, poison): no battle by itself
    if (!g_in) SetInBattle(true, enemy_hit ? "an enemy hit" : "an enemy's blow");
    g_last = NowMs();
    if (enemy_hit) {
        g_hit.insert(target);
        if (kill) g_down.insert(target);
        else g_down.erase(target);  // hit again: it is still standing (revived)
        g_all_down = g_down.size() == g_hit.size() ? g_last : 0;
    }
}

// 0x1EEC40, AP spent by a party member: still fighting
void BattleAp() {
    if (g_battle_speed <= 1) return;
    std::lock_guard lock(g_lock);
    if (g_in) g_last = NowMs();
}

// ta_state.cpp FileOpened: a new scene ends any battle
void BattleScene() {
    std::lock_guard lock(g_lock);
    SetInBattle(false, "a new scene");
}

// each frame's end (ta_framerate.cpp TA_FrameWait): the speed for now
void BattleTick() {
    if (g_battle_speed <= 1) {
        app::SetGameSpeed(1);  // also when turned off from the in-game menu mid-battle
        return;
    }
    if (g_active.load()) {
        std::lock_guard lock(g_lock);
        int64_t now = NowMs();
        if (gpu::g_movie.load()) SetInBattle(false, "a movie");
        else if (InMenus()) SetInBattle(false, "the front end");
        else if (g_all_down && now - g_all_down >= kLastBlowMs) SetInBattle(false, "every enemy hit is down");
        else if (now - g_last >= kQuietMs) SetInBattle(false, "no blow for 30 s");
    }
    app::SetGameSpeed(g_active.load() ? g_battle_speed : 1);
}
}  // namespace ta
