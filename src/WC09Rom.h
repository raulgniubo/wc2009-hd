// Small read-only helpers for the WC2009 ROM: NitroFS lookup, Konami .pac, LZ10.
#pragma once
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "types.h"

namespace melonDS::WC09
{
inline u32 RD32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
inline u16 RD16(const u8* p) { return p[0] | (p[1] << 8); }

struct FileRef { u32 Start = 0, Size = 0; };

inline bool FindFile(const u8* rom, u32 len, const std::string& path, FileRef& out)
{
    u32 fnt = RD32(rom + 0x40), fat = RD32(rom + 0x48);
    if (fnt >= len || fat >= len) return false;
    u32 dir = 0xF000;
    size_t pos = 0;
    while (true)
    {
        size_t slash = path.find('/', pos);
        std::string part = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        const u8* ent = rom + fnt + (dir & 0xFFF) * 8;
        u32 p = fnt + RD32(ent);
        u16 fid = RD16(ent + 4);
        bool found = false;
        while (p < len && rom[p])
        {
            u8 t = rom[p++]; u8 n = t & 0x7F;
            std::string name((const char*)rom + p, n); p += n;
            if (t & 0x80)
            {
                u16 sub = RD16(rom + p); p += 2;
                if (slash != std::string::npos && name == part) { dir = sub; found = true; break; }
            }
            else
            {
                if (slash == std::string::npos && name == part)
                {
                    out.Start = RD32(rom + fat + fid * 8);
                    out.Size = RD32(rom + fat + fid * 8 + 4) - out.Start;
                    return out.Start + out.Size <= len;
                }
                fid++;
            }
        }
        if (!found) return false;
        pos = slash + 1;
    }
}

struct PacEntry { std::string Name; u32 Start, Size; };

inline std::vector<PacEntry> ParsePac(const u8* d, u32 len)
{
    std::vector<PacEntry> out;
    if (len < 0x200) return out;
    u32 tableOff = RD16(d) + d[2] * 0x200;
    std::vector<std::string> names;
    for (u32 sec = 0; sec < tableOff && sec < len; sec += 0x200)
    {
        u32 p = sec + 3;
        while (p < len && d[p]) { u8 n = d[p]; names.emplace_back((const char*)d + p + 2, n); p += 2 + n; }
    }
    u32 t = tableOff;
    std::vector<std::pair<u32, u32>> ents;
    while (ents.size() < names.size() && t + 8 <= len)
    {
        if (((t - tableOff) & 0x1FF) == 0) t += 8;
        ents.emplace_back(RD32(d + t), RD32(d + t + 4)); t += 8;
    }
    u32 base = (t + 0x1FF) & ~0x1FFu;
    for (size_t i = 0; i < ents.size(); i++)
        if (base + ents[i].first + ents[i].second <= len)
            out.push_back({names[i], base + ents[i].first, ents[i].second});
    return out;
}

inline bool LZ10(const u8* src, u32 len, std::vector<u8>& out)
{
    if (len < 4 || src[0] != 0x10) return false;
    u32 size = src[1] | (src[2] << 8) | (src[3] << 16);
    out.clear(); out.reserve(size);
    u32 p = 4;
    while (out.size() < size && p < len)
    {
        u8 flags = src[p++];
        for (int b = 0; b < 8 && out.size() < size; b++)
        {
            if (flags & (0x80 >> b))
            {
                if (p + 1 >= len) return false;
                u32 n = (src[p] >> 4) + 3, disp = (((src[p] & 0xF) << 8) | src[p + 1]) + 1; p += 2;
                if (disp > out.size()) return false;
                for (u32 k = 0; k < n; k++) out.push_back(out[out.size() - disp]);
            }
            else
            {
                if (p >= len) return false;
                out.push_back(src[p++]);
            }
        }
    }
    return out.size() == size;
}


}
