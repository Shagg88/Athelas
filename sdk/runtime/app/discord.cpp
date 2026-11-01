// Discord status ([discord] enabled, off by default): what the game is doing ("Helm's Deep", "In the menus") shown on
// the player's Discord profile, through the Discord app's own local connection (the named pipe discord-ipc-N, the
// protocol Discord's Rich Presence libraries speak). Nothing goes over the network from here, and with Discord not
// running nothing happens. Needs the game's Discord application id (GameInfo::discord_app_id, or [discord] app_id).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/log.h"

namespace discord {

namespace {

std::string Json(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += char(c);
        else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += char(c);
    }
    return o + "\"";
}

struct Pipe {
    HANDLE h = INVALID_HANDLE_VALUE;
    bool Open() {
        for (int i = 0; i < 10 && h == INVALID_HANDLE_VALUE; i++) {
            std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        }
        return h != INVALID_HANDLE_VALUE;
    }
    void Close() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
    bool Send(uint32_t op, const std::string& json) {
        std::vector<char> b(8 + json.size());
        uint32_t len = uint32_t(json.size());
        std::memcpy(b.data(), &op, 4);
        std::memcpy(b.data() + 4, &len, 4);
        std::memcpy(b.data() + 8, json.data(), json.size());
        DWORD w = 0;
        return WriteFile(h, b.data(), DWORD(b.size()), &w, nullptr) && w == b.size();
    }
    // whatever Discord has answered, read and dropped so the pipe never fills; false when it closed
    bool Drain() {
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
            if (!avail) return true;
            std::vector<char> b(avail);
            DWORD r = 0;
            if (!ReadFile(h, b.data(), avail, &r, nullptr)) return false;
        }
    }
};

std::string AppId() {
    const std::string& s = app::Get().discord_app_id;
    if (!s.empty()) return s;
    return app::kGame.discord_app_id ? app::kGame.discord_app_id : "";
}

}  // namespace

void Start() {
    if (!app::Get().discord) return;
    std::string id = AppId();
    if (id.empty()) {
        LOGR("discord: no application id for this game, status off");
        return;
    }
    std::thread([id] {
        const int64_t started = int64_t(std::time(nullptr));
        Pipe p;
        std::string shown;
        int nonce = 0;
        auto next_try = std::chrono::steady_clock::now();
        bool told = false;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!app::Get().discord) {  // switched off in the in-game menu: the status goes, the line closes
                if (p.h != INVALID_HANDLE_VALUE) {
                    p.Send(1, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
                                  "},\"nonce\":\"" + std::to_string(++nonce) + "\"}");
                    p.Close();
                    shown.clear();
                }
                continue;
            }
            if (p.h == INVALID_HANDLE_VALUE) {
                if (std::chrono::steady_clock::now() < next_try) continue;
                next_try = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                if (!p.Open()) {
                    if (!told) LOGR("discord: Discord isn't running, trying again now and then");
                    told = true;
                    continue;
                }
                if (!p.Send(0, "{\"v\":1,\"client_id\":" + Json(id) + "}")) {
                    p.Close();
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                if (!p.Drain()) {
                    p.Close();
                    continue;
                }
                LOGR("discord: connected");
                shown.clear();
            }
            std::string details = app::kGame.activity ? app::kGame.activity() : std::string();
            std::string state = app::kGame.activity_state ? app::kGame.activity_state() : std::string();
            if (details.empty()) details = "In the menus";
            std::string key = details + "\n" + state;
            if (key != shown) {
                std::string act = "{\"details\":" + Json(details) + (state.empty() ? "" : ",\"state\":" + Json(state)) +
                                  ",\"timestamps\":{\"start\":" + std::to_string(started) + "}}";
                std::string msg = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
                                  ",\"activity\":" + act + "},\"nonce\":\"" + std::to_string(++nonce) + "\"}";
                if (!p.Send(1, msg)) {
                    LOGR("discord: connection closed");
                    p.Close();
                    continue;
                }
                shown = key;
                LOG_FIRST(20, "discord: status \"%s\"%s%s", details.c_str(), state.empty() ? "" : " / ", state.c_str());
            }
            if (!p.Drain()) {
                LOGR("discord: connection closed");
                p.Close();
            }
        }
    }).detach();
}

}  // namespace discord
