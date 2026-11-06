#include "WC09Cards.h"
#include "WC09CardTable.h"
#include "WC09Battle.h"
#include "GPU.h"
#include "GPU3D.h"
#include "WC09Rom.h"
#include <unordered_map>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <cstring>

#define XXH_STATIC_LINKING_ONLY
#include "xxhash/xxhash.h"

namespace melonDS::WC09
{

namespace
{
std::atomic<bool> Active{false};
std::mutex Lock;
std::vector<CardQuad> Building;
FrameCards Latest;
std::vector<CardQuad> Latest2D;

// 128 and 64 px textures hold the whole card face at texels 20..108 (x 0.15625..0.84375 of the
// width), full height. The 32 px texture holds only the artwork: texels (1,4)-(31,26) show the
// face region that sits at (33,24)-(95,90) in the 128 px texture. Measured from the ROM.
constexpr float FaceS0 = 20.f / 128.f, FaceS1 = 108.f / 128.f;
constexpr float ArtU0 = (33.f - 20.f) / 88.f, ArtU1 = (95.f - 20.f) / 88.f;
constexpr float ArtV0 = 24.f / 128.f, ArtV1 = 90.f / 128.f;

const TexHashEntry* Find(u64 hash, u32 size)
{
    auto it = std::lower_bound(std::begin(TexHashes), std::end(TexHashes), hash,
                               [](const TexHashEntry& e, u64 h) { return e.Hash < h; });
    if (it != std::end(TexHashes) && it->Hash == hash && it->Size == size) return it;
    return nullptr;
}

// small cache so the same texture slot isn't rehashed every polygon
struct SlotCache { u32 TexParam = 0; u64 Hash = 0; u32 Frame = 0xFFFFFFFF; };
SlotCache Cache[64];
u32 FrameCounter = 0;
}

std::atomic<bool> Smooth2D{true};
std::atomic<bool> HDCards{true};
std::atomic<bool> HDText{true};
void SetHDTextEnabled(bool on) { HDText = on; }
bool GetHDTextEnabled() { return HDText; }
void SetHDCardsEnabled(bool on) { HDCards = on; }
bool GetHDCardsEnabled() { return HDCards; }
void SetSmooth2D(bool on) { Smooth2D = on; }
bool GetSmooth2D() { return Smooth2D; }

void SetActive(bool active) { Active = active; }
bool IsActive() { return Active; }

u32 PasscodeOf(u16 card) { return card < NumCards ? Passcodes[card] : 0; }

int CardFromTexHash(u64 hash, u32 size)
{
    const TexHashEntry* e = Find(hash, size);
    return e ? e->Card : -1;
}

bool LoadCardImage(int card, std::vector<u8>& rgba, int& w, int& h)
{
    if (card < 0 || card >= (int)NumCards) return false;
    if (card == 0) return LoadHDImage("cardback", rgba, w, h);
    if (u32 pc = Passcodes[card])
    {
        char n[16];
        snprintf(n, sizeof(n), "%u", pc);
        if (LoadHDImage(n, rgba, w, h)) return true;
        snprintf(n, sizeof(n), "%08u", pc);
        if (LoadHDImage(n, rgba, w, h)) return true;
    }
    return LoadHDImage("card_" + std::to_string(card), rgba, w, h);
}

namespace { std::atomic<u64> Cards3DStamp{0}, Cards2DStamp{0}; }
void NoteCards3DRendered() { Cards3DStamp = FrameNum() + 1; }
void NoteCards2DRendered() { Cards2DStamp = FrameNum() + 1; }

// ---- big cards drawn as direct-color bitmap sprites (story) -------------------------------
namespace
{
const u8* CardROM = nullptr; u32 CardROMLen = 0;
constexpr int FPW = 11, FPH = 16, FPN = FPW * FPH * 3;   // 8x8-pixel cell averages of the face
std::vector<u8> Fingerprints;                             // NumCards * FPN, 5-bit channels
bool FingerprintsBuilt = false;
std::unordered_map<u64, int> BitmapCache;

void Fingerprint(const u16* px, u8* out)   // px: 128x128, stride 128, bit 15 = opaque
{
    for (int cy = 0; cy < FPH; cy++)
        for (int cx = 0; cx < FPW; cx++)
        {
            int sum[3] = {0, 0, 0};
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++)
                {
                    u16 c = px[(cy * 8 + y) * 128 + 20 + cx * 8 + x];
                    sum[0] += c & 31; sum[1] += (c >> 5) & 31; sum[2] += (c >> 10) & 31;
                }
            for (int k = 0; k < 3; k++) out[(cy * FPW + cx) * 3 + k] = (u8)(sum[k] / 64);
        }
}

bool DecodeCardTex128(const u8* data, u32 len, std::vector<u16>& px)
{
    std::vector<u8> buf;
    if (len && data[0] == 0x10) { if (!LZ10(data, len, buf)) return false; data = buf.data(); len = (u32)buf.size(); }
    std::vector<u16> pal; const u8* img = nullptr; u32 w = 0, h = 0;
    for (u32 p = 16; p + 8 <= len;)
    {
        u32 cs = RD32(data + p + 4);
        if (!memcmp(data + p, "PALT", 4) && p + 12 <= len)
        {
            u32 n = RD32(data + p + 8);
            for (u32 k = 0; k < n && p + 12 + 2 * k + 2 <= len; k++) pal.push_back(RD16(data + p + 12 + 2 * k));
        }
        else if (!memcmp(data + p, "IMGE", 4) && p + 20 <= len)
        {
            w = RD16(data + p + 12); h = RD16(data + p + 14); img = data + p + 20;
            if (p + 20 + w * h / 16 * 6 > len) img = nullptr;
        }
        if (cs < 8) break;
        p += cs;
    }
    if (!img || w != 128 || h != 128 || pal.empty()) return false;
    px.assign(128 * 128, 0);
    const u8* tex = img; const u8* pidx = img + 32 * 32 * 4;
    auto pc = [&](u32 i) -> u16 { return i < pal.size() ? pal[i] : 0; };
    auto mix = [](u16 a, u16 b, int wa, int wb) {
        u16 r = 0;
        for (int s = 0; s < 15; s += 5) r |= (u16)(((((a >> s) & 31) * wa + ((b >> s) & 31) * wb) / (wa + wb)) << s);
        return r;
    };
    for (int bi = 0; bi < 32 * 32; bi++)
    {
        u32 word = RD32(tex + bi * 4); u16 pv = RD16(pidx + bi * 2);
        u32 base = (pv & 0x3FFF) * 2; int mode = pv >> 14;
        u16 c0 = pc(base), c1 = pc(base + 1), cols[4]; bool t3 = false;
        if (mode == 0) { cols[0] = c0; cols[1] = c1; cols[2] = pc(base + 2); t3 = true; }
        else if (mode == 1) { cols[0] = c0; cols[1] = c1; cols[2] = mix(c0, c1, 1, 1); t3 = true; }
        else if (mode == 2) { cols[0] = c0; cols[1] = c1; cols[2] = pc(base + 2); cols[3] = pc(base + 3); }
        else { cols[0] = c0; cols[1] = c1; cols[2] = mix(c0, c1, 5, 3); cols[3] = mix(c0, c1, 3, 5); }
        int bx = (bi % 32) * 4, by = (bi / 32) * 4;
        for (int t = 0; t < 16; t++)
        {
            int ci = (word >> (2 * t)) & 3;
            px[(by + t / 4) * 128 + bx + t % 4] = (ci == 3 && t3) ? 0 : ((cols[ci] & 0x7FFF) | 0x8000);
        }
    }
    return true;
}

void BuildFingerprints()
{
    FingerprintsBuilt = true;
    if (!CardROM) return;
    FileRef f;
    if (!FindFile(CardROM, CardROMLen, "Data_arc_pac/card.pac", f)) return;
    const u8* pac = CardROM + f.Start;
    const PacEntry *tx = nullptr, *idx = nullptr;
    std::vector<PacEntry> ents = ParsePac(pac, f.Size);
    for (const PacEntry& e : ents)
    {
        if (e.Name.find("tx128_idx") != std::string::npos) idx = &e;
        else if (e.Name.find("tx128") != std::string::npos) tx = &e;
    }
    if (!tx || !idx) return;
    Fingerprints.assign((size_t)NumCards * FPN, 255);
    std::vector<u16> px;
    for (u32 c = 0; c < NumCards && (c + 1) * 4 + 4 <= idx->Size; c++)
    {
        u32 a = RD32(pac + idx->Start + c * 4), b = RD32(pac + idx->Start + c * 4 + 4);
        if (b <= a || b > tx->Size) continue;
        if (DecodeCardTex128(pac + tx->Start + a, b - a, px)) Fingerprint(px.data(), &Fingerprints[(size_t)c * FPN]);
    }
}
}

void SetCardROM(const u8* rom, u32 len)
{
    std::lock_guard<std::mutex> lk(Lock);
    if (rom == CardROM) return;
    CardROM = rom; CardROMLen = len;
    FingerprintsBuilt = false; Fingerprints.clear(); BitmapCache.clear();
}

int CardFromBitmap(const u16* px)
{
    // only card-shaped bitmaps: transparent margins, opaque face
    if ((px[64 * 128 + 2] & 0x8000) || (px[64 * 128 + 125] & 0x8000) || !(px[64 * 128 + 64] & 0x8000)) return -1;
    u64 key = XXH64(px, 128 * 128 * 2, 0);
    std::lock_guard<std::mutex> lk(Lock);
    auto it = BitmapCache.find(key);
    if (it != BitmapCache.end()) return it->second;
    if (!FingerprintsBuilt) BuildFingerprints();
    int best = -1; u32 bestD = ~0u, secondD = ~0u;
    if (!Fingerprints.empty())
    {
        u8 fp[FPN];
        Fingerprint(px, fp);
        for (u32 c = 1; c < NumCards; c++)
        {
            const u8* f = &Fingerprints[(size_t)c * FPN];
            u32 d = 0;
            for (int k = 0; k < FPN && d < secondD; k++) d += (u32)std::abs((int)f[k] - (int)fp[k]);
            if (d < bestD) { secondD = bestD; bestD = d; best = (int)c; }
            else if (d < secondD) secondD = d;
        }
        // average error under 1 (of 31) per channel, and clearly better than any other card
        if (bestD > (u32)FPN || (secondD != ~0u && bestD * 3 > secondD)) best = -1;
    }
    if (getenv("WC09_DEBUG")) { printf("WC09 bitmap card -> %d (d %u, next %u)\n", best, bestD, secondD); fflush(stdout); }
    if (BitmapCache.size() > 256) BitmapCache.clear();
    BitmapCache[key] = best;
    return best;
}

std::vector<CardQuad> GetLatest2D()
{
    std::lock_guard<std::mutex> lk(Lock);
    return Latest2D;
}

void MarkHandled2D(size_t index)
{
    std::lock_guard<std::mutex> lk(Lock);
    if (index < Latest2D.size()) Latest2D[index].Handled = true;
}

void OnPolygon(GPU& gpu, u32 texParam, const Polygon* poly, int nverts)
{
    if (!Active) return;
    if (((texParam >> 26) & 7) != 5) return;               // tex4x4 only
    u32 w = 8 << ((texParam >> 20) & 7), h = 8 << ((texParam >> 23) & 7);
    if (w != h || (w != 128 && w != 64 && w != 32)) return;
    if (nverts != 4) return;                                 // v1: unclipped quads only

    u32 addr = (texParam & 0xFFFF) << 3;
    u32 n = (w / 4) * (h / 4) * 4;                          // texel words (slot data)

    // hash at most once per texture slot per 3D frame
    SlotCache& c = Cache[(addr >> 8) & 63];
    u64 hash;
    if (c.Frame == FrameCounter && c.TexParam == (texParam & 0x03FFFFFF))
        hash = c.Hash;
    else
    {
        static u8 buf[128 * 128 / 4];
        for (u32 i = 0; i < n; i += 4)
            *(u32*)&buf[i] = gpu.ReadVRAM_Texture<u32>((addr + i) & 0x7FFFF);
        hash = XXH64(buf, n, 0);
        c = {texParam & 0x03FFFFFF, hash, FrameCounter};
    }

    const TexHashEntry* e = Find(hash, w);
    if (!e) return;

    CardQuad q{};
    q.Card = e->Card;
    q.Passcode = PasscodeOf(e->Card);
    q.TexSize = (u8)w;
    for (int i = 0; i < 4; i++)
    {
        const Vertex* v = poly->Vertices[i];
        q.X[i] = (s16)v->FinalPosition[0];
        q.Y[i] = (s16)v->FinalPosition[1];
        float s = v->TexCoords[0] / 16.f, t = v->TexCoords[1] / 16.f;   // texels
        if (w == 32)
        {
            q.U[i] = ArtU0 + (s - 1.f) / 30.f * (ArtU1 - ArtU0);
            q.V[i] = ArtV0 + (t - 4.f) / 22.f * (ArtV1 - ArtV0);
        }
        else
        {
            q.U[i] = (s / w - FaceS0) / (FaceS1 - FaceS0);
            q.V[i] = t / h;
        }
    }
    q.Screen = gpu.ScreenSwap ? 0 : 1;   // engine A (3D) goes to the top screen when swapped
    if (w == 32) { q.ClipU0 = ArtU0; q.ClipV0 = ArtV0; q.ClipU1 = ArtU1; q.ClipV1 = ArtV1; }
    else         { q.ClipU0 = 0.f;   q.ClipV0 = 0.f;   q.ClipU1 = 1.f;   q.ClipV1 = 1.f; }
    Building.push_back(q);
}

void OnFlush(bool screenSwap)
{
    FrameCounter++;
    if (!Active && Building.empty()) return;
    std::lock_guard<std::mutex> lk(Lock);
    Latest.On3DTop = screenSwap;
    Latest.Quads.swap(Building);
    Building.clear();
}

FrameCards GetLatest(bool all)
{
    std::lock_guard<std::mutex> lk(Lock);
    FrameCards out;
    out.On3DTop = Latest.On3DTop;
    // stamps are frame+1 so 0 means never; "recent" = within the last few VBlanks
    u64 now = FrameNum();
    bool gl3d = Cards3DStamp.load() && now + 1 - Cards3DStamp.load() < 4;
    bool gl2d = Cards2DStamp.load() && now + 1 - Cards2DStamp.load() < 4;
    if (all) gl3d = gl2d = false;
    if (!gl3d)
        for (const CardQuad& q : Latest.Quads)
            if (!BattleOnScreen(q.Screen)) out.Quads.push_back(q);
    {
        for (const CardQuad& q : Latest2D)
            if (!gl2d || !q.Handled) out.Quads.push_back(q);
    }
    return out;
}

// ---- 2D: card faces drawn as an 11x16-tile 8bpp text background -----------------------------

namespace
{
constexpr int CardTW = 11, CardTH = 16;
constexpr u32 CardCharBytes = CardTW * CardTH * 64;

u16 ReadBG(GPU& gpu, int engine, u32 addr)
{
    return engine == 0 ? gpu.ReadVRAM_ABG<u16>(addr) : gpu.ReadVRAM_BBG<u16>(addr);
}

}

void OnVBlank2D(GPU& gpu)
{
    if (!Active) return;
    std::vector<CardQuad> found;

    for (int engine = 0; engine < 2; engine++)
    {
        const GPU2D& u = engine == 0 ? gpu.GPU2D_A : gpu.GPU2D_B;
        u32 dispcnt = u.DispCnt;
        if (((dispcnt >> 16) & 3) != 1) continue;          // not normal BG display
        u32 bgmode = dispcnt & 7;
        int screen = (engine == 0) == gpu.ScreenSwap ? 0 : 1;

        for (int bg = 0; bg < 4; bg++)
        {
            if (!(dispcnt & (1 << (8 + bg)))) continue;     // layer off
            if (engine == 0 && bg == 0 && (dispcnt & (1 << 3))) continue;   // BG0 = 3D
            bool text = bg < 2 || (bg == 2 && bgmode <= 1) || (bg == 3 && bgmode == 0);
            if (!text) continue;
            u16 cnt = u.BGCnt[bg];
            if (!(cnt & (1 << 7))) continue;                 // card art is 256-color

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

            for (int y = 0; y < mapH; y++)
            {
                for (int x = 0; x < mapW; x++)
                {
                    u16 e0 = entry(x, y);
                    u16 t0 = e0 & 0x3FF;
                    if (e0 & 0xC00) continue;                                   // no flips
                    if ((entry(x + CardTW - 1, y) & 0x3FF) != t0 + CardTW - 1) continue;
                    if ((entry(x, y + 1) & 0x3FF) != t0 + CardTW) continue;
                    if ((entry(x + CardTW - 1, y + CardTH - 1) & 0x3FF) != t0 + CardTW * CardTH - 1) continue;
                    if ((entry(x + 5, y + 8) & 0x3FF) != t0 + 8 * CardTW + 5) continue;

                    u32 charAddr = charBase + t0 * 64;
                    static u8 buf[CardCharBytes];
                    for (u32 i = 0; i < CardCharBytes; i += 2)
                        *(u16*)&buf[i] = ReadBG(gpu, engine, charAddr + i);
                    u64 hash = XXH64(buf, CardCharBytes, 0);
                    const TexHashEntry* hit = Find(hash, 1);
                    if (!hit) continue;

                    int sx = (x * 8 - (u.BGXPos[bg] & 0x1FF)) & ((mapW * 8) - 1);
                    int sy = (y * 8 - (u.BGYPos[bg] & 0x1FF)) & ((mapH * 8) - 1);
                    if (sx >= 256) sx -= mapW * 8;
                    if (sy >= 192) sy -= mapH * 8;

                    CardQuad q{};
                    q.Card = hit->Card; q.Passcode = PasscodeOf(hit->Card);
                    q.TexSize = 1; q.Screen = (u8)screen; q.Engine = (s8)engine; q.BG = (s8)bg;
                    const int px[4] = {0, 0, CardTW * 8, CardTW * 8}, py[4] = {0, CardTH * 8, CardTH * 8, 0};
                    for (int k = 0; k < 4; k++)
                    {
                        q.X[k] = (s16)(sx + px[k]); q.Y[k] = (s16)(sy + py[k]);
                        q.U[k] = px[k] ? 1.f : 0.f; q.V[k] = py[k] ? 1.f : 0.f;
                    }
                    q.ClipU0 = q.ClipV0 = 0.f; q.ClipU1 = q.ClipV1 = 1.f;
                    q.MapX = (s16)(x * 8); q.MapY = (s16)(y * 8);
                    found.push_back(q);
                    x += CardTW - 1;
                }
            }
        }
    }

    std::lock_guard<std::mutex> lk(Lock);
    Latest2D.swap(found);
}

}
