#include "kernel/disc.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace kernel {

namespace {

constexpr uint32_t kSector = 2048;
// the game partition: at the start of an xiso, past the video partition of a whole original Xbox disc dump
constexpr uint64_t kPartitions[] = {0, 0x18300000};
// volume descriptor, sector 32 of the partition: magic, root table sector and size, FILETIME, magic again at +0x7EC
constexpr uint32_t kVolume = 32 * kSector;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kMagicSize = sizeof(kMagic) - 1;
// A folder is a table of entries in a binary tree: +0 left and +2 right entry (in 4-byte units, 0 for none),
// +4 sector, +8 size, +0xC attributes (0x10 folder), +0xD name length, +0xE name. A table of 0xFF is empty.
constexpr uint32_t kEntryHead = 0xE;
constexpr uint8_t kFolder = 0x10;
constexpr uint32_t kMaxTable = 16 << 20;

std::unique_ptr<Disc> g_mounted;

// Small reads during play (a level's streamed parts, sound, video) are served from blocks of the image held in
// memory, and the block after one being read is fetched in the background. A drive or file system with a high
// cost per read then costs once per megabyte: on an ARM laptop 64 KB stream reads took 30-80 ms each (issue 12).
// Reads of half a block or more (whole files at a level load) go to the file directly.
constexpr uint32_t kBlock = 1u << 20;
constexpr size_t kBlocks = 48;
constexpr uint32_t kDirect = kBlock / 2;
constexpr size_t kMaxAhead = 4;

bool SameName(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(uint8_t(x)) == std::toupper(uint8_t(y));
           });
}

std::string Upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

}  // namespace

struct Disc::Cache {
    struct Block {
        uint64_t index = ~0ull;
        std::vector<uint8_t> data;
        uint32_t size = 0;
        uint64_t used = 0;
        bool ready = false;
    };
    const Disc* disc;
    std::mutex m;
    std::condition_variable filled, work;
    std::vector<Block> blocks{kBlocks};
    uint64_t tick = 0;
    std::deque<uint64_t> ahead;
    bool stop = false;
    std::thread worker;

    explicit Cache(const Disc* d) : disc(d) {
        worker = std::thread([this] {
            std::unique_lock<std::mutex> lock(m);
            for (;;) {
                work.wait(lock, [this] { return stop || !ahead.empty(); });
                if (stop) return;
                uint64_t index = ahead.front();
                ahead.pop_front();
                if (!Find(index)) Fill(index, lock);
            }
        });
    }
    ~Cache() {
        {
            std::lock_guard<std::mutex> lock(m);
            stop = true;
        }
        work.notify_all();
        worker.join();
    }
    Block* Find(uint64_t index) {
        for (Block& b : blocks)
            if (b.index == index) return &b;
        return nullptr;
    }
    // with the lock held; the block read happens without it, the block marked as not ready meanwhile
    Block* Fill(uint64_t index, std::unique_lock<std::mutex>& lock) {
        Block* victim = nullptr;
        for (Block& b : blocks)
            if (b.ready || b.index == ~0ull)
                if (!victim || b.used < victim->used) victim = &b;
        if (!victim) return nullptr;  // every block is being read
        victim->index = index;
        victim->ready = false;
        victim->used = ++tick;
        if (victim->data.size() != kBlock) victim->data.resize(kBlock);
        lock.unlock();
        uint32_t got = disc->ReadAt(index * kBlock, victim->data.data(), kBlock);
        lock.lock();
        victim->size = got;
        victim->ready = true;
        if (!got) victim->index = ~0ull;  // a failed read is tried again next time
        filled.notify_all();
        return victim;
    }
};

Disc::Disc() = default;

Disc::~Disc() {
    cache_.reset();
    if (file_ == -1) return;
#ifdef _WIN32
    CloseHandle(HANDLE(file_));
#else
    close(int(file_));
#endif
}

// each read carries its own position, so threads reading at once do not queue on one handle
uint32_t Disc::ReadAt(uint64_t at, void* out, uint32_t len) const {
#ifdef _WIN32
    thread_local HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OVERLAPPED o{};
    o.Offset = DWORD(at);
    o.OffsetHigh = DWORD(at >> 32);
    o.hEvent = event;
    DWORD got = 0;
    if (!ReadFile(HANDLE(file_), out, len, nullptr, &o) && GetLastError() != ERROR_IO_PENDING) return 0;
    if (!GetOverlappedResult(HANDLE(file_), &o, &got, TRUE)) return 0;
    return got;
#else
    ssize_t got = pread(int(file_), out, len, off_t(at));
    return got > 0 ? uint32_t(got) : 0;
#endif
}

bool Disc::Open(const std::filesystem::path& path, std::string& error) {
    path_ = path;
#ifdef _WIN32
    file_ = intptr_t(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
#else
    file_ = open(path.c_str(), O_RDONLY);
#endif
    if (file_ == -1) {
        error = "cannot open the file";
        return false;
    }
    for (uint64_t base : kPartitions) {
        uint8_t volume[kSector];
        if (ReadAt(base + kVolume, volume, kSector) != kSector || std::memcmp(volume, kMagic, kMagicSize) != 0 ||
            std::memcmp(volume + 0x7EC, kMagic, kMagicSize) != 0)
            continue;
        base_ = base;
        uint32_t sector, size;
        std::memcpy(&sector, volume + 0x14, 4);
        std::memcpy(&size, volume + 0x18, 4);
        std::memcpy(&time_, volume + 0x1C, 8);
        root_ = {{}, base + uint64_t(sector) * kSector, size, true, {}};
        if (!ReadDir(root_, 0)) {
            error = "its folders cannot be read";
            return false;
        }
        cache_ = std::make_unique<Cache>(this);
        return true;
    }
    error = "no Xbox disc image";
    return false;
}

bool Disc::ReadDir(Entry& dir, int depth) {
    if (!dir.size) return true;
    if (depth > 32 || dir.size > kMaxTable) return false;
    std::vector<uint8_t> table(dir.size);
    if (ReadAt(dir.offset, table.data(), dir.size) != dir.size) return false;
    std::vector<bool> seen(dir.size / 4 + 1);
    std::vector<uint32_t> todo{0};
    while (!todo.empty()) {
        uint32_t at = todo.back();
        todo.pop_back();
        if (at + kEntryHead > dir.size || seen[at / 4]) continue;
        seen[at / 4] = true;
        uint16_t left, right;
        uint32_t sector, size;
        std::memcpy(&left, &table[at], 2);
        std::memcpy(&right, &table[at + 2], 2);
        if (left == 0xFFFF) continue;
        std::memcpy(&sector, &table[at + 4], 4);
        std::memcpy(&size, &table[at + 8], 4);
        uint8_t attributes = table[at + 0xC], length = table[at + 0xD];
        if (at + kEntryHead + length > dir.size) continue;
        if (left) todo.push_back(uint32_t(left) * 4);
        if (right) todo.push_back(uint32_t(right) * 4);
        dir.children.push_back({std::string(reinterpret_cast<const char*>(&table[at + kEntryHead]), length),
                                base_ + uint64_t(sector) * kSector, size, (attributes & kFolder) != 0, {}});
    }
    std::sort(dir.children.begin(), dir.children.end(),
              [](const Entry& a, const Entry& b) { return Upper(a.name) < Upper(b.name); });
    for (Entry& child : dir.children)
        if (child.dir && !ReadDir(child, depth + 1)) return false;
    return true;
}

const Disc::Entry* Disc::Find(const std::vector<std::string>& parts) const {
    std::vector<const Entry*> path{&root_};
    for (const std::string& part : parts) {
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (path.size() > 1) path.pop_back();
            continue;
        }
        const Entry* dir = path.back();
        auto it = std::find_if(dir->children.begin(), dir->children.end(), [&](const Entry& e) { return SameName(e.name, part); });
        if (it == dir->children.end()) return nullptr;
        path.push_back(&*it);
    }
    return path.back();
}

const Disc::Entry* Disc::Find(std::string_view path) const {
    std::vector<std::string> parts;
    size_t s = 0;
    while (s <= path.size()) {
        size_t e = std::min(path.find('\\', s), path.size());
        parts.emplace_back(path.substr(s, e - s));
        s = e + 1;
    }
    return Find(parts);
}

uint32_t Disc::ReadCached(uint64_t at, void* out, uint32_t len) const {
    if (!cache_ || len >= kDirect) return ReadAt(at, out, len);
    Cache& c = *cache_;
    std::unique_lock<std::mutex> lock(c.m);
    uint32_t done = 0;
    while (done < len) {
        uint64_t index = (at + done) / kBlock;
        Cache::Block* b = c.Find(index);
        while (b && !b->ready) {
            c.filled.wait(lock);
            b = c.Find(index);
        }
        if (!b) b = c.Fill(index, lock);
        if (!b) {  // no block free: read past the cache
            lock.unlock();
            return done + ReadAt(at + done, static_cast<uint8_t*>(out) + done, len - done);
        }
        b->used = ++c.tick;
        uint32_t in = uint32_t(at + done - index * kBlock);
        if (in >= b->size) break;  // the image's end
        uint32_t n = std::min(len - done, b->size - in);
        std::memcpy(static_cast<uint8_t*>(out) + done, b->data.data() + in, n);
        done += n;
        // past the middle of a block, the next one is wanted soon
        if (in + n > kBlock / 2 && b->size == kBlock && !c.Find(index + 1) && c.ahead.size() < kMaxAhead &&
            std::find(c.ahead.begin(), c.ahead.end(), index + 1) == c.ahead.end()) {
            c.ahead.push_back(index + 1);
            c.work.notify_one();
        }
    }
    return done;
}

uint32_t Disc::Read(const Entry& file, uint64_t pos, void* out, uint32_t len) const {
    if (file.dir || pos >= file.size) return 0;
    return ReadCached(file.offset + pos, out, uint32_t(std::min<uint64_t>(len, file.size - pos)));
}

bool Disc::Load(std::string_view path, std::vector<uint8_t>& out) const {
    const Entry* e = Find(path);
    if (!e || e->dir) return false;
    out.resize(e->size);
    return Read(*e, 0, out.data(), e->size) == e->size;
}

void MountDisc(std::unique_ptr<Disc> disc) { g_mounted = std::move(disc); }

const Disc* MountedDisc() { return g_mounted.get(); }

}  // namespace kernel
