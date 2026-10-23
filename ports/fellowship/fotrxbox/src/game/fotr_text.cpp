// FOTR Xbox: [graphics] sharp_text, the game's text drawn at three times its size, so it stays sharp on a large
// screen (The Return of the King's port has a setting of the same name; there it uses the PC version's fonts).
//
// The game draws its text on the processor with the Xbox's TrueType font library (fonts\MyriadMM.ttf and
// neuehuo_.ttf), a label at a time, into a linear A8R8G8B8 texture of the label's size in 640x480 pixels (0x96FA0
// makes it: XGSetTextureHeader 0x1A0F81), and draws that texture as a quad with texel coordinates. 0xD0740 draws a run
// of text into such a texture (eax the texture; font**, y, the label's rect {width, height}, its style, first
// character, length, alignment; ret 0x1C), with button pictures from xboxdings sized to the line (0xD0680, a multiple
// of 32 pixels) and the font's height set beforehand (0x29D70, SetTextHeight: ecx font**, eax pixels).
//
// With sharp_text each run is drawn a second time, as the game draws it but three times larger: into a texture of the
// port's own (in physical memory past the game's 64 MB, mem::AllocHostPhysical), with the font three times as tall,
// the rect and the starting line three times the size. That picture is kept for the game's texture, by its address,
// with a hash of what the game drew there; runs drawn one after another into the same texture add up in it. When the
// renderer uploads the texture with those contents, the larger picture is drawn in its place (the SDK's
// g_builtin_contents; the quad's texel coordinates are the game's texture's, so the larger picture fills the same
// place). A texture the game has changed some other way keeps its own picture.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/log.h"
#include "cpu/x86_context.h"
#include "cpu/x86_ops.h"
#include "gpu/texture_pack.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace fotr {
bool g_sharp_text = false;  // [graphics] sharp_text
}

X86_EXTERN_FUNC(__imp__sub_000D0740);
X86_FUNC(sub_00029D70);

namespace {
constexpr uint32_t kScale = 3;
constexpr uint32_t kLinA8R8G8B8 = 0x12;
constexpr uint32_t kScratchBytes = 12u << 20;

struct Shadow {
    uint32_t w = 0, h = 0, pitch = 0, k = 0;  // the game's texture, and the scale
    uint64_t drawn = 0;            // the game's texture's contents after the last run drawn
    std::vector<uint8_t> px;       // w*k x h*k, B G R A, rows packed
};
std::mutex g_lock;
std::unordered_map<uint32_t, Shadow> g_shadows;  // by the game texture's data address
uint32_t g_scratch = 0, g_header = 0, g_rect = 0;  // our texture's pixels and header, the scaled rect

uint64_t Hash(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t v;
        std::memcpy(&v, p + i, 8);
        h = (h ^ v) * 0x100000001b3ull;
    }
    for (; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

struct TexInfo {
    uint32_t data, w, h, pitch;
};
// a linear A8R8G8B8 texture header (common, data, lock, format, size), else false
bool Describe(uint32_t tex, TexInfo& t) {
    if (tex < 0x10000 || !mem::IsCommitted(tex) || !mem::IsCommitted(tex + 0x13)) return false;
    uint32_t format = ::kernel::R32(tex + 0x0C), size = ::kernel::R32(tex + 0x10);
    if (((format >> 8) & 0xFF) != kLinA8R8G8B8 || !size) return false;
    t.data = mem::VirtualFromPhysical(::kernel::R32(tex + 4) & 0x0FFFFFFFu);
    t.w = (size & 0xFFF) + 1;
    t.h = ((size >> 12) & 0xFFF) + 1;
    t.pitch = ((size >> 24) + 1) * 64;
    return mem::IsCommitted(t.data) && mem::IsCommitted(t.data + t.pitch * t.h - 1);
}

uint64_t Contents(const TexInfo& t) {
    return Hash(static_cast<const uint8_t*>(::kernel::HostPtr(t.data)), size_t(t.pitch) * t.h);
}

// the larger picture, for the renderer (any thread): the game's texture as last drawn by 0xD0740
bool Builtin(const gpu::pack::Contents& c, gpu::pack::Image& out) {
    if (c.format != kLinA8R8G8B8 || !c.data) return false;
    uint32_t va = uint32_t(c.data - static_cast<const uint8_t*>(::kernel::HostPtr(0)));
    std::lock_guard l(g_lock);
    auto it = g_shadows.find(va);
    if (it == g_shadows.end()) return false;
    const Shadow& s = it->second;
    if (s.w != c.width || s.h != c.height || size_t(s.pitch) * s.h > c.size) return false;
    if (Hash(c.data, size_t(s.pitch) * s.h) != s.drawn) return false;  // changed since: the game's own
    out = gpu::pack::Image{};
    out.width = s.w * s.k;
    out.height = s.h * s.k;
    out.format = gpu::pack::Format::BGRA8;
    out.levels.push_back(s.px);
    return true;
}

void Call(X86Context& ctx, uint8_t* base, void (*fn)(X86Context&, uint8_t*)) {
    X86_PUSH32(0u);  // a return address for the callee's ret
    fn(ctx, base);
}
}  // namespace

namespace fotr {
// after the settings load: the renderer asks for the larger pictures (memory for them is taken at the first text)
void InitText() {
    if (!g_sharp_text) return;
    gpu::pack::g_builtin_contents = Builtin;
    LOGR("fotr: text drawn at %ux (sharper text)", kScale);
}
}  // namespace fotr

X86_FUNC(sub_000D0740) {
    static bool tried = false;
    if (fotr::g_sharp_text && !tried) {
        tried = true;
        g_scratch = mem::AllocHostPhysical(kScratchBytes);
        g_header = mem::AllocSystem(0x20, 16);
        g_rect = mem::AllocSystem(0x40, 16);
        if (!g_scratch || !g_header || !g_rect) {
            LOGW("fotr: no memory for the sharper text; the game's own is drawn");
            g_scratch = 0;
        }
    }
    if (!g_scratch) {
        __imp__sub_000D0740(ctx, base);
        return;
    }
    const X86Context in = ctx;
    const uint32_t esp0 = ctx.esp, ret = ::kernel::R32(esp0);
    uint32_t arg[7];
    for (int i = 0; i < 7; i++) arg[i] = ::kernel::R32(esp0 + 4 + 4 * i);
    TexInfo t{};
    bool ok = Describe(in.eax, t) && arg[0] >= 0x10000 && arg[2] >= 0x10000 && mem::IsCommitted(arg[2] + 7);
    uint64_t before = ok ? Contents(t) : 0;
    __imp__sub_000D0740(ctx, base);  // the game's own run
    if (!ok) return;
    const X86Context out = ctx;

    // the largest scale up to kScale whose picture fits our texture
    uint32_t k = kScale, kw = 0, kh = 0, kpitch = 0;
    for (; k >= 2; k--) {
        kw = t.w * k;
        kh = t.h * k;
        kpitch = (kw * 4 + 63) & ~63u;
        if (size_t(kpitch) * kh <= kScratchBytes && kw <= 4096 && kh <= 4096) break;
    }
    if (k < 2) return;
    uint8_t* scratch = static_cast<uint8_t*>(::kernel::HostPtr(g_scratch));
    {
        // a run added to what an earlier one drew, else a picture of its own
        std::lock_guard l(g_lock);
        auto it = g_shadows.find(t.data);
        bool more = it != g_shadows.end() && it->second.w == t.w && it->second.h == t.h && it->second.k == k &&
                    it->second.drawn == before;
        for (uint32_t y = 0; y < kh; y++) {
            uint8_t* row = scratch + size_t(y) * kpitch;
            if (more) std::memcpy(row, &it->second.px[size_t(y) * kw * 4], size_t(kw) * 4);
            else std::memset(row, 0, kpitch);
        }
    }
    // our texture: the game's header with our pixels and size
    for (uint32_t o = 0; o < 0x14; o += 4) ::kernel::W32(g_header + o, ::kernel::R32(in.eax + o));
    ::kernel::W32(g_header + 0x00, 0x00040001u);  // a texture, one reference
    ::kernel::W32(g_header + 0x04, mem::PhysicalFromVirtual(g_scratch));
    ::kernel::W32(g_header + 0x08, 0);
    ::kernel::W32(g_header + 0x10, (kw - 1) | (kh - 1) << 12 | (kpitch / 64 - 1) << 24);
    // the rect three times the size
    for (uint32_t o = 0; o < 0x40; o += 4) ::kernel::W32(g_rect + o, ::kernel::R32(arg[2] + o));
    ::kernel::W32(g_rect + 0, ::kernel::R32(arg[2] + 0) * k);
    ::kernel::W32(g_rect + 4, ::kernel::R32(arg[2] + 4) * k);
    // the font k times as tall for the run, put back after
    uint32_t font = ::kernel::R32(arg[0]);
    uint32_t height = font >= 0x10000 ? ::kernel::R32(font + 4) : 0;
    if (!height || height > 400) return;
    ctx = in;
    ctx.esp = esp0 - 0x40;  // below the game's call: free stack
    ctx.ecx = arg[0];
    ctx.eax = height * k;
    Call(ctx, base, sub_00029D70);
    // the run again, into ours
    ctx.esp = esp0;
    ::kernel::W32(esp0, ret);
    uint32_t again[7] = {arg[0], arg[1] * k, g_rect, arg[3], arg[4], arg[5], arg[6]};
    for (int i = 0; i < 7; i++) ::kernel::W32(esp0 + 4 + 4 * i, again[i]);
    ctx.eax = g_header;
    ctx.ebx = in.ebx;
    ctx.esi = in.esi;
    ctx.edi = in.edi;
    ctx.ebp = in.ebp;
    __imp__sub_000D0740(ctx, base);
    ctx.esp = esp0 - 0x40;
    ctx.ecx = arg[0];
    ctx.eax = height;
    Call(ctx, base, sub_00029D70);
    for (int i = 0; i < 7; i++) ::kernel::W32(esp0 + 4 + 4 * i, arg[i]);
    ctx = out;
    {
        std::lock_guard l(g_lock);
        Shadow& s = g_shadows[t.data];
        s.w = t.w;
        s.h = t.h;
        s.pitch = t.pitch;
        s.k = k;
        s.drawn = Contents(t);
        s.px.resize(size_t(kw) * kh * 4);
        for (uint32_t y = 0; y < kh; y++) std::memcpy(&s.px[size_t(y) * kw * 4], scratch + size_t(y) * kpitch, size_t(kw) * 4);
    }
    LOG_FIRST(1, "fotr: sharper text: a %ux%u label drawn at %ux%u", t.w, t.h, kw, kh);
#ifndef _WIN32
    // test builds: FOTR_TEXT_DUMP=<folder>, the first 40 larger pictures as PNG files
    static const char* dump = std::getenv("FOTR_TEXT_DUMP");
    static int dumped = 0;
    if (dump && dumped < 40) {
        std::vector<uint8_t> rgba(size_t(kw) * kh * 4);
        for (uint32_t y = 0; y < kh; y++)
            for (uint32_t x = 0; x < kw; x++) {
                const uint8_t* p = scratch + size_t(y) * kpitch + x * 4;
                uint8_t* o = &rgba[(size_t(y) * kw + x) * 4];
                o[0] = p[2], o[1] = p[1], o[2] = p[0], o[3] = p[3];
            }
        char name[64];
        std::snprintf(name, sizeof name, "/text_%02d_%ux%u.png", dumped++, t.w, t.h);
        gpu::pack::WritePng(std::string(dump) + name, kw, kh, rgba);
    }
#endif
}
