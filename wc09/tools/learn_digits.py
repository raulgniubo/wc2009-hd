#!/usr/bin/env python3
"""Digit glyph tiles of the list font from a WC09_DUMP VRAM dump (deck editor, engine B BG3):
tiles 0x329 + d are the digits d.  Saves their shapes (tile_digits.npz) and the tiles themselves
as extra pack entries (tile_digit_tiles.npz: hash -> rgba)."""
import struct, sys, numpy as np, xxhash
d = open(sys.argv[1], 'rb').read()
BBG = d[0x80000:0xA0000]
o = 0x80000 + 0x20000 + 0x40000 + 0x20000; PAL = d[o:o + 2048]
def col(c): return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
cnt = 0x061a; cb = ((cnt >> 2) & 15) * 0x4000; sb = ((cnt >> 8) & 31) * 0x800
ent = struct.unpack_from('<H', BBG, sb + (8 * 32 + 21) * 2)[0]   # the '0' of "1600"
bank = ent >> 12
print('ent %04x bank %d' % (ent, bank))
shapes = {}; tiles = {}
for dgt in range(10):
    t = 0x329 + dgt
    rgba = np.zeros((8, 8, 4), np.uint8)
    for y in range(8):
        for x in range(8):
            b = BBG[cb + t * 32 + y * 4 + x // 2]; ci = (b >> 4) if x & 1 else b & 15
            if ci:
                c = struct.unpack_from('<H', PAL, 0x400 + (bank * 16 + ci) * 2)[0]
                rgba[y, x] = col(c) + (255,)
    a = rgba[..., 3] > 0; lum = rgba[..., :3].astype(int).sum(-1)
    shapes[str(dgt)] = np.stack([a, (lum > 360) & a]).astype(np.uint8)
    h = xxhash.xxh64(rgba.tobytes(), seed=0x7117).intdigest()
    tiles[str(h)] = rgba
    print(dgt, '%016x' % h); print('\n'.join(''.join('#' if rgba[y, x, 3] and rgba[y, x, :3].astype(int).sum() > 360 else ('o' if rgba[y, x, 3] else '.') for x in range(8)) for y in range(8)))
np.savez('/home/claude/tools/tile_digits.npz', **shapes)
np.savez('/home/claude/tools/tile_digit_tiles.npz', **tiles)
