// File system helpers for the WC2009 HD core that work the same on iOS, Android, macOS, Linux and
// Windows (paths are UTF-8 everywhere; on Windows they're converted for the wide-character APIs).
#pragma once
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace wc09p
{
FILE* Fopen(const std::string& path, const char* mode);
bool Exists(const std::string& path);
bool IsDir(const std::string& path);
bool MakeDir(const std::string& path);              // one level, fine if it exists
bool Rename(const std::string& from, const std::string& to);
bool Remove(const std::string& path);
// names (not paths) of the entries of a folder; dirs gets the sub-folders' names if given
std::vector<std::string> ListDir(const std::string& dir, std::vector<std::string>* dirs = nullptr);
int64_t Tell(FILE* f);
bool Seek(FILE* f, int64_t offset, int origin);
bool ReadWhole(const std::string& path, std::vector<unsigned char>& out);
}
