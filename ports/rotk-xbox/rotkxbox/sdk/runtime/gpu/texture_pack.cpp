#include "gpu/texture_pack.h"

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>

#include "gpu/stb/stb_image.h"
#include "gpu/stb/stb_image_write.h"

namespace gpu::pack {

namespace {

constexpr uint32_t FourCC(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}
constexpr uint32_t kMagic = FourCC('D', 'D', 'S', ' ');
constexpr uint32_t kDxt1 = FourCC('D', 'X', 'T', '1'), kDxt3 = FourCC('D', 'X', 'T', '3'),
                   kDxt5 = FourCC('D', 'X', 'T', '5'), kDx10 = FourCC('D', 'X', '1', '0');
// DDS_HEADER, offsets after the magic
constexpr size_t kHeaderBytes = 124, kHdrFlags = 4, kHdrHeight = 8, kHdrWidth = 12, kHdrPitch = 16, kHdrMips = 24,
                 kPfSize = 72, kPfFlags = 76, kPfFourCC = 80, kPfBits = 84, kPfR = 88, kPfG = 92, kPfB = 96,
                 kPfA = 100, kHdrCaps = 104;
constexpr uint32_t kFlagsBase = 0x1 | 0x2 | 0x4 | 0x1000;  // CAPS HEIGHT WIDTH PIXELFORMAT
constexpr uint32_t kFlagPitch = 0x8, kFlagMips = 0x20000, kFlagLinear = 0x80000;
constexpr uint32_t kPfAlpha = 0x1, kPfFourCCFlag = 0x4, kPfRgb = 0x40;
constexpr uint32_t kCapsTexture = 0x1000, kCapsComplex = 0x8, kCapsMipmap = 0x400000;
constexpr size_t kDx10Bytes = 20;
constexpr uint32_t kDimension2D = 3;

void Put(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }
uint32_t Get(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v;
    std::memcpy(&v, &b[at], 4);
    return v;
}

bool Block(Format f) {
    return f == Format::BC1 || f == Format::BC2 || f == Format::BC3 ||
           f == Format::BC7;
}

// a DXGI format from a DX10 header, its sRGB variants read as UNORM
Format FromDxgi(uint32_t f) {
    switch (f) {
        case 28: case 29: return Format::RGBA8;
        case 71: case 72: return Format::BC1;
        case 74: case 75: return Format::BC2;
        case 77: case 78: return Format::BC3;
        case 87: case 91: return Format::BGRA8;
        case 98: case 99: return Format::BC7;
        default: return Format::Unknown;
    }
}

}  // namespace

bool Supported(Format f) {
    return Block(f) || f == Format::BGRA8 || f == Format::RGBA8;
}

uint32_t RowBytes(Format f, uint32_t w) {
    w = std::max(w, 1u);
    if (!Block(f)) return w * 4;
    uint32_t blocks = (w + 3) / 4;
    return blocks * (f == Format::BC1 ? 8 : 16);
}

uint32_t Rows(Format f, uint32_t h) {
    h = std::max(h, 1u);
    return Block(f) ? (h + 3) / 4 : h;
}

std::string Name(uint64_t hash) {
    char b[17];
    std::snprintf(b, sizeof(b), "%016llX", static_cast<unsigned long long>(hash));
    return b;
}

std::string PngName(uint32_t width, uint32_t height, uint64_t hash) {
    char b[64];
    std::snprintf(b, sizeof(b), "%ux%u_%016llx.png", width, height, static_cast<unsigned long long>(hash));
    return b;
}

namespace {

bool ReadFile(const std::filesystem::path& file, std::vector<uint8_t>& out) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// one level down: each texel the mean of the (up to) four above it
std::vector<uint8_t> Halve(const std::vector<uint8_t>& src, uint32_t w, uint32_t h, uint32_t& nw, uint32_t& nh) {
    nw = std::max(w / 2, 1u);
    nh = std::max(h / 2, 1u);
    std::vector<uint8_t> dst(size_t(nw) * nh * 4);
    for (uint32_t y = 0; y < nh; y++)
        for (uint32_t x = 0; x < nw; x++) {
            uint32_t x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
            uint32_t y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
            for (int c = 0; c < 4; c++) {
                auto at = [&](uint32_t xx, uint32_t yy) { return uint32_t(src[(size_t(yy) * w + xx) * 4 + c]); };
                dst[(size_t(y) * nw + x) * 4 + c] = uint8_t((at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1) + 2) / 4);
            }
        }
    return dst;
}

// a BC colour block (8 bytes) into 16 RGBA texels; bc1 allows the punch-through black
void ColourBlock(const uint8_t* b, bool bc1, uint8_t out[16][4]) {
    uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
    uint8_t p[4][4];
    auto rgb = [](uint16_t c, uint8_t* o) {
        o[0] = uint8_t((c >> 11 & 31) * 255 / 31);
        o[1] = uint8_t((c >> 5 & 63) * 255 / 63);
        o[2] = uint8_t((c & 31) * 255 / 31);
        o[3] = 255;
    };
    rgb(c0, p[0]);
    rgb(c1, p[1]);
    if (!bc1 || c0 > c1) {
        for (int i = 0; i < 3; i++) {
            p[2][i] = uint8_t((2 * p[0][i] + p[1][i] + 1) / 3);
            p[3][i] = uint8_t((p[0][i] + 2 * p[1][i] + 1) / 3);
        }
        p[2][3] = p[3][3] = 255;
    } else {
        for (int i = 0; i < 3; i++) p[2][i] = uint8_t((p[0][i] + p[1][i]) / 2), p[3][i] = 0;
        p[2][3] = 255;
        p[3][3] = 0;
    }
    uint32_t bits = uint32_t(b[4]) | uint32_t(b[5]) << 8 | uint32_t(b[6]) << 16 | uint32_t(b[7]) << 24;
    for (int i = 0; i < 16; i++) std::memcpy(out[i], p[bits >> (2 * i) & 3], 4);
}

struct PendingPng {
    std::filesystem::path file;
    uint32_t w, h;
    std::vector<uint8_t> rgba;
};
// the writer thread's queue, never destroyed: the thread may still wait on it while the process exits
struct Writes {
    std::mutex lock;
    std::condition_variable cv;
    std::deque<PendingPng> queue;
    bool thread = false;
};
Writes& Queue() {
    static Writes* w = new Writes;
    return *w;
}

}  // namespace

bool ToRgba(Format f, uint32_t w, uint32_t h, const std::vector<uint8_t>& level, std::vector<uint8_t>& rgba) {
    size_t n = size_t(w) * h;
    rgba.assign(n * 4, 0);
    if (f == Format::RGBA8 || f == Format::BGRA8) {
        if (level.size() < n * 4) return false;
        std::memcpy(rgba.data(), level.data(), n * 4);
        if (f == Format::BGRA8)
            for (size_t i = 0; i < n; i++) std::swap(rgba[i * 4], rgba[i * 4 + 2]);
        return true;
    }
    if (f != Format::BC1 && f != Format::BC2 && f != Format::BC3) return false;
    uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4, bytes = f == Format::BC1 ? 8 : 16;
    if (level.size() < size_t(bw) * bh * bytes) return false;
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            const uint8_t* b = &level[(size_t(by) * bw + bx) * bytes];
            uint8_t t[16][4];
            ColourBlock(f == Format::BC1 ? b : b + 8, f == Format::BC1, t);
            if (f == Format::BC2) {  // 4 bit alpha
                for (int i = 0; i < 16; i++) t[i][3] = uint8_t((b[i / 2] >> (i & 1) * 4 & 15) * 17);
            } else if (f == Format::BC3) {  // interpolated alpha
                uint8_t a[8] = {b[0], b[1]};
                if (a[0] > a[1]) {
                    for (int i = 1; i < 7; i++) a[i + 1] = uint8_t(((7 - i) * a[0] + i * a[1] + 3) / 7);
                } else {
                    for (int i = 1; i < 5; i++) a[i + 1] = uint8_t(((5 - i) * a[0] + i * a[1] + 2) / 5);
                    a[6] = 0;
                    a[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
                for (int i = 0; i < 16; i++) t[i][3] = a[bits >> (3 * i) & 7];
            }
            for (int i = 0; i < 16; i++) {
                uint32_t x = bx * 4 + uint32_t(i & 3), y = by * 4 + uint32_t(i >> 2);
                if (x < w && y < h) std::memcpy(&rgba[(size_t(y) * w + x) * 4], t[i], 4);
            }
        }
    return true;
}

bool ReadPng(const std::filesystem::path& file, Image& img) {
    img = Image{};
    std::vector<uint8_t> bytes;
    if (!ReadFile(file, bytes) || bytes.empty()) return false;
    return DecodePng(bytes.data(), bytes.size(), img);
}

BuiltinTexture g_builtin = nullptr;
BuiltinContents g_builtin_contents = nullptr;

void MakeLevels(Image& img) {
    if (img.levels.empty() || (img.format != Format::RGBA8 && img.format != Format::BGRA8)) return;
    img.levels.resize(1);
    for (uint32_t lw = img.width, lh = img.height; lw > 1 || lh > 1;) {
        uint32_t nw, nh;
        std::vector<uint8_t> next = Halve(img.levels.back(), lw, lh, nw, nh);
        img.levels.push_back(std::move(next));
        lw = nw;
        lh = nh;
    }
}

bool DecodePng(const uint8_t* data, size_t size, Image& img) {
    img = Image{};
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(data, int(size), &w, &h, &comp, 4);
    if (!px) return false;
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        stbi_image_free(px);
        return false;
    }
    img.width = uint32_t(w);
    img.height = uint32_t(h);
    img.format = Format::RGBA8;
    img.levels.emplace_back(px, px + size_t(w) * size_t(h) * 4);
    stbi_image_free(px);
    for (uint32_t lw = img.width, lh = img.height; lw > 1 || lh > 1;) {
        uint32_t nw, nh;
        std::vector<uint8_t> next = Halve(img.levels.back(), lw, lh, nw, nh);
        img.levels.push_back(std::move(next));
        lw = nw;
        lh = nh;
    }
    return true;
}

bool WritePng(const std::filesystem::path& file, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba) {
    if (!width || !height || rgba.size() < size_t(width) * height * 4) return false;
    std::vector<uint8_t> out;
    auto sink = [](void* ctx, void* data, int size) {
        auto* v = static_cast<std::vector<uint8_t>*>(ctx);
        v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    };
    if (!stbi_write_png_to_func(sink, &out, int(width), int(height), 4, rgba.data(), int(width * 4))) return false;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    // written beside it and moved into place, so a pack maker never sees half a file
    std::filesystem::path tmp = file;
    tmp += ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
        if (!f) return false;
    }
    std::filesystem::rename(tmp, file, ec);
    return !ec;
}

void WritePngLater(std::filesystem::path file, uint32_t width, uint32_t height, std::vector<uint8_t> rgba) {
    Writes& q = Queue();
    std::lock_guard lock(q.lock);
    q.queue.push_back({std::move(file), width, height, std::move(rgba)});
    q.cv.notify_one();
    if (q.thread) return;
    q.thread = true;
    std::thread([&q] {
        for (;;) {
            PendingPng p;
            {
                std::unique_lock lock(q.lock);
                q.cv.wait(lock, [&q] { return !q.queue.empty(); });
                p = std::move(q.queue.front());
                q.queue.pop_front();
            }
            std::error_code ec;
            if (!std::filesystem::exists(p.file, ec)) WritePng(p.file, p.w, p.h, p.rgba);
        }
    }).detach();
}

bool WriteDds(const std::filesystem::path& file, const Image& img) {
    if (!Supported(img.format) || img.levels.empty() || !img.width || !img.height) return false;
    bool dx10 = img.format == Format::BC7;
    std::vector<uint8_t> h(4 + kHeaderBytes + (dx10 ? kDx10Bytes : 0), 0);
    Put(h, 0, kMagic);
    const size_t o = 4;
    uint32_t mips = uint32_t(img.levels.size());
    bool block = Block(img.format);
    Put(h, o + 0, kHeaderBytes);
    Put(h, o + kHdrFlags, kFlagsBase | (block ? kFlagLinear : kFlagPitch) | (mips > 1 ? kFlagMips : 0));
    Put(h, o + kHdrHeight, img.height);
    Put(h, o + kHdrWidth, img.width);
    Put(h, o + kHdrPitch, block ? uint32_t(img.levels[0].size()) : RowBytes(img.format, img.width));
    Put(h, o + kHdrMips, mips);
    Put(h, o + kPfSize, 32);
    switch (img.format) {
        case Format::BC1: Put(h, o + kPfFlags, kPfFourCCFlag); Put(h, o + kPfFourCC, kDxt1); break;
        case Format::BC2: Put(h, o + kPfFlags, kPfFourCCFlag); Put(h, o + kPfFourCC, kDxt3); break;
        case Format::BC3: Put(h, o + kPfFlags, kPfFourCCFlag); Put(h, o + kPfFourCC, kDxt5); break;
        case Format::BC7: Put(h, o + kPfFlags, kPfFourCCFlag); Put(h, o + kPfFourCC, kDx10); break;
        default: {
            bool bgra = img.format == Format::BGRA8;
            Put(h, o + kPfFlags, kPfRgb | kPfAlpha);
            Put(h, o + kPfBits, 32);
            Put(h, o + kPfR, bgra ? 0x00FF0000u : 0x000000FFu);
            Put(h, o + kPfG, 0x0000FF00u);
            Put(h, o + kPfB, bgra ? 0x000000FFu : 0x00FF0000u);
            Put(h, o + kPfA, 0xFF000000u);
            break;
        }
    }
    Put(h, o + kHdrCaps, kCapsTexture | (mips > 1 ? kCapsComplex | kCapsMipmap : 0));
    if (dx10) {
        size_t x = 4 + kHeaderBytes;
        Put(h, x, uint32_t(img.format));
        Put(h, x + 4, kDimension2D);
        Put(h, x + 12, 1);  // array size
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(h.data()), std::streamsize(h.size()));
    for (const auto& l : img.levels) f.write(reinterpret_cast<const char*>(l.data()), std::streamsize(l.size()));
    return bool(f);
}

bool ReadDds(const std::filesystem::path& file, Image& img) {
    img = Image{};
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 4 + kHeaderBytes || Get(b, 0) != kMagic) return false;
    const size_t o = 4;
    if (Get(b, o) != kHeaderBytes || Get(b, o + kPfSize) != 32) return false;
    uint32_t w = Get(b, o + kHdrWidth), h = Get(b, o + kHdrHeight);
    uint32_t mips = (Get(b, o + kHdrFlags) & kFlagMips) ? std::max(Get(b, o + kHdrMips), 1u) : 1;
    uint32_t pf = Get(b, o + kPfFlags), cc = Get(b, o + kPfFourCC);
    size_t data = 4 + kHeaderBytes;
    Format fmt = Format::Unknown;
    if (pf & kPfFourCCFlag) {
        if (cc == kDxt1) fmt = Format::BC1;
        else if (cc == kDxt3) fmt = Format::BC2;
        else if (cc == kDxt5) fmt = Format::BC3;
        else if (cc == kDx10 && b.size() >= data + kDx10Bytes) {
            fmt = FromDxgi(Get(b, data));
            if (Get(b, data + 4) != kDimension2D || Get(b, data + 12) > 1) return false;  // 2D, one image
            data += kDx10Bytes;
        }
    } else if ((pf & kPfRgb) && Get(b, o + kPfBits) == 32) {
        uint32_t r = Get(b, o + kPfR), bl = Get(b, o + kPfB);
        if (r == 0x00FF0000u && bl == 0x000000FFu) fmt = Format::BGRA8;
        else if (r == 0x000000FFu && bl == 0x00FF0000u) fmt = Format::RGBA8;
    }
    if (!Supported(fmt) || !w || !h || w > 16384 || h > 16384) return false;
    img.width = w;
    img.height = h;
    img.format = fmt;
    for (uint32_t l = 0; l < mips && l < 15; l++) {
        uint32_t lw = std::max(w >> l, 1u), lh = std::max(h >> l, 1u);
        size_t n = size_t(RowBytes(fmt, lw)) * Rows(fmt, lh);
        if (data + n > b.size()) break;  // a truncated chain keeps the levels it has
        img.levels.emplace_back(b.begin() + std::ptrdiff_t(data), b.begin() + std::ptrdiff_t(data + n));
        data += n;
        if (lw == 1 && lh == 1) break;
    }
    return !img.levels.empty();
}

void Library::Scan(const std::filesystem::path& dir, const std::string& skip) {
    files_.clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() == 0 && it->is_directory(ec) && it->path().filename() == skip) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        const auto& p = it->path();
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        std::string stem = p.stem().string();
        if ((ext != ".dds" && ext != ".png") || stem.size() < 16) continue;
        auto hex = [&](size_t at, uint64_t& h) {
            h = 0;
            for (size_t i = at; i < at + 16; i++) {
                char c = stem[i];
                uint32_t v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
                if (v >= 16) return false;
                h = h << 4 | v;
            }
            return true;
        };
        auto sep = [](char c) { return c == '_' || c == '-' || c == ' '; };
        uint64_t h;
        size_t tail = stem.size() - 16;
        // ROTK Fix's names end in the 16 digits (256x64_0123456789abcdef, any prefix); older ones here began with them
        if ((hex(tail, h) && (tail == 0 || sep(stem[tail - 1]))) || (hex(0, h) && (stem.size() == 16 || sep(stem[16]))))
            files_.emplace_back(h, p);
    }
    std::sort(files_.begin(), files_.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
}

const std::filesystem::path* Library::Find(uint64_t hash) const {
    auto it = std::lower_bound(files_.begin(), files_.end(), hash,
                               [](const auto& e, uint64_t h) { return e.first < h; });
    return it != files_.end() && it->first == hash ? &it->second : nullptr;
}

}  // namespace gpu::pack
