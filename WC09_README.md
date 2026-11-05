# wc2009-hd

A personal melonDS fork that draws HD card images over the cards in
**Yu-Gi-Oh! 5D's World Championship 2009: Stardust Accelerator** (US, game code `CY8E`).

The emulated DS still renders normally. The fork only watches which card textures
the game draws and where. The window then draws your HD image of that card on top,
at full window resolution, with correct perspective.

## Use it
1. Download the latest macOS build from **Actions → macOS (Apple Silicon) → latest run → Artifacts**.
2. Unzip it. The first time, right-click `melonDS.app` → Open (the build is ad-hoc signed).
3. Next to your ROM, create a folder named `HDCards` and put card images in it:
   - `HDCards/<passcode>.jpg` (or .png/.webp). Example: `70902743.jpg` for Red Dragon Archfiend.
   - Game-only cards without a passcode: `HDCards/card_<index>.png`.
   - Each image is the **full card face**, portrait, any resolution (capped at 1024 px tall when loaded).
4. For sharp 3D as well: Config → Video → OpenGL renderer, internal resolution 4x or higher.

Cards without an image in `HDCards` show the original art.

## Layout
- `src/WC09Cards.*`: core hook. It identifies card textures with XXH64 over the texel data (table in `WC09CardTable.h`) and records each card's screen quad and texture coords per 3D frame.
- `src/frontend/qt_sdl/WC09Overlay.*`: HD image loading/cache and projective UVs.
- `src/frontend/qt_sdl/Screen.cpp`: draws the overlay in both the OpenGL and the software (QPainter) display paths.
- `wc09/tools`: ROM research tools (pac/card.pac decoders, headless harness, table generator).
- `wc09/research/FINDINGS.md`: what's verified about the game's card assets.

## Status (v0.2)
Working (tested live on the real build, both the OpenGL and the standard display):
- Deck editor card view (3D, 128 px card texture).
- Duels: the card detail on the top screen (2D background tiles), the raised card in your hand
  (3D, 64 px), cards in your hand and on the field (3D, 32 px artwork, drawn in perspective).

Not yet:
- The card in the story intro (it is not one of the 2,934 card images).
- Both screens showing 3D at once (display capture).
- Sprites the game draws on top of the 2D card (stars, attribute icon) get covered by the HD card,
  which has its own.
- US ROM only.

## Getting HD images
`wc09/get_hd_cards.sh` downloads every card with a passcode (2,796 cards) from YGOPRODeck
into `HDCards/`, once. Run it next to your ROM: `bash get_hd_cards.sh`.

No ROMs, game files or card artwork are included. Bring your own.
