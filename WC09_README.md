# wc2009-hd

A personal melonDS fork that makes **Yu-Gi-Oh! 5D's World Championship 2009: Stardust Accelerator**
(US, game code `CY8E`) look much better on Mac and Windows. The emulated DS runs unchanged;
the fork recognizes what the game draws and swaps in HD versions.

## What it does
| Feature | How | Toggle (View menu) |
| --- | --- | --- |
| HD cards everywhere (deck editor, duel card view, hand, field) | Recognizes each card texture / tile set, draws your HD card image in its place | HD cards |
| HD battle images (big monster pictures during attacks) | Recognizes the battle image on its background layer, the 2D compositor samples an HD version | HD cards |
| HD card text in the duel card view | Name + full description redrawn in a real font, whole text fits without scrolling | HD card text |
| Smooth 2D graphics (menus, portraits, UI) | xBR-style edge-directed upscaling of 2D layers and sprites | Smooth 2D graphics |
| Sharp 3D | OpenGL renderer at 4x by default | Config > Video |

Defaults are set for the best look: OpenGL renderer at 4x, OpenGL display, screens side by side.

## Use it
1. Download the latest macOS build: **Actions > macOS (Apple Silicon) > latest run > Artifacts**.
2. Unzip, then right-click `melonDS.app` > Open the first time (ad-hoc signed).
3. Put your ROM in a folder with an `HDCards` folder next to it:
   - `bash get_hd_cards.sh` fills it with the full card images (by passcode, from YGOPRODeck).
   - Unzip the HD pack (battle images + game-only cards) into the same folder.

Layout of `HDCards/`:
- `<passcode>.jpg` - card faces; `card_<index>.jpg` for game-only cards without a passcode.
- `BattleMonsterR1/<id>_r.png`, `BattleMonsterR2/...`, `BattleMonsterL1/...`, `BattleMonsterL2/...` -
  battle images. Left-facing images that are just mirrors fall back to the right-facing file.
- Anything missing simply shows the original graphics.

## Layout of the code
- `src/WC09Cards.*`: identifies card textures (XXH64 over texel data, table in `WC09CardTable.h`)
  and 2D card faces; records each card's screen quad.
- `src/WC09Battle.*`: indexes the ROM's background image sets at load, recognizes them per frame,
  card text from the ROM's card database.
- `src/GPU2D_OpenGL.cpp`, `OpenGL_shaders/2DCompositorFS.glsl`, `2DSpriteFS.glsl`: HD background
  sampling, text panel, xBR smoothing.
- `src/frontend/qt_sdl/WC09Overlay.*`, `Screen.cpp`: HD card overlay, text panel rendering, image loading.
- `wc09/tools`: ROM research tools; `wc09/research/FINDINGS.md`: verified facts about the game's assets.

## Known gaps
- Duelist portraits and the large character figures are sprites; they get the smoothing filter,
  not real HD replacements.
- The card in the story intro isn't one of the 2,934 card images.
- HD battle images and HD text need the OpenGL renderer (the default). With the software renderer
  you still get HD cards and the text panel as an overlay.
- US ROM only.

No ROMs, game files or card artwork are included in this repository.
