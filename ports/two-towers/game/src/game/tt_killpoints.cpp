// The Two Towers: the points a killing blow earns, shown after its rating ("EXCELLENT +150"), as The Return of the
// King shows them. [gameplay] kill_points in settings.toml, off by default.
//
// 0x99D40 is the game's killing blow: it rates the blow from the combo meter (0x9DE10: Fair, Good, Excellent,
// Perfect), puts that rating's message up (0xCB9D0: string 0x1C2 + rating from the table at 0x3A7B44, its pointer
// in 0x4A49BC, its colour in 0x4A49A8), works out the blow's experience (0x99CA0: the enemy's ENEMY_EXPERIENCE times
// the rating's multiplier) and adds it to the player's (0x9DD90, the amount a float argument). The message the HUD
// draws is the string 0x4A49BC points at, so after the blow the pointer is moved to a copy of the rating with the
// points added; the next rating puts the table's own string back first.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace tt {
bool g_kill_points = false;  // settings.toml [gameplay] kill_points
}

X86_EXTERN_FUNC(__imp__sub_00099D40);
X86_EXTERN_FUNC(__imp__sub_0009DD90);

namespace {
constexpr uint32_t kRatingText = 0x4A49BC;  // the rating message's string
constexpr uint32_t kMaxChars = 40;          // longer than any rating
bool g_in_blow = false;
float g_points = 0;
uint32_t g_buffer = 0;  // guest memory for the rating with its points

// the rating string, wide (UTF-16) or narrow, with " +N" after it, into the buffer
bool WriteRating(uint32_t from, int points) {
    if (!from) return false;
    if (!g_buffer) g_buffer = mem::AllocSystem((kMaxChars + 16) * 2);
    if (!g_buffer) return false;
    char tail[16];
    std::snprintf(tail, sizeof(tail), " +%d", points);
    // wide when the first character's high byte is 0 and the second byte of the string is 0
    bool wide = kernel::G<uint8_t>(from) != 0 && kernel::G<uint8_t>(from + 1) == 0;
    uint32_t n = 0;
    if (wide) {
        for (; n < kMaxChars; n++) {
            uint16_t c = kernel::R16(from + n * 2);
            if (!c) break;
            kernel::W16(g_buffer + n * 2, c);
        }
        for (const char* t = tail; *t; t++, n++) kernel::W16(g_buffer + n * 2, uint16_t(uint8_t(*t)));
        kernel::W16(g_buffer + n * 2, 0);
    } else {
        for (; n < kMaxChars; n++) {
            uint8_t c = kernel::G<uint8_t>(from + n);
            if (!c) break;
            kernel::G<uint8_t>(g_buffer + n) = c;
        }
        for (const char* t = tail; *t; t++, n++) kernel::G<uint8_t>(g_buffer + n) = uint8_t(*t);
        kernel::G<uint8_t>(g_buffer + n) = 0;
    }
    static bool told = false;
    if (!told) {
        told = true;
        LOGI("tt: kill points shown after the rating (%s string)", wide ? "wide" : "narrow");
    }
    return true;
}
}  // namespace

// a blow's experience: the first amount added during a killing blow
X86_FUNC(sub_0009DD90) {
    if (g_in_blow) {
        float v;
        uint32_t bits = kernel::R32(ctx.esp + 4);
        std::memcpy(&v, &bits, 4);
        g_points += v;
        g_in_blow = false;  // only the blow's own award
    }
    __imp__sub_0009DD90(ctx, base);
}

X86_FUNC(sub_00099D40) {
    if (!tt::g_kill_points) {
        __imp__sub_00099D40(ctx, base);
        return;
    }
    g_in_blow = true;
    g_points = 0;
    __imp__sub_00099D40(ctx, base);
    g_in_blow = false;
    int points = int(g_points + 0.5f);
    uint32_t text = kernel::R32(kRatingText);
    if (points > 0 && text && text != g_buffer && WriteRating(text, points)) kernel::W32(kRatingText, g_buffer);
}
