#pragma once
// ROTK's level and hero files (.scx): chunks, the resources they hold, the game's LZ, and building new ones from
// pieces of the player's own files (rotk_eowyn.cpp). No game data here.
//
// A file is a run of chunks, 4-byte aligned. A data chunk is "COHS" (SHOC), its size (all of it, header too) at +4,
// its kind at +16: "RDHS" (SHDR) starts a resource (its type, four letters reversed, at +24; its data's size at +32),
// "TADS" (SDAT) holds data from +0x40, "tadR" (Rdat) LZ data from +0x44 that unpacks to the size at +0x40. Other
// chunks have their own tag first and their size at +4: "LLIF" fills to a 64 KB boundary (no chunk crosses one),
// "LRTC" and "DDAP" open a hero file, "COTS" (a level's table of streamed zones, by file offset) opens a level.
#include <cstdint>
#include <string>
#include <vector>

namespace rotk::scx {

struct Chunk {
    size_t at = 0, size = 0;
    bool packed = false;  // Rdat
};
struct Resource {
    std::string type;          // "txfs", "scbm", "rcb ", "bnk ", "evt "...
    size_t header_at = 0;      // its SHDR chunk
    size_t header_size = 0;
    std::vector<Chunk> chunks; // its data
    size_t end = 0;            // where the next resource's header (or the file's end) is
};

std::vector<Resource> Read(const std::vector<uint8_t>& file);
// a resource's data, unpacked; false when a chunk is broken
bool Data(const std::vector<uint8_t>& file, const Resource& r, std::vector<uint8_t>& out);
uint32_t Key(const std::vector<uint8_t>& data);  // +8 of a rig's, bank's or event table's data: whose it is

// the game's LZ: big-endian 16-bit words; 0x8800 | n a literal run of n bytes (n < 0x800); otherwise a copy of
// ((w >> 12) & 7) + 3 bytes (7: one more byte adds to it) from w & 0xFFF back (copied from the end when bit 15 is set);
// with 0x8800 set and (w >> 12) & 7 not 0 a run of one byte
bool Unpack(const uint8_t* src, size_t n, size_t size, std::vector<uint8_t>& out);
std::vector<uint8_t> Pack(const uint8_t* src, size_t n);

// a hero file again with some resources' data replaced (stored chunks, the same order and headers); empty when the
// file has none
std::vector<uint8_t> Rebuild(const std::vector<uint8_t>& file, const std::vector<Resource>& res,
                             const std::vector<std::pair<size_t, const std::vector<uint8_t>*>>& replace);
// a resource's data replaced where it is, in the space its chunks take up to the next resource (packed, filled to the
// end): a level's file keeps every other byte where it was. False (and the file unchanged) when it does not fit
bool ReplaceInPlace(std::vector<uint8_t>& file, const Resource& r, const std::vector<uint8_t>& data);

// bytes of a resource's data changed where they are: each chunk holding them unpacked, changed and packed again in
// its own space (the rest of a packed chunk after its stream is never read). False, and nothing changed, when one no
// longer fits
bool PatchData(std::vector<uint8_t>& file, const Resource& r, size_t at, const std::vector<uint8_t>& bytes);

// rigs: the bone table, 20 bytes a bone: its name code (8 bytes), 0, its index (big endian), 0xE2 (big endian)
struct Bone {
    uint64_t name = 0;
    uint32_t index = 0;
};
std::vector<Bone> Bones(const std::vector<uint8_t>& rig);

// meshes ("OBX "; a skin "PCsk" at 0x310): its parts (+0x18 their count, +0x1C their places), each with its skinned
// vertices (count, place from 0x310, 20 bytes each: x y z w floats, two bone indices, 255 none); the rig's key at +0x80.
// Each vertex's place in the mesh
bool Vertices(const std::vector<uint8_t>& mesh, std::vector<size_t>& at);

// texture packs ("TXX "): each texture's first picture only, and a DXT5 one made DXT1 where `dxt1` names it (its
// own 1-bit alpha kept): a smaller pack for the menus' figures
std::vector<uint8_t> FirstPictures(const std::vector<uint8_t>& pack, const std::vector<std::string>& dxt1);

// a pack with some textures of another (by name, all their pictures) added after its own; empty when one is missing.
// A texture's entry (0x50 bytes) has its pictures at +8 (12 bytes each: place in the data, size / 16, ffff, its first
// page, kind), its number in the pack at +0x48; the pack's pages (256 bytes, each texture's rounded up) at +0x10
std::vector<uint8_t> AddTextures(const std::vector<uint8_t>& pack, const std::vector<uint8_t>& from,
                                 const std::vector<std::string>& names);

// a texture in a pack by name: its first picture's place in the pack, width, height, format; false when none
struct Texture {
    size_t at = 0;
    int w = 0, h = 0, format = 0;
};
bool FindTexture(const std::vector<uint8_t>& pack, const char* name, Texture& t);
// DXT1 (0x0C) or DXT5 (0x0E) pictures to RGBA and RGBA to DXT1 (alpha below half transparent)
std::vector<uint8_t> DecodeDxt(const uint8_t* data, int w, int h, int format);
std::vector<uint8_t> EncodeDxt1(const std::vector<uint8_t>& rgba, int w, int h);

}  // namespace rotk::scx
