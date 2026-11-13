// Athelas - unpacking a release .7z held in memory (LZMA SDK's 7z decoder, public domain: third_party/lzma)
#include "unpack7z.h"

#include <windows.h>

extern "C" {
#include "../third_party/lzma/7z.h"
#include "../third_party/lzma/7zAlloc.h"
#include "../third_party/lzma/7zCrc.h"
}
#include <cstring>

namespace {
// the archive as a seekable stream over the downloaded bytes
struct MemIn {
    ISeekInStream vt;
    const unsigned char *data;
    size_t size, pos;
};
SRes mem_read(ISeekInStreamPtr p, void *buf, size_t *size)
{
    MemIn *m = Z7_CONTAINER_FROM_VTBL(p, MemIn, vt);
    size_t n = m->size - m->pos;
    if (*size < n) n = *size;
    std::memcpy(buf, m->data + m->pos, n);
    m->pos += n;
    *size = n;
    return SZ_OK;
}
SRes mem_seek(ISeekInStreamPtr p, Int64 *pos, ESzSeek origin)
{
    MemIn *m = Z7_CONTAINER_FROM_VTBL(p, MemIn, vt);
    Int64 base = origin == SZ_SEEK_SET ? 0 : origin == SZ_SEEK_CUR ? (Int64)m->pos : (Int64)m->size;
    Int64 to = base + *pos;
    if (to < 0 || to > (Int64)m->size) return SZ_ERROR_PARAM;
    m->pos = (size_t)to;
    *pos = to;
    return SZ_OK;
}
}  // namespace

static bool unpack_stream(ISeekInStream *stream,
                          const std::function<bool(const std::string &name, bool dir, const void *data, size_t size)> &each,
                          const std::function<void(unsigned done, unsigned count)> &progress)
{
    static bool crc = false;
    if (!crc) { CrcGenerateTable(); crc = true; }
    ISzAlloc alloc = { SzAlloc, SzFree }, temp = { SzAllocTemp, SzFreeTemp };
    CLookToRead2 look;
    LookToRead2_CreateVTable(&look, False);
    const size_t kBuf = 1 << 18;
    look.buf = (Byte *)ISzAlloc_Alloc(&alloc, kBuf);
    if (!look.buf) return false;
    look.bufSize = kBuf;
    look.realStream = stream;
    LookToRead2_INIT(&look)
    CSzArEx db;
    SzArEx_Init(&db);
    bool ok = SzArEx_Open(&db, &look.vt, &alloc, &temp) == SZ_OK;
    UInt32 block = 0xFFFFFFFF;
    Byte *out = NULL;
    size_t outSize = 0;
    // (each may throw, out of memory say: what was allocated here is still freed, and the caller's file handle closed)
    try {
    for (UInt32 i = 0; ok && i < db.NumFiles; i++) {
        // the name (UTF-16 in the archive): the launcher's names are plain ASCII paths
        size_t len = SzArEx_GetFileNameUtf16(&db, i, NULL);
        std::vector<UInt16> w(len);
        SzArEx_GetFileNameUtf16(&db, i, w.data());
        std::string name;
        for (size_t k = 0; k + 1 < len; k++) name += w[k] < 0x80 ? (char)w[k] : '_';
        for (char &ch : name) if (ch == '\\') ch = '/';
        bool dir = SzArEx_IsDir(&db, i) != 0;
        if (dir) { ok = each(name, true, NULL, 0); continue; }
        size_t offset = 0, size = 0;
        if (SzArEx_Extract(&db, &look.vt, i, &block, &out, &outSize, &offset, &size, &alloc, &temp) != SZ_OK) { ok = false; break; }
        ok = each(name, false, out + offset, size);
        if (progress) progress(i + 1, db.NumFiles);
    }
    } catch (...) {
        ISzAlloc_Free(&alloc, out);
        SzArEx_Free(&db, &alloc);
        ISzAlloc_Free(&alloc, look.buf);
        throw;
    }
    ISzAlloc_Free(&alloc, out);
    SzArEx_Free(&db, &alloc);
    ISzAlloc_Free(&alloc, look.buf);
    return ok;
}

bool unpack7z(const std::string &archive,
              const std::function<bool(const std::string &name, bool dir, const void *data, size_t size)> &each,
              const std::function<void(unsigned done, unsigned count)> &progress)
{
    MemIn in;
    in.vt.Read = mem_read;
    in.vt.Seek = mem_seek;
    in.data = (const unsigned char *)archive.data();
    in.size = archive.size();
    in.pos = 0;
    return unpack_stream(&in.vt, each, progress);
}

// the archive read from its file as it goes: the download is never held in memory whole
namespace {
struct FileIn {
    ISeekInStream vt;
    HANDLE h;
};
SRes file_read(ISeekInStreamPtr p, void *buf, size_t *size)
{
    FileIn *f = Z7_CONTAINER_FROM_VTBL(p, FileIn, vt);
    DWORD want = *size > (1u << 30) ? (1u << 30) : (DWORD)*size, got = 0;
    if (!ReadFile(f->h, buf, want, &got, NULL)) { *size = 0; return SZ_ERROR_READ; }
    *size = got;
    return SZ_OK;
}
SRes file_seek(ISeekInStreamPtr p, Int64 *pos, ESzSeek origin)
{
    FileIn *f = Z7_CONTAINER_FROM_VTBL(p, FileIn, vt);
    LARGE_INTEGER to, now;
    to.QuadPart = *pos;
    if (!SetFilePointerEx(f->h, to, &now, origin == SZ_SEEK_SET ? FILE_BEGIN : origin == SZ_SEEK_CUR ? FILE_CURRENT : FILE_END))
        return SZ_ERROR_READ;
    *pos = now.QuadPart;
    return SZ_OK;
}
}  // namespace

bool unpack7z_handle(void *file,
                     const std::function<bool(const std::string &name, bool dir, const void *data, size_t size)> &each,
                     const std::function<void(unsigned done, unsigned count)> &progress)
{
    FileIn in;
    in.vt.Read = file_read;
    in.vt.Seek = file_seek;
    in.h = (HANDLE)file;
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(in.h, zero, NULL, FILE_BEGIN)) return false;
    return unpack_stream(&in.vt, each, progress);
}

bool unpack7z_file(const std::wstring &path,
                   const std::function<bool(const std::string &name, bool dir, const void *data, size_t size)> &each,
                   const std::function<void(unsigned done, unsigned count)> &progress)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    try { ok = unpack7z_handle(h, each, progress); } catch (...) { CloseHandle(h); throw; }
    CloseHandle(h);
    return ok;
}
