#pragma once
// The game's code and data in an xbe, without its certificate and signature: two copies of a release whose
// files differ only there (another printing, another dump tool, a resigned image) run the same under one build.
// FNV-1a 64 over the image header's boot fields (base, sizes, the xored entry point and kernel thunk, init flags,
// TLS, stack and heap) and each section's flags, address, sizes and raw bytes. Written into the executable by
// xbe_recomp (kImageSectionsHash) and checked by the runtime.
#include <cstdint>
#include <cstring>
#include <vector>

namespace xbe {

inline uint64_t SectionsHash(const std::vector<uint8_t>& f) {
    auto u32 = [&](size_t at) -> uint32_t {
        uint32_t v = 0;
        if (at + 4 <= f.size()) std::memcpy(&v, f.data() + at, 4);
        return v;
    };
    if (f.size() < 0x180 || std::memcmp(f.data(), "XBEH", 4)) return 0;
    uint64_t h = 0xCBF29CE484222325ull;
    auto mix = [&](const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    };
    auto mix32 = [&](uint32_t v) { mix(reinterpret_cast<const uint8_t*>(&v), 4); };
    uint32_t base = u32(0x104);
    mix32(base);
    mix32(u32(0x10C));  // image size
    mix32(u32(0x128));  // entry point
    mix32(u32(0x158));  // kernel thunk
    for (uint32_t at : {0x108u, 0x124u, 0x12Cu, 0x130u, 0x134u, 0x138u}) mix32(u32(at));  // headers, init flags, TLS, stack, heap
    uint32_t n = u32(0x11C), at = u32(0x120);
    if (at < base || n > 256) return 0;
    for (uint32_t i = 0; i < n; i++) {
        size_t s = size_t(at - base) + size_t(i) * 0x38;
        uint32_t va = u32(s + 4), vsize = u32(s + 8), raw = u32(s + 12), rsize = u32(s + 16);
        mix32(u32(s));  // flags
        mix32(va);
        mix32(vsize);
        mix32(rsize);
        if (size_t(raw) + rsize > f.size()) return 0;
        mix(f.data() + raw, rsize);
    }
    return h;
}

}  // namespace xbe
