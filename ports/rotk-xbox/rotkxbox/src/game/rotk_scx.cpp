#include "game/rotk_scx.h"

#include <algorithm>
#include <cstring>

namespace rotk::scx {

namespace {

uint32_t U32(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v = 0;
    if (at + 4 <= d.size()) std::memcpy(&v, &d[at], 4);
    return v;
}
void Put32(std::vector<uint8_t>& d, size_t at, uint32_t v) { std::memcpy(&d[at], &v, 4); }
bool Tag(const std::vector<uint8_t>& d, size_t at, const char* t) { return at + 4 <= d.size() && std::memcmp(&d[at], t, 4) == 0; }

constexpr size_t kBlock = 0x10000;     // no chunk crosses a 64 KB boundary
constexpr size_t kChunk = 0x2000;      // the largest data chunk, header too
constexpr size_t kStoredHead = 0x40, kPackedHead = 0x44;

// the bytes after "TADS"/"tadR" in a data chunk's header (+20..+0x40): the game never reads them
const uint8_t kChunkTail[0x2C] = {};

void Fill(std::vector<uint8_t>& out, size_t n) {  // a fill chunk of n bytes to a block's end (its size if it has room)
    size_t at = out.size();
    out.resize(at + n, 0);
    if (n >= 4) std::memcpy(&out[at], "LLIF", 4);
    if (n >= 8) Put32(out, at + 4, uint32_t(n));
}

// a chunk added at the end, after a fill to the next 64 KB boundary when it would cross it (or leave less than a
// fill's room before it)
void Add(std::vector<uint8_t>& out, const std::vector<uint8_t>& chunk) {
    size_t room = kBlock - out.size() % kBlock;
    if (chunk.size() > room || (room - chunk.size() > 0 && room - chunk.size() < 16)) Fill(out, room);
    out.insert(out.end(), chunk.begin(), chunk.end());
}

std::vector<uint8_t> DataChunk(const uint8_t* p, size_t n, const std::vector<uint8_t>* packed) {
    std::vector<uint8_t> c(packed ? kPackedHead + packed->size() : kStoredHead + n, 0);
    std::memcpy(&c[0], "COHS", 4);
    std::memcpy(&c[16], packed ? "tadR" : "TADS", 4);
    std::memcpy(&c[20], kChunkTail, sizeof kChunkTail);
    if (packed) {
        Put32(c, 0x40, uint32_t(n));
        std::memcpy(&c[kPackedHead], packed->data(), packed->size());
    } else {
        std::memcpy(&c[kStoredHead], p, n);
    }
    c.resize((c.size() + 3) & ~size_t(3), 0);  // the next chunk 4-byte aligned (the data's size is in its header)
    Put32(c, 4, uint32_t(c.size()));
    return c;
}

}  // namespace

std::vector<Resource> Read(const std::vector<uint8_t>& d) {
    std::vector<Resource> res;
    size_t o = 0;
    while (o + 16 <= d.size()) {
        uint32_t size = U32(d, o + 4);
        if (Tag(d, o, "COHS")) {
            if (size < 16 || size > d.size() - o) break;  // broken: what was read so far
            if (Tag(d, o + 16, "RDHS")) {
                if (size < 0x24) break;
                if (!res.empty()) res.back().end = o;
                Resource r;
                for (int i = 0; i < 4; i++) r.type += char(d[o + 27 - i]);
                r.header_at = o;
                r.header_size = size;
                res.push_back(r);
            } else if ((Tag(d, o + 16, "TADS") || Tag(d, o + 16, "tadR")) && !res.empty()) {
                res.back().chunks.push_back({o, size, Tag(d, o + 16, "tadR")});
            }
            o += size >= 16 ? size : 16;
        } else if (Tag(d, o, "LLIF")) {  // to the block's end, as the game reads it (0xF3CC3)
            o = (o / kBlock + 1) * kBlock;
        } else if (Tag(d, o, "DDAP") || Tag(d, o, "LRTC") || Tag(d, o, "COTS")) {
            if (size >= 8 && size > d.size() - o) break;
            o += size >= 8 ? size : 16;
        } else {
            o += 4;
        }
    }
    if (!res.empty()) res.back().end = d.size();
    return res;
}

bool Data(const std::vector<uint8_t>& d, const Resource& r, std::vector<uint8_t>& out) {
    out.clear();
    for (const Chunk& c : r.chunks) {
        if (c.at + c.size > d.size()) return false;
        if (c.packed) {
            if (c.size < kPackedHead) return false;
            std::vector<uint8_t> piece;
            if (!Unpack(&d[c.at + kPackedHead], c.size - kPackedHead, U32(d, c.at + 0x40), piece)) return false;
            out.insert(out.end(), piece.begin(), piece.end());
        } else {
            if (c.size < kStoredHead) return false;
            out.insert(out.end(), d.begin() + long(c.at + kStoredHead), d.begin() + long(c.at + c.size));
        }
    }
    uint32_t want = U32(d, r.header_at + 32);  // the data's size (a stored chunk may carry a few bytes more)
    if (want && want <= out.size()) out.resize(want);
    return true;
}

uint32_t Key(const std::vector<uint8_t>& data) { return U32(data, 8); }

bool Unpack(const uint8_t* src, size_t n, size_t size, std::vector<uint8_t>& out) {
    out.clear();
    if (size > (64u << 20)) return false;  // no resource is near this
    out.reserve(size);
    size_t i = 0;
    while (out.size() < size) {
        if (i + 2 > n) return false;
        uint32_t w = uint32_t(src[i]) << 8 | src[i + 1];
        i += 2;
        if ((w & 0x8800) == 0x8800) {
            uint32_t k = (w >> 12) & 7;
            if (k == 0) {  // a literal run
                uint32_t c = w & 0x7FF;
                if (i + c > n) return false;
                out.insert(out.end(), src + i, src + i + c);
                i += c;
            } else {  // a run of one byte
                uint32_t dist = ((w >> 5) & 0x38) | k, len = (w & 0xFF) + 3;
                if (dist > out.size()) return false;
                uint8_t b = out[out.size() - dist];
                out.insert(out.end(), len, b);
            }
        } else {
            uint32_t len = (w >> 12) & 7, off = w & 0xFFF;
            if (len == 7) {
                if (i >= n) return false;
                len = src[i++] + 7u;
            }
            len += 3;
            if (off == 0 || off > out.size()) return false;
            size_t p = out.size() - off;
            if (w & 0x8000) {  // from the end, back
                p += 2;
                for (uint32_t k = 0; k < len; k++) {
                    if (p < k || p - k >= out.size()) return false;
                    out.push_back(out[p - k]);
                }
            } else {
                for (uint32_t k = 0; k < len; k++) out.push_back(out[p + k]);
            }
        }
    }
    out.resize(size);
    return true;
}

// greedy with one byte of lookahead: hash chains (3 bytes) for copies forward and copies back (a run read backwards,
// as the game's packer also does); runs of one byte; a copy no longer than its distance
namespace {
struct Match {
    size_t len = 0;
    uint32_t word = 0;  // the encoded op (with its extra byte, if any, from len)
    int kind = 0;       // 1 copy, 2 copy back, 3 run
};
}  // namespace

std::vector<uint8_t> Pack(const uint8_t* src, size_t n) {
    std::vector<uint8_t> out, lit;
    constexpr int kHash = 1 << 15;
    std::vector<int> head(kHash, -1), prev(n, -1), rhead(kHash, -1), rprev(n, -1);
    auto h3 = [](uint8_t a, uint8_t b, uint8_t c) { return int(((a << 10) ^ (b << 5) ^ c ^ (a >> 3)) & (kHash - 1)); };
    size_t done = 0;  // positions put in the tables
    auto insert_to = [&](size_t upto) {
        for (; done < upto && done < n; done++) {
            size_t i = done;
            if (i + 3 <= n) {
                int h = h3(src[i], src[i + 1], src[i + 2]);
                prev[i] = head[h];
                head[h] = int(i);
            }
            if (i >= 2) {  // the three bytes ending at i, backwards
                int h = h3(src[i], src[i - 1], src[i - 2]);
                rprev[i] = rhead[h];
                rhead[h] = int(i);
            }
        }
    };
    auto flush = [&] {
        size_t k = 0;
        while (k < lit.size()) {
            size_t c = std::min<size_t>(0x7FF, lit.size() - k);
            uint32_t w = 0x8800 | uint32_t(c);
            out.push_back(uint8_t(w >> 8));
            out.push_back(uint8_t(w));
            out.insert(out.end(), lit.begin() + long(k), lit.begin() + long(k + c));
            k += c;
        }
        lit.clear();
    };
    auto find = [&](size_t i) {
        Match m;
        if (i + 3 > n) return m;
        insert_to(i);  // only what is already out
        int tries = 512;
        for (int j = head[h3(src[i], src[i + 1], src[i + 2])]; j >= 0 && tries-- > 0; j = prev[size_t(j)]) {
            size_t off = i - size_t(j);
            if (off > 0xFFF) break;
            size_t lim = std::min<size_t>({265, n - i, off});
            if (lim < 3 || lim <= m.len) continue;
            size_t l = 0;
            while (l < lim && src[size_t(j) + l] == src[i + l]) l++;
            if (l >= 3 && l > m.len) {
                m = {l, uint32_t(off), 1};
                if (l == lim && lim == 265) break;
            }
        }
        // back: out[p - k] for k = 0.., p = i - off + 2 (off < 0x800)
        tries = 256;
        for (int j = rhead[h3(src[i], src[i + 1], src[i + 2])]; j >= 0 && tries-- > 0; j = rprev[size_t(j)]) {
            size_t p = size_t(j);
            if (p + 1 >= i + 0) {
                if (p >= i) continue;
            }
            size_t off = i - p + 2;
            if (off >= 0x800) break;
            size_t lim = std::min<size_t>({265, n - i, p + 1});
            if (lim <= m.len) continue;
            size_t l = 0;
            while (l < lim && src[p - l] == src[i + l]) l++;
            if (l >= 3 && l > m.len) m = {l, uint32_t(off), 2};
        }
        // a run of a byte 1..63 back (not a multiple of 8)
        for (size_t dist = 1; dist <= 63 && dist <= i; dist++) {
            if (!(dist & 7)) continue;
            uint8_t b = src[i - dist];
            size_t l = 0;
            while (l < 258 && i + l < n && src[i + l] == b) l++;
            if (l >= 3 && l > m.len + 0) {
                m = {l, uint32_t(dist), 3};
                break;
            }
        }
        return m;
    };
    auto emit = [&](const Match& m) {
        flush();
        if (m.kind == 3) {
            uint32_t w = 0x8800 | (m.word & 7) << 12 | (m.word & 0x38) << 5 | uint32_t(m.len - 3);
            out.push_back(uint8_t(w >> 8));
            out.push_back(uint8_t(w));
            return;
        }
        uint32_t l = uint32_t(m.len - 3);
        uint32_t w = (std::min<uint32_t>(l, 7) << 12) | m.word | (m.kind == 2 ? 0x8000u : 0);
        out.push_back(uint8_t(w >> 8));
        out.push_back(uint8_t(w));
        if (l >= 7) out.push_back(uint8_t(l - 7));
    };
    size_t i = 0;
    Match next;
    bool have_next = false;
    while (i < n) {
        Match m = have_next ? next : find(i);
        have_next = false;
        if (m.len >= 3 && i + 1 < n) {
            next = find(i + 1);
            if (next.len > m.len + 1) {  // better one byte on: this one a literal
                lit.push_back(src[i]);
                i++;
                have_next = true;
                continue;
            }
        }
        if (m.len >= 3) {
            emit(m);
            i += m.len;
        } else {
            lit.push_back(src[i]);
            i++;
        }
    }
    flush();
    return out;
}

std::vector<uint8_t> Rebuild(const std::vector<uint8_t>& file, const std::vector<Resource>& res,
                             const std::vector<std::pair<size_t, const std::vector<uint8_t>*>>& replace) {
    if (res.size() < 2 || res[1].header_at > file.size()) return {};
    for (const Resource& r : res) {
        if (r.header_size < 0x24 || r.header_at + r.header_size > file.size()) return {};
        for (const Chunk& c : r.chunks)
            if (c.at + c.size > file.size()) return {};
    }
    // the head as it is (CTRL, the first resource, PADD) up to the second resource's header
    std::vector<uint8_t> out(file.begin(), file.begin() + long(res[1].header_at));
    for (size_t i = 1; i < res.size(); i++) {
        const Resource& r = res[i];
        const std::vector<uint8_t>* with = nullptr;
        for (auto& [k, v] : replace)
            if (k == i) with = v;
        std::vector<uint8_t> header(file.begin() + long(r.header_at), file.begin() + long(r.header_at + r.header_size));
        if (with) {
            Put32(header, 32, uint32_t(with->size()));
            Add(out, header);
            for (size_t k = 0; k < with->size(); k += kChunk - kStoredHead)
                Add(out, DataChunk(with->data() + k, std::min(kChunk - kStoredHead, with->size() - k), nullptr));
        } else {
            Add(out, header);
            for (const Chunk& c : r.chunks) Add(out, std::vector<uint8_t>(file.begin() + long(c.at), file.begin() + long(c.at + c.size)));
        }
    }
    size_t room = (kBlock - out.size() % kBlock) % kBlock;
    if (room) Fill(out, room);
    return out;
}

bool ReplaceInPlace(std::vector<uint8_t>& file, const Resource& r, const std::vector<uint8_t>& data) {
    if (r.chunks.empty()) return false;
    const size_t from = r.chunks.front().at, to = r.end;
    if (to > file.size() || from >= to) return false;
    // nothing in the space but the resource's own chunks, fills and nothing (zeros): anything else is not ours to
    // write over
    for (size_t o = from; o < to;) {
        if (Tag(file, o, "COHS")) {
            bool own = false;
            for (const Chunk& c : r.chunks) own |= c.at == o;
            uint32_t size = U32(file, o + 4);
            if (!own || size < 16 || size > to - o) return false;
            o += size;
        } else if (Tag(file, o, "LLIF")) {
            o = (o / kBlock + 1) * kBlock;
        } else if (o + 4 <= to && U32(file, o) == 0) {
            o += 4;
        } else {
            return false;
        }
    }
    // The game reads a block of 64 KB at a time: a fill chunk ends the block wherever it is (0xF3CC3), so fills only
    // go at a block's end. A stored chunk gives its resource no more than the resource's size (0xF3D45): the space
    // left before the next resource goes in a last stored chunk, after the data
    for (int stored_tail = 0; stored_tail < 2; stored_tail++) {
        std::vector<uint8_t> out;
        auto pos = [&] { return from + out.size(); };
        auto fill_block = [&] {
            size_t room = kBlock - pos() % kBlock, at = out.size();
            out.resize(at + room, 0);
            std::memcpy(&out[at], "LLIF", 4);
            if (room >= 8) Put32(out, at + 4, uint32_t(room));
        };
        auto add = [&](const std::vector<uint8_t>& chunk) {
            size_t room = kBlock - pos() % kBlock;
            if (chunk.size() > room || room - chunk.size() < 4) fill_block();
            out.insert(out.end(), chunk.begin(), chunk.end());
        };
        // the data in chunks packed as full as they go (from the last chunk's ratio, a few tries); with stored_tail
        // the last piece stored
        size_t i = 0;
        double ratio = 0.8;  // packed / unpacked
        while (i < data.size()) {
            constexpr size_t room = kChunk - kPackedHead;
            size_t left = data.size() - i;
            size_t n = std::min(left, std::max<size_t>(room, size_t(double(room) / ratio * 0.98)));
            std::vector<uint8_t> z = Pack(&data[i], n);
            size_t good = 0;
            std::vector<uint8_t> good_z;
            for (int step = 0; step < 12; step++) {
                if (z.size() <= room) {
                    good = n;
                    good_z = z;
                    if (n == left) break;
                    size_t more = std::min(left, n + std::max<size_t>(64, n / 32));
                    if (good && step > 0 && more - n < 64) break;
                    std::vector<uint8_t> z2 = Pack(&data[i], more);
                    if (z2.size() > room) break;
                    n = more;
                    z.swap(z2);
                } else {
                    if (n <= room) break;  // does not pack: stored below
                    n = std::max(room, n - std::max<size_t>(64, n / 16));
                    z = Pack(&data[i], n);
                }
            }
            if (good) ratio = std::max(0.05, double(good_z.size()) / double(good));
            bool last = good && i + good == data.size();
            if (!good || (last && stored_tail)) {
                size_t take = std::min(left, kChunk - kStoredHead);
                add(DataChunk(&data[i], take, nullptr));
                i += take;
            } else {
                add(DataChunk(&data[i], good, &good_z));
                i += good;
            }
        }
        // to the next resource: whole blocks filled, then one stored chunk of nothing but space (the game takes none
        // of it: the resource is full)
        while (pos() < to && to - pos() >= kBlock - pos() % kBlock) fill_block();
        if (pos() > to) continue;
        size_t rest = to - pos();
        if (rest && rest < kStoredHead) continue;
        while (rest) {  // in chunks no larger than the format's (each at least a header)
            size_t n = rest <= kChunk ? rest : rest - kChunk < kStoredHead ? rest - kStoredHead : kChunk;
            std::vector<uint8_t> pad(n, 0);
            std::memcpy(&pad[0], "COHS", 4);
            Put32(pad, 4, uint32_t(n));
            std::memcpy(&pad[16], "TADS", 4);
            out.insert(out.end(), pad.begin(), pad.end());
            rest -= n;
        }
        if (pos() != to) continue;
        std::memcpy(&file[from], out.data(), out.size());
        Put32(file, r.header_at + 32, uint32_t(data.size()));
        return true;
    }
    return false;
}

bool PatchData(std::vector<uint8_t>& file, const Resource& r, size_t at, const std::vector<uint8_t>& bytes) {
    struct Change {
        const Chunk* c;
        std::vector<uint8_t> chunk;  // its new bytes, the same size
    };
    std::vector<Change> changes;
    size_t raw = 0;  // where each chunk's data starts in the resource
    for (const Chunk& c : r.chunks) {
        if (c.at + c.size > file.size() || c.size < (c.packed ? kPackedHead : kStoredHead)) return false;
        size_t n = c.packed ? U32(file, c.at + 0x40) : c.size - kStoredHead;
        if (raw < at + bytes.size() && raw + n > at) {
            std::vector<uint8_t> piece;
            if (c.packed) {
                if (!Unpack(&file[c.at + kPackedHead], c.size - kPackedHead, n, piece)) return false;
            } else {
                piece.assign(file.begin() + long(c.at + kStoredHead), file.begin() + long(c.at + c.size));
            }
            if (piece.size() < n) return false;
            for (size_t k = 0; k < bytes.size(); k++)
                if (at + k >= raw && at + k < raw + n) piece[at + k - raw] = bytes[k];
            std::vector<uint8_t> chunk(file.begin() + long(c.at), file.begin() + long(c.at + c.size));
            if (c.packed) {
                std::vector<uint8_t> z = Pack(piece.data(), piece.size());
                if (z.size() > c.size - kPackedHead) return false;
                std::fill(chunk.begin() + long(kPackedHead), chunk.end(), 0);
                std::memcpy(&chunk[kPackedHead], z.data(), z.size());
            } else {
                std::memcpy(&chunk[kStoredHead], piece.data(), piece.size());
            }
            changes.push_back({&c, std::move(chunk)});
        }
        raw += n;
    }
    if (changes.empty()) return false;
    for (const Change& ch : changes) std::memcpy(&file[ch.c->at], ch.chunk.data(), ch.chunk.size());
    return true;
}

std::vector<Bone> Bones(const std::vector<uint8_t>& d) {
    // the longest run of entries (they need not be 4-byte aligned)
    auto entry = [&](size_t o) {
        return o + 20 <= d.size() && U32(d, o + 8) == 0 && d[o + 16] == 0 && d[o + 17] == 0 && d[o + 18] == 0 &&
               d[o + 19] == 0xE2;
    };
    size_t best_at = 0, best_n = 0;
    for (size_t o = 0; o + 20 <= d.size(); o++) {
        if (!entry(o) || (o >= 20 && entry(o - 20))) continue;  // a run's start
        size_t n = 0;
        while (entry(o + n * 20)) n++;
        if (n > best_n) {
            best_n = n;
            best_at = o;
        }
    }
    std::vector<Bone> bones;
    for (size_t k = 0; k < best_n; k++) {
        size_t o = best_at + k * 20;
        Bone b;
        std::memcpy(&b.name, &d[o], 8);
        b.index = uint32_t(d[o + 12]) << 24 | uint32_t(d[o + 13]) << 16 | uint32_t(d[o + 14]) << 8 | d[o + 15];
        bones.push_back(b);
    }
    return bones;
}

bool Vertices(const std::vector<uint8_t>& m, std::vector<size_t>& at) {
    constexpr size_t kSkin = 0x310;
    at.clear();
    if (m.size() < kSkin + 0x40 || std::memcmp(&m[0], "OBX ", 4) != 0 || std::memcmp(&m[kSkin], "PCsk", 4) != 0) return false;
    uint32_t parts = U32(m, kSkin + 0x18);
    if (parts == 0 || parts > 16) return false;
    for (uint32_t p = 0; p < parts; p++) {
        size_t part = kSkin + U32(m, kSkin + 0x1C + p * 4);
        uint32_t count = U32(m, part + 0x10), place = U32(m, part + 0x14), stride = U32(m, part + 0x18);
        if (stride != 20 || kSkin + place + size_t(count) * 20 > m.size()) return false;
        for (uint32_t k = 0; k < count; k++) at.push_back(kSkin + place + size_t(k) * 20);
    }
    return true;
}

namespace {
// a DXT5 block (alpha 8 bytes, colour 8 bytes) as a DXT1 block: colour as it is, 4 colours (c0 > c1), or 3 and
// transparent where the alpha is below half
void BlockToDxt1(const uint8_t* b5, uint8_t* b1) {
    uint8_t a[8] = {b5[0], b5[1]};
    if (a[0] > a[1]) {
        for (int k = 1; k < 7; k++) a[k + 1] = uint8_t(((7 - k) * a[0] + k * a[1]) / 7);
    } else {
        for (int k = 1; k < 5; k++) a[k + 1] = uint8_t(((5 - k) * a[0] + k * a[1]) / 5);
        a[6] = 0;
        a[7] = 255;
    }
    uint64_t ai = 0;
    for (int k = 0; k < 6; k++) ai |= uint64_t(b5[2 + k]) << (8 * k);
    bool clear[16];
    bool any = false;
    for (int p = 0; p < 16; p++) {
        clear[p] = a[(ai >> (3 * p)) & 7] < 128;
        any |= clear[p];
    }
    uint16_t c0 = uint16_t(b5[8] | b5[9] << 8), c1 = uint16_t(b5[10] | b5[11] << 8);
    uint32_t idx = uint32_t(b5[12]) | uint32_t(b5[13]) << 8 | uint32_t(b5[14]) << 16 | uint32_t(b5[15]) << 24;
    // the colour of each pixel as a weight of c0 (0..3: c0, c1, 2/3 c0, 1/3 c0)
    int sel[16];
    for (int p = 0; p < 16; p++) sel[p] = int((idx >> (2 * p)) & 3);
    bool swap = any ? c0 > c1 : c0 < c1;
    if (swap) {
        std::swap(c0, c1);
        static const int kSwap[4] = {1, 0, 3, 2};
        for (int& s : sel) s = kSwap[s];
    }
    uint32_t out = 0;
    for (int p = 0; p < 16; p++) {
        int s = sel[p];
        if (any) s = clear[p] ? 3 : (s >= 2 ? 2 : s);  // three colours: the two blends become the midpoint
        else if (c0 == c1) s = 0;
        out |= uint32_t(s) << (2 * p);
    }
    b1[0] = uint8_t(c0);
    b1[1] = uint8_t(c0 >> 8);
    b1[2] = uint8_t(c1);
    b1[3] = uint8_t(c1 >> 8);
    for (int k = 0; k < 4; k++) b1[4 + k] = uint8_t(out >> (8 * k));
}
}  // namespace

// "TXX " (+4 0x210), "HEAD" (its size 8: +0x10 the data's size in 256-byte pages, +0x12 the count), "TXHE" (+4 its size): 0x50 a
// texture: its name (8), four pictures of 12 bytes (place in the data, size in 16-byte units, 0xFFFF, the texture's
// first page: the 256-byte pages of the textures before it, each rounded up; 2 for the first and 1 for the others), width and height, format (0x0C DXT1, 0x0E DXT5), the
// pictures' count, 1 when more than one, log2 of the width; then 4 bytes, 0x0000FFFF, its number, 0; "CLHE" 0; "TXDA"
// (+4 the data's size) and the data
std::vector<uint8_t> FirstPictures(const std::vector<uint8_t>& p, const std::vector<std::string>& dxt1) {
    if (p.size() < 0x40 || std::memcmp(&p[0], "TXX ", 4) != 0 || std::memcmp(&p[8], "HEAD", 4) != 0) return {};
    uint16_t count = uint16_t(p[0x12] | p[0x13] << 8);
    constexpr size_t he = 0x18;
    if (std::memcmp(&p[he], "TXHE", 4) != 0) return {};
    size_t entries = he + 8, da = p.size();
    for (size_t o = entries + size_t(count) * 0x50; o + 8 <= p.size(); o += 4)
        if (std::memcmp(&p[o], "TXDA", 4) == 0) {
            da = o;
            break;
        }
    if (da == p.size()) return {};
    size_t data = da + 8;
    std::vector<uint8_t> out(p.begin(), p.begin() + long(data));  // the headers, changed below
    std::vector<uint8_t> pix;
    uint32_t page = 0;  // each texture's first page: the pages before it, each texture's rounded up
    for (uint16_t t = 0; t < count; t++) {
        size_t e = entries + size_t(t) * 0x50;
        std::string name(reinterpret_cast<const char*>(&p[e]), strnlen(reinterpret_cast<const char*>(&p[e]), 8));
        uint32_t place = U32(p, e + 8), size = uint32_t(p[e + 12] | p[e + 13] << 8) * 16u;
        uint8_t format = p[e + 0x3C];
        if (e + 0x50 > data || data + uint64_t(place) + size > p.size()) return {};
        std::vector<uint8_t> first(p.begin() + long(data + place), p.begin() + long(data + place + size));
        bool to1 = format == 0x0E && std::find(dxt1.begin(), dxt1.end(), name) != dxt1.end();
        if (to1) {
            std::vector<uint8_t> conv(first.size() / 2);
            for (size_t b = 0; b + 16 <= first.size(); b += 16) BlockToDxt1(&first[b], &conv[b / 2]);
            first.swap(conv);
            out[e + 0x3C] = 0x0C;
        }
        size_t at = (pix.size() + 0x7F) & ~size_t(0x7F);
        pix.resize(at, 0);
        pix.insert(pix.end(), first.begin(), first.end());
        // the first picture alone
        Put32(out, e + 8, uint32_t(at));
        out[e + 12] = uint8_t(first.size() / 16);
        out[e + 13] = uint8_t(first.size() / 16 >> 8);
        out[e + 16] = uint8_t(page);
        out[e + 17] = uint8_t(page >> 8);
        page += uint32_t((first.size() + 0xFF) >> 8);
        out[e + 18] = 2;
        out[e + 19] = 0;
        std::memset(&out[e + 20], 0, 36);
        out[e + 0x3D] = 1;
        out[e + 0x3E] = 0;
    }
    Put32(out, da + 4, uint32_t(pix.size()));
    if (page > 0xFFFF) return {};
    out[0x10] = uint8_t(page);
    out[0x11] = uint8_t(page >> 8);
    out.insert(out.end(), pix.begin(), pix.end());
    return out;
}

namespace {
// a pack's entries and its data: false when it is not one
bool PackParts(const std::vector<uint8_t>& p, uint16_t& count, size_t& da) {
    if (p.size() < 0x40 || std::memcmp(&p[0], "TXX ", 4) != 0 || std::memcmp(&p[8], "HEAD", 4) != 0 ||
        std::memcmp(&p[0x18], "TXHE", 4) != 0)
        return false;
    count = uint16_t(p[0x12] | p[0x13] << 8);
    if (U32(p, 0x1C) != uint32_t(count) * 0x50u) return false;
    for (size_t o = 0x20 + size_t(count) * 0x50; o + 8 <= p.size(); o += 4)
        if (std::memcmp(&p[o], "TXDA", 4) == 0) {
            da = o;
            return da + 8 + U32(p, da + 4) <= p.size();
        }
    return false;
}
}  // namespace

std::vector<uint8_t> AddTextures(const std::vector<uint8_t>& p, const std::vector<uint8_t>& from,
                                 const std::vector<std::string>& names) {
    uint16_t count = 0, from_count = 0;
    size_t da = 0, from_da = 0;
    if (!PackParts(p, count, da) || !PackParts(from, from_count, from_da)) return {};
    std::vector<uint8_t> entries, pix(p.begin() + long(da + 8), p.begin() + long(da + 8 + U32(p, da + 4)));
    uint32_t pages = uint32_t(p[0x10] | p[0x11] << 8);
    uint16_t n = count;
    for (const std::string& name : names) {
        size_t e = 0;
        for (uint16_t k = 0; k < from_count && !e; k++) {
            size_t at = 0x20 + size_t(k) * 0x50;
            if (strnlen(reinterpret_cast<const char*>(&from[at]), 8) == name.size() && std::memcmp(&from[at], name.data(), name.size()) == 0)
                e = at;
        }
        if (!e) return {};
        std::vector<uint8_t> entry(from.begin() + long(e), from.begin() + long(e + 0x50));
        int mips = std::clamp<int>(entry[0x3D], 1, 4);
        uint32_t lo = UINT32_MAX, hi = 0, used = 0;
        for (int m = 0; m < mips; m++) {
            uint32_t place = U32(entry, 8 + size_t(m) * 12), size = uint32_t(entry[12 + m * 12] | entry[13 + m * 12] << 8) * 16u;
            if (place > UINT32_MAX - size) return {};
            lo = std::min(lo, place);
            hi = std::max(hi, place + size);
            used += (size + 0x7F) & ~0x7Fu;
        }
        if (from_da + 8 + size_t(hi) > from.size() || lo > hi) return {};
        size_t at = (pix.size() + 0x7F) & ~size_t(0x7F);
        pix.resize(at, 0);
        pix.insert(pix.end(), from.begin() + long(from_da + 8 + lo), from.begin() + long(from_da + 8 + hi));
        for (int m = 0; m < mips; m++) {
            size_t f = 8 + size_t(m) * 12;
            Put32(entry, f, U32(entry, f) - lo + uint32_t(at));
            entry[f + 8] = uint8_t(pages);
            entry[f + 9] = uint8_t(pages >> 8);
        }
        pages += (used + 0xFF) >> 8;
        Put32(entry, 0x48, n++);
        entries.insert(entries.end(), entry.begin(), entry.end());
    }
    if (pages > 0xFFFF) return {};
    std::vector<uint8_t> out(p.begin(), p.begin() + long(0x20 + size_t(count) * 0x50));
    out.insert(out.end(), entries.begin(), entries.end());
    out.insert(out.end(), p.begin() + long(0x20 + size_t(count) * 0x50), p.begin() + long(da + 8));  // CLHE, TXDA's header
    size_t new_da = da + entries.size();
    Put32(out, new_da + 4, uint32_t(pix.size()));
    Put32(out, 0x1C, uint32_t(n) * 0x50u);
    out[0x10] = uint8_t(pages);
    out[0x11] = uint8_t(pages >> 8);
    out[0x12] = uint8_t(n);
    out[0x13] = uint8_t(n >> 8);
    out.insert(out.end(), pix.begin(), pix.end());
    return out;
}

bool FindTexture(const std::vector<uint8_t>& p, const char* name, Texture& t) {
    if (p.size() < 0x40 || std::memcmp(&p[0], "TXX ", 4) != 0) return false;
    uint16_t count = uint16_t(p[0x12] | p[0x13] << 8);
    size_t da = 0;
    for (size_t o = 0x20 + size_t(count) * 0x50; o + 8 <= p.size(); o += 4)
        if (std::memcmp(&p[o], "TXDA", 4) == 0) {
            da = o;
            break;
        }
    if (!da) return false;
    for (uint16_t k = 0; k < count; k++) {
        size_t e = 0x20 + size_t(k) * 0x50;
        if (strnlen(reinterpret_cast<const char*>(&p[e]), 8) != std::strlen(name) || std::memcmp(&p[e], name, std::strlen(name)) != 0)
            continue;
        t.at = da + 8 + U32(p, e + 8);
        t.w = p[e + 0x38] | p[e + 0x39] << 8;
        t.h = p[e + 0x3A] | p[e + 0x3B] << 8;
        t.format = p[e + 0x3C];
        if (t.format != 0x0C && t.format != 0x0E) return false;
        size_t blocks = size_t((t.w + 3) / 4) * size_t((t.h + 3) / 4);
        return t.w > 0 && t.h > 0 && t.at + blocks * (t.format == 0x0E ? 16 : 8) <= p.size();
    }
    return false;
}

namespace {
void Rgb565(uint16_t c, uint8_t* o) {
    o[0] = uint8_t((c >> 11 & 31) * 255 / 31);
    o[1] = uint8_t((c >> 5 & 63) * 255 / 63);
    o[2] = uint8_t((c & 31) * 255 / 31);
}
uint16_t To565(const uint8_t* c) { return uint16_t((c[0] * 31 + 127) / 255 << 11 | (c[1] * 63 + 127) / 255 << 5 | (c[2] * 31 + 127) / 255); }
}  // namespace

std::vector<uint8_t> DecodeDxt(const uint8_t* data, int w, int h, int format) {
    std::vector<uint8_t> out(size_t(w) * size_t(h) * 4, 0);
    size_t bs = format == 0x0E ? 16 : 8;
    const uint8_t* b = data;
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4, b += bs) {
            const uint8_t* cb = format == 0x0E ? b + 8 : b;
            uint16_t c0 = uint16_t(cb[0] | cb[1] << 8), c1 = uint16_t(cb[2] | cb[3] << 8);
            uint32_t idx = uint32_t(cb[4]) | uint32_t(cb[5]) << 8 | uint32_t(cb[6]) << 16 | uint32_t(cb[7]) << 24;
            uint8_t pal[4][4];
            Rgb565(c0, pal[0]);
            Rgb565(c1, pal[1]);
            pal[0][3] = pal[1][3] = 255;
            bool four = c0 > c1 || format == 0x0E;
            for (int k = 0; k < 3; k++) {
                pal[2][k] = uint8_t(four ? (2 * pal[0][k] + pal[1][k]) / 3 : (pal[0][k] + pal[1][k]) / 2);
                pal[3][k] = uint8_t(four ? (pal[0][k] + 2 * pal[1][k]) / 3 : 0);
            }
            pal[2][3] = 255;
            pal[3][3] = four ? 255 : 0;
            uint8_t a[8] = {};
            uint64_t ai = 0;
            if (format == 0x0E) {
                a[0] = b[0];
                a[1] = b[1];
                if (a[0] > a[1])
                    for (int k = 1; k < 7; k++) a[k + 1] = uint8_t(((7 - k) * a[0] + k * a[1]) / 7);
                else {
                    for (int k = 1; k < 5; k++) a[k + 1] = uint8_t(((5 - k) * a[0] + k * a[1]) / 5);
                    a[6] = 0;
                    a[7] = 255;
                }
                for (int k = 0; k < 6; k++) ai |= uint64_t(b[2 + k]) << (8 * k);
            }
            for (int p = 0; p < 16; p++) {
                int x = bx + p % 4, y = by + p / 4;
                if (x >= w || y >= h) continue;
                uint8_t* o = &out[(size_t(y) * size_t(w) + size_t(x)) * 4];
                std::memcpy(o, pal[(idx >> (2 * p)) & 3], 4);
                if (format == 0x0E) o[3] = a[(ai >> (3 * p)) & 7];
            }
        }
    return out;
}

std::vector<uint8_t> EncodeDxt1(const std::vector<uint8_t>& rgba, int w, int h) {
    std::vector<uint8_t> out;
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4) {
            uint8_t px[16][4] = {};
            bool any_clear = false;
            int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0}, opaque = 0;
            for (int p = 0; p < 16; p++) {
                int x = std::min(bx + p % 4, w - 1), y = std::min(by + p / 4, h - 1);
                std::memcpy(px[p], &rgba[(size_t(y) * size_t(w) + size_t(x)) * 4], 4);
                if (px[p][3] < 128) {
                    any_clear = true;
                    continue;
                }
                opaque++;
                for (int k = 0; k < 3; k++) {
                    lo[k] = std::min<int>(lo[k], px[p][k]);
                    hi[k] = std::max<int>(hi[k], px[p][k]);
                }
            }
            uint8_t a[3] = {uint8_t(hi[0]), uint8_t(hi[1]), uint8_t(hi[2])}, b[3] = {uint8_t(lo[0]), uint8_t(lo[1]), uint8_t(lo[2])};
            if (!opaque) a[0] = a[1] = a[2] = b[0] = b[1] = b[2] = 0;
            uint16_t c0 = To565(a), c1 = To565(b);
            if (any_clear ? c0 > c1 : c0 < c1) std::swap(c0, c1);
            bool four = c0 > c1;
            uint8_t pal[4][3];
            Rgb565(c0, pal[0]);
            Rgb565(c1, pal[1]);
            for (int k = 0; k < 3; k++) {
                pal[2][k] = uint8_t(four ? (2 * pal[0][k] + pal[1][k]) / 3 : (pal[0][k] + pal[1][k]) / 2);
                pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
            }
            uint32_t idx = 0;
            for (int p = 0; p < 16; p++) {
                int sel = 0;
                if (px[p][3] < 128 && !four) {
                    sel = 3;
                } else {
                    int best = 1 << 30;
                    for (int s = 0; s < (four ? 4 : 3); s++) {
                        int d = 0;
                        for (int k = 0; k < 3; k++) d += (px[p][k] - pal[s][k]) * (px[p][k] - pal[s][k]);
                        if (d < best) {
                            best = d;
                            sel = s;
                        }
                    }
                }
                idx |= uint32_t(sel) << (2 * p);
            }
            uint8_t blk[8] = {uint8_t(c0), uint8_t(c0 >> 8), uint8_t(c1), uint8_t(c1 >> 8),
                              uint8_t(idx), uint8_t(idx >> 8), uint8_t(idx >> 16), uint8_t(idx >> 24)};
            out.insert(out.end(), blk, blk + 8);
        }
    return out;
}

}  // namespace rotk::scx
