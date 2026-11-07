#!/usr/bin/env python3
"""HD tile pack from WC09_TILEDUMP layer dumps (see WC09Battle.cpp UpdateHDTiles).
make_tiles.py OUTDIR DUMP.bin [DUMP.bin ...]
  - digits of the game's list font are redrawn with a modern font (same colours)
  - other coloured tiles (icons, badges) are smoothed in context (pixsmooth over the whole layer)
  - grey-only tiles (proportional text, handled by the text smoothing) are left alone
Writes OUTDIR/tiles/atlas.png (2048x2048, 64x64 per tile) and OUTDIR/tiles/keys.png."""
import sys, os, struct, glob
import numpy as np
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(__file__))
from pixsmooth import smooth

K = 8
FONT = '/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf'
out = sys.argv[1]; dumps = sys.argv[2:]
os.makedirs(f'{out}/tiles', exist_ok=True)

def load(f):
    d = open(f, 'rb').read()
    H = np.frombuffer(d[:25 * 33 * 8], dtype=np.uint64).reshape(25, 33)
    P = np.frombuffer(d[25 * 33 * 8:25 * 33 * 8 + 200 * 264 * 4], dtype=np.uint8).reshape(200, 264, 4).copy()
    return H, P

# ---- digit shapes of the list font: learned from tiles whose digit is known (see DIGITS)
DIGIT_FILE = os.path.join(os.path.dirname(__file__), 'tile_digits.npz')
digit_shapes = {}
if os.path.exists(DIGIT_FILE):
    z = np.load(DIGIT_FILE)
    for k in z.files: digit_shapes[k] = z[k]

def shape_of(t):
    """binary signature: opaque + bright"""
    a = t[..., 3] > 0
    lum = t[..., :3].astype(int).sum(-1)
    return np.stack([a, (lum > 360) & a]).astype(np.uint8)

def which_digit(t):
    s = shape_of(t)
    for d, ref in digit_shapes.items():
        if np.array_equal(s, ref): return d
    return None

def font_digit(t, d):
    """redraw an 8x8 digit tile: fill = its bright colour, outline = its dark colour"""
    a = t[..., 3] > 0
    px = t[a][:, :3].astype(int)
    lum = px.sum(-1)
    fill = tuple(int(v) for v in px[lum.argmax()])
    dark = tuple(int(v) for v in px[lum.argmin()])
    S = 64 * 4
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    dr = ImageDraw.Draw(im)
    f = ImageFont.truetype(FONT, int(S * .98))
    dr.text((S * 3.5 / 8, S * .54), d, font=f, anchor='mm', fill=fill + (255,), stroke_width=int(S * .1), stroke_fill=dark + (255,))
    return im.resize((64, 64), Image.LANCZOS)

def is_grey(t):
    a = t[..., 3] > 0
    if not a.any(): return True
    px = t[a][:, :3].astype(int)
    return (px.max(-1) - px.min(-1)).max() < 24

tiles = {}   # hash -> 64x64 RGBA image
_dt = os.path.join(os.path.dirname(__file__), 'tile_digit_tiles.npz')
if os.path.exists(_dt):
    z = np.load(_dt)
    for k in z.files:
        t = z[k]; tiles[int(k)] = font_digit(t, which_digit(t))
# attribute badges drawn with tiles (deck lists): the duel's HD attribute icons, cut in quarters
ATTR_SRC = np.asarray(Image.open('/tmp/claude-0/d2d/attr_obj.png').convert('RGBA')).astype(int)
ATTR_HD = Image.open('/home/claude/wc09/ui_hd/duel2d/attr_obj.png').convert('RGBA')
def attr_tiles(H, P):
    found = {}
    Pi = P.astype(int)
    for ty in range(24):
        for tx in range(32):
            g = Pi[ty * 8:ty * 8 + 16, tx * 8:tx * 8 + 16]
            if g.shape[:2] != (16, 16) or (g[..., 3] > 0).sum() < 150: continue
            for i in range(ATTR_SRC.shape[0] // 16):
                c = ATTR_SRC[i * 16:i * 16 + 16]
                if ((c[..., 3] > 0) != (g[..., 3] > 0)).sum() > 20: continue
                m = (c[..., 3] > 0) & (g[..., 3] > 0)
                if np.abs(c[m][:, :3] - g[m][:, :3]).mean() > 20: continue
                hd = ATTR_HD.crop((0, i * 128, 128, i * 128 + 128))
                for j in range(4):
                    qx, qy = j & 1, j >> 1
                    h = int(H[ty + qy, tx + qx])
                    if h: found[h] = hd.crop((qx * 64, qy * 64, qx * 64 + 64, qy * 64 + 64))
                break
    return found

for f in dumps:
    H, P = load(f)
    for h, im in attr_tiles(H, P).items(): tiles.setdefault(h, im)
    big = None
    for ty in range(25):
        for tx in range(33):
            h = int(H[ty, tx])
            if not h or h in tiles: continue
            t = P[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
            d = which_digit(t)
            if d is not None:
                tiles[h] = font_digit(t, d); continue
            if is_grey(t): continue
            a = t[..., 3] > 0
            if len({tuple(c) for c in t[a][:, :3]}) > 6: continue   # shaded pictures: smoothing turns them to mush
            if big is None: big = smooth(Image.fromarray(P), K, merge=24)
            tiles[h] = big.crop((tx * 64, ty * 64, tx * 64 + 64, ty * 64 + 64))

keys = sorted(tiles)[:1024]
atlas = Image.new('RGBA', (2048, 2048), (0, 0, 0, 0))
kim = Image.new('RGBA', (len(keys) * 3, 1), (0, 0, 0, 255))
for i, k in enumerate(keys):
    atlas.paste(tiles[k], ((i % 32) * 64, (i // 32) * 64))
    for j in range(3):
        v = (k >> (24 * j)) & 0xFFFFFF
        kim.putpixel((i * 3 + j, 0), (v & 255, (v >> 8) & 255, (v >> 16) & 255, 255))
rows = (len(keys) + 31) // 32
atlas.crop((0, 0, 2048, max(64, rows * 64))).save(f'{out}/tiles/atlas.png')
kim.save(f'{out}/tiles/keys.png')
print(len(keys), 'tiles')
