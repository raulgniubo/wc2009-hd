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
};

constexpr int kSpriteImageBase = 2000000;

void BuildSpriteIndex(const u8* rom, u32 len);

// Identify a tiled sprite. readTile(offset, bytes, out) copies OBJ VRAM bytes.
SpriteHD IdentifySprite(int width, int height, bool bpp8, u32 tileOffset,
                        const std::function<void(u32, u32, u8*)>& readVRAM);

// "<set>/<name>" for the HD loader (HDCards/<set>/<name>.png).
std::string SpriteImageName(int image);

}
