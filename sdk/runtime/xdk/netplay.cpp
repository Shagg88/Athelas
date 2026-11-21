// Online co-op by lockstep: input exchange over UDP (netplay.h)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <winsock2.h>  // the harness's Winsock names on POSIX sockets
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <utility>
#include <filesystem>
#include <vector>
#include <cstdio>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "xdk/netplay.h"
#include "core/settings_file.h"

#if XBE_HAS_UPNP
#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>
#endif

#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/lockstep.h"
#include "kernel/memory.h"
#include "app/app.h"

namespace netplay {

input::Pad LocalSample();  // xapi.cpp: this PC's first controller (and a test script)

namespace {

constexpr uint32_t kMagic = 0x504E5441;  // "ATNP"
enum : uint8_t { kHello = 1, kWelcome = 2, kInput = 3, kHash = 4, kRefuse = 5, kFile = 6, kFileAck = 7, kFilesDone = 8,
                 kFilesDoneAck = 9, kAlive = 10, kBye = 11, kPing = 12, kPong = 13 };
constexpr uint32_t kChunk = 1024;
// what the host may send before the game starts (the settings and a save folder): anything bigger is refused
constexpr uint32_t kMaxFile = 64u << 20, kMaxFiles = 4096, kMaxName = 400;  // name + chunk + header < 1500 bytes
constexpr uint64_t kMaxTotal = 256ull << 20;
constexpr int kGoneSeconds = 15;  // nothing at all from the partner this long (they send every half second): gone
constexpr int kRedundant = 16;  // each input packet repeats the last frames, so a lost packet costs nothing
// and from the frame the partner says it needs next (however many were lost), at most this many a packet
constexpr int kMaxSend = 64;
constexpr int kMaxDelay = 15;  // frames of input delay at most (the automatic choice's range, and by hand)

struct WirePad {
    uint16_t buttons;
    uint8_t analog[8];
    int16_t lx, ly, rx, ry;
};
static_assert(sizeof(WirePad) == 18);

WirePad ToWire(const input::Pad& p) {
    WirePad w{};
    w.buttons = p.buttons;
    std::memcpy(w.analog, p.analog, 8);
    w.lx = p.lx, w.ly = p.ly, w.rx = p.rx, w.ry = p.ry;
    return w;
}
input::Pad FromWire(const WirePad& w) {
    input::Pad p;
    p.connected = true;
    p.buttons = w.buttons;
    std::memcpy(p.analog, w.analog, 8);
    p.lx = w.lx, p.ly = w.ly, p.rx = w.rx, p.ry = w.ry;
    return p;
}

struct Session {
    bool on = false, host = false;
    int delay = 4;
    SOCKET s = INVALID_SOCKET;
    sockaddr_in peer{};
    std::atomic<bool> connected{false};
    std::mutex m;
    std::condition_variable cv;
    std::map<uint32_t, WirePad> mine, theirs;      // frame -> input
    std::map<uint32_t, uint64_t> my_hash, their_hash;
    std::vector<std::pair<uint32_t, uint64_t>> sent_hash;  // the last few this PC sent, sent again with each
    uint32_t frame = 0;   // frames shown so far
    uint32_t current = 0; // the frame the game is running now (inputs for it are known)
    bool desync_told = false;
    uint16_t port = 0;       // this PC's UDP port (network order)
    std::string my_address;  // the host's internet address and port, when the router told it
    // the partner left or stopped answering: their player stands still from then on. Atomic: the receive thread and
    // the game thread write it under the mutex, but the heartbeat thread reads it without (audit, 19 Nov)
    std::atomic<bool> gone{false};
    std::chrono::steady_clock::time_point heard{};  // the partner's last packet
    uint64_t file_bytes = 0;                        // what the host's files take so far (kMaxTotal)
    uint32_t their_need = 0;     // the partner's next frame needed from us (resend from there)
    std::vector<uint32_t> send_chunks;  // host, while sending files: each file's chunk count (acks outside are dropped)
    std::string internet_ip;     // the host's internet address from STUN, when the router didn't open the port
    std::string lan_ip;          // the host's address on its home network (a friend in the same house types this)
    std::string fail;            // why the session can't start (shown in the window; the game then ends)
    bool refused_someone = false;  // the host turned away a player 2 with another version
    bool behind_shared = false;
    bool gone_shown = false;       // "Your partner has left" shown    // the router's internet address is private (double NAT, carrier-grade NAT)
    bool delay_set = false;      // XBE_NETPLAY_DELAY: no automatic delay
    std::vector<int64_t> rtt_us; // the host's pings answered, round trips
    int64_t ping_sent_us = 0;
    // the settings and save files the host sends before the game starts (name -> bytes, and which chunks arrived)
    struct Incoming {
        std::vector<uint8_t> data;
        std::vector<bool> got;
    };
    std::map<std::string, Incoming> files;
    std::map<std::pair<uint32_t, uint32_t>, bool> acked;  // host: (file, chunk) acknowledged
    bool files_done = false, files_done_acked = false;
    bool files_written = false;  // player 2: every file landed (only then is the host told, and told again)
    uint32_t files_expected = 0;
    std::filesystem::path session_settings, session_saves;
};

Session& S() {
    static Session s;
    return s;
}

void Send(const void* data, size_t n) {
    Session& s = S();
    // tests: XBE_NETPLAY_LOSS=<percent> drops that share of what this PC sends (a bad connection on one machine)
    static const int loss = [] {
        const char* e = std::getenv("XBE_NETPLAY_LOSS");
        return e ? std::clamp(std::atoi(e), 0, 90) : 0;
    }();
    if (loss && std::rand() % 100 < loss) return;
    ::sendto(s.s, static_cast<const char*>(data), int(n), 0, reinterpret_cast<const sockaddr*>(&s.peer), sizeof(s.peer));
}

void SendHeader(uint8_t type, std::string& out) {
    out.assign(reinterpret_cast<const char*>(&kMagic), 4);
    out.push_back(char(type));
}

uint64_t Fnv(const uint8_t* p, size_t n, uint64_t h = 1469598103934665603ull);

// this program's own file: both players must run the same build, or the games drift apart at once
uint64_t ExeHash() {
    static const uint64_t h = [] {
        std::string path;
#ifdef _WIN32
        char b[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, b, MAX_PATH);
        path = b;
#else
        path = "/proc/self/exe";
#endif
        uint64_t v = 1469598103934665603ull;
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            std::vector<uint8_t> buf(1 << 20);
            size_t n;
            while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) v = Fnv(buf.data(), n & ~size_t(7), v);
            std::fclose(f);
        }
        return v;
    }();
    return h;
}

uint64_t Fnv(const uint8_t* p, size_t n, uint64_t h) {
    for (size_t i = 0; i + 8 <= n; i += 8) {
        uint64_t v;
        std::memcpy(&v, p + i, 8);
        h = (h ^ v) * 1099511628211ull;
    }
    return h;
}

// the guest's memory below 64 MB that is in use (about 40 MB), a sixtieth a frame: slice k of 60 of the pages into h.
// All of it once a second without a hitch (the whole at once took ~13 ms every 60 frames; review, 18 Nov 2026); both
// PCs hash the same slices at the same frames, so the second's hash is the same when the games are.
constexpr uint32_t kHashFirst = 0x10000 >> 12, kHashEnd = 0x4000000 >> 12, kHashSlices = 60;
uint64_t StateHashSlice(uint32_t k, uint64_t h) {
    const uint32_t per = (kHashEnd - kHashFirst + kHashSlices - 1) / kHashSlices;
    const uint32_t a = kHashFirst + k * per, b = std::min(kHashEnd, a + per);
    for (uint32_t page = a; page < b; page++) {
        const uint32_t p = page << 12;
        if (mem::IsCommitted(p)) h = Fnv(static_cast<const uint8_t*>(kernel::HostPtr(p)), 0x1000, h);
    }
    return h;
}

int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// a name the host may send: "settings.toml", or "saves/" and a relative path whose parts are plain names (no "..",
// no root, no drive, no colon or backslash), so nothing can land outside the session folder
bool SafeName(const std::string& name) {
    if (name == "settings.toml") return true;
    if (name.rfind("saves/", 0) != 0 || name.size() <= 6) return false;
    size_t at = 6;
    while (at <= name.size()) {
        size_t e = name.find('/', at);
        std::string part = name.substr(at, e == std::string::npos ? std::string::npos : e - at);
        if (part.empty() || part == "." || part == ".." || part.find_first_of(":\\*?\"<>|") != std::string::npos) return false;
        // Windows drops a trailing dot or space and treats device names (CON, NUL, COM1...) as devices
        if (part.back() == '.' || part.back() == ' ') return false;
        std::string stem = part.substr(0, part.find('.'));
        for (char& c : stem) c = char(std::toupper(static_cast<unsigned char>(c)));
        static const char* const kDevices[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6",
                                               "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6",
                                               "LPT7", "LPT8", "LPT9", "CONIN$", "CONOUT$"};
        for (const char* d : kDevices)
            if (stem == d) return false;
        for (unsigned char c : part)
            if (c < 0x20) return false;
        if (e == std::string::npos) break;
        at = e + 1;
    }
    return true;
}

// files by path, any characters in it (a user folder like C:\Users\José)
std::FILE* OpenFile(const std::filesystem::path& p, const char* mode) {
#ifdef _WIN32
    std::wstring m(mode, mode + std::strlen(mode));
    return _wfopen(p.c_str(), m.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}

void Receive() {
    Session& s = S();
    char buf[1500];
    for (;;) {
        sockaddr_in from{};
#ifdef _WIN32
        int fl = sizeof(from);
#else
        socklen_t fl = sizeof(from);
#endif
        int n = ::recvfrom(s.s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n < 5) continue;
        uint32_t magic;
        std::memcpy(&magic, buf, 4);
        if (magic != kMagic) continue;
        uint8_t type = uint8_t(buf[4]);
        std::lock_guard<std::mutex> l(s.m);
        // the partner alone: the host takes the first player 2 with the same build; after that, and for the guest
        // always, anything from another address is dropped (a stray or forged packet can't move a player or end a game)
        const bool from_peer = from.sin_addr.s_addr == s.peer.sin_addr.s_addr && from.sin_port == s.peer.sin_port;
        if (s.connected || !s.host) {
            if (!from_peer) {
                if (type == kHello && s.host) {
                    static bool told = false;
                    if (!told) LOGR("netplay: someone else (%s) tried to join; the game already has its player 2", inet_ntoa(from.sin_addr));
                    told = true;
                }
                continue;
            }
            s.heard = std::chrono::steady_clock::now();
        }
        if (type == kHello && s.host) {
            uint64_t theirs = 0;
            if (n >= 13) std::memcpy(&theirs, buf + 5, 8);
            if (theirs != ExeHash()) {
                static bool told = false;
                if (!told) LOGR("netplay: refused %s: player 2 runs another version of the game (both need the same Athelas update)",
                                inet_ntoa(from.sin_addr));
                told = true;
                s.refused_someone = true;
                std::string out;
                SendHeader(kRefuse, out);
                ::sendto(s.s, out.data(), int(out.size()), 0, reinterpret_cast<const sockaddr*>(&from), sizeof(from));
                continue;
            }
            if (!s.connected) {
                LOGR("netplay: player 2 joined from %s:%u", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
                s.peer = from;
                s.heard = std::chrono::steady_clock::now();
                s.connected = true;
            }
            std::string out;
            SendHeader(kWelcome, out);
            Send(out.data(), out.size());
        } else if (type == kRefuse && !s.host && !s.connected) {
            s.fail = "Player 1 has a different version of Athelas. Both players need the same "
                     "Athelas update to play online co-op, on the same kind of PC (a Windows on ARM laptop and a regular "
                     "PC can't play together).";
        } else if (type == kWelcome && !s.host) {
            if (!s.connected) LOGR("netplay: joined player 1's game");
            s.connected = true;
        } else if (type == kInput && n >= 14 && s.connected) {
            // first frame, count, the frame the partner needs next from us, then the pads
            uint32_t first, need;
            std::memcpy(&first, buf + 5, 4);
            int count = uint8_t(buf[9]);
            std::memcpy(&need, buf + 10, 4);
            if (count > kMaxSend || n < 14 + count * int(sizeof(WirePad))) continue;
            if (need > s.their_need && need <= s.frame + 2 * uint32_t(kMaxDelay) + 2) s.their_need = need;
            // only frames near the one being played (no unbounded growth from odd frame numbers)
            const uint32_t lo = s.frame > 64 ? s.frame - 64 : 0, hi = s.frame + 2 * uint32_t(kMaxDelay) + kMaxSend;
            for (int i = 0; i < count; i++) {
                uint32_t f = first + uint32_t(i);
                if (f < lo || f > hi) continue;
                WirePad w;
                std::memcpy(&w, buf + 14 + i * sizeof(WirePad), sizeof(WirePad));
                s.theirs.emplace(f, w);
            }
        } else if (type == kBye && s.connected) {
            if (!s.gone) LOGR("netplay: the partner left the game; playing on alone");
            s.gone = true;  // shown on screen by the game thread (OnFrame), not here
        } else if (type == kFile && !s.host && s.connected && !s.files_done_acked && n >= 19) {
            uint32_t idx, chunk, total;
            uint16_t name_len;
            std::memcpy(&idx, buf + 5, 4);
            std::memcpy(&chunk, buf + 9, 4);
            std::memcpy(&total, buf + 13, 4);
            std::memcpy(&name_len, buf + 17, 2);
            if (name_len > kMaxName || n < 19 + name_len || total > kMaxFile) continue;
            std::string name(buf + 19, name_len);
            if (!SafeName(name)) {
                static bool told = false;
                if (!told) LOGR("netplay: refused a file from player 1 with an unsafe name");
                told = true;
                continue;
            }
            size_t len = size_t(n) - 19 - name_len;
            auto found = s.files.find(name);
            if (found == s.files.end()) {
                if (s.files.size() >= kMaxFiles || s.file_bytes + total > kMaxTotal) continue;
                s.file_bytes += total;
            }
            auto& f = s.files[name];
            if (f.data.size() != total) {
                if (found != s.files.end()) continue;  // the same name with another size: ignored
                f.data.assign(total, 0);
                f.got.assign((total + kChunk - 1) / kChunk, false);
            }
            if (chunk < f.got.size() && size_t(chunk) * kChunk + len <= total) {
                std::memcpy(f.data.data() + size_t(chunk) * kChunk, buf + 19 + name_len, len);
                f.got[chunk] = true;
            }
            std::string out;
            SendHeader(kFileAck, out);
            out.append(reinterpret_cast<const char*>(&idx), 4);
            out.append(reinterpret_cast<const char*>(&chunk), 4);
            Send(out.data(), out.size());
        } else if (type == kFileAck && s.host && s.connected && n >= 13) {
            uint32_t idx, chunk;
            std::memcpy(&idx, buf + 5, 4);
            std::memcpy(&chunk, buf + 9, 4);
            if (idx < s.send_chunks.size() && chunk < s.send_chunks[idx]) s.acked[{idx, chunk}] = true;  // while sending only
        } else if (type == kPing && !s.host && s.connected && n >= 13) {
            std::string out(buf, 13);
            out[4] = char(kPong);
            Send(out.data(), out.size());
        } else if (type == kPong && s.host && s.connected && n >= 13) {
            int64_t t;
            std::memcpy(&t, buf + 5, 8);
            if (t == s.ping_sent_us && t != 0 && s.rtt_us.size() < 8) {
                s.rtt_us.push_back(NowUs() - t);
                s.ping_sent_us = 0;  // one answer a ping
            }
        } else if (type == kFilesDone && !s.host && s.connected && s.files_done_acked) {
            // the host didn't hear our acknowledgement: again (never after a failure: player 1 would start alone)
            if (!s.files_written) continue;
            std::string out;
            SendHeader(kFilesDoneAck, out);
            Send(out.data(), out.size());
        } else if (type == kFilesDone && !s.host && s.connected && n >= 9) {
            std::memcpy(&s.files_expected, buf + 5, 4);
            s.files_expected = std::min(s.files_expected, kMaxFiles);
            if (n >= 10) s.delay = std::clamp(int(uint8_t(buf[9])), 1, kMaxDelay);  // the host's choice, for both
            s.files_done = true;
        } else if (type == kFilesDoneAck && s.host && s.connected) {
            s.files_done_acked = true;
        } else if (type == kHash && n >= 6 && s.connected) {
            // the last few hashes (a lost packet loses no check): count, then frame and hash pairs
            int count = uint8_t(buf[5]);
            if (count > 8 || n < 6 + count * 12) continue;
            for (int i = 0; i < count; i++) {
                uint32_t f;
                uint64_t h;
                std::memcpy(&f, buf + 6 + i * 12, 4);
                std::memcpy(&h, buf + 10 + i * 12, 8);
                if (f + 600 >= s.frame && f <= s.frame + 600) s.their_hash[f] = h;
            }
        }
        s.cv.notify_all();
    }
}

void (*g_close_port)() = nullptr;  // the router's port mapping, taken back at the end
void (*g_renew_port)() = nullptr;  // asked for again every hour (the router keeps it 4 hours: a long session)

// the end of the session (the game closing, or cancelled): the partner is told at once, the router's port given back
void Finish() {
    static std::atomic<bool> done{false};
    if (done.exchange(true)) return;
    Session& s = S();
    if (s.connected) {
        std::string out(reinterpret_cast<const char*>(&kMagic), 4);
        out.push_back(char(kBye));
        for (int i = 0; i < 3; i++) Send(out.data(), out.size());
    }
    if (g_close_port) g_close_port();
}
}  // namespace

void End() { Finish(); }

// replays (xapi.cpp) check the game's memory with the same hash co-op uses
uint64_t MemoryHashSlice(uint32_t k, uint64_t h) { return StateHashSlice(k % kHashSlices, h); }
uint64_t BuildHash() { return ExeHash(); }

namespace {

// the host: asks the router (UPnP) to send the game's UDP port to this PC, and says the address player 2 types in.
// Taken back when the game ends. Without UPnP on the router the port has to be forwarded by hand.
void OpenRouterPort(uint16_t port) {
#if XBE_HAS_UPNP
    int err = 0;
    UPNPDev* devs = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &err);
    if (!devs) {
        LOGR("netplay: no router answered (UPnP): forward UDP port %u to this PC by hand", port);
        return;
    }
    static UPNPUrls urls;
    static IGDdatas data;
    char lan[64] = {}, wan[64] = {};
    int r = UPNP_GetValidIGD(devs, &urls, &data, lan, sizeof(lan), wan, sizeof(wan));
    freeUPNPDevlist(devs);
    // 1: a router on the internet; 2 (UPNP_PRIVATEIP_IGD): a router whose own internet address is private (another
    // router or the provider's shared address in front): the port is still opened, and the warning below says so
    if (r != 1 && r != 2) {
        if (r > 0) FreeUPNPUrls(&urls);
        LOGR("netplay: the router doesn't open ports by itself (UPnP %d): forward UDP port %u to this PC by hand", r, port);
        return;
    }
    std::string p = std::to_string(port);
    int a = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, p.c_str(), p.c_str(), lan, "Athelas online co-op",
                                "UDP", nullptr, "14400");
    char ext[64] = {};
    UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, ext);
    if (a != UPNPCOMMAND_SUCCESS) {
        LOGR("netplay: the router refused to open UDP port %u (%s): forward it by hand", port, strupnperror(a));
        return;
    }
    LOGR("netplay: router port %u opened; player 2 joins %s:%u", port, ext[0] ? ext : "(this PC's internet address)", port);
    // the router's own "internet" address is a private one: another router, or the internet provider's shared address
    // (carrier-grade NAT, 100.64.0.0/10), sits in front, and the port opened here doesn't reach the internet
    in_addr ea{};
    bool private_ext = false;
    if (ext[0] && inet_pton(AF_INET, ext, &ea) == 1) {
        uint32_t a = ntohl(ea.s_addr);
        private_ext = (a >> 24) == 10 || (a >> 20) == 0xAC1 || (a >> 16) == 0xC0A8 || (a >> 22) == (0x6440 >> 6);
    }
    if (private_ext) {
        LOGR("netplay: the router's internet address %s is private: another router or the internet provider's shared "
             "address is in front (player 2 may not get through)", ext);
        S().behind_shared = true;
    } else if (ext[0]) {
        S().my_address = std::string(ext) + ":" + std::to_string(port);
    }
    static uint16_t opened = port;
    g_close_port = [] {
        std::string p = std::to_string(opened);
        UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, p.c_str(), "UDP", nullptr);
    };
    static char lan_kept[64];
    std::memcpy(lan_kept, lan, sizeof(lan_kept));
    g_renew_port = [] {
        std::string p = std::to_string(opened);
        UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, p.c_str(), p.c_str(), lan_kept, "Athelas online co-op",
                            "UDP", nullptr, "14400");
    };
#else
    LOGR("netplay: this build can't ask the router to open ports: forward UDP port %u to this PC by hand", port);
#endif
}

// the host's internet address as the internet sees it (STUN, RFC 5389: a binding request to a public server, which
// answers with the address it saw), on the game's own socket, before the receiving thread starts. For the window when
// the router didn't say (no UPnP). Empty when no server answers within a second and a half.
std::string AskInternetAddress(SOCKET sock) {
    static const char* const kPublic[][2] = {{"stun.l.google.com", "19302"}, {"stun.cloudflare.com", "3478"}};
    std::vector<std::pair<std::string, std::string>> kServers;
    if (const char* e = std::getenv("XBE_STUN")) {  // tests: another server, "host:port"
        std::string v = e;
        size_t c = v.rfind(':');
        kServers.push_back({v.substr(0, c), c == std::string::npos ? "3478" : v.substr(c + 1)});
    } else {
        for (auto& p : kPublic) kServers.push_back({p[0], p[1]});
    }
#ifdef _WIN32
    DWORD tv = 1500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{1, 500000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    std::string found;
    for (auto& srv : kServers) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(srv.first.c_str(), srv.second.c_str(), &hints, &res) != 0 || !res) continue;
        sockaddr_in to = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
        freeaddrinfo(res);
        uint8_t req[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};  // binding request, magic cookie
        for (int i = 8; i < 20; i++) req[i] = uint8_t(std::rand());
        ::sendto(sock, reinterpret_cast<const char*>(req), 20, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
        uint8_t buf[512];
        sockaddr_in from{};
#ifdef _WIN32
        int fl = sizeof(from);
#else
        socklen_t fl = sizeof(from);
#endif
        int n = ::recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n < 20 || from.sin_addr.s_addr != to.sin_addr.s_addr || std::memcmp(buf + 8, req + 8, 12) != 0) continue;
        // attributes: XOR-MAPPED-ADDRESS (0x0020), else MAPPED-ADDRESS (0x0001), IPv4
        for (int at = 20; at + 4 <= n;) {
            int type = buf[at] << 8 | buf[at + 1], len = buf[at + 2] << 8 | buf[at + 3];
            if (at + 4 + len > n) break;
            if ((type == 0x0020 || type == 0x0001) && len >= 8 && buf[at + 5] == 0x01) {
                uint16_t port = uint16_t(buf[at + 6] << 8 | buf[at + 7]);
                uint8_t ip[4] = {buf[at + 8], buf[at + 9], buf[at + 10], buf[at + 11]};
                if (type == 0x0020) {
                    port ^= 0x2112;
                    const uint8_t cookie[4] = {0x21, 0x12, 0xA4, 0x42};
                    for (int i = 0; i < 4; i++) ip[i] ^= cookie[i];
                }
                found = std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) + "." +
                        std::to_string(ip[3]);
                (void)port;
                break;
            }
            at += 4 + ((len + 3) & ~3);
        }
        if (!found.empty()) break;
    }
#ifdef _WIN32
    tv = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    tv = timeval{0, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    return found;
}

// this PC's address on its home network (the route out to the internet, no packet sent): for a friend in the same house,
// whose router may not send the internet address back inside. Empty when there is none.
std::string LanAddress() {
    SOCKET t = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (t == INVALID_SOCKET) return {};
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr.s_addr = htonl(0x08080808);
    std::string out;
    if (::connect(t, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0) {
        sockaddr_in me{};
#ifdef _WIN32
        int l = sizeof(me);
#else
        socklen_t l = sizeof(me);
#endif
        if (::getsockname(t, reinterpret_cast<sockaddr*>(&me), &l) == 0 && me.sin_addr.s_addr) out = inet_ntoa(me.sin_addr);
    }
    ::closesocket(t);
    return out;
}

void Start() {
    Session& s = S();
    // the test variable, else the settings ([network] online_coop, online_address, online_port)
    std::string spec;
    if (const char* e = std::getenv("XBE_NETPLAY")) {
        spec = e;
    } else {
        const app::Settings& set = app::Get();
        if (set.online_coop == "host") {
            spec = "host:" + std::to_string(set.online_port);
        } else if (set.online_coop == "join") {
            // as typed: spaces around it trimmed, and "address:port" taken whole (the port field left as it was)
            std::string a = set.online_address;
            a.erase(0, a.find_first_not_of(" \t"));
            a.erase(a.find_last_not_of(" \t\r\n") + 1);
            if (a.empty()) {
                s.on = true;
                s.fail = "To join, type player 1's address on the Network tab (\"Friend's address\").";
                return;
            }
            size_t c = a.find(':');
            spec = c != std::string::npos && a.find(':', c + 1) == std::string::npos ? "join:" + a
                                                                                     : "join:" + a + ":" + std::to_string(set.online_port);
        } else {
            return;
        }
    }
    const char* e = spec.c_str();
    s.on = true;  // from here a failure is shown (the player asked for online co-op; never a quiet game alone)
    if (!kernel::ls::kSafepoints) {
        s.fail = "This build of the game can't play online co-op.";
        return;
    }
    kernel::ls::g_on = true;  // both games must run the same: lockstep (kernel/lockstep.h)
    if (const char* d = std::getenv("XBE_NETPLAY_DELAY")) {  // the host's choice by hand (player 2 always takes the host's)
        s.delay = std::clamp(std::atoi(d), 1, kMaxDelay);
        s.delay_set = true;
    }
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    s.s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in me{};
    me.sin_family = AF_INET;
    if (spec.rfind("host:", 0) == 0) {
        s.host = true;
        me.sin_port = htons(uint16_t(std::atoi(spec.c_str() + 5)));
    } else if (spec.rfind("join:", 0) == 0) {
        std::string rest = spec.substr(5);
        size_t colon = rest.rfind(':');
        std::string addr = rest.substr(0, colon);
        s.peer.sin_family = AF_INET;
        s.peer.sin_port = htons(uint16_t(std::atoi(rest.c_str() + colon + 1)));
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (colon == std::string::npos) addr = rest;
        int port = std::atoi(rest.c_str() + colon + 1);
        if (colon == std::string::npos || port <= 0 || port > 65535) {
            s.fail = "\"" + rest + "\" isn't an address and port. Type player 1's address, like 203.0.113.7 or 203.0.113.7:47100.";
            return;
        }
        s.peer.sin_port = htons(uint16_t(port));
        if (getaddrinfo(addr.c_str(), nullptr, &hints, &res) == 0 && res) {
            s.peer.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
            freeaddrinfo(res);
        } else {
            s.fail = "Can't find \"" + addr + "\" on the internet. Check player 1's address on the Network tab.";
            return;
        }
    } else {
        s.fail = std::string("XBE_NETPLAY is host:<port> or join:<address>:<port>, not ") + e;
        return;
    }
    if (::bind(s.s, reinterpret_cast<sockaddr*>(&me), sizeof(me)) != 0) {
        s.fail = "Can't use UDP port " + std::to_string(ntohs(me.sin_port)) +
                 ": another program (or another copy of the game) has it. Close it, or pick another port on the Network "
                 "tab (both players need the same one).";
        return;
    }
    s.port = me.sin_port;
    LOGR("netplay: %s, %d frames of input delay", s.host ? "hosting as player 1" : "joining as player 2", s.delay);
    if (s.host) {
        // the router and the internet address take a few seconds: said at once, so the player knows the game started
        if (g_status) g_status("Online co-op: getting ready (asking your router to open the game's port)...");
        if (!std::getenv("XBE_NETPLAY_NO_UPNP")) OpenRouterPort(ntohs(me.sin_port));
        if (s.my_address.empty() && !std::getenv("XBE_NETPLAY_NO_STUN")) {
            s.internet_ip = AskInternetAddress(s.s);
            if (!s.internet_ip.empty()) LOGR("netplay: this PC's internet address is %s", s.internet_ip.c_str());
        }
        s.lan_ip = LanAddress();
    }
    std::thread(Receive).detach();
    // twice a second while connected: "still here", so a partner whose game is paused or loading isn't taken for gone
    std::thread([] {
        Session& s = S();
        std::string alive(reinterpret_cast<const char*>(&kMagic), 4), bye = alive;
        alive.push_back(char(kAlive));
        bye.push_back(char(kBye));
        for (int tick = 1;; tick++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (tick % 7200 == 0 && g_renew_port) g_renew_port();  // every hour
            // once this PC has given the partner up, "goodbye" instead: the partner mustn't wait for us forever
            if (s.connected) s.gone ? Send(bye.data(), bye.size()) : Send(alive.data(), alive.size());
        }
    }).detach();
    std::atexit(Finish);
}

bool Started() {
    static bool once = (Start(), true);
    (void)once;
    return S().on && S().fail.empty();
}

}  // namespace

bool Active() { return Started(); }

int LocalPort() { return S().host ? 0 : 1; }

void OnFrame(const std::function<void()>& redraw) {
    if (!Started()) return;
    Session& s = S();
    using namespace std::chrono;
    std::unique_lock<std::mutex> l(s.m);
    if (!s.connected) return;  // Prepare didn't run (it connects before the game starts)
    uint32_t f = ++s.frame;
    // this player's input now, for a few frames ahead (the frames before that are empty on both PCs)
    l.unlock();
    WirePad local = ToWire(LocalSample());
    l.lock();
    s.mine[f + uint32_t(s.delay)] = local;
    auto send_inputs = [&] {
        if (s.gone) return;
        std::string out;
        SendHeader(kInput, out);
        // the last few frames, and back to the one the partner needs next if that is earlier (lost packets)
        uint32_t last = f + uint32_t(s.delay), first = last >= uint32_t(kRedundant) ? last - kRedundant + 1 : 1;
        if (s.their_need && s.their_need < first) first = s.their_need;
        first = std::max({first, uint32_t(s.delay) + 1, last >= uint32_t(kMaxSend) ? last - kMaxSend + 1 : 1u});
        const uint32_t need = f + 1;
        out.append(reinterpret_cast<const char*>(&first), 4);
        out.push_back(char(last - first + 1));
        out.append(reinterpret_cast<const char*>(&need), 4);
        for (uint32_t i = first; i <= last; i++) out.append(reinterpret_cast<const char*>(&s.mine[i]), sizeof(WirePad));
        Send(out.data(), out.size());
    };
    send_inputs();
    // the partner's input for the next frame: needed before the game runs it
    uint32_t next = f + 1;
    auto have = [&] { return s.gone || next <= uint32_t(s.delay) || s.theirs.count(next); };
    auto waited = steady_clock::now();
    bool told = false;
    while (!have()) {
        s.cv.wait_for(l, milliseconds(50));
        if (!have()) send_inputs();  // ours may have been lost: send again
        if (steady_clock::now() - waited > seconds(1)) {
            // the partner's game is behind (loading, its menu open): a line on screen while this one waits
            l.unlock();
            app::Subtitle(LocalPort() == 0 ? "Waiting for player 2..." : "Waiting for player 1...", 0.5);
            l.lock();
            if (!told) LOGR("netplay: waiting for the partner's input (frame %u)", next);
            told = true;
            if (redraw) {  // the picture again with the line over it (the game thread is held here)
                l.unlock();
                redraw();
                l.lock();
            }
        }
        if (steady_clock::now() - s.heard > seconds(kGoneSeconds)) {
            // gone (closed the game without a word, or the connection lost): the game goes on, their player no
            // longer moves; this PC's game alone decides what happens from here
            LOGR("netplay: the partner has gone (nothing at all for %d s at frame %u); playing on alone", kGoneSeconds, next);
            s.gone = true;
            // said to them too (they may still hear us while we don't hear them): they stop waiting for us
            std::string bye(reinterpret_cast<const char*>(&kMagic), 4);
            bye.push_back(char(kBye));
            for (int i = 0; i < 3; i++) Send(bye.data(), bye.size());
        }
    }
    // the partner gone, by goodbye or silence: said on screen once
    if (s.gone && !s.gone_shown) {
        s.gone_shown = true;
        l.unlock();
        app::Subtitle("Your partner has left. Playing on alone.", 5.0);
        l.lock();
    }
    s.current = next;
    // forget what is no longer needed
    while (!s.mine.empty() && s.mine.begin()->first + 64 < f) s.mine.erase(s.mine.begin());
    while (!s.theirs.empty() && s.theirs.begin()->first + 64 < f) s.theirs.erase(s.theirs.begin());
    // both PCs hash the game's memory a slice a frame and compare each second's (frames f-59 to f, named f)
    static uint64_t second_hash = 1469598103934665603ull;
    if (!s.gone) {
        l.unlock();
        second_hash = StateHashSlice(f % kHashSlices, second_hash);
        l.lock();
    }
    if (f % kHashSlices == kHashSlices - 1 && !s.gone) {
        const uint64_t h = second_hash;
        second_hash = 1469598103934665603ull;
        s.my_hash[f] = h;
        s.sent_hash.push_back({f, h});
        if (s.sent_hash.size() > 4) s.sent_hash.erase(s.sent_hash.begin());
        std::string out;
        SendHeader(kHash, out);
        out.push_back(char(s.sent_hash.size()));
        for (auto& [hf, hh] : s.sent_hash) {
            out.append(reinterpret_cast<const char*>(&hf), 4);
            out.append(reinterpret_cast<const char*>(&hh), 8);
        }
        Send(out.data(), out.size());
    }
    while (!s.my_hash.empty() && s.my_hash.begin()->first + 600 < f) s.my_hash.erase(s.my_hash.begin());
    while (!s.their_hash.empty() && s.their_hash.begin()->first + 600 < f) s.their_hash.erase(s.their_hash.begin());
    for (auto it = s.their_hash.begin(); it != s.their_hash.end();) {
        auto mine = s.my_hash.find(it->first);
        if (mine == s.my_hash.end()) {
            ++it;
            continue;
        }
        if (mine->second != it->second) {
            if (!s.desync_told) {
                LOGR("netplay: OUT OF STEP at frame %u: the two games differ from here (different game files, or a "
                     "setting changed on one PC?)", it->first);
                l.unlock();
                app::Subtitle("The two games no longer match. Restart the session to play together again.", 8.0);
                l.lock();
            }
            s.desync_told = true;
        } else if ((it->first + 1) % 600 == 0 && !s.desync_told) {  // every 10th second (frames 599, 1199...)
            LOGI("netplay: in step at frame %u", it->first);
        }
        s.my_hash.erase(mine);
        it = s.their_hash.erase(it);
    }
}

namespace {

// "[section]" -> its lines, in order; text before the first section under ""
std::vector<std::pair<std::string, std::string>> Sections(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out{{"", ""}};
    size_t at = 0;
    while (at < text.size()) {
        size_t e = text.find('\n', at);
        std::string line = text.substr(at, e == std::string::npos ? std::string::npos : e - at + 1);
        at = e == std::string::npos ? text.size() : e + 1;
        size_t b = line.find_first_not_of(" \t");
        if (b != std::string::npos && line[b] == '[') {
            size_t c = line.find(']', b);
            out.push_back({line.substr(b, c == std::string::npos ? std::string::npos : c - b + 1), ""});
        }
        out.back().second += line;
    }
    return out;
}

std::string ReadAll(const std::filesystem::path& p) {
    std::string t;
    if (std::FILE* f = OpenFile(p, "rb")) {
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof(b), f)) > 0) t.append(b, n);
        std::fclose(f);
    }
    return t;
}

// the host's settings with this PC's own sections for what only shows here (the screen, the sound, the buttons) and
// its own part in the session (network: joining, not hosting)
std::string MergeSettings(const std::string& host, const std::string& mine) {
    // what only acts on this PC: input settings act before this player's input is sent (core/input.cpp), so each
    // player keeps their own buttons, keyboard and mouse, dead zones and window behaviour
    static const char* const kLocal[] = {"[display]", "[audio]",  "[controls]", "[network]",     "[input]",
                                         "[keyboard]", "[window]", "[discord]",  "[achievements]"};
    auto is_local = [](const std::string& name) {
        for (const char* l : kLocal)
            if (name == l) return true;
        return false;
    };
    std::string out;
    for (auto& [name, body] : Sections(host))
        if (!is_local(name)) out += body;
    for (auto& [name, body] : Sections(mine))
        if (is_local(name)) out += (out.empty() || out.back() == '\n' ? "" : "\n") + body;
    return out;
}

// before the game starts: the player cancelled (the window), or the partner went quiet for kGoneSeconds: the game ends
// (there is nothing to play without them yet)
void EndBeforeStart(const char* why) {
    LOGR("netplay: %s", why);
    if (g_status) g_status("");
    Finish();
    std::fflush(nullptr);
    std::_Exit(0);
}

void GiveUpIfGone(const char* doing) {
    Session& s = S();
    if (g_cancelled && g_cancelled()) EndBeforeStart("cancelled");
    bool quiet, left;
    {
        std::lock_guard<std::mutex> l(s.m);
        quiet = std::chrono::steady_clock::now() - s.heard > std::chrono::seconds(kGoneSeconds);
        left = s.gone;  // the partner said goodbye (cancelled, or couldn't start)
    }
    if (!left && !quiet) return;
    const std::string why = std::string(left ? "the partner left while " : "the partner stopped answering while ") + doing;
    // said in the window before the game closes (it vanished silently before): the other player cancelled, couldn't
    // start (Athelas in a folder it can't write to) or lost the connection
    if (g_status && g_cancelled) {
        LOGR("netplay: %s", why.c_str());
        g_status(std::string("Online co-op can't start.\n\n") +
                 (left ? "The other player left (they cancelled, or their game couldn't start: their window says why)."
                       : "The other player stopped answering (their game closed, or the connection was lost).") +
                 "\n\nClose this window to quit.");
        while (!g_cancelled() && !(g_windowless && g_windowless())) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EndBeforeStart(why.c_str());
}

bool HostSendFiles() {
    Session& s = S();
    std::vector<std::pair<std::string, std::string>> files;
    // the values this game runs (every setting, defaults for any missing or invalid in the file), not the file as it
    // is: player 2 reads its own file first, so a setting left out here would stay player 2's own and differ
    files.push_back({"settings.toml", cfg::Text(app::SettingsList(), ReadAll(app::SettingsFile()))});
    std::error_code ec;
    std::filesystem::path saves = app::SavesDir();
    for (auto it = std::filesystem::recursive_directory_iterator(saves, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec))
            files.push_back({std::string("saves/") + reinterpret_cast<const char*>(std::filesystem::relative(it->path(), saves, ec).generic_u8string().c_str()),
                             ReadAll(it->path())});
    size_t bytes = 0;
    for (auto& f : files) bytes += f.second.size();
    // what player 2 accepts (Receive): checked here first, so a save it would refuse says why instead of waiting
    for (auto& f : files)
        if (!SafeName(f.first) || f.first.size() > kMaxName || f.second.size() > kMaxFile) {
            s.fail = "A file in your saves folder can't be sent to player 2 (" + f.first.substr(0, 120) +
                     "): its name or size is outside what online co-op takes.";
            return false;
        }
    if (files.size() > kMaxFiles || bytes > kMaxTotal) {
        s.fail = "Your saves folder is too big to send to player 2 (" + std::to_string(files.size()) + " files, " +
                 std::to_string(bytes >> 20) + " MB).";
        return false;
    }
    {
        std::lock_guard<std::mutex> l(s.m);
        s.send_chunks.clear();
        for (auto& f : files)
            s.send_chunks.push_back(std::max<uint32_t>(1, (uint32_t(f.second.size()) + kChunk - 1) / kChunk));
    }
    LOGR("netplay: sending player 2 the settings and %zu save files (%zu KB)", files.size() - 1, bytes / 1024);
    for (;;) {
        bool all = true;
        int sent = 0;
        for (uint32_t i = 0; i < files.size(); i++) {
            const std::string& data = files[i].second;
            uint32_t total = uint32_t(data.size()), chunks = std::max<uint32_t>(1, (total + kChunk - 1) / kChunk);
            for (uint32_t c = 0; c < chunks; c++) {
                {
                    std::lock_guard<std::mutex> l(s.m);
                    if (s.acked.count({i, c})) continue;
                }
                all = false;
                if (sent >= 64) continue;  // a window of 64 chunks a round
                std::string out;
                SendHeader(kFile, out);
                uint16_t nl = uint16_t(files[i].first.size());
                out.append(reinterpret_cast<const char*>(&i), 4);
                out.append(reinterpret_cast<const char*>(&c), 4);
                out.append(reinterpret_cast<const char*>(&total), 4);
                out.append(reinterpret_cast<const char*>(&nl), 2);
                out += files[i].first;
                out.append(data, size_t(c) * kChunk, std::min<size_t>(kChunk, total - std::min<size_t>(total, size_t(c) * kChunk)));
                Send(out.data(), out.size());
                sent++;
            }
        }
        if (all) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        GiveUpIfGone("sending player 2 the settings and saves");
    }
    uint32_t count = uint32_t(files.size());
    for (;;) {
        std::string out;
        SendHeader(kFilesDone, out);
        out.append(reinterpret_cast<const char*>(&count), 4);
        out.push_back(char(s.delay));
        Send(out.data(), out.size());
        {
            std::unique_lock<std::mutex> l(s.m);
            if (s.cv.wait_for(l, std::chrono::milliseconds(200), [&] { return s.files_done_acked; })) break;
        }
        GiveUpIfGone("sending player 2 the settings and saves");
    }
    {
        std::lock_guard<std::mutex> l(s.m);
        s.send_chunks.clear();  // no acknowledgements taken from here on
        s.acked.clear();
    }
    LOGR("netplay: player 2 has the settings and saves");
    return true;
}

void GuestTakeFiles() {
    Session& s = S();
    {
        std::unique_lock<std::mutex> l(s.m);
        while (!s.cv.wait_for(l, std::chrono::milliseconds(200), [&] {
            if (!s.files_done || s.files.size() < s.files_expected) return false;
            for (auto& [name, f] : s.files)
                for (bool g : f.got)
                    if (!g) return false;
            return true;
        })) {
            l.unlock();
            GiveUpIfGone("receiving player 1's settings and saves");
            l.lock();
        }
    }
    // the session folder beside the exe: last session's copies moved into "DELETE ME" first (never deleted here), so
    // nothing old mixes in
    std::error_code ec;
    std::filesystem::path dir = app::ExeDir() / "online-session";
    if (std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(app::ExeDir() / "DELETE ME", ec);
        std::filesystem::rename(dir, app::ExeDir() / "DELETE ME" /
                                         ("online-session-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count())), ec);
        // couldn't be moved (a file in it held open: OneDrive, antivirus, an Explorer window): the new copies would
        // land among the old ones, and old saves would make the two games differ
        if (std::filesystem::exists(dir, ec)) {
            if (s.fail.empty())
                s.fail = "Last session's copy (" + dir.string() + ") couldn't be moved aside: a program may have a file in "
                         "it open. Close it (or move that folder yourself) and try again.";
            std::lock_guard<std::mutex> l(s.m);
            s.files_done_acked = true;  // no more files taken
            s.files.clear();
            return;
        }
    }
    std::filesystem::create_directories(dir / "saves", ec);
    size_t count = 0;
    for (auto& [name, f] : s.files) {
        // SafeName: plain relative parts only; checked again here, as the folder it lands in must be the session's
        std::filesystem::path p = (dir / std::filesystem::path(reinterpret_cast<const char8_t*>(name.c_str()))).lexically_normal();
        auto rel = p.lexically_relative(dir);
        if (rel.empty() || *rel.begin() == "..") continue;
        std::string text(f.data.begin(), f.data.end());
        if (name == "settings.toml") text = MergeSettings(text, ReadAll(app::SettingsFile()));
        std::filesystem::create_directories(p.parent_path(), ec);
        // every file must land: a missing settings file or save would start a different game here (it drifts apart)
        bool written = false;
        if (std::FILE* out = OpenFile(p, "wb")) {
            written = std::fwrite(text.data(), 1, text.size(), out) == text.size();
            written = std::fclose(out) == 0 && written;
        }
        if (!written && s.fail.empty())
            s.fail = "Can't write player 1's settings and saves into " + dir.string() +
                     " (the game's folder must be one you can write to, not Program Files). Move Athelas to another "
                     "folder and try again.";
        count++;
    }
    {
        std::lock_guard<std::mutex> l(s.m);
        s.files_done_acked = true;  // no more files taken from here on
        s.files.clear();            // written: not kept in memory for the session
    }
    if (!s.fail.empty()) return;  // not acknowledged: player 1 isn't left starting a game for nobody
    {
        std::lock_guard<std::mutex> l(s.m);
        s.files_written = true;
    }
    s.session_settings = dir / "settings.toml";
    s.session_saves = dir / "saves";
    std::string out;
    SendHeader(kFilesDoneAck, out);
    for (int i = 0; i < 5; i++) Send(out.data(), out.size());  // the host stops resending once one arrives
    LOGR("netplay: playing with player 1's settings and saves (%zu files, in %s)", count, dir.string().c_str());
}

}  // namespace

void (*g_status)(const std::string& text) = nullptr;
bool (*g_cancelled)() = nullptr;
bool (*g_windowless)() = nullptr;

void Prepare() {
    Started();
    Session& s = S();
    if (!s.on) return;  // online co-op not chosen
    auto status = [](const std::string& t) {
        if (g_status) g_status(t);
    };
    // the session can't start: said in the window until the player closes it, then the game ends (never a quiet
    // game alone when online co-op was chosen)
    auto fail = [&](const std::string& why) {
        LOGE("netplay: %s", why.c_str());
        {
            // the partner is told at once (the heartbeat sends goodbye from now on), not when this window closes
            std::lock_guard<std::mutex> l(s.m);
            s.gone = true;
        }
        if (g_status && g_cancelled) {
            status("Online co-op can't start.\n\n" + why + "\n\nClose this window to quit.");
            while (!g_cancelled() && !(g_windowless && g_windowless()))
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        EndBeforeStart("the session couldn't start");
    };
    if (!s.fail.empty()) fail(s.fail);
    const std::string firewall = "\n\nIf Windows asks whether to let the game use the network, allow it.";
    auto host_text = [&] {
        std::string t = s.my_address.empty()
                            ? "Online co-op: waiting for player 2 to join.\n\nYour friend types " +
                                  (s.internet_ip.empty() ? std::string("your internet address and port ") + std::to_string(ntohs(s.port))
                                                         : s.internet_ip + ":" + std::to_string(ntohs(s.port))) +
                                  " on their Network tab." +
                                  (s.behind_shared ? std::string()
                                                   : "\n\nYour router didn't open the port by itself: forward UDP port " +
                                                         std::to_string(ntohs(s.port)) + " to this PC in the router's settings first.")
                            : "Online co-op: waiting for player 2 to join.\n\nYour friend types this on their Network tab:\n\n    " +
                                  s.my_address;
        if (s.behind_shared)
            t += "\n\nYour router opened the port, but another router or your internet provider sits in front of it, "
                 "so player 2 may not get through. If they can't, let player 2 host instead, or use streaming (see the "
                 "README).";
        if (!s.lan_ip.empty())
            t += "\n\nA friend on the same home network types " + s.lan_ip + ":" + std::to_string(ntohs(s.port)) + " instead.";
        if (s.refused_someone)
            t += "\n\nSomeone with a different version of Athelas tried to join: both players need the same update, on the "
                 "same kind of PC (a Windows on ARM laptop and a regular PC can't play together).";
        return t + firewall;
    };
    {
        std::unique_lock<std::mutex> l(s.m);
        LOGR("netplay: waiting for %s", s.host ? "player 2 to join" : "player 1");
        bool refused_shown = false, hint_shown = false;
        const auto began = std::chrono::steady_clock::now();
        if (s.host)
            status(host_text());
        else
            status("Online co-op: joining player 1's game...");
        while (!s.connected) {
            if (!s.host) {
                std::string out;
                SendHeader(kHello, out);
                uint64_t h = ExeHash();
                out.append(reinterpret_cast<const char*>(&h), 8);
                Send(out.data(), out.size());
            }
            s.cv.wait_for(l, std::chrono::milliseconds(200));
            if (!s.fail.empty()) {
                std::string why = s.fail;
                l.unlock();
                fail(why);
            }
            if (!s.host && !hint_shown && std::chrono::steady_clock::now() - began > std::chrono::seconds(10)) {
                hint_shown = true;
                l.unlock();
                status("Online co-op: joining player 1's game... (no answer yet)\n\nCheck that player 1's game is waiting "
                       "for you, that the address and port are the ones it shows, and that player 1 allowed the game "
                       "through Windows Firewall (or forwarded the port on their router).");
                l.lock();
            }
            if (s.host && s.refused_someone && !refused_shown) {
                refused_shown = true;
                l.unlock();
                status(host_text());
                l.lock();
            }
            if (g_cancelled && g_cancelled()) {
                l.unlock();
                EndBeforeStart("cancelled while waiting");
            }
        }
    }
    if (s.host) {
        // the round trip to player 2 (a few pings): the input delay both use, unless set by hand
        for (int i = 0; i < 8; i++) {
            std::string out;
            SendHeader(kPing, out);
            {
                std::lock_guard<std::mutex> l(s.m);
                s.ping_sent_us = NowUs();
                out.append(reinterpret_cast<const char*>(&s.ping_sent_us), 8);
            }
            Send(out.data(), out.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
        std::lock_guard<std::mutex> l(s.m);
        if (!s.rtt_us.empty()) {
            std::sort(s.rtt_us.begin(), s.rtt_us.end());
            int64_t rtt = s.rtt_us[s.rtt_us.size() / 2];
            // the input must cross before it is needed: half the round trip, in frames, and two to spare
            if (!s.delay_set) s.delay = std::clamp(int(2 + (rtt / 2 + 16666) / 16667), 3, kMaxDelay);
            LOGR("netplay: round trip %lld ms, input delay %d frames", (long long)(rtt / 1000), s.delay);
        }
    }
    status(s.host ? "Online co-op: player 2 joined. Sending them your settings and saves..."
                  : "Online co-op: joined. Receiving player 1's settings and saves...");
    if (s.host) {
        if (!HostSendFiles()) fail(s.fail);
    } else {
        GuestTakeFiles();
        if (!s.fail.empty()) fail(s.fail);
    }
    if (!s.host) LOGR("netplay: input delay %d frames (player 1's choice)", s.delay);
    status("");
}

std::filesystem::path SessionSettings() { return S().session_settings; }
std::filesystem::path SessionSaves() { return S().session_saves; }

input::Pad Pad(int port) {
    Session& s = S();
    input::Pad p;
    if (port > 1) return p;  // ports 3 and 4: nobody
    p.connected = true;
    std::lock_guard<std::mutex> l(s.m);
    uint32_t f = s.current;
    if (f <= uint32_t(s.delay)) return p;  // the first frames: nobody presses anything
    if (port != LocalPort() && s.gone) return p;  // the partner left: their player stands still
    auto& from = port == LocalPort() ? s.mine : s.theirs;
    auto it = from.find(f);
    if (it != from.end()) p = FromWire(it->second);
    return p;
}

}  // namespace netplay
