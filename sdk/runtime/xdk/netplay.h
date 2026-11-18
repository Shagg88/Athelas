// Online co-op by lockstep (research, notes/lockstep/design.md): two PCs run the same game in lockstep mode
// (kernel/lockstep.h) and exchange only their controller input, a few frames ahead. Before each frame both PCs have
// both players' input for it, so both run the same frames.
//
// Settings [network] online_coop ("host" player 1, "join" player 2), online_address, online_port, for a game with
// GameInfo::online_coop built with lockstep safepoints (XBE_LOCKSTEP); tests: XBE_NETPLAY=host:<port> or
// join:<address>:<port>. XBE_NETPLAY_DELAY=<frames> (default 4); XBE_NETPLAY_NO_UPNP=1 leaves the router alone.
// Each player's own first controller is their port (host port 1, guest port 2); the game sees both connected.
// Every second both PCs compare a hash of the game's memory and log the first frame they differ.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "core/input.h"

namespace netplay {

bool Active();
// the game shows a frame (D3DDevice_Swap, on its own thread with the lockstep turn held): sends this player's input,
// waits for the partner's for the next frame; while it waits, redraw (if any) shows the frame again a few times a
// second, so the on-screen "Waiting for player N..." is seen
void OnFrame(const std::function<void()>& redraw = nullptr);
// the controller the game sees on that port while a session runs
input::Pad Pad(int port);
// this PC's player's port (0 host, 1 guest)
int LocalPort();
// at the start, before the settings are read: connects, and the host sends its settings and save folder; the guest
// plays with them (from <exe folder>/online-session; its own [display], [audio], [controls], [network] kept, its own saves
// untouched). Nothing without XBE_NETPLAY.
void Prepare();
// the game is ending (every exit: kernel::Shutdown, app::Fail): the partner is told at once and the router's port given
// back (std::exit's handlers don't run on those paths). Nothing without a session; once only.
void End();
// set by the app before Prepare: shows what the connection is waiting for (a line of text; empty closes it) and says
// when the player cancelled. Null (tests): the log alone.
extern void (*g_status)(const std::string& text);
extern bool (*g_cancelled)();
extern bool (*g_windowless)();  // no window could be made (a message can't wait for Cancel)
// the guest's copies, once Prepare has them (empty otherwise)
std::filesystem::path SessionSettings();
std::filesystem::path SessionSaves();

}  // namespace netplay
