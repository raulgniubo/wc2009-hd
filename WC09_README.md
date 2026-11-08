# wc2009-hd

A personal melonDS fork that makes **Yu-Gi-Oh! 5D's World Championship 2009: Stardust Accelerator**
(US, game code `CY8E`) look much better on Mac and Windows. The emulated DS runs unchanged;
the fork recognizes what the game draws and swaps in HD versions.

## What it does
| Feature | How | Toggle (View menu) |
| --- | --- | --- |
| HD cards everywhere (deck editor, duel card view, hand, field, story intro) | Recognizes each card texture / tile set / bitmap, and the renderer itself samples your HD card image in its place, so dialogs, monsters and effects drawn over a card stay on top | HD cards |
| HD battle images (big monster pictures during attacks) | Recognizes the battle image on its background layer, the 2D compositor samples an HD version | HD cards |
| HD card text in the duel card view | Name + full description redrawn in a real font, whole text fits without scrolling | HD card text |
| HD menus and screens (deck editor, main menu, shop, records, setup screens...) | Every 8-bit background image in the ROM is indexed at load; when one shows up, the 2D compositor samples an AI-upscaled version | HD cards |
| HD duelist portraits, big character figures, booster packs | Sprite tiles are indexed at load (Konami OBJD and Nitro cell sets); the sprite shader samples the upscaled image | HD cards |
| HD 3D textures (duel field, buttons, mini monsters on the field, overworld models, effects) | Each texture the game uploads is hashed by its decoded pixels; if `HDCards/tex3d/<hash>.png` exists, the 3D renderer samples it instead | HD cards |
| HD overworld backgrounds and map | The overworld's Nitro background screens (RPG2D_2) are indexed like the menus | HD cards |
| Modern duel UI (phase bar, buttons, action icons, sword, highlights, alerts, list windows, rock-paper-scissors screen) | Redesigned pictures keyed by texture structure (any colour state) in `HDCards/tex3d/s_<hash>[_<hue><level>].png`, sprites in `HDCards/duel2d/`, `HDCards/janken/` | HD cards |
| Sharp game text (alerts, menus, speech bubbles, deck lists) | Text layers and text sprites are detected by their anti-aliasing colour ramps and redrawn with cubic interpolation | HD card text |
| Smooth 2D graphics (everything else 2D) | xBR-style edge-directed upscaling of 2D layers and sprites | Smooth 2D graphics |
| Sharp 3D | OpenGL renderer at 4x by default | Config > Video |

## The apps
Dual HD runs on Mac, Windows, iPhone/iPad and Android, all on the same emulator core
(`mobile/core`, OpenGL ES on phones, desktop OpenGL through glad on computers):
- **Mac / Windows**: `desktop/` (Qt 6 + SDL 2). Builds: Actions > macOS / Windows > Artifacts.
- **iPhone / iPad**: `mobile/ios`. **Android**: `mobile/android`.

Each game with an HD pack offers four looks: **HD** (everything in high resolution, original look),
**HD + Redesign**, **HD Cards** and **Original**.

## The HD pack
A folder next to the ROM with a `pack.json` (`"game": "CY8"`):
- `cards/` - card faces: `<passcode>.jpg`, `card_<index>.jpg` for game-only cards, `cardback.png`.
- `hd/` - HD pictures that keep the original look: `BattleMonsterR1/<id>_r.png` (left-facing ones
  fall back to the mirrored right-facing file), `tex3d/<hash>.png` (XXH64 of the decoded texels),
  `<set>/<name>.png` for menus and sprites.
- `redesign/` - the modern redesign, used only in "HD + Redesign" (it wins over `hd/` there):
  `tex3d/s_<hash>[_<hue><level>].png` keyed by texture structure, `duel2d/`, `janken/`, `tiles/`.
- `data/cardtable.bin` - which texture is which card (made from the ROM by `wc09/tools`).
- Anything missing simply shows the original graphics. `wc09/pack/make_pack.py` turns an older
  `HDCards` folder into this layout.

## Layout of the code
- `src/WC09Cards.*`: identifies card textures (XXH64 over texel data, table from the pack's `data/cardtable.bin`)
  and 2D card faces; records each card's screen quad.
- `src/WC09Battle.*`: indexes the ROM's background image sets at load, recognizes them per frame,
  card text from the ROM's card database.
- `src/WC09Sprites.*`: sprite tile index and per-sprite identification. `src/WC09Rom.h`: ROM/.pac/LZ helpers.
- `src/GPU2D_OpenGL.cpp`, `OpenGL_shaders/2DCompositorFS.glsl`, `2DSpriteFS.glsl`: HD background
  sampling, text panel, xBR smoothing.
- `mobile/core`: the shared emulator core and its C API (`wc09_core.h`), HD pack loading, text panels.
- `desktop/`: Dual HD for Mac and Windows. `mobile/ios`, `mobile/android`: the phone apps.
- `wc09/tools`: ROM research tools; `wc09/research/FINDINGS.md`: verified facts about the game's assets.

## Known gaps
- The player's own avatar (face/hair parts) is recolored at runtime, so it keeps the original pixels
  with smoothing.
- Text drawn by the game into tiles (dialog, menu labels) keeps the original font with smoothing.
- 4-bit menu layers are only replaced when the visible layer exactly matches the stored image, so
  windows the game writes text into keep their original pixels.
- HD battle images and HD text need the OpenGL renderer (the default). With the software renderer
  you still get HD cards and the text panel as an overlay.
- US ROM only.

No ROMs, game files or card artwork are included in this repository.
