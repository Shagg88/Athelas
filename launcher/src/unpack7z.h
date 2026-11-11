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
