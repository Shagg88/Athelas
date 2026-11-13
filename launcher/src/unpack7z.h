// Athelas - unpacking a release .7z held in memory (unpack7z.cpp)
#pragma once
#include <functional>
#include <string>
#include <vector>

// each file and folder in the archive, in order: name ("folder/file", forward slashes), whether it is a folder, and
// its bytes; each returns false to stop. false: the archive couldn't be read, or each stopped it.
bool unpack7z(const std::string &archive,
              const std::function<bool(const std::string &name, bool dir, const void *data, size_t size)> &each,
              const std::function<void(unsigned done, unsigned count)> &progress);
// the same, from a file on disk (read as it goes: a 32-bit launcher needn't hold a 120 MB download in memory)
bool unpack7z_file(const std::wstring &path,
                   const std::function<bool(const std::string &name, bool dir, const void *data, size_t size)> &each,
                   const std::function<void(unsigned done, unsigned count)> &progress);
// the same, from a file already open for reading (a Windows HANDLE, left open): read from its start
bool unpack7z_handle(void *file,
                     const std::function<bool(const std::string &name, bool dir, const void *data, size_t size)> &each,
                     const std::function<void(unsigned done, unsigned count)> &progress);
