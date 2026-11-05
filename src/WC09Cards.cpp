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

// card face inside the 128px texture: texels 20.5 .. 107.5 horizontally, full height
constexpr float FaceS0 = 20.5f / 128.f, FaceS1 = 107.5f / 128.f;

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
        float s = (v->TexCoords[0] / 16.f) / w, t = (v->TexCoords[1] / 16.f) / h;
        q.U[i] = (s - FaceS0) / (FaceS1 - FaceS0);
        q.V[i] = t;
    }
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
    return Latest;
}

}
