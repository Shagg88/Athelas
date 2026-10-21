// The Two Towers: the game's text (UI Text database) asked for before it is loaded.
//
// The text tables are at 0x3A7B44: one pointer per language (+0, +4, +8), filled by the loader 0x3FC60 from a UI
// package. The lookups 0x3FA60 (id; most of the game's), 0x3FAD0 (id, language) and 0x3FB20 (id) take the language's table, else the first, and read
// its +0xC. On a player's Steam Deck (issues #5, #9, #10: Wine, the game files on a slow card) two threads asked for a
// line 1.6 s after starting, before any table was loaded, and the game crashed reading address 0xC. When no table is
// there the lookups now answer as they do for a line they can't find (0, no text), so the line is simply empty
// until the text is loaded.
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <map>
#include <string>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"

X86_EXTERN_FUNC(__imp__sub_0003FA60);
X86_EXTERN_FUNC(__imp__sub_0003FAD0);
X86_EXTERN_FUNC(__imp__sub_0003FB20);

namespace tt {
void TextShown(uint32_t id);  // tt_lang.cpp: the subtitles
uint32_t GandalfText(uint32_t id, uint32_t ret);  // tt_gandalf.cpp: a line of ours on the hero screen, or 0
}

namespace {
// testing: TT_TEST_NO_TEXT=<n> answers the first n lookups as if no text were loaded yet
bool Pretend() {
    static const char* e = std::getenv("TT_TEST_NO_TEXT");
    static int left = e ? std::atoi(e) : 0;
    return left > 0 && left-- > 0;
}
// testing: TT_TEXT_TRACE logs each line asked for (once a second: the lines asked for in that second, with counts)
void Trace(uint32_t id) {
    static const bool on = std::getenv("TT_TEXT_TRACE") != nullptr;
    if (!on) return;
    static std::map<uint32_t, int> seen;
    static auto last = std::chrono::steady_clock::now();
    seen[id]++;
    auto now = std::chrono::steady_clock::now();
    if (now - last > std::chrono::seconds(1)) {
        std::string s;
        for (auto& [k, n] : seen) s += std::to_string(k) + "x" + std::to_string(n) + " ";
        LOGI("tt: text lines %s", s.c_str());
        seen.clear();
        last = now;
    }
}
void Early(uint32_t id) {
    static int logged = 0;
    if (logged++ < 3) LOGW("tt: text %u asked for before the game's text is loaded: left empty", id);
}
}  // namespace

// thiscall (the tables), id; ret 4: the lookup most of the game uses (156 callers)
X86_FUNC(sub_0003FA60) {
    uint32_t tables = ctx.ecx, id = ::kernel::R32(ctx.esp + 4);
    if (Pretend() || (!::kernel::R32(tables) && !::kernel::R32(tables + 4) && !::kernel::R32(tables + 8))) {
        Early(id);
        ::kernel::Return(ctx, 0, 1);
        return;
    }
    Trace(id);
    tt::TextShown(id);
    if (uint32_t ours = tt::GandalfText(id, ::kernel::R32(ctx.esp))) {
        ::kernel::Return(ctx, ours, 1);
        return;
    }
    __imp__sub_0003FA60(ctx, base);
}

// thiscall (the tables), id, language; ret 8
X86_FUNC(sub_0003FAD0) {
    uint32_t tables = ctx.ecx, id = ::kernel::R32(ctx.esp + 4), language = ::kernel::R32(ctx.esp + 8);
    if (Pretend() || (language < 3 && !::kernel::R32(tables + language * 4) && !::kernel::R32(tables))) {
        Early(id);
        ::kernel::Return(ctx, 0, 2);
        return;
    }
    Trace(id);
    tt::TextShown(id);
    if (uint32_t ours = tt::GandalfText(id, ::kernel::R32(ctx.esp))) {
        ::kernel::Return(ctx, ours, 2);
        return;
    }
    __imp__sub_0003FAD0(ctx, base);
}

// thiscall (the tables), id; ret 4
X86_FUNC(sub_0003FB20) {
    uint32_t tables = ctx.ecx, id = ::kernel::R32(ctx.esp + 4);
    if (Pretend() || (!::kernel::R32(tables) && !::kernel::R32(tables + 4) && !::kernel::R32(tables + 8))) {
        Early(id);
        ::kernel::Return(ctx, 0, 1);
        return;
    }
    Trace(id);
    tt::TextShown(id);
    if (uint32_t ours = tt::GandalfText(id, ::kernel::R32(ctx.esp))) {
        ::kernel::Return(ctx, ours, 1);
        return;
    }
    __imp__sub_0003FB20(ctx, base);
}
