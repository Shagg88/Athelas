// The Third Age: GitHub #40, a crash on the game's file-read thread (guest thread 1C on that PC) after 7 minutes of
// play: a read of address 4, inside the completion callback that sub_000DB510 calls when a read has finished.
// sub_000DB510 is the game's background reader: under the lock 0x3023DC it waits for the request at [0x302428]
// (0x34AE9), calls the request's callback [esi+0x1C] with the buffer and the result (0xDB586, or 0xDB59D with two
// more values), unlinks the request and takes the next. The callback found an object of its own already gone: on the
// Xbox the reader and the game shared one core, here they run at the same time.
//
// Each callback is noted on its way in (the request, the callback, its first values) and the crash handler prints the
// last one for the thread that crashed (app::g_crash_note).
//
// The callback found (jobs 183-185, from the game's code): 0x100E00, the streamed sound's block reader (queued by
// 0x1CEC90 with no context of its own: it uses the current buffer slot at [0x291210]). At 0x100E40 it reads
// [[0x291210]+4] without a check; 0x180669 empties [0x291210] when the stream is stopped (a scene change, a cutscene)
// and doesn't cancel a read already queued. #40's registers match: the table entry 0x2BB808 in edx, edi = 0, a read
// of address 4. TA_StreamReadDone: a block that finishes after its stream was stopped is dropped, the way the
// callback's own end does it (0x100F09: slot and busy flag cleared, then its usual tail).
#include <atomic>
#include <cstdint>

#include "app/app.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"

namespace {
struct Note {
    std::atomic<uint32_t> thread{0}, request{0}, callback{0}, a0{0}, a1{0}, owner{0};
};
Note g_note;

void Remember(X86Context& ctx) {
    uint32_t req = ctx.esi;
    g_note.thread = kernel::Current() ? kernel::Current()->id : 0;
    g_note.request = req;
    g_note.callback = req ? ::kernel::R32(req + 0x1C) : 0;
    g_note.owner = req ? ::kernel::R32(req + 0x0C) : 0;
    g_note.a0 = ctx.eax;
    g_note.a1 = ctx.edx;
}

void CrashNote(uint32_t thread) {
    if (!g_note.callback.load() || g_note.thread.load() != thread) return;
    LOGE("ta: the last file-read callback started on this thread: request %08X, callback %08X, values %08X %08X, [request+0C] %08X "
         "(GitHub #40)", g_note.request.load(), g_note.callback.load(), g_note.a0.load(), g_note.a1.load(),
         g_note.owner.load());
}
const bool g_set = [] {
    app::g_crash_note = CrashNote;
    return true;
}();
}  // namespace

// [[hook]] 0xDB586 / 0xDB59D: the reader is about to call the request's callback (esi the request)
void TA_ReadDone(X86Context& ctx, uint8_t* base) {
    (void)base;
    Remember(ctx);
}

// [[hook]] 0x100E40, on_true 0x100F09: the streamed sound's block arrived after its stream was stopped ([0x291210]
// emptied): dropped instead of reading through the empty slot (GitHub #40)
bool TA_StreamReadDone(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    (void)base;
    if (::kernel::R32(0x291210) != 0) return false;
    static std::atomic<int> told{0};
    if (told++ < 3) LOGR("ta: a streamed sound block arrived after its stream was stopped: dropped (GitHub #40)");
    return true;
}
