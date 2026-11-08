#include "Portable.h"
#include <cstring>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace wc09p
{
static fs::path P(const std::string& s)
{
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}

static std::string U8(const fs::path& p)
{
#if defined(__cpp_char8_t)
    std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
#else
    return p.u8string();
#endif
}

FILE* Fopen(const std::string& path, const char* mode)
{
#ifdef _WIN32
    std::wstring wm(mode, mode + strlen(mode));
    return _wfopen(P(path).c_str(), wm.c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

bool Exists(const std::string& path) { std::error_code ec; return fs::exists(P(path), ec); }
bool IsDir(const std::string& path) { std::error_code ec; return fs::is_directory(P(path), ec); }
bool MakeDir(const std::string& path) { std::error_code ec; fs::create_directory(P(path), ec); return IsDir(path); }
bool Rename(const std::string& from, const std::string& to) { std::error_code ec; fs::rename(P(from), P(to), ec); return !ec; }
bool Remove(const std::string& path) { std::error_code ec; return fs::remove(P(path), ec); }

std::vector<std::string> ListDir(const std::string& dir, std::vector<std::string>* dirs)
{
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(P(dir), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
    {
        std::string n = U8(it->path().filename());
        if (n.empty() || n[0] == '.') continue;
        std::error_code e2;
        if (it->is_directory(e2)) { if (dirs) dirs->push_back(n); }
        else out.push_back(n);
    }
    return out;
}

int64_t Tell(FILE* f)
{
#ifdef _WIN32
    return _ftelli64(f);
#else
    return (int64_t)ftello(f);
#endif
}

bool Seek(FILE* f, int64_t offset, int origin)
{
#ifdef _WIN32
    return _fseeki64(f, offset, origin) == 0;
#else
    return fseeko(f, (off_t)offset, origin) == 0;
#endif
}

bool ReadWhole(const std::string& path, std::vector<unsigned char>& out)
{
    out.clear();
    FILE* f = Fopen(path, "rb");
    if (!f) return false;
    Seek(f, 0, SEEK_END);
    int64_t n = Tell(f);
    Seek(f, 0, SEEK_SET);
    bool ok = n >= 0;
    if (ok && n > 0) { out.resize((size_t)n); ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n; }
    fclose(f);
    if (!ok) out.clear();
    return ok;
}
}
