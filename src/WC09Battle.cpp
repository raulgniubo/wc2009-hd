#include "WC09Battle.h"
#include "WC09Rom.h"
#include "WC09Sprites.h"
#include "WC09Cards.h"
#include "GPU.h"
#include "NDS.h"
#include "NDSCart.h"

#include <algorithm>
#include <cstring>
#include <atomic>
#include <thread>
#include <memory>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <tuple>
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
// The ROM index (backgrounds, sprites, card text, glyphs, card fingerprints) is built on a
// background thread when a ROM shows up; until it is ready the game just shows its own pixels.
std::atomic<bool> IndexReady{false};
struct IndexerThread { std::thread T; ~IndexerThread() { if (T.joinable()) T.join(); } } Indexer;

std::vector<std::string> CardNames, CardDescs;
std::vector<int> CardAtk, CardDef, CardLevel;   // printed values

void LoadCardText(const u8* rom, u32 len)
{
    CardNames.clear(); CardDescs.clear(); CardAtk.clear(); CardDef.clear(); CardLevel.clear();
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
        else if (e.Name == "card_prop.bin")
            for (u32 i = 0; (i + 1) * 8 <= e.Size; i++)
            {
                u32 p0 = RD32(pac + e.Start + i * 8);
                u32 p1 = RD32(pac + e.Start + i * 8 + 4);
                CardAtk.push_back(((p0 >> 14) & 0x1FF) * 10); CardDef.push_back(((p0 >> 23) & 0x1FF) * 10);
                CardLevel.push_back((p1 >> 10) & 0xF);
            }
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
    // one image: its tiles (4bpp or 8bpp), its map
    auto addImage = [&](const std::string& name, bool b8, const u8* ch, u32 chrsz, const u8* mp, u16 tw, u16 th)
    {
        const u32 tb = b8 ? 64 : 32;
        const u64 seed = b8 ? 0 : 4;      // keep 4bpp and 8bpp tile hashes apart
        u64 chash = XXH64(ch, chrsz, seed);
        int csid;
        auto it = charsetByHash.find(chash);
        if (it == charsetByHash.end())
        {
            csid = (int)CharSets.size(); CharSets.emplace_back(); charsetByHash[chash] = csid;
            u32 ntiles = chrsz / tb;
            for (u32 k = 1; k < ntiles; k++)
            {
                u64 th64 = XXH64(ch + k * tb, tb, seed);
                auto o = tileOwner.find(th64);
                if (o == tileOwner.end()) { tileOwner[th64] = csid; tileNum[th64] = (u16)k; }
                else if (o->second != csid) o->second = -1;
            }
        }
        else csid = it->second;

        Image img;
        img.Name = name;
        img.CharSet = csid; img.TW = tw; img.TH = th;
        img.Map.resize((size_t)tw * th);
        for (size_t k = 0; k < img.Map.size(); k++) img.Map[k] = RD16(mp + k * 2);
        CharSets[csid].Images.push_back((int)Images.size());
        Images.push_back(std::move(img));
    };

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
                    if (body[0] > 1) break;           // 0 = 4bpp, 1 = 8bpp
                    u32 mapsz = RD32(body + 4); u16 tw = RD16(body + 8), th = RD16(body + 10);
                    u32 chrsz = RD32(body + 16);
                    if (20 + mapsz + chrsz > cs - 8 || mapsz < (u32)tw * th * 2) break;
                    addImage(f + "/" + e.Name.substr(0, e.Name.find('.')), body[0] == 1,
                             body + 20 + mapsz, chrsz, body + 20, tw, th);
                    break;
                }
                p += cs;
            }
        }
    }

    // Nitro backgrounds (NCGR tiles + NSCR map with the same name): the overworld map screens
    for (const char* path : {"RPG2D_2", "RACE"})
    {
        FileRef ref;
        if (!FindFile(rom, len, std::string("Data_arc_pac/") + path + ".pac", ref)) continue;
        std::map<std::string, std::vector<u8>> cgr, scr;
        for (const PacEntry& e : ParsePac(rom + ref.Start, ref.Size))
        {
            bool isC = e.Name.find(".lzncgr") != std::string::npos, isS = e.Name.find(".lznscr") != std::string::npos;
            if (!isC && !isS) continue;
            std::vector<u8> dec;
            const u8* src = rom + ref.Start + e.Start;
            if (e.Size >= 4 && src[0] == 0x10) { if (!LZ10(src, e.Size, dec)) continue; }
            else dec.assign(src, src + e.Size);
            (isC ? cgr : scr)[e.Name.substr(0, e.Name.find('.'))] = std::move(dec);
        }
        for (auto& [stem, s] : scr)
        {
            auto c = cgr.find(stem);
            if (c == cgr.end()) continue;
            const std::vector<u8>& g = c->second;
            if (s.size() < 0x24 || g.size() < 0x30 || memcmp(s.data(), "RCSN", 4) || memcmp(g.data(), "RGCN", 4)) continue;
            // NCGR: RAHC block at 0x10; NSCR: NRCS block at 0x10
            const u8* gb = g.data() + 0x18; const u8* sb = s.data() + 0x18;
            bool b8 = RD32(gb + 4) == 4;
            u32 gsize = RD32(gb + 16), goff = RD32(gb + 20);
            if (0x18 + goff + gsize > g.size()) continue;
            u16 wpx = RD16(sb + 0), hpx = RD16(sb + 2); u32 msize = RD32(sb + 8);
            u16 tw = wpx / 8, th = hpx / 8;
            if (!tw || !th || msize < (u32)tw * th * 2 || 0x18 + 12 + msize > s.size()) continue;
            addImage(std::string(path) + "/" + stem, b8, g.data() + 0x18 + goff, gsize, sb + 12, tw, th);
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

// Menu screens show many small HD sprite images, first needed the moment the player presses a
// button; decoding them then (on the emulation thread) makes the sound crackle. The first image
// of a menu folder starts a background load of the whole folder.
namespace
{
struct PrefetchedImage { std::vector<u8> Rgba; int W = 0, H = 0; };
std::mutex PrefLock;
std::unordered_map<std::string, PrefetchedImage> Prefetched;
std::vector<std::string> PrefQueue;
std::vector<std::string> AsyncQueue;       // rendered panels asked for right now: first
std::map<std::string, bool> AsyncPending;
std::map<std::string, bool> PrefFolders;
std::condition_variable PrefCV;
bool PrefStop = false;
struct PrefetchThread
{
    std::thread T;
    ~PrefetchThread() { { std::lock_guard<std::mutex> lk(PrefLock); PrefStop = true; } PrefCV.notify_all(); if (T.joinable()) T.join(); }
} Prefetcher;

void PrefetchLoop()
{
    for (;;)
    {
        std::string name;
        {
            std::unique_lock<std::mutex> lk(PrefLock);
            PrefCV.wait(lk, [] { return PrefStop || !PrefQueue.empty() || !AsyncQueue.empty(); });
            if (PrefStop) return;
            if (!AsyncQueue.empty()) { name = AsyncQueue.front(); AsyncQueue.erase(AsyncQueue.begin()); }
            else { name = PrefQueue.back(); PrefQueue.pop_back(); }
            if (Prefetched.count(name)) continue;
        }
        HDImageProvider p;
        { std::lock_guard<std::mutex> lk(Lock); p = Provider; }
        PrefetchedImage im;
        bool ok;
        if (name.rfind("cardimg/", 0) == 0) ok = LoadCardImage(atoi(name.c_str() + 8), im.Rgba, im.W, im.H);
        else ok = p && p(name, im.Rgba, im.W, im.H);
        std::lock_guard<std::mutex> lk(PrefLock);
        AsyncPending.erase(name);
        if (ok) Prefetched[name] = std::move(im);
        else Prefetched[name] = PrefetchedImage{};   // failed: an empty entry says so
    }
}

void PrefetchFolder(const std::string& name)
{
    size_t slash = name.find('/');
    if (slash == std::string::npos) return;
    std::string folder = name.substr(0, slash + 1);
    static const char* const kMenus[] = { "trunk/", "MainMenu/", "DCustom/", "janken/", "duel2d/" };
    bool menu = false;
    for (const char* m : kMenus) if (folder == m) menu = true;
    if (!menu) return;
    {
        std::lock_guard<std::mutex> lk(PrefLock);
        if (PrefFolders[folder]) return;
        PrefFolders[folder] = true;
    }
    std::vector<std::string> names = SpriteImageNamesWithPrefix(folder);
    std::lock_guard<std::mutex> lk(PrefLock);
    for (auto& n : names) PrefQueue.push_back(n);
    if (!Prefetcher.T.joinable()) Prefetcher.T = std::thread(PrefetchLoop);
    PrefCV.notify_all();
}
}

int LoadHDImageAsync(const std::string& name, std::vector<u8>& rgba, int& w, int& h)
{
    std::lock_guard<std::mutex> lk(PrefLock);
    auto it = Prefetched.find(name);
    if (it != Prefetched.end())
    {
        bool ok = it->second.W > 0;
        rgba = std::move(it->second.Rgba); w = it->second.W; h = it->second.H;
        Prefetched.erase(it);
        return ok ? 1 : -1;
    }
    if (!AsyncPending[name])
    {
        AsyncPending[name] = true;
        AsyncQueue.push_back(name);
        if (AsyncQueue.size() > 8) { AsyncPending.erase(AsyncQueue.front()); AsyncQueue.erase(AsyncQueue.begin()); }
        if (!Prefetcher.T.joinable()) Prefetcher.T = std::thread(PrefetchLoop);
        PrefCV.notify_all();
    }
    return 0;
}

// card art: loaded on the worker thread, the last few kept decoded (lists show the same cards
// over and over)
int LoadCardImageAsync(int card, std::vector<u8>& rgba, int& w, int& h)
{
    static std::mutex cl;
    static std::vector<std::pair<int, std::shared_ptr<PrefetchedImage>>> cache;
    {
        std::lock_guard<std::mutex> lk(cl);
        for (size_t i = 0; i < cache.size(); i++)
            if (cache[i].first == card)
            {
                auto e = cache[i]; cache.erase(cache.begin() + i); cache.insert(cache.begin(), e);
                if (e.second->W <= 0) return -1;
                rgba = e.second->Rgba; w = e.second->W; h = e.second->H;
                return 1;
            }
    }
    auto im = std::make_shared<PrefetchedImage>();
    int r = LoadHDImageAsync("cardimg/" + std::to_string(card), im->Rgba, im->W, im->H);
    if (r == 0) return 0;
    if (r < 0) im->W = 0;
    std::lock_guard<std::mutex> lk(cl);
    cache.insert(cache.begin(), {card, im});
    if (cache.size() > 16) cache.pop_back();
    if (r < 0) return -1;
    rgba = im->Rgba; w = im->W; h = im->H;
    return 1;
}

bool LoadHDImage(const std::string& name, std::vector<u8>& rgba, int& w, int& h)
{
    {
        std::lock_guard<std::mutex> lk(PrefLock);
        auto it = Prefetched.find(name);
        if (it != Prefetched.end() && it->second.W > 0)
        {
            rgba = std::move(it->second.Rgba); w = it->second.W; h = it->second.H;
            Prefetched.erase(it);   // the renderer keeps its own texture from here on
            return true;
        }
    }
    HDImageProvider p;
    { std::lock_guard<std::mutex> lk(Lock); p = Provider; }
    static const bool perf = getenv("WC09_PERF") != nullptr;
    auto t0 = std::chrono::steady_clock::now();
    bool ok = p && p(name, rgba, w, h);
    if (perf) { printf("WC09 sync load %s %.1f ms\n", name.c_str(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()); fflush(stdout); }
    if (IndexReady) PrefetchFolder(name);
    return ok;
}

std::atomic<u64> PanelStamp{0};
std::atomic<u64> VBlankCount{0};
void NotePanelRendered() { PanelStamp = VBlankCount.load(); }
u64 FrameNum() { return VBlankCount.load(); }
bool PanelRenderedRecently() { return VBlankCount.load() - PanelStamp.load() < 3; }

namespace
{
struct CardSet { std::vector<CardSetItem> Items; int W, H; };
std::mutex CardSetLock;
std::map<u64, int> CardSetIds;
std::map<int, CardSet> CardSets;
int NextCardSet = kCardSetBase;
}

int RegisterCardSet(const std::vector<CardSetItem>& items, int w, int h)
{
    std::lock_guard<std::mutex> lk(CardSetLock);
    u64 key = XXH64(items.data(), items.size() * sizeof(CardSetItem), ((u64)w << 32) | (u32)h);
    auto it = CardSetIds.find(key);
    if (it != CardSetIds.end()) return it->second;
    if (CardSets.size() > 64) { CardSets.clear(); CardSetIds.clear(); }
    int id = NextCardSet++;
    CardSetIds[key] = id;
    CardSets[id] = {items, w, h};
    return id;
}

bool GetCardSet(int image, std::vector<CardSetItem>& items, int& w, int& h)
{
    std::lock_guard<std::mutex> lk(CardSetLock);
    auto it = CardSets.find(image);
    if (it == CardSets.end()) return false;
    items = it->second.Items; w = it->second.W; h = it->second.H;
    return true;
}

std::string BattleImageName(int image)
{
    if (IsCardSet(image)) return "cardset/" + std::to_string(image - kCardSetBase);
    if (IsCardImage(image)) return "card/" + std::to_string(image - kCardImageBase);
    if (image >= kStatPanelBase && image < kStatPanelBase + 400000) return "statpanel/" + std::to_string(image);
    if (image >= kLpPanelBase && image < kLpPanelBase + 400000) return "lppanel/" + std::to_string(image);
    if (image >= kDeckTextBase && image < kSpriteImageBase)
        return std::string(((image - kDeckTextBase) & 1) ? "deckname/" : "decktext/") + std::to_string((image - kDeckTextBase) / 2);
    if (image >= kSpriteImageBase) return SpriteImageName(image);
    if (image >= kTextPanelBase)
        return "textpanel/" + std::to_string((image - kTextPanelBase) / 2) + (((image - kTextPanelBase) & 1) ? "s" : "");
    if (image < 0 || image >= (int)Images.size()) return {};
    return Images[image].Name;
}

bool IsIndexReady() { return IndexReady.load(); }

std::string CardNameEn(int card) { if (!IndexReady) return "";
    return card >= 0 && card < (int)CardNames.size() ? CardNames[card] : ""; }
std::string CardDescEn(int card) { if (!IndexReady) return "";
    return card >= 0 && card < (int)CardDescs.size() ? CardDescs[card] : ""; }

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

namespace
{
// The duel card view prints ATK/DEF with the game's pixel font on the text layer, over the card.
// Glyph shapes are learned from cards whose shown values equal the printed ones, then any
// value can be read back and drawn in HD.
std::unordered_map<u64, int> GlyphDigit;   // glyph hash -> digit, -2 = conflicting
struct StatPanel { int Card, Atk, Def; bool Bubble; int Level, BX, BY; };
std::map<int, StatPanel> StatPanels;
std::map<std::tuple<int, int, int, bool, int, int, int>, int> StatPanelIds;
int NextStatPanel = kStatPanelBase;

// Reads the two numbers at screen rows y0..y0+h, x0..x1 of a 4-bit text layer.
bool ReadStats(GPU& gpu, int engine, int bg, int card, int x0, int x1, int y0, int h, int& atk, int& def)
{
    const GPU2D& u = engine == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
    u16 cnt = u.BGCnt[bg];
    if (cnt & 0x80) return false;
    u32 charBase = ((cnt >> 2) & 0xF) * 0x4000, scrBase = ((cnt >> 8) & 0x1F) * 0x800;
    if (engine == 0) { charBase += ((u.DispCnt >> 24) & 7) * 0x10000; scrBase += ((u.DispCnt >> 27) & 7) * 0x10000; }
    int mapW = (cnt >> 14) & 1 ? 64 : 32, mapH = (cnt >> 14) & 2 ? 64 : 32;
    int sx0 = u.BGXPos[bg] & 0x1FF, sy0 = u.BGYPos[bg] & 0x1FF;
    std::vector<u32> cols;   // per screen column: bit mask of set pixels over the rows
    for (int x = x0; x < x1; x++)
    {
        u32 m = 0;
        for (int y = 0; y < h; y++)
        {
            int px = (x + sx0) & (mapW * 8 - 1), py = (y0 + y + sy0) & (mapH * 8 - 1);
            u32 block = (px >> 8) + ((py >> 8) * (mapW >> 5));
            u16 e = ReadBG(gpu, engine, scrBase + block * 0x800 + ((((py >> 3) & 31) * 32 + ((px >> 3) & 31)) * 2));
            int tx = px & 7, ty = py & 7;
            if (e & 0x400) tx = 7 - tx;
            if (e & 0x800) ty = 7 - ty;
            u16 two = ReadBG(gpu, engine, charBase + (e & 0x3FF) * 32 + ty * 4 + (tx >> 1 & ~1));
            u8 b = (u8)(two >> ((tx & 2) ? 8 : 0));
            if ((tx & 1) ? (b >> 4) : (b & 15)) m |= 1u << y;
        }
        cols.push_back(m);
    }
    // numbers = runs of columns split by gaps of 5+ empty columns. The outlined digits touch
    // each other, so each number is cut into 8-column cells from its right end (the font's pitch)
    std::vector<std::vector<u64>> nums;
    for (int i = 0; i < (int)cols.size();)
    {
        if (!cols[i]) { i++; continue; }
        int j = i, last = i;
        while (j < (int)cols.size() && j - last < 5) { if (cols[j]) last = j; j++; }
        int width = last - i + 1, n = (width + 4) / 8;
        if (n < 1 || n > 5) return false;
        std::vector<u64> digits(n);
        for (int k = 0; k < n; k++)
        {
            u32 cell[8];
            for (int c = 0; c < 8; c++) { int x = last - 8 * k - 7 + c; cell[c] = (x >= i && x <= last) ? cols[x] : 0; }
            digits[n - 1 - k] = XXH64(cell, sizeof(cell), 8);
        }
        nums.push_back(digits);
        i = last + 1;
    }
    if (getenv("WC09_STATDBG"))
    {
        printf("stats card %d:", card);
        for (auto& n : nums) printf(" [%zu]", n.size());
        printf(" cols:");
        for (u32 c : cols) printf(" %x", c);
        printf("\n"); fflush(stdout);
    }
    if (nums.size() != 2 || nums[0].size() > 5 || nums[1].size() > 5) return false;
    // learn from the printed values (most cards show them unchanged)
    if (card >= 0 && card < (int)CardAtk.size())
    {
        std::string a = std::to_string(CardAtk[card]), d = std::to_string(CardDef[card]);
        if (a.size() == nums[0].size() && d.size() == nums[1].size())
        {
            auto learn = [&](u64 hsh, int digit) {
                auto it = GlyphDigit.find(hsh);
                if (it == GlyphDigit.end()) GlyphDigit[hsh] = digit;
                else if (it->second != digit) it->second = -2;
            };
            for (size_t i = 0; i < a.size(); i++) learn(nums[0][i], a[i] - '0');
            for (size_t i = 0; i < d.size(); i++) learn(nums[1][i], d[i] - '0');
        }
    }
    int v[2];
    for (int n = 0; n < 2; n++)
    {
        v[n] = 0;
        for (u64 hsh : nums[n])
        {
            auto it = GlyphDigit.find(hsh);
            if (it == GlyphDigit.end() || it->second < 0) return false;
            v[n] = v[n] * 10 + it->second;
        }
    }
    atk = v[0]; def = v[1];
    return true;
}
}

bool GetStatPanel(int image, StatPanelInfo& out)
{
    std::lock_guard<std::mutex> lk(Lock);
    auto it = StatPanels.find(image);
    if (it == StatPanels.end()) return false;
    const StatPanel& p = it->second;
    out = {p.Card, p.Atk, p.Def, p.Bubble, p.Level, p.BX, p.BY};
    return true;
}

namespace
{
RectHide DetailBadge[2];
RectHide LpBand[2];
std::atomic<u64> BadgeSeen[2];             // frame + 1 when the badge was last hidden
std::atomic<int> BadgeX0[2], BadgeY0[2], BadgeX1[2], BadgeY1[2];
}

bool LpPanelShown(int engine) { return LpBand[engine & 1].On; }

bool HideSprite(int engine, int x, int y, int w, int h, bool affine)
{
    {
        const RectHide& b = LpBand[engine & 1];
        if (b.On && x >= b.X0 && x + w <= b.X1 + 1 && y >= b.Y0 && y < b.Y1) return true;
    }
    const RectHide& r = DetailBadge[engine & 1];
    // scaled sprites are never the badge (e.g. the big current-ATK digits drawn over the card)
    if (!r.On || w > 16 || h > 16 || affine) return false;
    // the level badge the game puts on the card's top-right corner: redrawn in HD by the panel
    if (!(x >= r.X0 + 50 && x + w <= r.X1 + 6 && y >= r.Y0 + 4 && y + h <= r.Y0 + 34)) return false;
    int e = engine & 1;
    u64 now = VBlankCount.load() + 1;
    if (BadgeSeen[e].load() != now)
    {
        BadgeSeen[e] = now;
        BadgeX0[e] = x; BadgeY0[e] = y; BadgeX1[e] = x + w; BadgeY1[e] = y + h;
    }
    else
    {
        BadgeX0[e] = std::min(BadgeX0[e].load(), x); BadgeY0[e] = std::min(BadgeY0[e].load(), y);
        BadgeX1[e] = std::max(BadgeX1[e].load(), x + w); BadgeY1[e] = std::max(BadgeY1[e].load(), y + h);
    }
    return true;
}

namespace
{
bool TextLayer[2][4];
std::vector<u8> TextMask[2][4];      // 64x64 map tiles, 255 = text tile
u32 TextMaskGen[2][4];
bool TextOutlined[2][4];
struct TextCacheEntry { u64 Key = 0; bool Text = false; };
TextCacheEntry TextCache[2][4];

// A text layer uses only palette entries 1..15 of a few 16-colour groups, each group a ramp
// whose brightest channel climbs from near black (1) to bright (15), and several ramp steps
// (anti-aliasing). Pictures never look like that.
bool DetectTextLayer(GPU& gpu, int engine, int bg)
{
    const GPU2D& u = engine == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
    u16 cnt = u.BGCnt[bg];
    bool bpp8 = cnt & 0x80;
    if (bpp8 && (u.DispCnt & (1u << 30))) return false;     // extended palettes: not handled
    u32 charBase = ((cnt >> 2) & 0xF) * 0x4000, scrBase = ((cnt >> 8) & 0x1F) * 0x800;
    if (engine == 0) { charBase += ((u.DispCnt >> 24) & 7) * 0x10000; scrBase += ((u.DispCnt >> 27) & 7) * 0x10000; }
    int mapW = (cnt >> 14) & 1 ? 64 : 32, mapH = (cnt >> 14) & 2 ? 64 : 32;
    int sx0 = u.BGXPos[bg] & 0x1FF, sy0 = u.BGYPos[bg] & 0x1FF;
    const u8* pal = &gpu.Palette[engine * 0x400];

    // visible map entries
    static u16 ents[33 * 25]; static u16 pos[33 * 25];
    int ne = 0;
    for (int ty = 0; ty < 25; ty++)
        for (int tx = 0; tx < 33; tx++)
        {
            int mx = ((sx0 >> 3) + tx) & (mapW - 1), my = ((sy0 >> 3) + ty) & (mapH - 1);
            u32 block = (mx >> 5) + ((my >> 5) * (mapW >> 5));
            pos[ne] = (u16)(my * 64 + mx);
            ents[ne++] = ReadBG(gpu, engine, scrBase + block * 0x800 + (((my & 31) * 32 + (mx & 31)) * 2));
        }
    // unique tiles and their pixels (the game writes text into the tile data, not the map)
    static u16 tiles[1024], banks[1024];
    static u8 data[1024 * 64];
    static s16 tileOf[33 * 25];
    int nt = 0;
    u32 bytes = bpp8 ? 64 : 32;
    for (int i = 0; i < ne; i++)
    {
        u16 t = ents[i] & 0x3FF, bank = bpp8 ? 0 : (ents[i] >> 12);
        int found = -1;
        for (int k = 0; k < nt && found < 0; k++) if (tiles[k] == t && banks[k] == bank) found = k;
        if (found < 0)
        {
            if (nt == 1024) return false;
            tiles[nt] = t; banks[nt] = bank;
            for (u32 b = 0; b < bytes; b += 2)
            {
                u16 two = ReadBG(gpu, engine, charBase + t * bytes + b);
                data[nt * bytes + b] = two & 0xFF; data[nt * bytes + b + 1] = two >> 8;
            }
            found = nt++;
        }
        tileOf[i] = (s16)found;
    }
    u64 key = XXH64(data, nt * bytes, cnt) ^ (XXH64(ents, ne * 2, 7) * 31) ^ (XXH64(pal, 512, 1) * 131) ^ ((u64)sx0 << 40) ^ ((u64)sy0 << 50);
    TextCacheEntry& c = TextCache[engine][bg];
    if (c.Key == key) return c.Text;
    c.Key = key; c.Text = false;

    // which 16-colour groups are text ramps (index 1 near black climbing to a bright 15)
    u32 used[16] = {};
    for (int n = 0; n < nt; n++)
        for (u32 b = 0; b < bytes; b++)
        {
            u8 v = data[n * bytes + b];
            for (int k = 0; k < (bpp8 ? 1 : 2); k++)
            {
                u32 idx = bpp8 ? v : ((v >> (4 * k)) & 0xF);
                if (!idx) continue;
                used[bpp8 ? idx >> 4 : banks[n]] |= 1u << (idx & 15);
            }
        }
    u16 ramp = 0; int dirSum = 0;
    for (int g = 0; g < 16; g++)
    {
        if (!used[g] || (used[g] & 1) || __builtin_popcount(used[g]) < 6) continue;
        // brightness must move one way along the used indices (dark outline -> bright fill or the
        // reverse), from near black to bright
        int prev = -1, first = -1, last = -1, dir = 0; bool ok = true;
        for (int k = 1; k < 16 && ok; k++)
        {
            if (!(used[g] & (1u << k))) continue;
            u16 col = pal[(g * 16 + k) * 2] | (pal[(g * 16 + k) * 2 + 1] << 8);
            int m = ((col & 31) * 2 + ((col >> 5) & 31) * 4 + ((col >> 10) & 31)) / 7;
            if (first < 0) first = m;
            last = m;
            if (prev >= 0)
            {
                int d = m > prev + 1 ? 1 : m < prev - 1 ? -1 : 0;
                if (d && dir && d != dir) ok = false;
                if (d) dir = d;
            }
            prev = m;
        }
        int lo = std::min(first, last), hi = std::max(first, last);
        if (ok && dir && lo <= 10 && hi >= 22) { ramp |= 1 << g; dirSum += dir; }
    }
    // per tile: text when every pixel it shows comes from a ramp group
    std::vector<u8> mask(64 * 64, 0);
    int textTiles = 0;
    // 255 = only text pixels, 128 = text mixed with small icons (drawn with the generic mode)
    std::vector<s16> tileText(nt, -1);
    for (int i = 0; i < ne; i++)
    {
        int n = tileOf[i];
        if (tileText[n] < 0)
        {
            int text = 0, other = 0;
            for (u32 b = 0; b < bytes; b++)
            {
                u8 v = data[n * bytes + b];
                for (int k = 0; k < (bpp8 ? 1 : 2); k++)
                {
                    u32 idx = bpp8 ? v : ((v >> (4 * k)) & 0xF);
                    if (!idx) continue;
                    if (ramp & (1 << (bpp8 ? idx >> 4 : banks[n]))) text++; else other++;
                }
            }
            // non-text tiles on a text layer (small icons, digits in other colours): smoothed
            // with the generic mode too
            tileText[n] = !text && !other ? 0 : !other ? 255 : 128;
        }
        if (tileText[n]) { mask[pos[i]] = (u8)tileText[n]; if (tileText[n] == 255) textTiles++; }
    }
    // a few tiles that match by chance are not text: need a real amount of it
    c.Text = textTiles >= 4;
    if (getenv("WC09_TXTDBG"))
    {
        int n255 = 0, n128 = 0; for (u8 v : mask) { n255 += v == 255; n128 += v == 128; }
        printf("txtmask e%d b%d text %d full %d mixed %d ramp %04x dir %d\n", engine, bg, c.Text, n255, n128, ramp, dirSum);
        if (engine == 1 && bg == 3)
            for (int y = 0; y < 24; y++)
            {
                printf("row %2d ", y);
                for (int x = 0; x < 32; x++) printf("%c", mask[y * 64 + x] == 255 ? '#' : mask[y * 64 + x] == 128 ? '+' : '.');
                printf("\n");
            }
        fflush(stdout);
    }
    if (c.Text)
    {
        std::lock_guard<std::mutex> lk(Lock);
        TextMask[engine][bg].swap(mask);
        TextMaskGen[engine][bg]++;
        TextOutlined[engine][bg] = dirSum > 0;   // dark outline, bright fill
    }
    return c.Text;
}
}

bool IsTextLayer(int engine, int bg)
{
    std::lock_guard<std::mutex> lk(Lock);
    return TextLayer[engine & 1][bg & 3];
}

u32 GetTextMask(int engine, int bg, std::vector<u8>& mask, bool* outlined)
{
    std::lock_guard<std::mutex> lk(Lock);
    mask = TextMask[engine & 1][bg & 3];
    if (outlined) *outlined = TextOutlined[engine & 1][bg & 3];
    return TextMaskGen[engine & 1][bg & 3];
}

// ---- HD tile pack -----------------------------------------------------------------------------
// Background tiles replaced one by one (menu digits, icons, list rows...): key = hash of the 8x8
// tile as shown (colours after the palette, flips applied).  HDCards/tiles/keys.png lists the
// keys (3 pixels per key, 24 bits each, alpha 255); HDCards/tiles/atlas.png holds the tiles,
// 64x64 each, 32 per row, in the same order.
namespace
{
std::unordered_map<u64, u16> HDTileKeys;     // key -> slot + 1
std::atomic<bool> HDTilesReady{false};
std::vector<u16> HDTileMapV[2][4];
u32 HDTileGen[2][4];
bool HDTileAny[2][4];
u64 HDTileLastKey[2][4];   // what the visible tiles looked like when last scanned
}

void LoadHDTileKeys()
{
    std::vector<u8> rgba; int w = 0, h = 0;
    std::unordered_map<u64, u16> keys;
    if (LoadHDImage("tiles/keys", rgba, w, h) && w > 0)
    {
        int n = (w * h) / 3;
        for (int i = 0; i < n && i < 1024; i++)
        {
            u64 k = 0;
            for (int j = 0; j < 3; j++)
            {
                const u8* px = &rgba[(i * 3 + j) * 4];
                k |= (u64)(px[0] | (px[1] << 8) | (px[2] << 16)) << (24 * j);
            }
            if (k) keys[k] = (u16)(i + 1);
        }
    }
    {
        std::lock_guard<std::mutex> lk(Lock);
        HDTileKeys.swap(keys);
    }
    HDTilesReady = !HDTileKeys.empty();
    if (getenv("WC09_DEBUG")) { printf("WC09 HD tiles: %zu\n", HDTileKeys.size()); fflush(stdout); }
}

// hashes of the visible tiles of a text-mode layer; fills HD tile slots, optionally dumps them.
// Reads the flat VRAM copies the renderer keeps (what it last drew); does nothing when the map,
// the tiles it uses, the palettes and the scroll are unchanged since the last frame.
static void UpdateHDTiles(GPU& gpu, int engine, int bg, bool dump)
{
    GPU2D& u = engine == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
    const u8* vram = engine == 0 ? gpu.VRAMFlat_ABG : gpu.VRAMFlat_BBG;
    const u32 vmask = engine == 0 ? 0x7FFFF : 0x1FFFF;
    auto rd16 = [&](u32 a) -> u16 { a &= vmask & ~1u; return (u16)(vram[a] | (vram[a + 1] << 8)); };
    u16 cnt = u.BGCnt[bg];
    bool bpp8 = cnt & 0x80;
    bool ext = bpp8 && (u.DispCnt & (1u << 30));
    u32 charBase = ((cnt >> 2) & 0xF) * 0x4000, scrBase = ((cnt >> 8) & 0x1F) * 0x800;
    if (engine == 0) { charBase += ((u.DispCnt >> 24) & 7) * 0x10000; scrBase += ((u.DispCnt >> 27) & 7) * 0x10000; }
    int mapW = (cnt >> 14) & 1 ? 64 : 32, mapH = (cnt >> 14) & 2 ? 64 : 32;
    int sx0 = u.BGXPos[bg] & 0x1FF, sy0 = u.BGYPos[bg] & 0x1FF;
    const u8* pal = &gpu.Palette[engine * 0x400];
    u32 extSlot = (bg < 2 && (cnt & 0x2000)) ? bg + 2 : bg;
    u32 bytes = bpp8 ? 64 : 32;

    static u16 ents[25][33];
    u32 tmin = 0x3FF, tmax = 0;
    for (int ty = 0; ty < 25; ty++)
        for (int tx = 0; tx < 33; tx++)
        {
            int mx = ((sx0 >> 3) + tx) & (mapW - 1), my = ((sy0 >> 3) + ty) & (mapH - 1);
            u32 block = (mx >> 5) + ((my >> 5) * (mapW >> 5));
            u16 ent = rd16(scrBase + block * 0x800 + (((my & 31) * 32 + (mx & 31)) * 2));
            ents[ty][tx] = ent;
            tmin = std::min<u32>(tmin, ent & 0x3FF); tmax = std::max<u32>(tmax, ent & 0x3FF);
        }
    // change detection
    u64 key = XXH64(ents, sizeof(ents), cnt) ^ ((u64)sx0 << 40) ^ ((u64)sy0 << 50) ^ (XXH64(pal, 512, 3) * 31);
    {
        u32 a0 = (charBase + tmin * bytes) & vmask, len = (tmax - tmin + 1) * bytes;
        if (a0 + len <= vmask + 1) key ^= XXH64(vram + a0, len, 5) * 131;
        else key ^= XXH64(vram, vmask + 1, 5) * 131;
        if (ext) key ^= XXH64(u.GetBGExtPal(extSlot, 0), 16 * 512, 7) * 17;
    }
    if (!dump && key == HDTileLastKey[engine][bg]) return;
    HDTileLastKey[engine][bg] = key;

    std::vector<u16> map(64 * 64, 0);
    bool any = false;
    static u64 hashes[25][33];
    static u8 pix[25 * 8][33 * 8][4];
    for (int ty = 0; ty < 25; ty++)
        for (int tx = 0; tx < 33; tx++)
        {
            int mx = ((sx0 >> 3) + tx) & (mapW - 1), my = ((sy0 >> 3) + ty) & (mapH - 1);
            u16 ent = ents[ty][tx];
            u8 rgba[64][4];
            u32 t = ent & 0x3FF;
            const u8* data;
            u8 tmp[64];
            u32 a0 = (charBase + t * bytes) & vmask;
            if (a0 + bytes <= vmask + 1) data = vram + a0;
            else { for (u32 i = 0; i < bytes; i++) tmp[i] = vram[(a0 + i) & vmask]; data = tmp; }
            bool blank = true;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                {
                    int sxp = (ent & 0x400) ? 7 - x : x, syp = (ent & 0x800) ? 7 - y : y;
                    u32 idx = bpp8 ? data[syp * 8 + sxp] : ((data[syp * 4 + (sxp >> 1)] >> ((sxp & 1) * 4)) & 0xF);
                    u8* o = rgba[y * 8 + x];
                    if (!idx) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
                    blank = false;
                    u16 col;
                    if (ext) col = u.GetBGExtPal(extSlot, ent >> 12)[idx];
                    else if (bpp8) col = pal[idx * 2] | (pal[idx * 2 + 1] << 8);
                    else { u32 i = (ent >> 12) * 16 + idx; col = pal[i * 2] | (pal[i * 2 + 1] << 8); }
                    o[0] = (col & 31) << 3; o[1] = ((col >> 5) & 31) << 3; o[2] = ((col >> 10) & 31) << 3; o[3] = 255;
                }
            u64 h = blank ? 0 : XXH64(rgba, sizeof(rgba), 0x7117);
            if (dump) { hashes[ty][tx] = h; for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) memcpy(pix[ty * 8 + y][tx * 8 + x], rgba[y * 8 + x], 4); }
            if (!h) continue;
            auto it = HDTileKeys.find(h);
            if (it != HDTileKeys.end()) { map[my * 64 + mx] = it->second; any = true; }
        }
    if (dump)
    {
        static int n = 0;
        char path[512];
        snprintf(path, sizeof(path), "%s/L%d%d_%04d.bin", getenv("WC09_TILEDUMP"), engine, bg, n++);
        if (FILE* f = fopen(path, "wb"))
        {
            fwrite(hashes, sizeof(hashes), 1, f); fwrite(pix, sizeof(pix), 1, f);
            int hdr[4] = {sx0 & 7, sy0 & 7, engine, bg}; fwrite(hdr, sizeof(hdr), 1, f);
            fclose(f);
        }
    }
    std::lock_guard<std::mutex> lk(Lock);
    if (any || HDTileAny[engine][bg])
    {
        if (map != HDTileMapV[engine][bg])
        {
            if (getenv("WC09_DEBUG")) { int c = 0; for (u16 v : map) c += v != 0; printf("WC09 HD tiles e%d b%d: %d tiles\n", engine, bg, c); fflush(stdout); }
            HDTileMapV[engine][bg].swap(map); HDTileGen[engine][bg]++;
        }
    }
    HDTileAny[engine][bg] = any;
}

bool HDTilesAvailable() { return HDTilesReady.load(); }

u32 GetHDTileMap(int engine, int bg, std::vector<u16>& map, bool& any)
{
    std::lock_guard<std::mutex> lk(Lock);
    any = HDTileAny[engine & 1][bg & 3];
    if (any) map = HDTileMapV[engine & 1][bg & 3];
    return HDTileGen[engine & 1][bg & 3];
}


namespace
{
// ---- duel life point panel ------------------------------------------------------------------
// The panel under the card view (life points, gauges, turn, phase) is built from many small
// sprites. It is redrawn as one HD image: the numbers are read back from the digit sprites
// (matched by shape against duel2d.pac lp_num128), the phase from the phase name sprites.
std::unordered_map<u64, int> LpDigit;       // 8x8 shape mask -> digit
std::unordered_map<u64, int> PhaseTile;     // 8x8 shape mask -> phase 0..5 (DP SP M1 BP M2 EP)

u64 TileMask(const u8* t, bool bpp8)
{
    u64 m = 0;
    for (int i = 0; i < 64; i++)
    {
        u8 v = bpp8 ? t[i] : ((t[i >> 1] >> ((i & 1) * 4)) & 15);
        if (v) m |= 1ull << i;
    }
    return m;
}

bool ObjdTiles(const std::vector<u8>& dec, std::vector<std::vector<u8>>& tiles, bool& bpp8)
{
    if (dec.size() < 16 || memcmp(dec.data(), "NTBG", 4)) return false;
    for (u32 p = 16; p + 8 <= dec.size();)
    {
        u32 cs = RD32(&dec[p + 4]);
        if (cs < 8 || p + cs > dec.size()) return false;
        if (!memcmp(&dec[p], "OBJD", 4) && cs >= 32)
        {
            const u8* b = &dec[p + 8];
            u32 mapsz = RD32(b + 4); u16 tw = RD16(b + 8), th = RD16(b + 10); u32 chrsz = RD32(b + 20);
            u32 n = (u32)tw * th;
            if (!n || 24 + mapsz + chrsz > cs - 8 || mapsz < n * 4) return false;
            const u8* mp = b + 24; const u8* ch = b + 24 + mapsz;
            bpp8 = chrsz >= n * 64;
            u32 tb = bpp8 ? 64 : 32;
            for (u32 t = 0; t < n; t++)
            {
                u32 off = RD32(mp + t * 4) * 32;
                if (off + tb > chrsz) { tiles.emplace_back(tb, 0); continue; }
                tiles.emplace_back(ch + off, ch + off + tb);
            }
            return true;
        }
        p += cs;
    }
    return false;
}

void LoadLpGlyphs(const u8* rom, u32 len)
{
    LpDigit.clear(); PhaseTile.clear();
    FileRef ref;
    if (!FindFile(rom, len, "Data_arc_pac/duel2d.pac", ref)) return;
    const u8* pac = rom + ref.Start;
    static const char* const kPhase[6] = {"duel_obj05_up", "duel_obj06_up", "duel_obj07_up", "duel_obj08_up", "duel_obj09_up", "duel_obj10_up"};
    for (const PacEntry& pe : ParsePac(pac, ref.Size))
    {
        std::string stem = pe.Name.substr(0, pe.Name.find('.'));
        int phase = -1;
        for (int k = 0; k < 6; k++) if (stem == kPhase[k]) phase = k;
        if (stem != "lp_num128" && phase < 0) continue;
        std::vector<u8> dec;
        if (pe.Size && pac[pe.Start] == 0x10) { if (!LZ10(pac + pe.Start, pe.Size, dec)) continue; }
        else dec.assign(pac + pe.Start, pac + pe.Start + pe.Size);
        std::vector<std::vector<u8>> tiles; bool bpp8 = false;
        if (!ObjdTiles(dec, tiles, bpp8)) continue;
        for (size_t i = 0; i < tiles.size(); i++)
        {
            u64 m = TileMask(tiles[i].data(), bpp8);
            if (!m) continue;
            if (stem == "lp_num128") { if (i < 10) LpDigit[m] = (int)i; }
            else if (!PhaseTile.count(m)) PhaseTile[m] = phase;
            else if (PhaseTile[m] != phase) PhaseTile[m] = -1;   // shared letter shapes say nothing
        }
    }
}

struct LpPanel { int Lp0, Lp1, Turn, Phase; bool TurnRight; };
std::map<int, LpPanel> LpPanels;
std::map<std::tuple<int, int, int, int, bool>, int> LpPanelIds;
int NextLpPanel = kLpPanelBase;

// Reads the panel's numbers from the OAM of `engine`. False if it doesn't look like the panel.
bool ReadLpPanel(GPU& gpu, int engine, LpPanel& out)
{
    struct D { int x, y, v; };
    std::vector<D> digits;
    int votes[6] = {};
    const u8* oam = &gpu.OAM[engine * 0x400];
    const GPU2D& u = engine == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
    if (!(u.DispCnt & (1 << 4))) return false;     // 1D mapping only
    u32 shift = 5 + ((u.DispCnt >> 20) & 3);
    static const u8 sizes[3][4][2] = {{{8, 8}, {16, 16}, {32, 32}, {64, 64}}, {{16, 8}, {32, 8}, {32, 16}, {64, 32}}, {{8, 16}, {8, 32}, {16, 32}, {32, 64}}};
    for (int k = 0; k < 128; k++)
    {
        u16 a0 = RD16(oam + k * 8), a1 = RD16(oam + k * 8 + 2), a2 = RD16(oam + k * 8 + 4);
        if ((a0 & 0x300) == 0x200 || (a0 >> 14) == 3) continue;
        int y = a0 & 0xFF, x = a1 & 0x1FF; if (x >= 256) x -= 512;
        if (y < 146 || y > 191 || x < 40 || x > 215) continue;
        int w = sizes[a0 >> 14][a1 >> 14][0], h = sizes[a0 >> 14][a1 >> 14][1];
        if (h != 8) continue;
        bool bpp8 = a0 & (1 << 13);
        u32 tb = bpp8 ? 64 : 32;
        for (int t = 0; t < w / 8; t++)
        {
            u8 tile[64];
            u32 base = ((a2 & 0x3FF) << shift) + t * tb;
            for (u32 i = 0; i < tb; i += 2)
            {
                u16 v = engine ? gpu.ReadVRAM_BOBJ<u16>(base + i) : gpu.ReadVRAM_AOBJ<u16>(base + i);
                tile[i] = v & 0xFF; tile[i + 1] = v >> 8;
            }
            u64 m = TileMask(tile, bpp8);
            if (w == 8) { auto it = LpDigit.find(m); if (it != LpDigit.end()) digits.push_back({x, y, it->second}); }
            else { auto it = PhaseTile.find(m); if (it != PhaseTile.end() && it->second >= 0) votes[it->second]++; }
        }
    }
    if (digits.size() < 4) return false;
    // rows by y, numbers by x gaps
    std::sort(digits.begin(), digits.end(), [](const D& a, const D& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    std::vector<std::vector<D>> nums;
    for (const D& d : digits)
    {
        if (nums.empty() || std::abs(nums.back().back().y - d.y) > 2 || d.x - nums.back().back().x > 10) nums.push_back({});
        nums.back().push_back(d);
    }
    int topY = digits.front().y;
    out = {-1, -1, -1, -1, false};
    for (auto& n : nums)
    {
        int v = 0; for (const D& d : n) v = v * 10 + d.v;
        bool top = std::abs(n.front().y - topY) <= 2;
        // the turn counter sits at the side of the player whose turn it is (x 72 or x 192)
        if (n.size() <= 3 && (n.front().x < 90 || n.front().x > 172)) { out.Turn = v; out.TurnRight = n.front().x > 172; }
        else if (top) out.Lp0 = v;
        else out.Lp1 = v;
    }
    int best = -1, bv = 0;
    for (int k = 0; k < 6; k++) if (votes[k] > bv) { bv = votes[k]; best = k; }
    out.Phase = best;
    return out.Lp0 >= 0 && out.Lp1 >= 0;
}
}

bool GetLpPanel(int image, int& lp0, int& lp1, int& turn, int& phase, bool& turnRight)
{
    std::lock_guard<std::mutex> lk(Lock);
    auto it = LpPanels.find(image);
    if (it == LpPanels.end()) return false;
    lp0 = it->second.Lp0; lp1 = it->second.Lp1; turn = it->second.Turn; phase = it->second.Phase; turnRight = it->second.TurnRight;
    return true;
}

void OnVBlankBattle(GPU& gpu)
{
    VBlankCount++;
    if (getenv("WC09_REGS") && VBlankCount % 120 == 0)
    {
        for (int e = 0; e < 2; e++)
        {
            const GPU2D& u = e == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
            printf("WC09 regs %c: dispcnt %08x bgcnt %04x %04x %04x %04x swap %d blend %04x %d %d win %02x %02x %02x %02x w0 %d %d %d %d scroll %d,%d %d,%d %d,%d %d,%d\n", 'A' + e, u.DispCnt,
                   u.BGCnt[0], u.BGCnt[1], u.BGCnt[2], u.BGCnt[3], gpu.ScreenSwap, u.BlendCnt, u.EVA, u.EVB,
                   u.WinCnt[0], u.WinCnt[1], u.WinCnt[2], u.WinCnt[3], u.Win0Coords[0], u.Win0Coords[1], u.Win0Coords[2], u.Win0Coords[3],
                   u.BGXPos[0], u.BGYPos[0], u.BGXPos[1], u.BGYPos[1], u.BGXPos[2], u.BGYPos[2], u.BGXPos[3], u.BGYPos[3]);
        }
        fflush(stdout);
        if (getenv("WC09_OAMDBG"))
            for (int e = 0; e < 2; e++)
                for (int k = 0; k < 128; k++)
                {
                    const u16* a = (const u16*)&gpu.OAM[e * 0x400 + k * 8];
                    if ((a[0] & 0x300) == 0x200) continue;
                    int y = a[0] & 0xFF, x = a[1] & 0x1FF; if (x >= 256) x -= 512;
                    printf("OAM %c%d x %d y %d a0 %04x a1 %04x a2 %04x\n", 'A' + e, k, x, y, a[0], a[1], a[2]);
                }
    }
    // debug: WC09_DUMP=<file> writes BG/OBJ VRAM, palettes, OAM and registers every 2 seconds
    if (const char* dd = getenv("WC09_DUMP"); dd && *dd && VBlankCount % 15 == 0)
        if (FILE* f = fopen(dd, "wb"))
        {
            for (u32 a = 0; a < 0x80000; a += 2) { u16 v = gpu.ReadVRAM_ABG<u16>(a); fwrite(&v, 2, 1, f); }
            for (u32 a = 0; a < 0x20000; a += 2) { u16 v = gpu.ReadVRAM_BBG<u16>(a); fwrite(&v, 2, 1, f); }
            for (u32 a = 0; a < 0x40000; a += 2) { u16 v = gpu.ReadVRAM_AOBJ<u16>(a); fwrite(&v, 2, 1, f); }
            for (u32 a = 0; a < 0x20000; a += 2) { u16 v = gpu.ReadVRAM_BOBJ<u16>(a); fwrite(&v, 2, 1, f); }
            fwrite(gpu.Palette, 1, sizeof(gpu.Palette), f);
            fwrite(gpu.OAM, 1, sizeof(gpu.OAM), f);
            for (int e = 0; e < 2; e++)
            {
                const GPU2D& u = e == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
                u32 regs[16] = {u.DispCnt, u.BGCnt[0], u.BGCnt[1], u.BGCnt[2], u.BGCnt[3],
                                u.BGXPos[0], u.BGYPos[0], u.BGXPos[1], u.BGYPos[1], u.BGXPos[2], u.BGYPos[2],
                                u.BGXPos[3], u.BGYPos[3], u.BlendCnt, (u32)(u.EVA | (u.EVB << 8)), (u32)gpu.ScreenSwap};
                fwrite(regs, 4, 16, f);
            }
            fclose(f);
        }
    BGReplacement found[2][4];
    if (IsActive() && GetHDCardsEnabled())
    {
        auto* cart = gpu.NDS.NDSCartSlot.GetCart();
        if (cart && cart->GetROM() != IndexedROM)
        {
            if (Indexer.T.joinable()) Indexer.T.join();
            IndexReady = false;
            const u8* rom = cart->GetROM(); u32 len = cart->GetROMLength();
            IndexedROM = rom;
            Indexer.T = std::thread([rom, len]() {
                BuildIndex(rom, len);
                BuildSpriteIndex(rom, len);
                LoadCardText(rom, len);
                LoadLpGlyphs(rom, len);
                LoadHDTileKeys();
                SetCardROM(rom, len);
                PrepareCardFingerprints();
                IndexReady = true;
                if (getenv("WC09_DEBUG")) { printf("WC09 index ready\n"); fflush(stdout); }
            });
        }

        for (int engine = 0; engine < 2 && IndexReady; engine++)
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
                const bool b8 = cnt & (1 << 7);
                const u32 tb = b8 ? 64 : 32;
                const u64 seed = b8 ? 0 : 4;

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
                std::map<std::tuple<int, int, int, bool, int>, int> votes;
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
                            for (u32 i = 0; i < tb; i += 2) *(u16*)&tile[i] = ReadBG(gpu, engine, charBase + t * tb + i);
                            hashOf[t] = XXH64(tile, tb, seed); hashed[t] = true;
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
                            if (++votes[{tp.Image, ox, oy, mirror, tileBase}] >= 6) goto voted;
                        }
                    }
                }
            voted:
                int best = 0; int bestBase = 0, bestOX = 0, bestOY = 0;
                for (auto& [k, v] : votes)
                {
                    if (v <= best) continue;
                    best = v;
                    auto [imgId, ox, oy, mirror, base] = k;
                    bestBase = base; bestOX = ox; bestOY = oy;
                    BGReplacement& rep = found[engine][bg];
                    rep.Image = imgId;
                    rep.OriginX = (float)(((ox % mapW) + mapW) % mapW) * 8.f;
                    rep.OriginY = (float)(((oy % mapH) + mapH) % mapH) * 8.f;
                    rep.Width = Images[imgId].TW * 8.f; rep.Height = Images[imgId].TH * 8.f;
                    rep.Mirror = mirror;
                }
                if (best < 2) found[engine][bg] = BGReplacement{};
                // 4bpp layers often hold windows the game writes text into: only replace when the
                // visible part of the layer is exactly the stored image
                if (!b8 && found[engine][bg].Image >= 0)
                {
                    const Image& img = Images[found[engine][bg].Image];
                    bool mir = found[engine][bg].Mirror;
                    int good = 0, bad = 0;
                    for (int cy = 0; cy < 24; cy++)
                        for (int cx = 0; cx < 32; cx++)
                        {
                            int mx = sx0 + cx, my = sy0 + cy;
                            int ix = (((mx - bestOX) % mapW) + mapW) % mapW, iy = (((my - bestOY) % mapH) + mapH) % mapH;
                            if (ix >= img.TW || iy >= img.TH) continue;
                            if (mir) ix = img.TW - 1 - ix;
                            u16 want = img.Map[iy * img.TW + ix] & 0x3FF;
                            u16 have = entry(mx, my) & 0x3FF;
                            if (want == 0) { if (have == 0 || have == (u16)bestBase) continue; bad++; }
                            else if (have == (u16)(want + bestBase)) good++;
                            else bad++;
                        }
                    if (good < 8 || bad > good / 50) found[engine][bg] = BGReplacement{};
                }
            }
        }
    }
    // 2D cards (card detail view, deck editor, battle cut-ins...): the compositor samples the
    // HD card face in place of the card's tiles. Several cards on one background become one
    // composed image.
    if (IndexReady && IsActive() && GetHDCardsEnabled())
    {
        std::vector<CardQuad> cards = GetLatest2D();
        for (int e = 0; e < 2; e++)
            for (int b = 0; b < 4; b++)
            {
                std::vector<size_t> here;
                for (size_t i = 0; i < cards.size(); i++)
                    if (cards[i].Engine == e && cards[i].BG == b) here.push_back(i);
                BGReplacement& rep = found[e][b];
                if (here.empty() || rep.Image >= 0) continue;
                if (here.size() == 1)
                {
                    const CardQuad& q = cards[here[0]];
                    rep.Image = kCardImageBase + q.Card;
                    rep.OriginX = q.MapX; rep.OriginY = q.MapY;
                    rep.Width = 88; rep.Height = 128;
                }
                else
                {
                    int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
                    for (size_t i : here)
                    {
                        x0 = std::min<int>(x0, cards[i].MapX); y0 = std::min<int>(y0, cards[i].MapY);
                        x1 = std::max<int>(x1, cards[i].MapX + 88); y1 = std::max<int>(y1, cards[i].MapY + 128);
                    }
                    std::vector<CardSetItem> items;
                    for (size_t i : here) items.push_back({cards[i].Card, cards[i].MapX - x0, cards[i].MapY - y0});
                    rep.Image = RegisterCardSet(items, x1 - x0, y1 - y0);
                    rep.OriginX = (float)x0; rep.OriginY = (float)y0;
                    rep.Width = (float)(x1 - x0); rep.Height = (float)(y1 - y0);
                    rep.Fallback = true;
                }
                for (size_t i : here) MarkHandled2D(i);
            }
    }
    // deck editor / card list detail view: a big 3D card at the left of the top screen, the
    // description on BG3 and the name on BG2 (4-bit text layers): swap both for HD text
    if (IndexReady && IsActive() && GetHDTextEnabled())
    {
        FrameCards fc = GetLatest(true);
        for (const CardQuad& q : fc.Quads)
        {
            if (q.TexSize != 128 || q.Screen != 0 || q.Card == 0) continue;
            int x0 = std::min({q.X[0], q.X[1], q.X[2], q.X[3]}), x1 = std::max({q.X[0], q.X[1], q.X[2], q.X[3]});
            int y0 = std::min({q.Y[0], q.Y[1], q.Y[2], q.Y[3]}), y1 = std::max({q.Y[0], q.Y[1], q.Y[2], q.Y[3]});
            if (x0 < -4 || x0 > 20 || y0 < 16 || y0 > 40 || x1 - x0 < 70 || x1 - x0 > 110 || y1 - y0 < 110) continue;
            int e = gpu.ScreenSwap ? 0 : 1;   // engine on the top screen
            const GPU2D& u = e == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
            auto textLayer = [&](int bg) { return (u.DispCnt & (1 << (8 + bg))) && !(u.BGCnt[bg] & 0x80) && found[e][bg].Image < 0; };
            if (textLayer(3))
            {
                BGReplacement& r = found[e][3];
                r.Image = kDeckTextBase + q.Card * 2; r.ScreenSpace = true;
                r.OriginX = kDeckDescX; r.OriginY = kDeckDescY; r.Width = kDeckDescW; r.Height = kDeckDescH;
            }
            if (textLayer(2))
            {
                BGReplacement& r = found[e][2];
                r.Image = kDeckTextBase + q.Card * 2 + 1; r.ScreenSpace = true;
                r.OriginX = kDeckNameX; r.OriginY = kDeckNameY; r.Width = kDeckNameW; r.Height = kDeckNameH;
            }
            break;
        }
    }
    // duel card detail: replace the description text background with a generated HD panel
    RectHide detailBadge[2];
    if (IndexReady && IsActive() && GetHDTextEnabled())
    {
        FrameCards fc = GetLatest(true);
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

            // ATK/DEF under the card read back and drawn in HD (-1: not readable, the game's own
            // digits show), and the level badge redrawn on its corner. One bigger panel; its
            // fully transparent parts show the game's layer.
            int atk = -1, def = -1;
            if (!ReadStats(gpu, q.Engine, textBG, q.Card, 4, 96, kStatsY, kStatsH, atk, def)) atk = def = -1;
            int level = 0, bx = 0, by = 0;
            u64 seen = BadgeSeen[q.Engine].load();
            if (seen && VBlankCount.load() + 1 - seen < 10 && q.Card < (int)CardLevel.size() && CardLevel[q.Card] > 0)
            {
                level = CardLevel[q.Card];
                // centred on the HD card's own star row, right-aligned with it
                bx = q.X[0] + 82; by = q.Y[0] + 19;
            }
            {
                std::lock_guard<std::mutex> lk2(Lock);
                auto key = std::make_tuple(q.Card, atk, def, bubble, level, bx, by);
                auto it = StatPanelIds.find(key);
                int id;
                if (it != StatPanelIds.end()) id = it->second;
                else
                {
                    if (StatPanels.size() > 256) { StatPanels.clear(); StatPanelIds.clear(); }
                    id = NextStatPanel++;
                    if (NextStatPanel >= kStatPanelBase + 400000) NextStatPanel = kStatPanelBase;
                    StatPanels[id] = {q.Card, atk, def, bubble, level, bx, by};
                    StatPanelIds[key] = id;
                }
                rep.Image = id;
                rep.OriginX = 0; rep.OriginY = kPanelY; rep.Width = kPanelX + kPanelW;
                rep.Height = kStatsY + kStatsH - kPanelY;
                rep.Fallback = true;
            }
            // the level badge over the card's corner: hidden while the HD card is shown
            detailBadge[q.Engine] = {true, q.X[0], q.Y[0], q.X[0] + 88, q.Y[0] + 128};
            break;
        }
    }

    bool onScreen[2] = {false, false};
    for (int e = 0; e < 2; e++)
        for (int b = 0; b < 4; b++)
            if (found[e][b].Image >= 0 && found[e][b].Image < kTextPanelBase && !IsCardImage(found[e][b].Image) && !IsCardSet(found[e][b].Image) && !found[e][b].ScreenSpace) onScreen[(e == 0) == gpu.ScreenSwap ? 0 : 1] = true;

    // life point panel: BG1 shows the gauge image -> redraw the whole panel in HD
    RectHide lpBand[2];
    if (IndexReady && IsActive() && GetHDCardsEnabled())
        for (int e = 0; e < 2; e++)
        {
            BGReplacement& rep = found[e][1];
            if (rep.Image < 0 || rep.Image >= kTextPanelBase || BattleImageName(rep.Image) != "duel2d/status_bghpbarp") continue;
            LpPanel lp;
            bool okLp = ReadLpPanel(gpu, e, lp);
            if (getenv("WC09_LPDBG")) { printf("lp read %d: %d %d t%d p%d\n", okLp, lp.Lp0, lp.Lp1, lp.Turn, lp.Phase); fflush(stdout); }
            if (!okLp) continue;
            int id;
            {
                std::lock_guard<std::mutex> lk2(Lock);
                auto key = std::make_tuple(lp.Lp0, lp.Lp1, lp.Turn, lp.Phase, lp.TurnRight);
                auto it = LpPanelIds.find(key);
                if (it != LpPanelIds.end()) id = it->second;
                else
                {
                    if (LpPanels.size() > 512) { LpPanels.clear(); LpPanelIds.clear(); }
                    id = NextLpPanel++;
                    if (NextLpPanel >= kLpPanelBase + 400000) NextLpPanel = kLpPanelBase;
                    LpPanels[id] = lp; LpPanelIds[key] = id;
                }
            }
            rep = BGReplacement{};
            rep.Image = id; rep.ScreenSpace = true;
            rep.OriginX = kLpX; rep.OriginY = kLpY; rep.Width = kLpW; rep.Height = kLpH;
            rep.NoWindow = true;   // the game clips its gauges with windows; the HD panel draws its own
            lpBand[e] = {true, kLpX, kLpY, kLpX + kLpW, kLpY + kLpH};
        }

    // text layers: one layer is re-examined per VBlank (round robin, 8 VBlanks for all), the
    // others keep their last result; the scan reads the layer's tiles, too costly for every frame
    static bool lastText[2][4] = {};
    bool textLayer[2][4] = {};
    if (IsActive() && GetHDTextEnabled())
    {
        int slot = (int)(VBlankCount.load() & 7);
        for (int e = 0; e < 2; e++)
        {
            const GPU2D& u = e == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
            if (((u.DispCnt >> 16) & 3) != 1) continue;
            u32 mode = u.DispCnt & 7;
            for (int b = 0; b < 4; b++)
            {
                // layers swapped for a screen-space HD image still show the game's own pixels
                // outside it (e.g. the opponent's speech bubble under the card text panel)
                if (!(u.DispCnt & (1 << (8 + b)))) continue;
                if (found[e][b].Image >= 0 && !found[e][b].ScreenSpace) continue;
                if (e == 0 && b == 0 && (u.DispCnt & (1 << 3))) continue;
                bool text = b < 2 || (b == 2 && mode <= 1) || (b == 3 && mode == 0);
                if (!text) continue;
                textLayer[e][b] = (e * 4 + b == slot) ? DetectTextLayer(gpu, e, b) : lastText[e][b];
            }
        }
    }
    for (int e = 0; e < 2; e++) for (int b = 0; b < 4; b++) lastText[e][b] = textLayer[e][b];

    // HD tiles: every visible text-mode layer, every frame (lists scroll and change quickly)
    {
        static const char* tdump = getenv("WC09_TILEDUMP");
        bool dumpNow = tdump && (VBlankCount.load() % 60) == 0;
        bool updated[2][4] = {};
        static const bool perf = getenv("WC09_PERF") != nullptr;
        auto tp0 = std::chrono::steady_clock::now();
        if ((HDTilesReady || dumpNow) && IsActive() && GetHDCardsEnabled())
            for (int e = 0; e < 2; e++)
            {
                const GPU2D& u = e == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
                if (((u.DispCnt >> 16) & 3) != 1) continue;
                u32 mode = u.DispCnt & 7;
                for (int b = 0; b < 4; b++)
                {
                    bool on = (u.DispCnt & (1 << (8 + b))) && !(found[e][b].Image >= 0 && !found[e][b].ScreenSpace) &&
                              !(e == 0 && b == 0 && (u.DispCnt & (1 << 3))) &&
                              (b < 2 || (b == 2 && mode <= 1) || (b == 3 && mode == 0));
                    if (on) { UpdateHDTiles(gpu, e, b, dumpNow); updated[e][b] = true; }
                }
            }
        if (perf)
        {
            static double worst = 0; static int n = 0;
            worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tp0).count());
            if (++n == 60) { printf("WC09 HD tiles worst %.2f ms\n", worst); fflush(stdout); worst = 0; n = 0; }
        }
        // layers not scanned this frame never keep tiles from an earlier screen
        std::lock_guard<std::mutex> lk(Lock);
        for (int e = 0; e < 2; e++) for (int b = 0; b < 4; b++)
            if (!updated[e][b]) { HDTileLastKey[e][b] = 0; if (HDTileAny[e][b]) { HDTileAny[e][b] = false; HDTileGen[e][b]++; } }
    }

    std::lock_guard<std::mutex> lk(Lock);
    OnScreen[0] = onScreen[0]; OnScreen[1] = onScreen[1];
    static const bool debug = getenv("WC09_DEBUG") != nullptr;
    for (int e = 0; e < 2; e++) for (int b = 0; b < 4; b++)
    {
        if (debug && TextLayer[e][b] != textLayer[e][b]) printf("WC09 text layer: engine %d bg %d %s\n", e, b, textLayer[e][b] ? "on" : "off");
        TextLayer[e][b] = textLayer[e][b];
    }
    DetailBadge[0] = detailBadge[0]; DetailBadge[1] = detailBadge[1];
    LpBand[0] = lpBand[0]; LpBand[1] = lpBand[1];
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
