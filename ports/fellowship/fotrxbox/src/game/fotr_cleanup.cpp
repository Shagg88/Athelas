// FOTR Xbox: a safe clean-up for the objects the scene change deletes (#21).
//
// After a scene change the game's clean-up pass (0x113960 -> 0x8C490 -> 0x8C100) deletes the objects nobody uses
// any more through their virtual deleting destructor (0x2AEB0 -> 0x1249B0). A player's crash entering Bywater
// (Athelas 1.5, 144 fps) was in 0x1249B0 reading a text field that held 3F398AB2, a float (0.725): the object's
// type was intact but some of its fields had been written over with floats, as by code still writing a position
// into memory that had been freed and reused for this object. The writer isn't known (no save to reproduce it yet).
//
// So before the destructor runs, each field it releases is checked: its text fields (+0x14, +0x24, +0x28, each the
// game's string record or the empty string 0x1E2C78), the buffer it frees (+0x1C) and the object it releases
// (+0x20, when +0x18 is 1). One that doesn't point at memory (or whose string record doesn't) is left out of the
// clean-up: a few bytes stay allocated, instead of the game closing.
#include <cstdint>
#include <cstdio>
#include <algorithm>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/memory.h"

namespace {
constexpr uint32_t kEmptyString = 0x1E2C78;

bool Readable(uint32_t va) { return va >= 0x10000 && mem::IsCommitted(va) && mem::IsCommitted(va + 3); }

// a string field: the empty string, or a record whose first word is its (readable or null) buffer
bool StringOk(uint8_t* base, uint32_t field) {
    uint32_t p = X86_MEM32(field);
    if (p == kEmptyString) return true;
    if (!Readable(p)) return false;
    uint32_t buf = X86_MEM32(p);
    return buf == 0 || Readable(buf);
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_001249B0);
X86_FUNC(sub_001249B0) {
    uint32_t obj = ctx.ecx;
    if (Readable(obj) && Readable(obj + 0x28)) {
        int fixed = 0;
        for (uint32_t off : {0x14u, 0x24u, 0x28u})
            if (!StringOk(base, obj + off)) {
                X86_MEM32(obj + off) = kEmptyString;
                fixed++;
            }
        uint32_t buf = X86_MEM32(obj + 0x1C);
        if (buf && !Readable(buf)) {
            X86_MEM32(obj + 0x1C) = 0;
            fixed++;
        }
        uint32_t owned = X86_MEM32(obj + 0x20);
        if (X86_MEM32(obj + 0x18) == 1 && owned && (!Readable(owned) || !Readable(X86_MEM32(owned)))) {
            X86_MEM32(obj + 0x20) = 0;
            fixed++;
        }
        if (fixed) LOG_FIRST(8, "fotr: clean-up of %08X: %d field(s) written over, left out (see #21)", obj, fixed);
    }
    __imp__sub_001249B0(ctx, base);
}

#ifndef _WIN32
// Testing only, in the Linux test build: FOTR_HEAPCHECK=1 looks for the writer. Blocks the game frees (RtlFreeHeap,
// 0x7416F, statically linked; RtlAllocateHeap 0x739B8) are filled with a pattern and held back for a while instead
// of going back to the heap; when one is finally freed, any word of it that no longer holds the pattern was written
// after the free. Logged with the block's size, its first word at the free (an object's type), and the code
// addresses found on the stack at its allocation and at its free.
#include <cstdlib>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint32_t kPoison = 0xFEEEFEEEu;
constexpr uint32_t kMaxBlock = 4096, kMaxHeld = 1u << 20;
struct Held {
    uint32_t ptr, size, heap, flags, type;
    uint32_t alloc_at[6], free_at[6];
};
struct Live {
    uint32_t size;
    uint32_t at[6];
};
std::mutex g_heap_lock;
std::unordered_map<uint32_t, Live> g_live;
std::deque<Held> g_held;
uint32_t g_held_bytes = 0, g_reports = 0;
uint64_t g_frees = 0, g_checked = 0;

bool HeapCheck() {
    static int on = -1;
    if (on < 0) on = std::getenv("FOTR_HEAPCHECK") != nullptr;
    return on;
}
// code addresses on the guest stack, nearest first: the calls that led here (approximately)
void Callers(uint8_t* base, uint32_t esp, uint32_t* out) {
    int n = 0;
    for (uint32_t i = 0; i < 96 && n < 6; i++) {
        uint32_t v = X86_MEM32(esp + i * 4);
        if (v >= 0x11000 && v < 0x1B0000 && v != 0x74150 && v != 0x74B35 && v != 0x7434A && v != 0x74B0F) out[n++] = v;
    }
    for (; n < 6; n++) out[n] = 0;
}
void Check(uint8_t* base, const Held& h) {
    uint32_t bad = 0, first = 0;
    char vals[200];
    int len = 0;
    for (uint32_t o = 0; o + 4 <= h.size; o += 4) {
        uint32_t v = X86_MEM32(h.ptr + o);
        if (v == kPoison) continue;
        if (!bad++) first = o;
        if (len < 150) len += std::snprintf(vals + len, sizeof vals - len, " +%X=%08X", o, v);
    }
    if (!bad || g_reports >= 200) return;
    g_reports++;
    LOGR("heapcheck: block %08X (%u bytes, type %08X) written %u word(s) after its free, first +%X:%s | alloc %X %X %X %X %X %X | free %X %X %X %X %X %X",
         h.ptr, h.size, h.type, bad, first, vals, h.alloc_at[0], h.alloc_at[1], h.alloc_at[2], h.alloc_at[3],
         h.alloc_at[4], h.alloc_at[5], h.free_at[0], h.free_at[1], h.free_at[2], h.free_at[3], h.free_at[4], h.free_at[5]);
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000739B8);
X86_FUNC(sub_000739B8) {  // RtlAllocateHeap(heap, flags, size): stdcall
    if (!HeapCheck()) return __imp__sub_000739B8(ctx, base);
    uint32_t esp = ctx.esp, size = X86_MEM32(esp + 12);
    Live l{size, {}};
    Callers(base, esp, l.at);
    __imp__sub_000739B8(ctx, base);
    if (ctx.eax) {
        std::lock_guard lock(g_heap_lock);
        g_live[ctx.eax] = l;
    }
}

X86_EXTERN_FUNC(__imp__sub_0007416F);
X86_FUNC(sub_0007416F) {  // RtlFreeHeap(heap, flags, ptr): stdcall, returns al
    if (!HeapCheck()) return __imp__sub_0007416F(ctx, base);
    uint32_t esp = ctx.esp, ptr = X86_MEM32(esp + 12);
    std::unique_lock lock(g_heap_lock);
    auto it = ptr ? g_live.find(ptr) : g_live.end();
    if (it == g_live.end() || it->second.size > kMaxBlock || it->second.size < 8) {
        if (it != g_live.end()) g_live.erase(it);
        lock.unlock();
        return __imp__sub_0007416F(ctx, base);
    }
    Held h{ptr, it->second.size, X86_MEM32(esp + 4), X86_MEM32(esp + 8), X86_MEM32(ptr), {}, {}};
    std::copy(it->second.at, it->second.at + 6, h.alloc_at);
    Callers(base, esp, h.free_at);
    g_live.erase(it);
    g_frees++;
    for (uint32_t o = 0; o + 4 <= h.size; o += 4) X86_MEM32(ptr + o) = kPoison;
    g_held.push_back(h);
    g_held_bytes += h.size;
    if (g_held_bytes <= kMaxHeld) {  // held: the game is told it was freed
        static bool said = false;
        if (!said) said = true, LOGR("heapcheck: on (freed blocks up to %u bytes held back, %u MB in all)", kMaxBlock, kMaxHeld >> 20);
        ctx.eax = 1;
        ctx.esp += 4 + 12;
        return;
    }
    // the oldest held block is checked and freed in this call's place
    Held old = g_held.front();
    g_held.pop_front();
    g_held_bytes -= old.size;
    Check(base, old);
    if (++g_checked % 2000 == 1) LOGR("heapcheck: %llu frees, %llu checked, %u held (%u KB)", (unsigned long long)g_frees, (unsigned long long)g_checked, unsigned(g_held.size()), g_held_bytes >> 10);
    lock.unlock();
    X86_MEM32(esp + 4) = old.heap;
    X86_MEM32(esp + 8) = old.flags;
    X86_MEM32(esp + 12) = old.ptr;
    __imp__sub_0007416F(ctx, base);
}
#endif
