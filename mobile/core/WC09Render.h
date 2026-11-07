// HD images for the WC09 core on mobile: card art / UI images from the HDCards folder, and the
// text panels (card text, stats, deck text, life points) drawn with canvas_ity instead of Qt.
#pragma once
#include <string>
#include <vector>
#include "types.h"

namespace wc09m
{
// folder = the HDCards folder; fontDir holds Inter-Medium.ttf / Inter-Bold.ttf
void SetupHDProvider(const std::string& folder, const std::string& fontDir);
}
