/*
    WC09 HD cards: identifies Yu-Gi-Oh! WC2009 card textures as the game draws them,
    and records where each card lands on screen, so a frontend can draw HD art on top.

    The core never changes what the DS renders. It only observes polygons.
*/
#pragma once

#include <mutex>
#include <vector>
#include "types.h"

namespace melonDS
{
class GPU;
struct Polygon;

namespace WC09
{

struct CardQuad
{
    u16 Card;          // index into card.pac / WC09CardTable (0 = card back)
    u32 Passcode;      // 0 for game-only cards
    s16 X[4], Y[4];    // DS screen pixels (0..255, 0..191) of the 3D screen
    float U[4], V[4];  // 0..1 across the card face (not the whole texture); may extend past 0..1
    float ClipU0, ClipV0, ClipU1, ClipV1;  // only draw the part of the face inside this rect
    u8 TexSize;        // 128, 64 (full card face), 32 (artwork only), 1 (2D BG card, 88x128)
    u8 Screen;         // 0 = top, 1 = bottom
};

struct FrameCards
{
    bool On3DTop = true;            // which physical screen shows the 3D layer
    std::vector<CardQuad> Quads;    // 3D cards in draw order, then 2D cards
};

// Turned on by the frontend when the loaded ROM is WC2009 US (game code CY8E).
void SetActive(bool active);
bool IsActive();

// Called by GPU3D for every submitted polygon (cheap early-out when inactive).
void OnPolygon(GPU& gpu, u32 texParam, const Polygon* poly, int nverts);

// Called by GPU3D when a polygon list gets flushed (one 3D frame done).
void OnFlush(bool screenSwap);

// Called by GPU at VBlank: finds cards drawn as 2D background tiles (e.g. the duel's
// card detail view on the top screen).
void OnVBlank2D(GPU& gpu);

// Frontend: copy of the most recent completed 3D frame's card quads.
FrameCards GetLatest();

// Card index -> passcode (0 if none), or 0 when out of range.
u32 PasscodeOf(u16 card);

}
}
