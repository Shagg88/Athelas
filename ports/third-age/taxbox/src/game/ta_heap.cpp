// The Third Age: what fills the game's own memory heaps when one runs out (GitHub #85, 14 Nov 2026).
//
// A save made after travelling from Rohan back to Eregion crashes as Eregion loads: the asset loader (0x1F4A20) makes a
// new asset (0x189300), whose allocator (0x16BDA0) gets nothing from the scene's heap and the loader writes through the
// null result. The heap allocator: eax the alignment, ecx flags, [esp+4] the size; with flags & 0x0E == 8 it takes the
// heap at [0x31A488] (it first asks 0xE7740 for that heap's free bytes and largest block, then allocates with 0x103A60);
// otherwise 0x16BAB0. A heap (static research): [+0] its first block, [+0x0C] free bytes, [+0x10] the largest free
// block, [+0x14] "sums out of date"; a block: [+4] the next, [+8] bit 0 in use, [+0x0C] its size. The other heaps
// are listed at 0x31A5CC ([0x31A5C4] of them).
//
// This logs, once per failing size, the request, the console's memory and the scene heap's blocks (its record is
// *[0x31A488]: [+0] the first block, [+0x0C] free bytes, [+0x10] the largest free block, [+0x14] "measure again"),
// so a report shows how full it is. It changes nothing in the game.
//
// Found with the player's save (14 Nov 2026, notes/ta-85-scene-heap.md): the scene heap (about 512 KB) is really
// full - 256 blocks, 505 KB in use, 7 KB free - when the next shader asset (SHDR/scbm, 9.7 KB) is made. Not the port's
// memory (5 MB of the 64 free), not the general heap (28 MB, all it asked for; 4 MB more didn't help), not a stale
// "largest free" figure (re-measuring didn't help). The fix to look for: where the scene heap is made and sized.
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace {

// a guest address the report may read: committed memory, all n bytes (review: a stray value or a damaged link
// reading uncommitted memory would make the report itself the crash)
bool Readable(uint32_t a, uint32_t n) {
    return a && a < 0x80000000u && a + n > a && mem::IsCommitted(a) && mem::IsCommitted(a + n - 1);
}

struct HeapSums {
    uint32_t blocks = 0, used = 0, used_bytes = 0, free_bytes = 0, largest = 0;
    bool bad = false;  // a loop or an address outside memory: walked no further
};

HeapSums Walk(uint32_t heap) {
    HeapSums s;
    if (!Readable(heap, 4)) { s.bad = true; return s; }
    uint32_t b = ::kernel::R32(heap);
    for (uint32_t n = 0; b; n++) {
        if (n > 200000 || !Readable(b, 0x10)) { s.bad = true; break; }
        uint32_t flags = ::kernel::R32(b + 8), size = ::kernel::R32(b + 0x0C);
        s.blocks++;
        if (flags & 1) { s.used++; s.used_bytes += size; }
        else { s.free_bytes += size; if (size > s.largest) s.largest = size; }
        b = ::kernel::R32(b + 4);
    }
    return s;
}

void LogHeap(const char* what, uint32_t heap) {
    HeapSums s = Walk(heap);
    LOGR("ta: heap %s %08X: %u blocks (%u in use, %u KB), %u KB free, largest free %u KB%s", what, heap, s.blocks, s.used,
         s.used_bytes / 1024, s.free_bytes / 1024, s.largest / 1024, s.bad ? " (walk stopped: damaged list?)" : "");
}

std::string Tag(uint32_t v) {
    std::string t;
    for (int i = 3; i >= 0; i--) {
        char c = char((v >> (i * 8)) & 0xFF);
        t += c >= 32 && c < 127 ? c : '.';
    }
    return t;
}

}  // namespace

X86_EXTERN_FUNC(__imp__sub_0016BDA0);
X86_FUNC(sub_0016BDA0) {
    uint32_t size = ::kernel::R32(ctx.esp + 4), flags = ctx.ecx, align = ctx.eax, asset = ctx.esi;
    // the flags it really uses: [0x282698] instead when [0x28269C] is set and flags bit 13 is clear
    uint32_t eff = ((::kernel::R16(0x28269C) & 0xFF) && !((flags >> 13) & 1)) ? ::kernel::R32(0x282698) : flags;
    __imp__sub_0016BDA0(ctx, base);
    if (ctx.eax) return;
    static std::mutex mu;
    std::lock_guard lock(mu);  // (the allocator may run on more than one game thread)
    static std::set<uint32_t> logged;
    if (logged.size() > 16 || !logged.insert(size).second) return;
    LOGR("ta: the game's heap allocator found no room: %u bytes (alignment %u, flags %08X, used as %08X: %s)", size, align,
         flags, eff, (eff & 0x0E) == 8 ? "a scene heap" : "the general heap");
    if (Readable(asset + 0x10, 0x14))
        LOGR("ta:   for an asset %s/%s, its sizes %u + %u", Tag(::kernel::R32(asset + 0x10)).c_str(),
             Tag(::kernel::R32(asset + 0x18)).c_str(), ::kernel::R32(asset + 0x1C), ::kernel::R32(asset + 0x20));
    // the console's memory as the port counts it: is all of it used (the port taking more than a real Xbox would), or
    // only the game's own heap?
    mem::Stats m = mem::Query();
    LOGR("ta:   console memory: %u of %u KB free (virtual %u KB, contiguous %u KB, system %u KB)",
         m.available_pages * 4, m.total_pages * 4, m.virtual_committed * 4, m.contiguous_used * 4, m.system_used * 4);
    uint32_t h = ::kernel::R32(0x31A488);
    if (Readable(h, 4) && Readable(::kernel::R32(h), 0x18)) {
        uint32_t hs = ::kernel::R32(h);   // the heap's own record (0x16BDA0: mov edx, [eax])
        LOGR("ta:   scene heap %08X (record %08X): remembered %u KB free, largest %u bytes, re-measure flag %u",
             h, hs, ::kernel::R32(hs + 0x0C) / 1024, ::kernel::R32(hs + 0x10), ::kernel::R16(hs + 0x14) & 0xFF);
        LogHeap("measured now", hs);
    }
}

// The general heap's block (0x15D1C0): the game asks the C runtime (0x7E96F) for 0x1B86000 - [0x311C34] bytes and,
// while that fails, 16 KB less each time; then 0x15CDB0 (edx the block, eax its size) sets the heap up in it. Logged:
// what it wanted and what it got, with the console's memory then (#85: a smaller heap than on an Xbox would run out).
// (0x15CDB0 is not a function of its own in the recompiled game - 0x15D1C0 ends by jumping to it - so the routine
// that sizes the block is hooked, and what it settled on read after it: [0x2826A0] the size, [0x282694] the block)
X86_EXTERN_FUNC(__imp__sub_0015D1C0);
X86_FUNC(sub_0015D1C0) {
    // TA_HEAP_EXTRA_KB (tests, #85): the heap asks for that much more (what is subtracted from its budget, [0x311C34],
    // lowered by it while it asks, then put back)
    uint32_t extra = 0;
    if (const char* e = std::getenv("TA_HEAP_EXTRA_KB")) extra = uint32_t(std::strtoul(e, nullptr, 10)) * 1024u;
    // ([0x311C34] is 0 when it runs, so it goes below 0: the game's sub esi, eax then gives 0x1B86000 + extra, which its
    // checks take as an ordinary size)
    // (written only with the test switch: without it the game's memory is not touched at all, review)
    uint32_t used = ::kernel::R32(0x311C34);
    if (extra) ::kernel::W32(0x311C34, used - extra);
    uint32_t want = 0x1B86000u - (used - extra);
    mem::Stats m = mem::Query();
    __imp__sub_0015D1C0(ctx, base);
    if (extra) ::kernel::W32(0x311C34, used);
    if (extra) LOGR("ta: the general heap asks for %u KB more (TA_HEAP_EXTRA_KB; [0x311C34] was %u KB)", extra / 1024, used / 1024);
    LOGR("ta: the game's general heap: %u KB at %08X (it asked for %u KB); console memory before it %u of %u KB free",
         ::kernel::R32(0x2826A0) / 1024, ::kernel::R32(0x282694), want / 1024, m.available_pages * 4, m.total_pages * 4);
}

// The game's heaps of each kind are made from a table in its code (0x275B38: kind, size, how many, blocks), each by
// 0x17B510 (edx the kind, cx the most blocks, [esp+4] the size). The three of kind 1024 (flags 0x400: the scene heaps
// 0x16BDA0 uses for shaders and models) are 512 KB. #85: a save made after travelling back to Eregion with the party's
// later gear needs more than that there (257 blocks, 505 KB used, a 9.5 KB shader left over), and the game crashes
// instead of handling it; the console has about 5 MB free then. They are made 768 KB (TA_SCENE_HEAP_KB to change it,
// 512 the game's own), from the general heap (28 MB, not full when it happens).
X86_EXTERN_FUNC(__imp__sub_0017B510);
X86_FUNC(sub_0017B510) {
    const uint32_t kind = ctx.edx, size_at = ctx.esp + 4, size = ::kernel::R32(size_at);
    uint32_t want = 768;
    if (const char* e = std::getenv("TA_SCENE_HEAP_KB")) want = uint32_t(std::strtoul(e, nullptr, 10));
    const bool bigger = kind == 1024 && size == 0x80000u && want > 512 && want <= 4096;
    if (bigger) ::kernel::W32(size_at, want * 1024u);
    __imp__sub_0017B510(ctx, base);
    if (bigger) {
        ::kernel::W32(size_at, size);  // the caller's stack as it was
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 3) LOGR("ta: a scene heap made %u KB instead of 512 (#85)", want);
    }
}
