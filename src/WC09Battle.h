/*
    WC09 HD battle images: the big cut-out monster pictures shown during battles.

    The game draws them as 256-color text backgrounds. At load we index every battle image
    in the ROM (Data_arc_pac/duel2d/BattleMonster{L,R}{1,2}.pac). Each VBlank we look at the
    tiles a background shows, recognize which image it is and where it sits, and the OpenGL
    2D compositor then samples an HD version of that image in place of the original pixels.
    Because the swap happens inside the compositor, layer priority, blending and sprites drawn
    on top all stay correct.
*/
#pragma once

#include <functional>
#include <string>
#include <vector>
#include "types.h"

namespace melonDS
{
class GPU;
class NDS;

namespace WC09
{

struct BGReplacement
{
    int Image = -1;        // index into the battle image list, -1 = none
    float OriginX = 0, OriginY = 0;   // top-left of the image in BG pixel space
    float Width = 0, Height = 0;      // image size in BG pixels
    bool Mirror = false;              // drawn horizontally flipped
    bool ScreenSpace = false;         // rect is in screen pixels (fixed on screen, ignores BG scroll)
    bool Fallback = false;            // transparent HD pixels show the original pixels (card sets)
};

// Duel card-detail text panel area on the top screen (DS pixels).
constexpr int kPanelX = 40, kPanelY = 1, kPanelW = 213, kPanelH = 137;

// Image ids at or above this are text panels: (id - kTextPanelBase) / 2 = card index,
// odd ids are the short version (when a speech bubble shares the text layer).
constexpr int kTextPanelBase = 1000000;
constexpr int kPanelShortH = 123;

// Duel card view with ATK/DEF redrawn: ids from here (see GetStatPanel). The numbers sit at
// rows kStatsY..+kStatsH under the card.
constexpr int kStatPanelBase = 1600000;
constexpr int kStatsY = 137, kStatsH = 12;
bool GetStatPanel(int image, int& card, int& atk, int& def, bool& bubble);

// Sprites hidden because the HD card already shows them (the level badge on the card view).
struct RectHide { bool On = false; int X0 = 0, Y0 = 0, X1 = 0, Y1 = 0; };
bool HideSprite(int engine, int x, int y, int w, int h);

// Deck editor / card lists: HD description (id = base + card*2) and name (+1) on the top screen.
constexpr int kDeckTextBase = 1500000;
constexpr int kDeckDescX = 101, kDeckDescY = 25, kDeckDescW = 151, kDeckDescH = 148;
constexpr int kDeckNameX = 0, kDeckNameY = 1, kDeckNameW = 242, kDeckNameH = 21;

// Image ids at or above this are 2D card faces: id - kCardImageBase = card index.
constexpr int kCardImageBase = 5000000;
inline bool IsCardImage(int image) { return image >= kCardImageBase && image < 7000000; }

// Image ids at or above this are several 2D cards on one background, composed into one image.
constexpr int kCardSetBase = 7000000;
inline bool IsCardSet(int image) { return image >= kCardSetBase; }
struct CardSetItem { int Card; int X, Y; };   // position inside the set's rect, in BG pixels
bool GetCardSet(int image, std::vector<CardSetItem>& items, int& w, int& h);

// The renderer reports that it drew the text panel itself this frame (so the frontend
// doesn't draw its own on top).
void NotePanelRendered();
u64 FrameNum();
bool PanelRenderedRecently();

// Called at VBlank (after OnVBlank2D). Builds the index on first use.
void OnVBlankBattle(GPU& gpu);

// Replacement for engine (0 = A, 1 = B) and BG (0..3) as of the last VBlank.
BGReplacement GetBGReplacement(int engine, int bg);

// True if a battle image is being replaced on that physical screen (0 = top, 1 = bottom).
// Card overlays are skipped there: in battles the game draws the monster cut-out over the
// card, and an overlay on top would hide it.
bool BattleOnScreen(int screen);

// Name of an image for the HD loader, e.g. "BattleMonsterR2/7736_r" (HDCards/<name>.png),
// or "textpanel/<card>" for a generated card text panel.
std::string BattleImageName(int image);

// Card name / description (English) from the ROM's card database, or "" if unknown.
std::string CardNameEn(int card);
std::string CardDescEn(int card);

// HD image provider (set by the frontend): name -> straight RGBA8 pixels.
using HDImageProvider = std::function<bool(const std::string& name, std::vector<u8>& rgba, int& w, int& h)>;
void SetHDImageProvider(HDImageProvider provider);
bool LoadHDImage(const std::string& name, std::vector<u8>& rgba, int& w, int& h);

}
}
