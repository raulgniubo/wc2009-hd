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
