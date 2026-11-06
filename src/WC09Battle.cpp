#include "WC09Battle.h"
#include "WC09Rom.h"
#include "WC09Sprites.h"
#include "WC09Cards.h"
#include "GPU.h"
#include "NDS.h"
#include "NDSCart.h"

#include <cstring>
#include <atomic>
#include <cstdio>
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

// ---- battle image index ------------------------------------------------------------------

struct Image
{
    std::string Name;          // e.g. "7736_r"
    int CharSet;
    u16 TW, TH;
    std::vector<u16> Map;
};

struct TilePos { int Image; u8 X, Y; bool HF; };
struct CharSet
{
    std::vector<int> Images;
    std::vector<std::vector<TilePos>> Pos;    // tile number -> where it's used (capped)
};

struct TileRef { int CharSet; u16 Tile; };

std::vector<Image> Images;
std::vector<CharSet> CharSets;
std::unordered_map<u64, TileRef> TileIndex;    // only tiles unique to one char set
bool Indexed = false;
const u8* IndexedROM = nullptr;

std::vector<std::string> CardNames, CardDescs;

void LoadCardText(const u8* rom, u32 len)
{
    CardNames.clear(); CardDescs.clear();
    FileRef ref;
    if (!FindFile(rom, len, "Data_arc_pac/bin2.pac", ref)) return;
    const u8* pac = rom + ref.Start;
    const PacEntry *indx = nullptr, *names = nullptr, *descs = nullptr;
    auto ents = ParsePac(pac, ref.Size);
    for (const PacEntry& e : ents)
    {
        if (e.Name == "card_indx_e.bin") indx = &e;
        else if (e.Name == "card_name_e.bin") names = &e;
        else if (e.Name == "card_desc_e.bin") descs = &e;
    }
    if (!indx || !names || !descs) return;
    auto str = [&](const PacEntry* e, u32 off) {
        std::string out;
        for (u32 i = off; i < e->Size && pac[e->Start + i]; i++) out += (char)pac[e->Start + i];
        // the game's text is Latin-1; convert to UTF-8 for Qt
        std::string utf8;
        for (unsigned char c : out)
        {
            if (c == 0x9D) { utf8 += "\xE2\x97\x8F"; continue; }   // the game's bullet -> U+25CF
            if (c == 0x8F) { utf8 += "\xE2\x98\x85"; continue; }   // star
            if (c == '\r') continue;
            if (c < 0x80) utf8 += (char)c;
            else { utf8 += (char)(0xC0 | (c >> 6)); utf8 += (char)(0x80 | (c & 0x3F)); }
        }
        return utf8;
    };
    for (u32 i = 0; (i + 1) * 8 <= indx->Size; i++)
    {
        u32 n = RD32(pac + indx->Start + i * 8), d = RD32(pac + indx->Start + i * 8 + 4);
        CardNames.push_back(n < names->Size ? str(names, n) : "");
        CardDescs.push_back(d < descs->Size ? str(descs, d) : "");
    }
}

std::mutex Lock;
BGReplacement Current[2][4];
bool OnScreen[2];
HDImageProvider Provider;

void BuildIndex(const u8* rom, u32 len)
{
    Images.clear(); CharSets.clear(); TileIndex.clear();
    std::unordered_map<u64, int> charsetByHash;
    std::unordered_map<u64, int> tileOwner;         // hash -> charset, -1 = ambiguous
    std::unordered_map<u64, u16> tileNum;

    // every 256-color background image set we know how to replace: battle images, the duel UI,
    // menus and screens. Name = the .pac's file name without extension.
    static const char* const kSets[] = {
        "duel2d/BattleMonsterR1", "duel2d/BattleMonsterL1", "duel2d/BattleMonsterR2", "duel2d/BattleMonsterL2",
        "duel2d/field", "duel2d", "CharaMake", "Closet", "DCustom", "D_race_result", "DataRoom", "Dsimulator",
        "FreeDuel", "FriendMenu", "Getpack", "MainMenu", "Name", "Password", "Record", "Structure",
        "TagSetting", "WiFi", "WiFiDownload", "WiFiRanking", "WordSelect", "janken", "logo", "trunk",
        "Common", "cpuduel" };
    for (const char* path : kSets)
    {
        FileRef ref;
        if (!FindFile(rom, len, std::string("Data_arc_pac/") + path + ".pac", ref)) continue;
        std::string sp(path);
        std::string f = sp.substr(sp.rfind('/') == std::string::npos ? 0 : sp.rfind('/') + 1);
        for (const PacEntry& e : ParsePac(rom + ref.Start, ref.Size))
        {
            std::vector<u8> dec;
            const u8* src = rom + ref.Start + e.Start;
            if (e.Size >= 4 && src[0] == 0x10) { if (!LZ10(src, e.Size, dec)) continue; }
            else dec.assign(src, src + e.Size);
            if (dec.size() < 16 || memcmp(dec.data(), "NTBG", 4) != 0) continue;
            u32 p = 16;
            while (p + 8 <= dec.size())
            {
                u32 cs = RD32(&dec[p + 4]);
                if (cs < 8 || p + cs > dec.size()) break;
                if (memcmp(&dec[p], "BGDT", 4) == 0 && cs >= 28)
                {
                    const u8* body = &dec[p + 8];
                    if (body[0] != 1) break;          // 8bpp images only
                    u32 mapsz = RD32(body + 4); u16 tw = RD16(body + 8), th = RD16(body + 10);
                    u32 chrsz = RD32(body + 16);
                    if (20 + mapsz + chrsz > cs - 8 || mapsz < (u32)tw * th * 2) break;
                    const u8* mp = body + 20; const u8* ch = body + 20 + mapsz;
                    u64 chash = XXH64(ch, chrsz, 0);
                    int csid;
                    auto it = charsetByHash.find(chash);
                    if (it == charsetByHash.end())
                    {
                        csid = (int)CharSets.size(); CharSets.emplace_back(); charsetByHash[chash] = csid;
                        u32 ntiles = chrsz / 64;
                        for (u32 k = 1; k < ntiles; k++)
                        {
                            u64 th64 = XXH64(ch + k * 64, 64, 0);
                            auto o = tileOwner.find(th64);
                            if (o == tileOwner.end()) { tileOwner[th64] = csid; tileNum[th64] = (u16)k; }
                            else if (o->second != csid) o->second = -1;
                        }
                    }
                    else csid = it->second;

                    Image img;
                    img.Name = f + "/" + e.Name.substr(0, e.Name.find('.'));
                    img.CharSet = csid; img.TW = tw; img.TH = th;
                    img.Map.resize((size_t)tw * th);
                    for (size_t k = 0; k < img.Map.size(); k++) img.Map[k] = RD16(mp + k * 2);
                    CharSets[csid].Images.push_back((int)Images.size());
                    Images.push_back(std::move(img));
                    break;
                }
                p += cs;
            }
        }
    }
    for (auto& [h, cs] : tileOwner)
        if (cs >= 0) TileIndex[h] = {cs, tileNum[h]};
    for (CharSet& cs : CharSets)
    {
        cs.Pos.assign(1024, {});
        for (int imgId : cs.Images)
        {
            const Image& img = Images[imgId];
            for (int py = 0; py < img.TH; py++)
                for (int px = 0; px < img.TW; px++)
                {
                    u16 me = img.Map[py * img.TW + px];
                    auto& v = cs.Pos[me & 0x3FF];
                    if (v.size() < 6) v.push_back({imgId, (u8)px, (u8)py, (bool)((me >> 10) & 1)});
                }
        }
    }
    Indexed = true;
    if (getenv("WC09_DEBUG"))
    {
        printf("WC09 battle index: %zu images, %zu char sets, %zu unique tiles\n", Images.size(), CharSets.size(), TileIndex.size());
        fflush(stdout);
    }
}

u16 ReadBG(GPU& gpu, int engine, u32 addr)
{
    return engine == 0 ? gpu.ReadVRAM_ABG<u16>(addr) : gpu.ReadVRAM_BBG<u16>(addr);
}

}

void SetHDImageProvider(HDImageProvider provider) { std::lock_guard<std::mutex> lk(Lock); Provider = std::move(provider); }

bool LoadHDImage(const std::string& name, std::vector<u8>& rgba, int& w, int& h)
{
    HDImageProvider p;
    { std::lock_guard<std::mutex> lk(Lock); p = Provider; }
    return p && p(name, rgba, w, h);
}

std::atomic<u64> PanelStamp{0};
std::atomic<u64> VBlankCount{0};
void NotePanelRendered() { PanelStamp = VBlankCount.load(); }
u64 FrameNum() { return VBlankCount.load(); }
bool PanelRenderedRecently() { return VBlankCount.load() - PanelStamp.load() < 3; }

std::string BattleImageName(int image)
{
    if (image >= kSpriteImageBase) return SpriteImageName(image);
    if (image >= kTextPanelBase)
        return "textpanel/" + std::to_string((image - kTextPanelBase) / 2) + (((image - kTextPanelBase) & 1) ? "s" : "");
    if (image < 0 || image >= (int)Images.size()) return {};
    return Images[image].Name;
}

std::string CardNameEn(int card) { return card >= 0 && card < (int)CardNames.size() ? CardNames[card] : ""; }
std::string CardDescEn(int card) { return card >= 0 && card < (int)CardDescs.size() ? CardDescs[card] : ""; }

bool BattleOnScreen(int screen)
{
    std::lock_guard<std::mutex> lk(Lock);
    return OnScreen[screen & 1];
}

BGReplacement GetBGReplacement(int engine, int bg)
{
    std::lock_guard<std::mutex> lk(Lock);
    return Current[engine & 1][bg & 3];
}

void OnVBlankBattle(GPU& gpu)
{
    VBlankCount++;
    BGReplacement found[2][4];
    if (IsActive() && GetHDCardsEnabled())
    {
        auto* cart = gpu.NDS.NDSCartSlot.GetCart();
        if (cart && cart->GetROM() != IndexedROM)
        {
            BuildIndex(cart->GetROM(), cart->GetROMLength());
            BuildSpriteIndex(cart->GetROM(), cart->GetROMLength());
            LoadCardText(cart->GetROM(), cart->GetROMLength());
            IndexedROM = cart->GetROM();
        }

        for (int engine = 0; engine < 2 && Indexed; engine++)
        {
            const GPU2D& u = engine == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
            u32 dispcnt = u.DispCnt;
            if (((dispcnt >> 16) & 3) != 1) continue;
            u32 bgmode = dispcnt & 7;
            for (int bg = 0; bg < 4; bg++)
            {
                if (!(dispcnt & (1 << (8 + bg)))) continue;
                if (engine == 0 && bg == 0 && (dispcnt & (1 << 3))) continue;
                bool text = bg < 2 || (bg == 2 && bgmode <= 1) || (bg == 3 && bgmode == 0);
                if (!text) continue;
                u16 cnt = u.BGCnt[bg];
                if (!(cnt & (1 << 7))) continue;

                u32 charBase = ((cnt >> 2) & 0xF) * 0x4000;
                u32 scrBase = ((cnt >> 8) & 0x1F) * 0x800;
                if (engine == 0) { charBase += ((dispcnt >> 24) & 7) * 0x10000; scrBase += ((dispcnt >> 27) & 7) * 0x10000; }
                int size = cnt >> 14;
                int mapW = (size & 1) ? 64 : 32, mapH = (size & 2) ? 64 : 32;
                auto entry = [&](int x, int y) -> u16 {
                    x &= mapW - 1; y &= mapH - 1;
                    u32 block = (x >> 5) + ((y >> 5) * (mapW >> 5));
                    return ReadBG(gpu, engine, scrBase + block * 0x800 + (((y & 31) * 32 + (x & 31)) * 2));
                };

                int sx0 = (u.BGXPos[bg] & 0x1FF) >> 3, sy0 = (u.BGYPos[bg] & 0x1FF) >> 3;
                // votes: key = (image, originX, originY, mirror)
                std::map<std::tuple<int, int, int, bool>, int> votes;
                static u64 hashOf[1024]; static bool hashed[1024];
                memset(hashed, 0, sizeof(hashed));
                for (int cy = 0; cy < 25; cy++)
                {
                    for (int cx = 0; cx < 33; cx++)
                    {
                        int mx = sx0 + cx, my = sy0 + cy;
                        u16 e = entry(mx, my);
                        u16 t = e & 0x3FF;
                        if (t == 0) continue;
                        if (!hashed[t])
                        {
                            u8 tile[64];
                            for (int i = 0; i < 64; i += 2) *(u16*)&tile[i] = ReadBG(gpu, engine, charBase + t * 64 + i);
                            hashOf[t] = XXH64(tile, 64, 0); hashed[t] = true;
                        }
                        auto hit = TileIndex.find(hashOf[t]);
                        if (hit == TileIndex.end()) continue;
                        const TileRef& r = hit->second;
                        int tileBase = (int)t - (int)r.Tile;
                        bool ehf = (e >> 10) & 1;
                        for (const TilePos& tp : CharSets[r.CharSet].Pos[r.Tile])
                        {
                            const Image& img = Images[tp.Image];
                            int px = tp.X, py = tp.Y;
                            // the game may mirror the whole image with tile flips
                            bool mirror = tp.HF != ehf;
                            int ox = mirror ? mx - (img.TW - 1 - px) : mx - px;
                            int oy = my - py;
                            // confirm with a second cell
                            int qx = (px + img.TW / 2) % img.TW, qy = (py + img.TH / 2) % img.TH;
                            u16 qm = img.Map[qy * img.TW + qx];
                            u16 qv = entry(mirror ? ox + (img.TW - 1 - qx) : ox + qx, oy + qy);
                            if ((qm & 0x3FF) == 0) { if ((qv & 0x3FF) != 0 && (qv & 0x3FF) != (u16)tileBase) continue; }
                            else if ((qv & 0x3FF) != (u16)((qm & 0x3FF) + tileBase)) continue;
                            if (++votes[{tp.Image, ox, oy, mirror}] >= 6) goto voted;
                        }
                    }
                }
            voted:
                int best = 0;
                for (auto& [k, v] : votes)
                {
                    if (v <= best) continue;
                    best = v;
                    auto [imgId, ox, oy, mirror] = k;
                    BGReplacement& rep = found[engine][bg];
                    rep.Image = imgId;
                    rep.OriginX = (float)(((ox % mapW) + mapW) % mapW) * 8.f;
                    rep.OriginY = (float)(((oy % mapH) + mapH) % mapH) * 8.f;
                    rep.Width = Images[imgId].TW * 8.f; rep.Height = Images[imgId].TH * 8.f;
                    rep.Mirror = mirror;
                }
                if (best < 2) found[engine][bg] = BGReplacement{};
            }
        }
    }
    // duel card detail: replace the description text background with a generated HD panel
    if (IsActive() && GetHDTextEnabled())
    {
        FrameCards fc = GetLatest();
        for (const CardQuad& q : fc.Quads)
        {
            if (q.TexSize != 1 || q.Screen != 0 || q.Engine < 0) continue;
            if (q.X[0] < 4 || q.X[0] > 12 || q.Y[0] < 16 || q.Y[0] > 28) continue;
            int textBG = 2;
            if (q.BG == textBG) break;
            BGReplacement& rep = found[q.Engine][textBG];
            if (rep.Image >= 0) break;

            // speech bubbles sit on another background layer, under the text layer: look for
            // their solid fill tiles below y = 124 (right of the card) and keep clear of them
            const GPU2D& u = q.Engine == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
            int solid = 0;
            for (int bg = 0; bg < 4; bg++)
            {
                if (bg == textBG || bg == q.BG || !(u.DispCnt & (1 << (8 + bg)))) continue;
                if (q.Engine == 0 && bg == 0 && (u.DispCnt & (1 << 3))) continue;
                u16 cnt = u.BGCnt[bg];
                u32 charBase = ((cnt >> 2) & 0xF) * 0x4000, scrBase = ((cnt >> 8) & 0x1F) * 0x800;
                if (q.Engine == 0) { charBase += ((u.DispCnt >> 24) & 7) * 0x10000; scrBase += ((u.DispCnt >> 27) & 7) * 0x10000; }
                bool bpp8 = cnt & (1 << 7);
                int mapW = (cnt >> 14) & 1 ? 64 : 32, mapH = (cnt >> 14) & 2 ? 64 : 32;
                int sx0 = (u.BGXPos[bg] & 0x1FF), sy0 = (u.BGYPos[bg] & 0x1FF);
                for (int y = 128; y < 168; y += 8)
                    for (int x = 104; x < 248; x += 8)
                    {
                        int mx = ((x + sx0) >> 3) & (mapW - 1), my = ((y + sy0) >> 3) & (mapH - 1);
                        u32 block = (mx >> 5) + ((my >> 5) * (mapW >> 5));
                        u16 e = ReadBG(gpu, q.Engine, scrBase + block * 0x800 + (((my & 31) * 32 + (mx & 31)) * 2));
                        u32 t = e & 0x3FF;
                        if (!t) continue;
                        u32 bytes = bpp8 ? 64 : 32;
                        u16 first = ReadBG(gpu, q.Engine, charBase + t * bytes);
                        if ((first & 0xFF) == 0 || (first & 0xFF) != (first >> 8)) continue;
                        bool same = true;
                        for (u32 i = 2; i < bytes && same; i += 2) same = ReadBG(gpu, q.Engine, charBase + t * bytes + i) == first;
                        if (same) solid++;
                    }
            }
            bool bubble = solid >= 4;

            rep.Image = kTextPanelBase + q.Card * 2 + (bubble ? 1 : 0);
            rep.ScreenSpace = true;
            rep.OriginX = kPanelX; rep.OriginY = kPanelY; rep.Width = kPanelW;
            rep.Height = bubble ? kPanelShortH : kPanelH;
            break;
        }
    }

    bool onScreen[2] = {false, false};
    for (int e = 0; e < 2; e++)
        for (int b = 0; b < 4; b++)
            if (found[e][b].Image >= 0 && found[e][b].Image < kTextPanelBase) onScreen[(e == 0) == gpu.ScreenSwap ? 0 : 1] = true;

    std::lock_guard<std::mutex> lk(Lock);
    OnScreen[0] = onScreen[0]; OnScreen[1] = onScreen[1];
    static const bool debug = getenv("WC09_DEBUG") != nullptr;
    for (int e = 0; e < 2; e++) for (int b = 0; b < 4; b++)
    {
        if (debug && found[e][b].Image != Current[e][b].Image)
            printf("WC09 battle: engine %d bg %d -> %s at %.0f,%.0f%s\n", e, b,
                   found[e][b].Image >= 0 ? BattleImageName(found[e][b].Image).c_str() : "none",
                   found[e][b].OriginX, found[e][b].OriginY, found[e][b].Mirror ? " (mirrored)" : "");
        if (debug) fflush(stdout);
        Current[e][b] = found[e][b];
    }
}

}
