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
| HD menus and screens (deck editor, main menu, shop, records, setup screens...) | Every 8-bit background image in the ROM is indexed at load; when one shows up, the 2D compositor samples an AI-upscaled version | HD cards |
| HD duelist portraits, big character figures, booster packs | Sprite tiles are indexed at load (Konami OBJD and Nitro cell sets); the sprite shader samples the upscaled image | HD cards |
| HD 3D textures (duel field, buttons, mini monsters on the field, overworld models, effects) | Each texture the game uploads is hashed by its decoded pixels; if `HDCards/tex3d/<hash>.png` exists, the 3D renderer samples it instead | HD cards |
| HD overworld backgrounds and map | The overworld's Nitro background screens (RPG2D_2) are indexed like the menus | HD cards |
| Smooth 2D graphics (everything else 2D) | xBR-style edge-directed upscaling of 2D layers and sprites | Smooth 2D graphics |
| Sharp 3D | OpenGL renderer at 4x by default | Config > Video |

Defaults are set for the best look: OpenGL renderer at 4x, OpenGL display, screens side by side.

## Use it
1. Download the latest macOS build: **Actions > macOS (Apple Silicon) > latest run > Artifacts**.
2. Unzip, then right-click `melonDS.app` > Open the first time (ad-hoc signed).
3. Put your ROM in a folder with an `HDCards` folder next to it:
   - `bash get_hd_cards.sh` fills it with the full card images (by passcode, from YGOPRODeck).
   - Unzip the HD pack (battle images, menus, portraits, game-only cards) into the same folder.

Layout of `HDCards/`:
- `<passcode>.jpg` - card faces; `card_<index>.jpg` for game-only cards without a passcode.
- `BattleMonsterR1/<id>_r.png`, `BattleMonsterR2/...`, `BattleMonsterL1/...`, `BattleMonsterL2/...` -
  battle images. Left-facing images that are just mirrors fall back to the right-facing file.
- `tex3d/<hash>.png` for 3D textures (hash = XXH64 of the decoded texels; `WC09_DUMP3D=<dir>` dumps them).
- `<set>/<name>.png` for menus and sprites, e.g. `DCustom/dcus_menu_bg01.png`, `BustupR/bu001_r_c0.png`.
- Anything missing simply shows the original graphics.

## Layout of the code
- `src/WC09Cards.*`: identifies card textures (XXH64 over texel data, table in `WC09CardTable.h`)
  and 2D card faces; records each card's screen quad.
- `src/WC09Battle.*`: indexes the ROM's background image sets at load, recognizes them per frame,
  card text from the ROM's card database.
- `src/WC09Sprites.*`: sprite tile index and per-sprite identification. `src/WC09Rom.h`: ROM/.pac/LZ helpers.
- `src/GPU2D_OpenGL.cpp`, `OpenGL_shaders/2DCompositorFS.glsl`, `2DSpriteFS.glsl`: HD background
  sampling, text panel, xBR smoothing.
- `src/frontend/qt_sdl/WC09Overlay.*`, `Screen.cpp`: HD card overlay, text panel rendering, image loading.
- `wc09/tools`: ROM research tools; `wc09/research/FINDINGS.md`: verified facts about the game's assets.

## Known gaps
- The player's own avatar (face/hair parts) is recolored at runtime, so it keeps the original pixels
  with smoothing.
- Text drawn by the game into tiles (dialog, menu labels) keeps the original font with smoothing.
- 4-bit menu layers are only replaced when the visible layer exactly matches the stored image, so
  windows the game writes text into keep their original pixels.
- The card in the story intro isn't one of the 2,934 card images.
- HD battle images and HD text need the OpenGL renderer (the default). With the software renderer
  you still get HD cards and the text panel as an overlay.
- US ROM only.

No ROMs, game files or card artwork are included in this repository.
