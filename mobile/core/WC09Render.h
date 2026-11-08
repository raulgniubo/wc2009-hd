// HD images for the WC09 core: card art / HD pictures from the game's HD pack folder, and the
// text panels (card text, stats, deck text, life points) drawn with canvas_ity instead of Qt.
#pragma once
#include <string>
#include <vector>
#include "types.h"

namespace wc09m
{
// folder = the HD pack (cards/, hd/, redesign/, data/; "" = none); fontDir holds Inter-Medium.ttf /
// Inter-Bold.ttf
void SetupHDProvider(const std::string& folder, const std::string& fontDir);
// an HD pack for this game next to the ROM: a folder with a pack.json whose "game" is the start of
// the game code (e.g. "CY8"); "" if none
std::string FindPack(const std::string& romDir, const char* gameCode);
void SetPanelScale(int scale);   // the renderer scale (2-8): HD panels are drawn at it
}
