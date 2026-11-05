#include "WC09Cards.h"
#include "WC09CardTable.h"
#include "GPU.h"
#include "GPU3D.h"

#include <algorithm>
#include <atomic>

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

void SetActive(bool active) { Active = active; }
bool IsActive() { return Active; }

u32 PasscodeOf(u16 card) { return card < NumCards ? Passcodes[card] : 0; }

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

FrameCards GetLatest()
{
    std::lock_guard<std::mutex> lk(Lock);
    FrameCards out = Latest;
    out.Quads.insert(out.Quads.end(), Latest2D.begin(), Latest2D.end());
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
                    q.TexSize = 1; q.Screen = (u8)screen;
                    const int px[4] = {0, 0, CardTW * 8, CardTW * 8}, py[4] = {0, CardTH * 8, CardTH * 8, 0};
                    for (int k = 0; k < 4; k++)
                    {
                        q.X[k] = (s16)(sx + px[k]); q.Y[k] = (s16)(sy + py[k]);
                        q.U[k] = px[k] ? 1.f : 0.f; q.V[k] = py[k] ? 1.f : 0.f;
                    }
                    q.ClipU0 = q.ClipV0 = 0.f; q.ClipU1 = q.ClipV1 = 1.f;
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
