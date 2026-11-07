#!/usr/bin/env python3
"""Main menu / Duelist Menu redesign (MainMenu nitro cells): icon tiles, menu tags, titles,
back and arrow buttons, list highlight bar.  Writes HDCards/MainMenu/<cell>.png."""
import os, sys, re, glob
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from make_duel_ui import Canvas, lerp, WHITE, FONT, FONT_B
from vecicons import draw, Ico
from PIL import Image, ImageFont, ImageDraw, ImageFilter

NAT = '/home/claude/wc09/nit_png/MainMenu'
OUT = '/home/claude/wc09/ui_hd/MainMenu'
os.makedirs(OUT, exist_ok=True)
K = 8
EDGE = (96, 74, 44)
FACE = ((255, 252, 240), (238, 226, 196))
FACE_P = ((255, 236, 170), (232, 196, 110))

def shadow(c, b, r, off=1.6, alpha=110):
    """soft drop shadow inside the canvas"""
    sh = Canvas(c.w, c.h, c.k)
    x0, y0, x1, y1 = b
    sh.rrect((x0 + off, y0 + off, x1 + off, y1 + off), r, fill=(30, 20, 10, alpha))
    blur = sh.im.filter(ImageFilter.GaussianBlur(sh.S(.6)))
    c.im.alpha_composite(blur)

def tile(c, b, pressed, r=7):
    x0, y0, x1, y1 = b
    if not pressed: shadow(c, b, r, 1.8)
    c.rrect(b, r, fill=EDGE)
    top, bot = FACE_P if pressed else FACE
    c.grad_rrect((x0 + 1.1, y0 + 1.1, x1 - 1.1, y1 - 1.1), r - 1.1, top, bot)
    c.grad_rrect((x0 + 2.2, y0 + 1.8, x1 - 2.2, y0 + (y1 - y0) * .45), r - 2, lerp(top, WHITE, .55), top)

TILES = {3: 'swords', 5: 'trophy', 7: 'wireless', 9: 'person', 11: 'duelist', 13: 'shop', 15: 'book',
         17: 'options', 31: 'exit', 33: 'runner'}
# The game draws normal / selected / pressed tiles with the same pixels and different palettes, so
# the HD tile keeps the game's own outline ring (marker alpha ~0.3 = show the original pixels) and
# redraws the face and the icon.
from scipy import ndimage
MARK = 77
for k, kind in TILES.items():
    for pressed in (0, 1):
        n = k + pressed
        src = np.array(Image.open(f'{NAT}/sita_menu_c{n}.png').convert('RGBA'))
        h, w = src.shape[:2]
        A = src[..., 3] > 0
        inner = ndimage.binary_erosion(A, iterations=2)
        ys, xs = np.nonzero(inner)
        fx0, fy0, fx1, fy1 = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
        c = Canvas(w, h, K)
        ring = Image.fromarray(((A & ~inner) * 255).astype(np.uint8)).resize(c.im.size, Image.NEAREST)
        mark = Image.new('RGBA', c.im.size, EDGE + (MARK,))
        c.im.paste(mark, (0, 0), ring)
        c.grad_rrect((fx0 - .3, fy0 - .3, fx1 + .3, fy1 + .3), 4.2, FACE[0], FACE[1])
        cx, cy = (fx0 + fx1) / 2, (fy0 + fy1) / 2
        s = min(fx1 - fx0, fy1 - fy0) * .74
        draw(c, kind, (cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2))
        im = c.im.resize((w * K, h * K), Image.LANCZOS)
        im.save(f'{OUT}/sita_menu_c{n}.png')

# menu tags (40x37, opaque 0,5 - 38,34): small tile with a point on the right, icon inside
TAGS = ['swords', 'trophy', 'wireless', 'person', 'duelist', 'shop', 'book', 'options']
for i, kind in enumerate(TAGS):
    c = Canvas(40, 37, K)
    pts = [(1.5, 6), (28, 6), (37.4, 19.5), (28, 33), (1.5, 33)]
    c.poly([(x - .7 if x < 20 else x + .7, y - .7 if y < 19 else y + .7) for x, y in pts], fill=EDGE)
    sh = [(x, y) for x, y in pts]
    c.poly(sh, fill=FACE[0])
    g = Canvas(40, 37, K); g.grad_rrect((1.5, 6, 37.4, 33), 0, FACE[0], FACE[1])
    m = Image.new('L', c.im.size, 0); ImageDraw.Draw(m).polygon([(c.S(x), c.S(y)) for x, y in sh], fill=255)
    c.im.paste(g.im, (0, 0), m)
    draw(c, kind, (6, 8.5, 27, 30.5))
    c.finish(f'{OUT}/sita_menu_c{19 + i}.png', binary=False)

# back button (32x32, opaque 26x26): navy tile with a white back arrow (as in the duel)
for n, pressed in ((0, 0), (1, 1)):
    c = Canvas(32, 32, K)
    c.rrect((0, 0, 26, 26), 5.5, fill=(150, 200, 255) if not pressed else (220, 240, 255))
    c.grad_rrect((1.2, 1.2, 24.8, 24.8), 4.5, (52, 86, 160) if not pressed else (90, 140, 230), (22, 38, 82) if not pressed else (40, 80, 170))
    draw(c, 'back', (5, 5, 21, 21), olc=(10, 18, 44))
    c.finish(f'{OUT}/sita_menu_c{n}.png', binary=False)

# small arrow buttons (24x24, opaque 22x17): up, down, up/down, up/down pressed
for n, kind, pressed in ((27, 'up', 0), (28, 'down', 0), (29, 'updown', 0), (30, 'updown', 1)):
    c = Canvas(24, 24, K)
    c.rrect((0, 0, 22, 17), 3.5, fill=EDGE)
    c.grad_rrect((1, 1, 21, 16), 2.6, *(FACE_P if pressed else FACE))
    draw(c, kind, (6.5, 2.5, 15.5, 14.5))
    c.finish(f'{OUT}/sita_menu_c{n}.png', binary=False)

# list highlight bar (216x24, opaque 213x19)
c = Canvas(216, 24, K)
c.rrect((0, 0, 213, 19), 4, fill=(190, 140, 20))
c.grad_rrect((1, 1, 212, 18), 3.2, (255, 244, 140), (255, 200, 40))
c.grad_rrect((2, 1.6, 211, 8), 2.4, (255, 252, 210), (255, 238, 130))
c.finish(f'{OUT}/sita_menu_c2.png', binary=False)

# titles: opaque cells whose background is the bar they sit on -> keep each row's background
# colour, redraw the label as crisp text
LABELS_S = ['Free Duel', 'Tournament', 'Communications Menu', 'Friend Menu', 'Duelist Menu', 'Shop', 'Tutorial', 'Options']
LABELS_U = LABELS_S + ['Customize Duel Runner', 'Return To The Title Screen']
def title(name, label):
    label = label.replace(' ', '\u2002')
    src = np.array(Image.open(f'{NAT}/{name}.png').convert('RGBA'))
    H, W = src.shape[:2]
    bg = np.zeros((H, 3)); textmask = np.zeros((H, W), bool)
    for y in range(H):
        row = [tuple(p) for p in src[y]]
        cols = {}
        for p in row: cols[p] = cols.get(p, 0) + 1
        best = row[0] if row[0][3] else row[-1]; bg[y] = best[:3]   # the cell edges are plain background
        textmask[y] = [p != best and p[3] > 0 for p in row]
    alpha = src[..., 3]
    ys, xs = np.nonzero(textmask)
    tx0, tx1, ty0, ty1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    big = Image.new('RGBA', (W * K, H * K))
    for y in range(H):
        if alpha[y].max() == 0: continue
        ImageDraw.Draw(big).rectangle([0, y * K, W * K, (y + 1) * K], fill=tuple(int(v) for v in bg[y]) + (255,))
    # keep exact transparency of the cell
    a = Image.fromarray(alpha).resize((W * K, H * K), Image.NEAREST)
    big.putalpha(a)
    d = ImageDraw.Draw(big)
    hpx = 14.5 * K
    size = hpx
    f = ImageFont.truetype(FONT, int(size))
    while d.textlength(label, font=f) > (tx1 - tx0 + 2) * K and size > 6:
        size *= .96; f = ImageFont.truetype(FONT, int(size))
    cx, cy = (tx0 + tx1) / 2 * K, (ty0 + ty1) / 2 * K
    sh = Image.new('RGBA', big.size)
    ImageDraw.Draw(sh).text((cx + K * .5, cy + K * .7 + size * .36), label, font=f, anchor='ms', fill=(40, 50, 70, 120),
                            stroke_width=int(size * .09), stroke_fill=(40, 50, 70, 120))
    big.alpha_composite(sh.filter(ImageFilter.GaussianBlur(K * .5)))
    d.text((cx, cy + size * .36), label, font=f, anchor='ms', fill=(255, 255, 255, 255),
           stroke_width=max(1, int(size * .085)), stroke_fill=(70, 82, 104, 255))
    big.putalpha(a)
    big.save(f'{OUT}/{name}.png')
for i, l in enumerate(LABELS_S): title(f'sita_title_e_c{i}', l)
for i, l in enumerate(LABELS_U): title(f'ue_title_e_c{i}', l)
print('ok')
