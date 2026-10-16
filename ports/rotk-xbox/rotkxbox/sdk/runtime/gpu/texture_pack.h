#pragma once
// Texture packs: the game's textures written out as PNG files named by a hash of their contents, and PNG or DDS
// files with those names loaded in their place, at any size. The layout and names are ROTK Fix's, so one guide covers
// both: textures/dump/<w>x<h>_<16 hex digits>.png, and packs anywhere under textures/replace/ whose names end in the
// same 16 digits (a prefix is allowed: hud_health_256x64_0123456789abcdef.png). A DDS file keeps its own mip levels;
// a PNG gets them made.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gpu::pack {

// the formats a file may hold, all UNORM: 32 bit BGRA or RGBA, BC1, BC2, BC3, BC7. Values are DXGI's.
enum class Format : uint32_t { Unknown = 0, RGBA8 = 28, BC1 = 71, BC2 = 74, BC3 = 77, BGRA8 = 87, BC7 = 98 };

struct Image {
    uint32_t width = 0, height = 0;
    Format format = Format::Unknown;
    std::vector<std::vector<uint8_t>> levels;  // level 0 first, rows tightly packed (block rows for BC formats)
};

bool Supported(Format f);
// bytes of one row (one row of 4x4 blocks for BC formats) and the number of rows of a w x h level
uint32_t RowBytes(Format f, uint32_t w);
uint32_t Rows(Format f, uint32_t h);

bool WriteDds(const std::filesystem::path& file, const Image& img);
bool ReadDds(const std::filesystem::path& file, Image& img);

// the file name a texture's contents go by: 16 hex digits, and the dump's <w>x<h>_<16 lower case hex digits>.png
std::string Name(uint64_t hash);
std::string PngName(uint32_t width, uint32_t height, uint64_t hash);

// an RGBA picture as PNG: read with a full chain of mip levels made by halving (RGBA8), written from level 0
bool ReadPng(const std::filesystem::path& file, Image& img);
// the same from PNG bytes in memory
bool DecodePng(const uint8_t* data, size_t size, Image& img);
// a game's own replacement for a texture, by the pack hash (a texture pack file of the same name wins): true with the
// picture in out. Asked once per hash; set by game code, null for none.
using BuiltinTexture = bool (*)(uint64_t hash, uint32_t width, uint32_t height, Image& out);
extern BuiltinTexture g_builtin;
// the same found from a texture's contents, asked before g_builtin: its Xbox format, size and levels, its guest
// bytes (all levels, as stored, swizzled) and a P8 texture's 256 colours (D3DCOLOR), else null
struct Contents {
    uint32_t format, width, height, levels;
    const uint8_t* data;
    size_t size;
    const uint32_t* palette;
};
using BuiltinContents = bool (*)(const Contents& tex, Image& out);
extern BuiltinContents g_builtin_contents;
// an RGBA8 or BGRA8 picture's mip levels below level 0, each made by halving
void MakeLevels(Image& img);
bool WritePng(const std::filesystem::path& file, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba);
// written on a thread of its own, so a dump does not stall the frame; a file that exists is left alone
void WritePngLater(std::filesystem::path file, uint32_t width, uint32_t height, std::vector<uint8_t> rgba);
// level 0 of a texture as RGBA8, rows packed: from BGRA8, RGBA8, BC1, BC2 or BC3; false for another format
bool ToRgba(Format f, uint32_t width, uint32_t height, const std::vector<uint8_t>& level, std::vector<uint8_t>& rgba);

// replacement files (.png, .dds) under a folder and its subfolders, by hash; scanned once
class Library {
public:
    void Scan(const std::filesystem::path& dir, const std::string& skip = "dump");  // skip: a subfolder left out
    const std::filesystem::path* Find(uint64_t hash) const;
    size_t Size() const { return files_.size(); }

private:
    std::vector<std::pair<uint64_t, std::filesystem::path>> files_;  // sorted by hash
};

}  // namespace gpu::pack
