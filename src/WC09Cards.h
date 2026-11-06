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
    s8 Engine = -1, BG = -1;   // for 2D cards: which 2D engine / background shows it
    s16 MapX = 0, MapY = 0;    // for 2D cards: top-left in BG pixel space
    bool Handled = false;      // for 2D cards: the GL compositor swaps this one itself
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
FrameCards GetLatest(bool all = false);   // all: include cards the GL renderers draw themselves

// Master switch for drawing HD cards (the frontend checks it). On by default.
void SetHDCardsEnabled(bool on);
bool GetHDCardsEnabled();

// Redraw the duel card-detail name/description in a real font. On by default.
void SetHDTextEnabled(bool on);
bool GetHDTextEnabled();

// Smooth (xBR) upscaling of 2D layers in the OpenGL renderer. On by default.
void SetSmooth2D(bool on);
bool GetSmooth2D();

// Card index -> passcode (0 if none), or 0 when out of range.
u32 PasscodeOf(u16 card);

// Card shown by a tex4x4 texture whose texel words hash to `hash` (size 128/64/32, or 1 for the
// 2D 88x128 tiles), or -1. Card 0 is the card back.
int CardFromTexHash(u64 hash, u32 size);

// HD face image of a card (HDCards/<passcode>, card_<index>, or cardback for card 0).
// Straight RGBA8. Uses the frontend's image provider.
bool LoadCardImage(int card, std::vector<u8>& rgba, int& w, int& h);

// The OpenGL renderers draw HD cards themselves (inside the 3D scene / the 2D compositor),
// so layering, transparency and effects stay correct. When they report it, GetLatest() leaves
// those cards out and the frontend overlay only draws what nobody else handled.
void NoteCards3DRendered();
void NoteCards2DRendered();

// Card shown by a 128x128 direct-color bitmap laid out like the 128px card texture (face at
// x 20..108), as the story uses for big cards on screen; -1 if none matches. `px` has a
// stride of 128 pixels. Matching is approximate (the game's own decoder differs slightly).
int CardFromBitmap(const u16* px);
void SetCardROM(const u8* rom, u32 len);

// 2D cards found at the last VBlank, and marking one as swapped by the compositor.
std::vector<CardQuad> GetLatest2D();
void MarkHandled2D(size_t index);

}
}
