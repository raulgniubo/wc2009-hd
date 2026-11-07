#include "WC09Sprites.h"
#include "WC09Rom.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <mutex>
#include <unordered_map>

#define XXH_STATIC_LINKING_ONLY
#include "xxhash/xxhash.h"

namespace melonDS::WC09
{

namespace
{

struct TileEntry
{
    int Image;
    s16 OX, OY, OW, OH;   // the OAM (or whole image) this tile belongs to, inside the image
    u8 I, J;              // tile position inside that OAM (tile units, unflipped)
    bool HF, VF;
};

struct ImageInfo { std::string Name; s16 W, H; };

std::vector<ImageInfo> Images;
// tile content hash -> every place it appears (capped: very common tiles say nothing)
std::unordered_map<u64, std::vector<TileEntry>> Tiles;
constexpr size_t kMaxPlaces = 48;
std::unordered_map<u64, SpriteHD> Cache;
std::unordered_map<u64, int> Thumbs;   // card thumbnail sprite chars (32x32, 8bpp) -> card
std::unordered_map<u64, int> ThumbTex; // the same thumbnails as linear 32x32 8-bit textures -> card
std::unordered_map<u64, int> ThumbTiles; // single 8x8 tiles of the thumbnails -> card * 16 + tile (4x4 grid)
std::mutex Lock;

bool Blank(const u8* t, u32 n) { for (u32 i = 0; i < n; i++) if (t[i]) return false; return true; }

void AddTile(const u8* tile, u32 tb, const TileEntry& e)
{
    if (Blank(tile, tb)) return;
    auto& v = Tiles[XXH64(tile, tb, tb)];
    if (v.size() < kMaxPlaces) v.push_back(e);
}

std::vector<u8> Unpack(const u8* src, u32 size)
{
    std::vector<u8> out;
    if (size >= 4 && src[0] == 0x10) { if (!LZ10(src, size, out)) out.clear(); }
    else out.assign(src, src + size);
    return out;
}

// Konami NTBG with an OBJD chunk: a whole image as sprite tiles, row-major
void IndexOBJD(const std::string& name, const std::vector<u8>& dec)
{
    if (dec.size() < 16 || memcmp(dec.data(), "NTBG", 4) != 0) return;
    u32 p = 16;
    while (p + 8 <= dec.size())
    {
        u32 cs = RD32(&dec[p + 4]);
        if (cs < 8 || p + cs > dec.size()) return;
        if (memcmp(&dec[p], "OBJD", 4) == 0 && cs >= 32)
        {
            const u8* b = &dec[p + 8];
            u32 mapsz = RD32(b + 4); u16 tw = RD16(b + 8), th = RD16(b + 10);
            u32 chrsz = RD32(b + 20);
            u32 n = (u32)tw * th;
            if (!n || 24 + mapsz + chrsz > cs - 8 || mapsz < n * 4) return;
            const u8* mp = b + 24; const u8* ch = b + 24 + mapsz;
            u32 tb = chrsz >= n * 64 ? 64 : 32;
            int img = (int)Images.size();
            Images.push_back({name, (s16)(tw * 8), (s16)(th * 8)});
            for (u32 t = 0; t < n; t++)
            {
                u32 off = RD32(mp + t * 4) * 32;
                if (off + tb > chrsz) continue;
                AddTile(ch + off, tb, {kSpriteImageBase + img, 0, 0, (s16)(tw * 8), (s16)(th * 8),
                                       (u8)(t % tw), (u8)(t / tw), false, false});
            }
            return;
        }
        p += cs;
    }
}

const u8* Block(const std::vector<u8>& d, const char* magic, u32& size)
{
    if (d.size() < 16) return nullptr;
    u16 hs = RD16(&d[12]), n = RD16(&d[14]);
    u32 p = hs;
    for (u16 i = 0; i < n && p + 8 <= d.size(); i++)
    {
        u32 s = RD32(&d[p + 4]);
        if (s < 8 || p + s > d.size()) return nullptr;
        if (memcmp(&d[p], magic, 4) == 0) { size = s - 8; return &d[p + 8]; }
        p += s;
    }
    return nullptr;
}

// Nitro NCGR + NCER: each cell is one image, composed from its OAMs
void IndexCells(const std::string& base, const std::vector<u8>& ncgr, const std::vector<u8>& ncer)
{
    static const u8 sizes[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}} };
    u32 gs, es;
    const u8* g = Block(ncgr, "RAHC", gs);
    const u8* e = Block(ncer, "KBEC", es);
    if (!g || !e || gs < 24 || es < 12) return;
    u32 dsize = RD32(g + 16), doff = RD32(g + 20);
    if (doff + dsize > gs) return;
    const u8* chars = g + doff;
    u16 ncell = RD16(e), attrType = RD16(e + 2);
    u32 celloff = RD32(e + 4), mapflags = RD32(e + 8);
    u32 stride = attrType == 1 ? 16 : 8;
    u32 oamStart = celloff + ncell * stride;
    u32 tshift = 5 + (mapflags & 3);

    for (u16 c = 0; c < ncell; c++)
    {
        if (celloff + (c + 1) * stride > es) return;
        u16 nobj = RD16(e + celloff + c * stride);
        u32 ooff = RD32(e + celloff + c * stride + 4);
        if (!nobj || oamStart + ooff + nobj * 6 > es) continue;

        struct Obj { int x, y, w, h; u32 tile; bool bpp8, hf, vf; };
        std::vector<Obj> objs;
        int x0 = 9999, y0 = 9999, x1 = -9999, y1 = -9999;
        for (u16 o = 0; o < nobj; o++)
        {
            const u8* a = e + oamStart + ooff + o * 6;
            u16 a0 = RD16(a), a1 = RD16(a + 2), a2 = RD16(a + 4);
            int shape = a0 >> 14, size = a1 >> 14;
            if (shape > 2) continue;
            Obj ob;
            ob.w = sizes[shape][size][0]; ob.h = sizes[shape][size][1];
            ob.y = a0 & 0xFF; if (ob.y >= 128) ob.y -= 256;
            ob.x = a1 & 0x1FF; if (ob.x >= 256) ob.x -= 512;
            ob.tile = a2 & 0x3FF; ob.bpp8 = a0 & (1 << 13);
            bool rs = a0 & 0x100;
            ob.hf = !rs && (a1 & (1 << 12)); ob.vf = !rs && (a1 & (1 << 13));
            objs.push_back(ob);
            x0 = std::min(x0, ob.x); y0 = std::min(y0, ob.y);
            x1 = std::max(x1, ob.x + ob.w); y1 = std::max(y1, ob.y + ob.h);
        }
        if (objs.empty()) continue;
        int img = (int)Images.size();
        Images.push_back({base + "_c" + std::to_string(c), (s16)(x1 - x0), (s16)(y1 - y0)});
        for (const Obj& ob : objs)
        {
            u32 tb = ob.bpp8 ? 64 : 32;
            u32 tw = ob.w / 8, n = tw * (ob.h / 8);
            u32 start = ob.tile << tshift;
            for (u32 t = 0; t < n; t++)
            {
                if (start + (t + 1) * tb > dsize) break;
                AddTile(chars + start + t * tb, tb, {kSpriteImageBase + img, (s16)(ob.x - x0), (s16)(ob.y - y0),
                                                     (s16)ob.w, (s16)ob.h, (u8)(t % tw), (u8)(t / tw),
                                                     ob.hf, ob.vf});
            }
        }
    }
}

}

void BuildSpriteIndex(const u8* rom, u32 len)
{
    std::lock_guard<std::mutex> lk(Lock);
    Images.clear(); Tiles.clear(); Cache.clear(); Thumbs.clear(); ThumbTex.clear();

    // card thumbnails (deck lists, graveyard...): card.pac bg26 = one 32x32 8bpp OBJD per card,
    // the little card at pixels (6,3)-(25,28)
    {
        FileRef ref;
        if (FindFile(rom, len, "Data_arc_pac/card.pac", ref))
        {
            const u8* pac = rom + ref.Start;
            std::vector<PacEntry> ents = ParsePac(pac, ref.Size);
            const PacEntry *data = nullptr, *idx = nullptr;
            for (const PacEntry& e : ents)
            {
                if (e.Name.find("bg26_idx") != std::string::npos) idx = &e;
                else if (e.Name.find("bg26") != std::string::npos) data = &e;
            }
            for (u32 c = 0; data && idx && c * 4 + 8 <= idx->Size; c++)
            {
                u32 a = RD32(pac + idx->Start + c * 4), b = RD32(pac + idx->Start + c * 4 + 4);
                if (b <= a || b > data->Size) continue;
                std::vector<u8> d = Unpack(pac + data->Start + a, b - a);
                if (d.size() < 16 + 8 + 24 + 4 + 1024 || memcmp(d.data() + 16, "OBJD", 4)) continue;
                const u8* ch = d.data() + 16 + 8 + 24 + 4;
                Thumbs[XXH64(ch, 1024, 0)] = (int)c;
                u8 lin[1024];
                for (int t = 0; t < 16; t++)
                    for (int i = 0; i < 64; i++)
                        lin[((t / 4) * 8 + i / 8) * 32 + (t % 4) * 8 + i % 8] = ch[t * 64 + i];
                ThumbTex[XXH64(lin, 1024, 0)] = (int)c;
                for (int t = 0; t < 16; t++)
                {
                    u64 th = XXH64(ch + t * 64, 64, 64);
                    if (!ThumbTiles.count(th)) ThumbTiles[th] = (int)c * 16 + t;
                    else ThumbTiles[th] = -1;   // shared by several cards: says nothing
                }
            }
        }
    }

    // (the player's avatar parts - editface/editHair - are left out: the game recolors them
    // through palettes, which an HD image can't follow)
    static const char* const kObjd[] = { "duel2d/chr", "duel2d/chr_tag", "duel2d/MOBJ", "pack", "duel2d" };
    for (const char* path : kObjd)
    {
        FileRef ref;
        if (!FindFile(rom, len, std::string("Data_arc_pac/") + path + ".pac", ref)) continue;
        std::string sp(path), set = sp.substr(sp.rfind('/') == std::string::npos ? 0 : sp.rfind('/') + 1);
        for (const PacEntry& pe : ParsePac(rom + ref.Start, ref.Size))
            IndexOBJD(set + "/" + pe.Name.substr(0, pe.Name.find('.')), Unpack(rom + ref.Start + pe.Start, pe.Size));
    }

    // Nitro cell sets (NCGR + NCER in the same .pac): portraits, coins, menu objects
    for (const char* path : {"duel2d/BustupL", "duel2d/BustupR", "janken", "trunk", "MainMenu", "DCustom"})
    {
        FileRef ref;
        if (!FindFile(rom, len, std::string("Data_arc_pac/") + path + ".pac", ref)) continue;
        std::string sp(path), set = sp.substr(sp.rfind('/') == std::string::npos ? 0 : sp.rfind('/') + 1);
        std::map<std::string, std::pair<const PacEntry*, const PacEntry*>> sets;
        auto ents = ParsePac(rom + ref.Start, ref.Size);
        for (const PacEntry& pe : ents)
        {
            std::string stem = pe.Name.substr(0, pe.Name.rfind('.'));
            if (pe.Name.size() > 7 && pe.Name.compare(pe.Name.size() - 7, 7, ".lzncgr") == 0) sets[stem].first = &pe;
            if (pe.Name.size() > 7 && pe.Name.compare(pe.Name.size() - 7, 7, ".lzncer") == 0) sets[stem].second = &pe;
        }
        for (auto& [stem, pr] : sets)
        {
            if (!pr.first || !pr.second) continue;
            IndexCells(set + "/" + stem, Unpack(rom + ref.Start + pr.first->Start, pr.first->Size),
                       Unpack(rom + ref.Start + pr.second->Start, pr.second->Size));
        }
    }

    // overworld / story: cells in RPG2D.pac, their tiles in RPG2D_2.pac (story portraits, items,
    // overworld menus). The player's own avatar parts are recolored at runtime: left out.
    {
        FileRef rc, rg;
        if (FindFile(rom, len, "Data_arc_pac/RPG2D.pac", rc) && FindFile(rom, len, "Data_arc_pac/RPG2D_2.pac", rg))
        {
            std::map<std::string, const PacEntry*> cgr;
            auto gents = ParsePac(rom + rg.Start, rg.Size);
            for (const PacEntry& pe : gents)
                if (pe.Name.size() > 7 && pe.Name.compare(pe.Name.size() - 7, 7, ".lzncgr") == 0)
                    cgr[pe.Name.substr(0, pe.Name.size() - 7)] = &pe;
            for (const PacEntry& pe : ParsePac(rom + rc.Start, rc.Size))
            {
                if (pe.Name.size() <= 7 || pe.Name.compare(pe.Name.size() - 7, 7, ".lzncer") != 0) continue;
                std::string stem = pe.Name.substr(0, pe.Name.size() - 7);
                if (stem.rfind("f_", 0) == 0 || stem.rfind("m_", 0) == 0) continue;
                auto g = cgr.find(stem);
                if (g == cgr.end()) continue;
                IndexCells("RPG2D/" + stem, Unpack(rom + rg.Start + g->second->Start, g->second->Size),
                           Unpack(rom + rc.Start + pe.Start, pe.Size));
            }
        }
    }

    if (getenv("WC09_DEBUG"))
    {
        size_t common = 0; for (auto& [h, v] : Tiles) common += v.size() >= kMaxPlaces;
        printf("WC09 sprite index: %zu images, %zu tiles (%zu very common)\n", Images.size(), Tiles.size(), common);
        fflush(stdout);
    }
}

SpriteHD IdentifySprite(int width, int height, bool bpp8, u32 tileOffset,
                        const std::function<void(u32, u32, u8*)>& readVRAM)
{
    SpriteHD out;
    u32 tb = bpp8 ? 64 : 32;
    u32 tw = width / 8, n = tw * (height / 8);
    if (!n || n > 64) return out;
    static u8 data[64 * 64];
    readVRAM(tileOffset, n * tb, data);

    std::lock_guard<std::mutex> lk(Lock);
    if (width == 32 && height == 32 && bpp8)
    {
        auto t = Thumbs.find(XXH64(data, 1024, 0));
        if (t != Thumbs.end() && t->second > 0)
        {
            // the whole HD card over the little card's rectangle
            out.Image = kCardThumbBase + t->second;
            out.OX = -6; out.OY = -3; out.OW = 32; out.OH = 32; out.IW = 20; out.IH = 26;
            return out;
        }
    }
    if (width == 16 && height == 16 && bpp8)
    {
        // a quarter of a card thumbnail (some screens build the 32x32 picture from 16x16 sprites)
        int card = -1, base = -1; bool ok = true;
        for (u32 t = 0; t < 4 && ok; t++)
        {
            auto it = ThumbTiles.find(XXH64(data + t * 64, 64, 64));
            if (it == ThumbTiles.end() || it->second < 0) { ok = false; break; }
            int c = it->second / 16, ti = it->second % 16;
            int want = base < 0 ? ti : base + (t & 1) + (t >> 1) * 4;
            if (t == 0) { card = c; base = ti; }
            else if (c != card || ti != want) ok = false;
        }
        if (ok && card > 0)
        {
            out.Image = kCardThumbBase + card;
            out.OX = -6 + (base % 4) * 8; out.OY = -3 + (base / 4) * 8; out.OW = 16; out.OH = 16; out.IW = 20; out.IH = 26;
            return out;
        }
    }
    if (Tiles.empty()) return out;
    u64 key = XXH64(data, n * tb, ((u64)width << 40) | ((u64)height << 32) | tb);
    auto c = Cache.find(key);
    if (c != Cache.end()) return c->second;

    // candidates = image placements consistent with every tile seen so far
    struct Cand { int Image; s16 OX, OY, OW, OH, DX, DY; bool HF, VF; };
    std::vector<Cand> cands;
    bool started = false;
    for (u32 t = 0; t < n; t++)
    {
        const u8* tile = data + t * tb;
        if (Blank(tile, tb)) continue;
        auto it = Tiles.find(XXH64(tile, tb, tb));
        if (it == Tiles.end() || it->second.size() >= kMaxPlaces) continue;
        std::vector<Cand> here;
        for (const TileEntry& e : it->second)
            here.push_back({e.Image, e.OX, e.OY, e.OW, e.OH,
                            (s16)(((int)e.I - (int)(t % tw)) * 8), (s16)(((int)e.J - (int)(t / tw)) * 8), e.HF, e.VF});
        if (!started) { cands = here; started = true; continue; }
        std::vector<Cand> keep;
        for (const Cand& a : cands)
            for (const Cand& b : here)
                if (a.Image == b.Image && a.OX == b.OX && a.OY == b.OY && a.DX == b.DX && a.DY == b.DY &&
                    a.OW == b.OW && a.HF == b.HF && a.VF == b.VF) { keep.push_back(a); break; }
        if (!keep.empty()) cands.swap(keep);
    }
    // the chosen placement must explain most of the sprite: the game builds some sprites at
    // runtime (e.g. the player's avatar = face + hair blended), where only a few tiles match
    // a stored image and the rest would come out wrong
    if (!cands.empty())
    {
        const Cand& a = cands[0];
        int nonBlank = 0, matched = 0;
        for (u32 t = 0; t < n; t++)
        {
            const u8* tile = data + t * tb;
            if (Blank(tile, tb)) continue;
            nonBlank++;
            auto it = Tiles.find(XXH64(tile, tb, tb));
            if (it == Tiles.end()) continue;
            for (const TileEntry& e : it->second)
                if (e.Image == a.Image && e.OX == a.OX && e.OY == a.OY &&
                    ((int)e.I - (int)(t % tw)) * 8 == a.DX && ((int)e.J - (int)(t / tw)) * 8 == a.DY) { matched++; break; }
        }
        if (nonBlank == 0 || matched * 10 < nonBlank * 8) cands.clear();
    }
    if (!cands.empty())
    {
        const Cand& a = cands[0];
        const ImageInfo& im = Images[a.Image - kSpriteImageBase];
        out.Image = a.Image; out.OX = a.OX; out.OY = a.OY; out.OW = a.OW; out.OH = a.OH;
        out.DX = a.DX; out.DY = a.DY; out.HF = a.HF; out.VF = a.VF; out.IW = im.W; out.IH = im.H;
        out.NoMask = im.Name.rfind("janken/coin_cursor", 0) == 0;
    }
    static const bool spridDbg = getenv("WC09_SPRID") != nullptr;
    if (spridDbg)
    {
        int nonBlank = 0, known = 0;
        for (u32 t = 0; t < n; t++) { const u8* tile = data + t * tb; if (Blank(tile, tb)) continue; nonBlank++; if (Tiles.count(XXH64(tile, tb, tb))) known++; }
        printf("sprid %dx%d bpp%d tiles %d/%d -> %s +%d,%d (oam %d,%d %dx%d)\n", width, height, bpp8 ? 8 : 4, known, nonBlank,
               out.Image >= 0 ? Images[out.Image - kSpriteImageBase].Name.c_str() : "none", out.DX, out.DY, out.OX, out.OY, out.OW, out.OH);
        fflush(stdout);
    }
    if (Cache.size() > 8192) Cache.clear();
    Cache[key] = out;
    return out;
}

int CardFromThumbTexture(u64 hash)
{
    std::lock_guard<std::mutex> lk(Lock);
    auto it = ThumbTex.find(hash);
    return it == ThumbTex.end() ? -1 : it->second;
}

std::vector<std::string> SpriteImageNamesWithPrefix(const std::string& prefix)
{
    std::lock_guard<std::mutex> lk(Lock);
    std::vector<std::string> out;
    for (const ImageInfo& im : Images)
        if (im.Name.compare(0, prefix.size(), prefix) == 0) out.push_back(im.Name);
    return out;
}

std::string SpriteImageName(int image)
{
    std::lock_guard<std::mutex> lk(Lock);
    int i = image - kSpriteImageBase;
    return (i >= 0 && i < (int)Images.size()) ? Images[i].Name : std::string();
}

}

namespace melonDS::WC09
{
bool IsTextSprite(int width, int height, bool bpp8, const u8* tiles, const u16* pal)
{
    u32 n = (u32)(width / 8) * (height / 8) * (bpp8 ? 64 : 32);
    u32 used[16] = {};   // per 16-colour group, bit k = index k used
    u32 count = 0;
    for (u32 i = 0; i < n; i++)
    {
        u8 v = tiles[i];
        for (int k = 0; k < (bpp8 ? 1 : 2); k++)
        {
            u32 idx = bpp8 ? v : ((v >> (4 * k)) & 0xF);
            if (!idx) continue;
            used[bpp8 ? idx >> 4 : 0] |= 1u << (idx & 15);
            count++;
        }
    }
    if (count < 12) return false;
    int groups = 0;
    for (int g = 0; g < 16; g++)
    {
        if (!used[g]) continue;
        if (++groups > 2) return false;
        if (__builtin_popcount(used[g]) < 4) return false;
        // brightness must change monotonically with the index over the used entries
        int prev = -1, dir = 0;
        for (int k = 1; k < 16; k++)
        {
            if (!(used[g] & (1u << k))) continue;
            u16 c = pal[(bpp8 ? g * 16 : 0) + k];
            int l = (c & 31) * 2 + ((c >> 5) & 31) * 4 + ((c >> 10) & 31);   // rough luma
            if (prev >= 0)
            {
                int d = l > prev + 3 ? 1 : l < prev - 3 ? -1 : 0;
                if (d && dir && d != dir) return false;
                if (d) dir = d;
            }
            prev = l;
        }
        if (!dir) return false;
    }
    return groups > 0;
}
}
