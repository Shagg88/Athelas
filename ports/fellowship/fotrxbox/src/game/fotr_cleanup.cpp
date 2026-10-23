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
