// Athelas - what is left of building the ports on the player's PC (1.7.3/1.7.4). Since 1.7.5 the Xbox ports ship as
// programs again: this removes those releases' leftover build kits and tools, and reads the game's default.xbe for
// the "This version works" reports.
#include "builder.h"
#include "config.h"
#include "port.h"
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

namespace {

bool exists(const std::string &p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool read_file(const std::string &p, std::string &out)
{
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char b[65536]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) out.append(b, n);
    fclose(f);
    return true;
}
void remove_tree(const std::string &dir)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = fd.cFileName;
            if (n == "." || n == "..") continue;
            std::string p = dir + "\\" + n;
            // a junction or folder link is removed itself, never walked: its target can be outside this folder
            // (review, 4 Nov: else cleaning an old build could empty a folder the link points at)
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryA(p.c_str()); else DeleteFileA(p.c_str());
            } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(p);
            else { SetFileAttributesA(p.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileA(p.c_str()); }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}

// The player's default.xbe: from the game folder, or from inside the disc image
bool read_xbe(const LibGame &g, std::string &out)
{
    if (!g.iso) return read_file(g.path + "\\default.xbe", out);
    unsigned long long off = 0; uint32_t size = 0;
    if (!iso_locate(g.path, "default.xbe", off, size) || !size) return false;
    FILE *f = fopen(g.path.c_str(), "rb");
    if (!f) return false;
    out.resize(size);
    bool ok = _fseeki64(f, (long long)off, SEEK_SET) == 0 && fread(&out[0], 1, size, f) == size;
    fclose(f);
    return ok;
}

}   // namespace

// 1.7.5: the Xbox games ship ready to play again, so 1.7.3/1.7.4's build kits, build tools and each game's build
// folder (recompiled code, objects: hundreds of MB) are left over. Removed once every game they were for has its
// program; anything else in the folder is left alone.
void builder_cleanup_old()
{
    std::string root = exe_dir();
    if (!exists(root + "\\kits") && !exists(root + "\\tools\\llvm-mingw")) return;
    static const char *const ports[][2] = { { "TT-Xbox", "TwoTowers" }, { "ROTK-Xbox", "ROTKXbox" },
                                            { "FOTR-Xbox", "FOTRXbox" }, { "TA-Xbox", "TAXbox" } };
    for (auto &p : ports)
        if ((exists(root + "\\kits\\" + p[0]) || exists(root + "\\kits\\" + p[0] + "-arm64")) &&
            !exists(root + "\\" + p[0] + "\\" + p[1] + ".exe")) return;  // still needed
    for (auto &p : ports) {
        // the stamps of programs built here from a kit (review: an ARM64 PC kept preferring a 1.7.4 build with one)
        std::string e = root + "\\" + p[0] + "\\" + p[1];
        DeleteFileA((e + ".exe.build").c_str());
        DeleteFileA((e + "-arm64.exe.build").c_str());
    }
    for (auto &p : ports) {
        std::string b = root + "\\" + p[0] + "\\build";
        if (exists(b + "\\generated") || exists(b + "\\obj") || exists(b + "\\generated.stamp")) remove_tree(b);
    }
    remove_tree(root + "\\kits");
    remove_tree(root + "\\tools\\llvm-mingw");
    RemoveDirectoryA((root + "\\tools").c_str());  // only when nothing else is in it
}

// The game's default.xbe as SHA-1 hex ("" if unreadable): how an untested release the player says works is remembered
// and reported (GitHub)
#include <bcrypt.h>
std::string xbe_sha1(const LibGame &g)
{
    std::string xbe;
    if (!read_xbe(g, xbe)) return "";
    BCRYPT_ALG_HANDLE alg = NULL; BCRYPT_HASH_HANDLE h = NULL; unsigned char d[20]; std::string r;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, NULL, 0) != 0) return "";
    if (BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0 && BCryptHashData(h, (PUCHAR)xbe.data(), (ULONG)xbe.size(), 0) == 0 &&
        BCryptFinishHash(h, d, 20, 0) == 0) {
        static const char *x = "0123456789abcdef";
        for (unsigned char c : d) { r += x[c >> 4]; r += x[c & 15]; }
    }
    if (h) BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    return r;
}
