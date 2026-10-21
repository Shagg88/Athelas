#include "game/rotk_eowyn_build.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "game/rotk_scx.h"

namespace rotk::eowyn {

namespace {

uint32_t U32(const std::vector<uint8_t>& d, size_t at) {
    uint32_t v = 0;
    if (at + 4 <= d.size()) std::memcpy(&v, &d[at], 4);
    return v;
}
float F32(const std::vector<uint8_t>& d, size_t at) {
    float v = 0;
    if (at + 4 <= d.size()) std::memcpy(&v, &d[at], 4);
    return v;
}
bool Starts(const std::vector<uint8_t>& d, size_t at, const char* s) {
    size_t n = std::strlen(s);
    return at + n <= d.size() && std::memcmp(&d[at], s, n) == 0;
}

constexpr size_t kMeshName = 0x31C;  // the skin's name, after "PCsk", its version and size
constexpr size_t kMeshKey = 0x80;    // the rig the mesh is skinned for
constexpr size_t kTextureName = 0x20;

// the first resource of a type whose data passes `is`, its data in `out`; -1 none
template <class Is>
long Find(const std::vector<uint8_t>& file, const std::vector<scx::Resource>& res, const char* type, Is is,
          std::vector<uint8_t>& out) {
    for (size_t i = 0; i < res.size(); i++) {
        if (res[i].type != type) continue;
        std::vector<uint8_t> d;
        if (!scx::Data(file, res[i], d) || !is(d)) continue;
        out.swap(d);
        return long(i);
    }
    return -1;
}

// her bones with no match in his skeleton, by name code -> the bone they ride on, by name code: her shield on the
// forearm (not his bow's bone, which is put away when he has no bow out), the top of her hair as her helm (the head)
struct Follow {
    uint64_t bone, on;
};
uint64_t Code(const char* hex) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        auto nib = [](char c) { return uint64_t(c <= '9' ? c - '0' : c - 'a' + 10); };
        v |= (nib(hex[2 * i]) << 4 | nib(hex[2 * i + 1])) << (8 * i);
    }
    return v;
}
const Follow kFollow[] = {
    {Code("7d3a38e5f6f90100"), Code("05c539b3a80c0000")},  // shield -> forearm
    {Code("4f1e37baab0b0000"), Code("0f0a4ad57d000000")},  // hair top -> as the helm
};

struct Point {
    double x = 0, y = 0, z = 0;
    int n = 0;
};
std::map<int, Point> Centres(const std::vector<uint8_t>& mesh) {
    std::map<int, Point> c;
    std::vector<size_t> at;
    if (!scx::Vertices(mesh, at)) return c;
    for (size_t o : at)
        for (int k = 16; k < 18; k++) {
            int b = mesh[o + size_t(k)];
            if (b == 255) continue;
            Point& p = c[b];
            p.x += F32(mesh, o);
            p.y += F32(mesh, o + 4);
            p.z += F32(mesh, o + 8);
            p.n++;
        }
    for (auto& [b, p] : c) {
        p.x /= p.n;
        p.y /= p.n;
        p.z /= p.n;
    }
    return c;
}

// each bone of hers her mesh uses -> his: the same name, else as kFollow says, else his nearest (both are posed alike)
bool BoneMap(const Pieces& her, const std::vector<uint8_t>& his_rig, const std::vector<uint8_t>& his_mesh,
             int (&map)[256], std::string& why) {
    std::map<uint64_t, int> his_by_name, her_by_name;
    std::map<int, uint64_t> her_name;
    for (const scx::Bone& b : scx::Bones(his_rig)) his_by_name[b.name] = int(b.index);
    for (const scx::Bone& b : scx::Bones(her.rig)) {
        her_name[int(b.index)] = b.name;
        her_by_name[b.name] = int(b.index);
    }
    if (his_by_name.size() < 20 || her_name.size() < 20) {
        why = "a skeleton's bones were not found";
        return false;
    }
    auto hers = Centres(her.mesh), his = Centres(his_mesh);
    if (hers.empty() || his.empty()) {
        why = "a mesh's vertices were not found";
        return false;
    }
    for (int& m : map) m = -1;
    std::vector<std::pair<int, int>> later;  // bones that ride on another of hers
    for (auto& [b, p] : hers) {
        auto n = her_name.find(b);
        if (n != her_name.end()) {
            auto h = his_by_name.find(n->second);
            if (h != his_by_name.end()) {
                map[b] = h->second;
                continue;
            }
            bool done = false;
            for (const Follow& f : kFollow) {
                if (f.bone != n->second) continue;
                if (auto on = his_by_name.find(f.on); on != his_by_name.end()) map[b] = on->second;
                else if (auto mine = her_by_name.find(f.on); mine != her_by_name.end()) later.push_back({b, mine->second});
                done = true;
            }
            if (done) continue;
        }
        double best = 1e30;
        for (auto& [hb, hp] : his) {
            double d = (p.x - hp.x) * (p.x - hp.x) + (p.y - hp.y) * (p.y - hp.y) + (p.z - hp.z) * (p.z - hp.z);
            if (d < best) {
                best = d;
                map[b] = hb;
            }
        }
    }
    for (auto& [b, on] : later) map[b] = on >= 0 && on < 256 ? map[on] : -1;
    for (auto& [b, p] : hers)
        if (map[b] < 0) {
            why = "a bone of hers has no place on his skeleton";
            return false;
        }
    return true;
}

bool Remap(const std::vector<uint8_t>& her_mesh, const int (&map)[256], uint32_t key, std::vector<uint8_t>& out) {
    out = her_mesh;
    std::vector<size_t> at;
    if (!scx::Vertices(out, at)) return false;
    for (size_t o : at)
        for (int k = 16; k < 18; k++)
            if (out[o + size_t(k)] != 255) out[o + size_t(k)] = uint8_t(map[out[o + size_t(k)]]);
    std::memcpy(&out[kMeshKey], &key, 4);
    return true;
}

// parts of his mesh (by name) added to hers, already on his skeleton: his bow and arrow in her hands. A skin (at
// 0x310) has up to 32 parts: their places at +0x1C, their names at +0x9C (16 bytes each), its size at +8. A part is a
// header (to its first vertex) whose places are from 0x310, then its data; it is copied whole, its places moved by
// as much as it moved (a multiple of 0x100, so its data keeps its alignment). The mesh's data block ("ELDA" at 0x290)
// and the mesh are made as long again, the mesh to a whole KB as the game's are
constexpr size_t kSkin = 0x310, kElda = 0x290;
bool AddParts(std::vector<uint8_t>& mesh, const std::vector<uint8_t>& his, const std::vector<const char*>& names) {
    auto skin_ok = [](const std::vector<uint8_t>& m) {
        return m.size() > kSkin + 0x2A0 && Starts(m, kSkin, "PCsk") && Starts(m, kElda, "ELDA") && U32(m, kSkin + 0x18) <= 32 &&
               U32(m, kSkin + 8) >= 0x2A0 && kSkin + U32(m, kSkin + 8) <= m.size();
    };
    if (!skin_ok(mesh) || !skin_ok(his)) return false;
    uint32_t his_count = U32(his, kSkin + 0x18), his_size = U32(his, kSkin + 8);
    std::vector<uint8_t> out(mesh.begin(), mesh.begin() + long(kSkin + U32(mesh, kSkin + 8)));
    uint32_t count = U32(out, kSkin + 0x18);
    for (const char* name : names) {
        uint32_t k = 0;
        while (k < his_count && (std::strlen(name) >= 16 || std::strncmp(reinterpret_cast<const char*>(&his[kSkin + 0x9C + k * 16]), name, 16) != 0))
            k++;
        if (k == his_count || count == 32) return false;
        uint32_t from = U32(his, kSkin + 0x1C + k * 4);
        uint32_t to = k + 1 < his_count ? U32(his, kSkin + 0x1C + (k + 1) * 4) : his_size;
        uint32_t first = U32(his, kSkin + from + 0x14);  // its first vertex: the header's end
        if (from < 0x2A0 || to <= from || to > his_size || first <= from || first > to || ((first - from) & 3)) return false;
        uint32_t at = uint32_t(out.size() - kSkin);
        at += (from - at) & 0xFF;  // the same place in a 0x100 as his
        out.resize(kSkin + at, 0);
        out.insert(out.end(), his.begin() + long(kSkin + from), his.begin() + long(kSkin + to));
        uint32_t move = at - from;
        for (uint32_t o = 0; o < first - from; o += 4) {
            uint32_t v = U32(out, kSkin + at + o);
            if (v >= from && v <= to) {
                v += move;
                std::memcpy(&out[kSkin + at + o], &v, 4);
            }
        }
        std::memcpy(&out[kSkin + 0x1C + count * 4], &at, 4);
        std::memset(&out[kSkin + 0x9C + count * 16], 0, 16);
        std::memcpy(&out[kSkin + 0x9C + count * 16], name, std::strlen(name));
        count++;
    }
    uint32_t size = uint32_t(out.size() - kSkin), whole = uint32_t((out.size() + 8 + 0x3FF) & ~size_t(0x3FF));
    std::memcpy(&out[kSkin + 0x18], &count, 4);
    std::memcpy(&out[kSkin + 8], &size, 4);
    uint32_t elda = whole - 8 - uint32_t(kElda);
    std::memcpy(&out[kElda + 4], &elda, 4);
    out.resize(whole, 0);
    mesh.swap(out);
    return true;
}

// an event table: its count at +0x18, its events from +0x20 (16 bytes: time, its name's place, how many values, their
// place; a value is 8 bytes, its kind then it). True when a voice event was quietened
bool QuietVoice(std::vector<uint8_t>& evt) {
    uint32_t count = U32(evt, 0x18);
    if (evt.size() < 0x20 || count > (evt.size() - 0x20) / 16) return false;
    static const char kPlay[] = "audio: play";
    bool any = false;
    for (uint32_t i = 0; i < count; i++) {
        size_t e = 0x20 + size_t(i) * 16;
        uint32_t name = U32(evt, e + 4), values = U32(evt, e + 8), at = U32(evt, e + 12);
        if (values != 4 || name > evt.size() - sizeof kPlay || std::memcmp(&evt[name], kPlay, sizeof kPlay) != 0) continue;
        if (at > evt.size() - 32 || U32(evt, at) != 0 || U32(evt, size_t(at) + 4) != 2) continue;
        uint32_t none = 8;
        std::memcpy(&evt[size_t(at) + 4], &none, 4);
        any = true;
    }
    return any;
}

// his mesh (a skin whose name starts so), his rig (the mesh's key) and his textures (the first named so)
struct His {
    long mesh = -1, rig = -1, textures = -1;
    std::vector<uint8_t> mesh_data, rig_data, texture_data;
};
bool FindHis(const std::vector<uint8_t>& file, const std::vector<scx::Resource>& res, const char* mesh_name,
             const char* texture_name, His& h, std::string& why) {
    h.mesh = Find(file, res, "scbm", [&](const std::vector<uint8_t>& d) { return Starts(d, kMeshName, mesh_name); }, h.mesh_data);
    uint32_t key = U32(h.mesh_data, kMeshKey);
    h.rig = h.mesh < 0 ? -1 : Find(file, res, "rcb ", [&](const std::vector<uint8_t>& d) { return scx::Key(d) == key; }, h.rig_data);
    h.textures = Find(file, res, "txfs", [&](const std::vector<uint8_t>& d) { return Starts(d, kTextureName, texture_name); },
                      h.texture_data);
    if (h.mesh < 0 || h.rig < 0 || h.textures < 0) {
        why = std::string("Faramir's ") + (h.mesh < 0 ? "mesh" : h.rig < 0 ? "rig" : "textures") + " not found";
        return false;
    }
    return true;
}

}  // namespace

bool FindPieces(const std::vector<uint8_t>& pel02, Pieces& out, std::string& why) {
    auto res = scx::Read(pel02);
    long mesh = Find(pel02, res, "scbm", [](const std::vector<uint8_t>& d) { return Starts(d, kMeshName, "EOWa"); }, out.mesh);
    uint32_t key = U32(out.mesh, kMeshKey);
    long rig = mesh < 0 ? -1 : Find(pel02, res, "rcb ", [&](const std::vector<uint8_t>& d) { return scx::Key(d) == key; }, out.rig);
    long tex = Find(pel02, res, "txfs", [](const std::vector<uint8_t>& d) { return Starts(d, kTextureName, "ewoamap"); }, out.textures);
    if (mesh < 0 || rig < 0 || tex < 0) {
        why = std::string("her ") + (mesh < 0 ? "mesh" : rig < 0 ? "rig" : "textures") + " not found in pel02.scx";
        return false;
    }
    return true;
}

bool BuildHero(const std::vector<uint8_t>& faramir, const Pieces& her, std::vector<uint8_t>& out, std::string& why) {
    auto res = scx::Read(faramir);
    His h;
    if (!FindHis(faramir, res, "FAR", "faramap", h, why)) return false;
    int map[256];
    if (!BoneMap(her, h.rig_data, h.mesh_data, map, why)) return false;
    std::vector<uint8_t> mesh;
    if (!Remap(her.mesh, map, U32(h.mesh_data, kMeshKey), mesh)) {
        why = "her mesh could not be bound";
        return false;
    }
    // his voice quiet: his moves' sounds are events in his event tables ("evt "), "audio: play" with four values (a
    // channel of the character's sounds, the sound, two more); channel 2 is his voice (his shouts and grunts as he
    // fights), so those are made a channel the game ignores (8: the sound call 0x7FD40 takes 0-7). Footsteps,
    // swings and hits stay
    std::vector<std::pair<size_t, const std::vector<uint8_t>*>> replace;
    std::vector<std::vector<uint8_t>> events;
    events.reserve(res.size());
    for (size_t i = 0; i < res.size(); i++) {
        std::vector<uint8_t> d;
        if (res[i].type != "evt " || !scx::Data(faramir, res[i], d) || !QuietVoice(d)) continue;
        events.push_back(std::move(d));
        replace.push_back({i, &events.back()});
    }
    // his bow and its arrow (shown when he shoots), with the textures they are drawn with
    std::vector<uint8_t> textures = her.textures;
    std::vector<uint8_t> armed = mesh, armed_tex = scx::AddTextures(her.textures, h.texture_data, {"faramapb", "faramapd", "faramape"});
    if (!armed_tex.empty() && AddParts(armed, h.mesh_data, {"01RNGD000_BOW", "01RNGD000_ARRO"})) {
        mesh.swap(armed);
        textures.swap(armed_tex);
    } else {
        why = "his bow could not be added";  // she is still built, without it
    }
    replace.push_back({size_t(h.mesh), &mesh});
    replace.push_back({size_t(h.textures), &textures});
    std::sort(replace.begin(), replace.end());
    out = scx::Rebuild(faramir, res, replace);
    if (out.empty()) why = "the file could not be put together";
    return !out.empty();
}

bool BuildMenu(std::vector<uint8_t>& fro03, const Pieces& her, std::string& why) {
    auto res = scx::Read(fro03);
    His h;
    if (!FindHis(fro03, res, "FARx", "faramap", h, why)) return false;
    int map[256];
    if (!BoneMap(her, h.rig_data, h.mesh_data, map, why)) return false;
    std::vector<uint8_t> mesh;
    if (!Remap(her.mesh, map, U32(h.mesh_data, kMeshKey), mesh)) {
        why = "her mesh could not be bound";
        return false;
    }
    // the menu's textures are one picture each: hers made so (DXT5 ones made DXT1 if the room needs it). The
    // textures' space is kept as it was until the mesh fits too
    const scx::Resource& tr = res[size_t(h.textures)];
    std::vector<uint8_t> was(fro03.begin() + long(tr.chunks.front().at), fro03.begin() + long(tr.end));
    uint32_t was_size = U32(fro03, tr.header_at + 32);
    const std::vector<std::vector<std::string>> tries = {{}, {"ewoamapc"}, {"ewoamapa", "ewoamapc"}};
    bool fits = false;
    for (const auto& to1 : tries) {
        std::vector<uint8_t> tex = scx::FirstPictures(her.textures, to1);
        if (!tex.empty() && scx::ReplaceInPlace(fro03, tr, tex)) {
            fits = true;
            break;
        }
    }
    if (!fits) {
        why = "her textures do not fit in the menu's file";
        return false;
    }
    if (!scx::ReplaceInPlace(fro03, res[size_t(h.mesh)], mesh)) {
        std::memcpy(&fro03[tr.chunks.front().at], was.data(), was.size());
        std::memcpy(&fro03[tr.header_at + 32], &was_size, 4);
        why = "her mesh does not fit in the menu's file";
        return false;
    }
    return true;
}

namespace {
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // RGBA
    const uint8_t* at(int x, int y) const { return &px[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
};
// a part of an image scaled to w x h, each pixel the average of the part's pixels it covers
Image Scaled(const Image& src, int x0, int y0, int x1, int y1, int w, int h) {
    Image o{w, h, std::vector<uint8_t>(size_t(w) * size_t(h) * 4)};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int sx0 = x0 + (x1 - x0) * x / w, sx1 = std::max(sx0 + 1, x0 + (x1 - x0) * (x + 1) / w);
            int sy0 = y0 + (y1 - y0) * y / h, sy1 = std::max(sy0 + 1, y0 + (y1 - y0) * (y + 1) / h);
            int sum[4] = {}, n = 0;
            for (int sy = sy0; sy < sy1; sy++)
                for (int sx = sx0; sx < sx1; sx++, n++)
                    for (int k = 0; k < 4; k++) sum[k] += src.at(sx, sy)[k];
            for (int k = 0; k < 4; k++) o.px[(size_t(y) * size_t(w) + size_t(x)) * 4 + size_t(k)] = uint8_t(sum[k] / n);
        }
    return o;
}
bool InEllipse(int x, int y, int x0, int y0, int x1, int y1) {
    double cx = (x0 + x1 + 1) / 2.0, cy = (y0 + y1 + 1) / 2.0, rx = (x1 - x0 + 1) / 2.0, ry = (y1 - y0 + 1) / 2.0;
    double dx = (x + 0.5 - cx) / rx, dy = (y + 0.5 - cy) / ry;
    return dx * dx + dy * dy <= 1.0;
}
// src drawn at (ox, oy) where (x, y) of it is inside the ellipse (x0, y0)-(x1, y1) of src's own size
void Draw(Image& dst, const Image& src, int ox, int oy, int x0, int y0, int x1, int y1) {
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++) {
            if (!InEllipse(x, y, x0, y0, x1, y1)) continue;
            uint8_t* d = &dst.px[(size_t(oy + y) * size_t(dst.w) + size_t(ox + x)) * 4];
            std::memcpy(d, src.at(x, y), 3);
            d[3] = 255;
        }
}
}  // namespace

// Faramir's face (64 x 64, DXT1, the head in its top left 33 x 38): hers, in an oval of her hair with a dark edge,
// from her face texture (ewoamapa: the face unwrapped in its top left, her hair beside it)
bool BuildPortrait(std::vector<uint8_t>& glob, const Pieces& her, std::string& why) {
    scx::Texture face;
    if (!scx::FindTexture(her.textures, "ewoamapa", face) || face.w != 128 || face.h != 128) {
        why = "her face texture not found";
        return false;
    }
    Image src{face.w, face.h, scx::DecodeDxt(&her.textures[face.at], face.w, face.h, face.format)};
    constexpr int kW = 33, kH = 38;
    Image out{64, 64, std::vector<uint8_t>(64 * 64 * 4, 0)};
    Image edge{kW, kH, std::vector<uint8_t>(size_t(kW) * kH * 4, 0)};
    for (size_t i = 0; i < edge.px.size(); i += 4) {
        edge.px[i] = 20;
        edge.px[i + 1] = 14;
        edge.px[i + 2] = 10;
    }
    Draw(out, edge, 0, 0, 1, 0, kW - 2, kH - 1);
    Draw(out, Scaled(src, 0, 20, 14, 60, kW, kH), 0, 0, 2, 1, kW - 3, kH - 2);
    Draw(out, Scaled(src, 14, 2, 100, 84, 22, 29), 5, 5, 0, 0, 21, 28);
    std::vector<uint8_t> dxt = scx::EncodeDxt1(out.px, 64, 64);
    auto res = scx::Read(glob);
    for (const scx::Resource& r : res) {
        if (r.type != "txfg") continue;
        std::vector<uint8_t> d;
        scx::Texture t;
        if (!scx::Data(glob, r, d) || !scx::FindTexture(d, "faramir", t)) continue;
        if (t.w != 64 || t.h != 64 || t.format != 0x0C) break;
        if (scx::PatchData(glob, r, t.at, dxt)) return true;
        why = "her face does not fit in the game's file";
        return false;
    }
    why = "Faramir's face not found";
    return false;
}

}  // namespace rotk::eowyn
