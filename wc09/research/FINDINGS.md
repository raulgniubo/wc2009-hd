# WC2009 (US, CY8E) – Findings

Game code `CY8E`, US ROM. All findings below are verified from the ROM itself.

## File system
- Assets live in `Data_arc_pac/*.pac`, a Konami container format:
  - Name sectors are 0x200 bytes each. The first 3 bytes give the table offset: `u16 + u8*0x200`.
  - Each name record is `[len][?][name]`.
  - The table holds 8 header bytes, then N pairs of `(u32 start, u32 size)`. Starts are relative to `align(table_end, 0x200)`.
  - Parser: `tools/pac.py`.

## Card images: `card.pac` (74 MB)

| File | Contents |
| --- | --- |
| card_5bg128.bin | 2,934 × 12,184-byte records, uncompressed `NTBG` (PALT 256 colors + BGDT). 88×128 full card face, 8bpp 8×8 tiles + map. 2D layer. |
| card_5bg26.bin + _idx | 26 px icons, LZ10-compressed `NTBG` with OBJD (sprite) data |
| card_5tx128.bin + _idx | 128×128 3D texture, format 5 (tex4x4), LZ10-compressed `NTTX` (PALT + IMGE). Full card face, level stars baked in. |
| card_5tx64.bin | 64×64 tex4x4, fixed 2,096 bytes/card |
| card_5tx32.bin | 32×32 tex4x4, fixed 688 bytes/card |
| card_5tx16.bin | 16×16, variable size |

- Index 0 is the card back. Every set uses the same card numbering.
- `*_idx.bin`: 2,935 u32 offsets (2,934 cards + end). Real data starts 0x200 after the table value.
- tx128, tx64 and tx32 texel blocks are each unique per card (XXH64 → index table in `texhash_us.tsv`).

## Card database: `bin2.pac`
- `card_indx_e.bin`: per card, `(name_off, desc_off)` into `card_name_e.bin` / `card_desc_e.bin` (NUL-terminated).
- `card_pass.bin`: u32 passcode per card. 2,796 nonzero; game-only cards are 0.
- `card_prop.bin`: 8 bytes per card. The low 14 bits of the first u32 are the internal ID (3500 = Speed World).
- Also present: names and descriptions in 8 languages (e, f, g, i, j, k, r, s), plus `card_intid`, `card_genre`, `card_pack`, `card_sort*`, `card_link`.
- Full table: `cards_us.csv` (index, name, passcode, internal_id).

## Verified in the emulator (melonDS DS core, headless)
- In the deck editor, the big card image (top screen, 88×125 px) is a **3D textured quad**, using the tx128 texture for that card.
- The texture bytes in VRAM match the ROM exactly. Hashing them with XXH64 identifies the card every time: 7/7 cursor moves matched (Tune Warrior #2563, X-Saber Anapelera, Dark Valkyria, Dragon Ice, ...).
- The polygon gives screen corners plus texture coords (the card spans texels 20.5–107.5 × 0–128). That's enough to draw an HD image in exactly the right place, with perspective.
- PoC: `poc_compare.png` / `poc_4x_full.png`. An HD card face is composited at 4× over the real frame: crisp text from the game's own card data, art still a placeholder.
- Not a card.pac texture: the story-intro card on the top screen (Red Dragon Archfiend with blue art). Still to check: the duel field, and the bottom-list icons (bg26 is the likely source).
- `card_prop`: ATK = ((p0>>14)&0x1FF)*10, DEF = ((p0>>23)&0x1FF)*10, Level = (p1>>10)&0xF.

## Emulator hook point
- melonDS master has `GPU3D_Texcache` (XXH64 over VRAM ranges). On a cache miss for fmt 5 at 128/64/32 px, hash the texel words and look up `texhash_us.tsv` to get the card index, then load the HD image instead.

## Duel views (verified live)
| Where | How the game draws it | Texture |
| --- | --- | --- |
| Deck editor card | 3D quad | tx128 (full face) |
| Duel: top-screen card detail | 2D text BG on the sub engine, 11x16 tiles, 8bpp, sequential tile map | bg128 (full face, no stars) |
| Duel: raised hand card | 3D quad | tx64 (full face) |
| Duel: hand row and field cards | 3D quads, perspective on the field | tx32 (artwork only) |

tx32 = artwork only: texels (1,4)-(31,26) show the face region (33,24)-(95,90) of the tx128 face.
Face bbox: tx128 x 20..108, tx64 x 10..54 (same ratio), full height.
2D detection: scan text BGs for an 11x16 block of sequential tile numbers, hash the 11,264 bytes of tile data, look up bg128.

## 3D textures (verified against textures dumped from the running game)
- Content hash used by the fork: XXH64 over melonDS's decoded texcache buffer (u32 per texel:
  R6 | G6<<8 | B6<<16 | A5<<24, exactly as `GPU3D_Texcache` decodes), seed `(width<<16)|height`.
  `wc09/tools/tex3d.py` reproduces the decoder bit-exactly for all formats.
- `RPG3D.pac`, `RACE.pac`: standard NSBMD (BMD0 with MDL0 + TEX0). Texture/palette pairing via the
  MDL0 material dictionaries. 3D info dictionaries: u8 0, u8 count, u16 size; patricia block whose
  size (u16 at +6) is measured from the dictionary start; data block (u16 elem size, u16 size); names.
- `monmodel.pac`, `effect.pac`: NARC files holding Konami `YIMD` models. Header u32s at 0x48:
  texture count, texture table offset, palette count, palette table offset. Texture entry (0x20):
  u32 fmt, u16 w, u16 h, u32 texel off, u32 texel size, u32 tex4x4 index off, u32 size, ...
  Palette entry: u32 offset, u32 size.
- Mini monsters on the duel field: `monmini.pac/monmini_image.bin` = concatenated LZ10 NTTX (IMGE only,
  64x64 8bpp, color 0 transparent), offsets in `monmini_size.bin`; one shared 256-color palette in
  `duel3d.pac/monmini.5pl` (NTPL). 9,249 unique.
- Duel UI on the 3D layer (phase buttons, counters, field zones) is built at runtime: only known by dumping.

## Overworld 2D
- `RPG2D_2.pac`: NCGR/NCLR/NSCR backgrounds with the same stem (top-screen scenery `bgNN`, radar maps
  `*_rader`), and the tiles (NCGR/NCLR) for the cells in `RPG2D.pac` (NCER/NANR): story portraits
  `npcNN_buNN`, items, overworld menus. `f_*`/`m_*` = player avatar parts (recolored at runtime).
- The story intro card (Red Dragon Archfiend over the stadium) is not a text BG, not a sprite made of
  bg128 tiles and not a 3D texture: most likely an affine/bitmap BG. Not handled.
