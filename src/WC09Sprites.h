/*
    WC09 HD sprites: duelist portraits, the big character figures, the player's avatar parts,
    menu objects and booster packs. These are drawn as OBJ sprites.

    At ROM load we index every tile of those sprite sets (Konami OBJD images and Nitro
    NCGR/NCER cells) by content hash. When the OpenGL renderer sets up a sprite, it asks which
    image the sprite's tiles come from and where; the sprite shader then samples the HD version
    of that image instead of the original pixels.
*/
#pragma once

#include <functional>
#include <string>
#include "types.h"

namespace melonDS::WC09
{

struct SpriteHD
{
    int Image = -1;          // sprite image id (>= kSpriteImageBase), -1 = none
    s16 OX = 0, OY = 0;      // the matched OAM's top-left inside the image (pixels)
    s16 OW = 0, OH = 0;      // the matched OAM's size
    s16 DX = 0, DY = 0;      // runtime sprite tile-space offset into the matched OAM
    s16 IW = 0, IH = 0;      // full image size (pixels, original resolution)
    bool HF = false, VF = false;   // the matched OAM is drawn flipped inside the image
    bool Text = false;       // not an HD image, but anti-aliased text: drawn with smooth edges
};

// True if a sprite's pixels look like anti-aliased text: the colours it uses form one
// brightness ramp (monotone in palette order) with several steps. pal = 16 colours (4-bit) or
// 256 colours (8-bit) of the sprite's palette, BGR555.
bool IsTextSprite(int width, int height, bool bpp8, const u8* tiles, const u16* pal);

constexpr int kSpriteImageBase = 2000000;
// sprite image ids from here on are card thumbnails: the HD card face (id - base = card)
constexpr int kCardThumbBase = 5000000;

void BuildSpriteIndex(const u8* rom, u32 len);

// Identify a tiled sprite. readTile(offset, bytes, out) copies OBJ VRAM bytes.
SpriteHD IdentifySprite(int width, int height, bool bpp8, u32 tileOffset,
                        const std::function<void(u32, u32, u8*)>& readVRAM);

// Card shown by a 32x32 8-bit 3D texture holding a card thumbnail (lists), -1 if none.
int CardFromThumbTexture(u64 hash);

// "<set>/<name>" for the HD loader (HDCards/<set>/<name>.png).
std::string SpriteImageName(int image);

}
