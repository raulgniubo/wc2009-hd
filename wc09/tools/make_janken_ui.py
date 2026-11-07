#!/usr/bin/env python3
"""Modern redesign of the "who goes first" (rock-paper-scissors) screen:
background, player plates, coins, coin rings, FIRST/SECOND buttons, timer digits.
Writes into /home/claude/wc09/ui_hd/janken (HDCards/janken/<name>.png)."""
import os, math, sys
sys.path.insert(0, os.path.dirname(__file__))
from make_duel_ui import Canvas, lerp, WHITE, FONT, FONT_B
from PIL import Image, ImageDraw, ImageFilter

OUT = '/home/claude/wc09/ui_hd/janken'
os.makedirs(OUT, exist_ok=True)
NAT = '/home/claude/wc09/nit_png/janken'

# ---------------------------------------------------------------- backgrounds (256x192, 4 px per px)
def background(seed):
    K = 4; W, H = 256 * K, 192 * K
    im = Image.new('RGB', (W, H))
    px = im.load()
    top, bot = (14, 28, 66), (6, 12, 32)
    for y in range(H):
        c = lerp(top, bot, y / (H - 1))
        for x in range(W): px[x, y] = c
    d = ImageDraw.Draw(im, 'RGBA')
    # soft diagonal light bands
    for i in range(-4, 12):
        x0 = i * 160 + seed * 40
        d.polygon([(x0, 0), (x0 + 70, 0), (x0 - 200, H), (x0 - 270, H)], fill=(80, 140, 255, 14))
    # faint hex grid
    r = 26
    for row in range(-1, H // int(r * 1.5) + 2):
        for col in range(-1, W // int(r * 1.73) + 2):
            cx = col * r * 1.732 + (row % 2) * r * 0.866; cy = row * r * 1.5
            pts = [(cx + r * math.cos(math.pi / 6 + k * math.pi / 3), cy + r * math.sin(math.pi / 6 + k * math.pi / 3)) for k in range(6)]
            d.polygon(pts, outline=(90, 150, 255, 22))
    # vignette glow in the middle
    glow = Image.new('L', (W, H), 0)
    ImageDraw.Draw(glow).ellipse((W * .15, H * .2, W * .85, H * 1.0), fill=90)
    glow = glow.filter(ImageFilter.GaussianBlur(120))
    im = Image.composite(Image.new('RGB', (W, H), (40, 80, 170)), im, glow.point(lambda v: v // 2))
    return im

background(0).save(f'{OUT}/bg_back_ue.png')
background(1).save(f'{OUT}/bg_back_sita.png')

# player info plate (208x48): navy card, light-blue edge; icons and text come from other layers
c = Canvas(208, 48, 4)
c.rrect((0.5, 0.5, 207.5, 47.5), 6, fill=(120, 175, 255))
c.grad_rrect((1.5, 1.5, 206.5, 46.5), 5.2, (34, 60, 124), (16, 28, 64))
c.grad_rrect((3, 2.5, 205, 18), 4, (60, 96, 170), (34, 60, 124))
o = (8, 14, 34)
# icons at x 64..74: player, deck, rank
c.ellipse((66, 2.6, 72, 8.6), fill=(255, 205, 90), outline=o, width=0.5)
c.rrect((64.4, 8.6, 73.6, 14), 2.4, fill=(235, 90, 100), outline=o, width=0.5)
c.rrect((65.6, 18.6, 73, 29.6), 1.2, fill=(120, 170, 240), outline=o, width=0.5)
c.rrect((64.4, 19.6, 71.8, 30.4), 1.2, fill=(200, 225, 255), outline=o, width=0.5)
c.poly([(64.5, 34.5), (73.5, 34.5), (73.5, 37.5), (69, 44.5), (64.5, 37.5)], fill=(255, 205, 80), outline=o, width=0.5)
c.poly([(66.5, 36.2), (71.5, 36.2), (69, 40.5)], fill=(255, 240, 180))
c.finish(f'{OUT}/bg_info.png', binary=False)
# tag-duel name strip (256x16)
c = Canvas(256, 16, 4)
c.rrect((0.5, 0.5, 255.5, 15.5), 4, fill=(24, 40, 84), outline=(120, 175, 255), width=0.6)
c.finish(f'{OUT}/bg_tag_name.png', binary=False)

# ---------------------------------------------------------------- coins (48x48 cells)
# clean vector coin; the hand / "?" emblem is traced from the game's own art (smooth outline +
# lighter relief fill) so the shapes stay exactly the game's
import numpy as np
def _mask_up(m, K, blur, th):
    im = Image.fromarray((m * 255).astype(np.uint8)).resize((m.shape[1] * K, m.shape[0] * K), Image.BICUBIC)
    v = np.array(im.filter(ImageFilter.GaussianBlur(blur))).astype(float) / 255
    return np.clip((v - th) * 7 + 0.5, 0, 1)

def coin(k):
    K = 10; S = 48 * K
    src = np.array(Image.open(f'{NAT}/coin_obj_c{k}.png').convert('RGBA')).astype(float)
    yy, xx = np.mgrid[0:48, 0:48]
    inside = ((xx - 23.5) ** 2 + (yy - 23.5) ** 2) < 17.8 ** 2
    ssum = src[..., :3].sum(-1)
    dark = (ssum < 350) & (src[..., 3] > 0) & inside
    light = (ssum > 600) & (src[..., 3] > 0) & inside
    D = _mask_up(dark.astype(float), K, K * .62, .36)
    L = _mask_up(light.astype(float), K, K * .7, .45)
    c = Canvas(48, 48, K)
    cx = cy = 24; r = 23.4
    c.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(132, 80, 6))
    c.ellipse((cx - r * .955, cy - r * .955, cx + r * .955, cy + r * .955), fill=(250, 206, 70))
    for i in range(14):                 # face: soft light from the top left
        t = i / 13; rr = r * (.9 - .55 * t)
        c.ellipse((cx - rr - r * .1 * t, cy - rr - r * .12 * t, cx + rr - r * .1 * t, cy + rr - r * .12 * t),
                  fill=lerp((226, 160, 22), (255, 232, 130), t))
    c.ellipse((cx - r * .81, cy - r * .81, cx + r * .81, cy + r * .81), outline=(176, 112, 12), width=1.1)
    c.ellipse((cx - r * .77, cy - r * .77, cx + r * .77, cy + r * .77), outline=(255, 238, 160), width=.5)
    return c, D, L

# emblem pieces, drawn back to front: each piece = dark outline (grown by OW) then the relief fill,
# so overlapping fingers get clean separation lines automatically
OW = 1.25
EDGE, FILL_T, FILL_B = (104, 56, 0), (255, 242, 178), (238, 182, 60)
ES = 0.84
def _t(x, y): return (24 + (x - 24) * ES, 24.6 + (y - 24) * ES)
def piece(c, kind, *g):
    if kind == 'cap':
        g = (_t(*g[0]), _t(*g[1]), g[2] * ES)
    else:
        (x0, y0, x1, y1), r = g
        g = ((*_t(x0, y0), *_t(x1, y1)), r * ES)
    for grow, col in ((OW, EDGE), (0, None)):
        if kind == 'cap':                     # capsule (p0, p1, radius)
            (x0, y0), (x1, y1), r = g
            r += grow
            pts = []
            ang = math.atan2(y1 - y0, x1 - x0)
            for i in range(19):
                t = ang + math.pi / 2 + i * math.pi / 18
                pts.append((x0 + r * math.cos(t), y0 + r * math.sin(t)))
            for i in range(19):
                t = ang - math.pi / 2 + i * math.pi / 18
                pts.append((x1 + r * math.cos(t), y1 + r * math.sin(t)))
            if col: c.poly(pts, fill=col)
            else:
                ymin = min(p[1] for p in pts); ymax = max(p[1] for p in pts)
                c.poly(pts, fill=lerp(FILL_T, FILL_B, ((y0 + y1) / 2 - 8) / 34))
        else:                                 # rounded box (box, radius)
            (x0, y0, x1, y1), r = g
            b = (x0 - grow, y0 - grow, x1 + grow, y1 + grow)
            if col: c.rrect(b, r + grow, fill=col)
            else: c.grad_rrect(b, r, lerp(FILL_T, FILL_B, (y0 - 8) / 34), lerp(FILL_T, FILL_B, (y1 - 8) / 34))

def emblem(c, k):
    if k == 0:
        c.text((24.6, 25.4), '?', 24, fill=EDGE, font=FONT_B)
        c.text((24, 24.6), '?', 24, fill=FILL_T, font=FONT_B, stroke=1.3, stroke_fill=EDGE)
    elif k == 1:      # rock: fist seen from the front, thumb across the fingers
        piece(c, 'box', (13.5, 18, 34.5, 37.5), 6)
        for x in (16.8, 21.6, 26.4, 31.2):
            piece(c, 'cap', (x, 17.5), (x, 24), 3.3)
        piece(c, 'cap', (13.5, 29), (25.5, 26.2), 3.2)
    elif k == 2:      # scissors: index and middle finger up, the others curled under the thumb
        piece(c, 'cap', (21, 22), (16.5, 8.5), 3.1)
        piece(c, 'cap', (27, 22), (31.5, 8.5), 3.1)
        piece(c, 'box', (15, 21.5, 33.5, 39), 6)
        for x in (26.5, 31):
            piece(c, 'cap', (x, 22.5), (x, 26), 2.8)
        piece(c, 'cap', (15, 33), (25, 28.5), 3.0)
    elif k == 3:      # paper: open hand
        for (x0, y0, x1, y1, r) in ((18.6, 25, 14.2, 10.5, 2.6), (23, 24, 21.8, 7.2, 2.8),
                                    (27.6, 24, 29.6, 8, 2.8), (31.4, 26, 35.6, 13.2, 2.5)):
            piece(c, 'cap', (x0, y0), (x1, y1), r)
        piece(c, 'cap', (18, 34), (9.5, 27), 2.7)
        piece(c, 'box', (15.5, 22, 34, 40), 6.5)

for k in range(4):
    c, _, _ = coin(k)
    sh = Canvas(48, 48, c.k)                  # soft engraved shadow
    emblem(sh, k)
    sim = sh.im.split()[3].filter(ImageFilter.GaussianBlur(sh.S(0.8)))
    shadow = Image.new('RGBA', sh.im.size, (120, 70, 0, 0)); shadow.putalpha(sim.point(lambda v: v * 45 // 100))
    c.im.alpha_composite(shadow, (int(sh.S(0.9)), int(sh.S(1.1))))
    emblem(c, k)
    c.finish(f'{OUT}/coin_obj_c{k}.png')

# flip frames: the coin seen at an angle (32x48), and edge-on
for k, sq in ((4, .55), (5, .22)):
    c = Canvas(32, 48, 8)
    c.ellipse((16 - 15 * sq - 1.2, 1.2, 16 + 15 * sq + 1.2, 46.8), fill=(132, 80, 6))
    c.ellipse((16 - 15 * sq, 2.4, 16 + 15 * sq, 45.6), fill=(250, 206, 70))
    c.ellipse((16 - 15 * sq * .8, 6, 16 + 15 * sq * .8, 42), outline=(176, 112, 12), width=1)
    if sq > .3: c.text((16, 24.5), '?', 20, fill=(110, 60, 0))
    c.finish(f'{OUT}/coin_obj_c{k}.png')

# selection rings (52x52): red for the opponent (c0-c3), blue for you (c4-c7), pulsing; solid
# bands only (no hairlines: the sprite's own outline masks them into dashes)
for k in range(8):
    base = (255, 70, 110) if k < 4 else (60, 190, 255)
    t = (k % 4) / 3
    col = lerp(lerp(base, WHITE, .5), base, t)
    c = Canvas(52, 52, 8)
    c.ellipse((1.6, 1.6, 50.4, 50.4), outline=lerp(base, (0, 0, 0), .35), width=3.4)
    c.ellipse((2.3, 2.3, 49.7, 49.7), outline=col, width=2.0)
    c.finish(f'{OUT}/coin_cursor_c{k}.png')

# FIRST / SECOND TO GO plates (112x16): normal and selected (text is drawn by the game on top)
for k, (top, bot, edge) in enumerate([((44, 70, 130), (20, 34, 74), (110, 150, 220)),
                                      ((90, 180, 255), (40, 100, 220), (220, 245, 255))]):
    c = Canvas(112, 16, 8)
    c.rrect((0.5, 0.5, 111.5, 15.5), 7.5, fill=edge)
    c.grad_rrect((1.5, 1.5, 110.5, 14.5), 6.5, top, bot)
    c.finish(f'{OUT}/obj_select_c{k}.png')

# timer: clock badge and digits (cyan = you, red = last seconds)
c = Canvas(64, 32, 8)
c.rrect((0.5, 0.5, 63.5, 31.5), 6, fill=(120, 175, 255))
c.grad_rrect((1.5, 1.5, 62.5, 30.5), 5.2, (34, 60, 124), (16, 28, 64))
c.ellipse((6, 6, 26, 26), outline=WHITE, width=2)
c.line([(16, 16), (16, 9.5)], fill=WHITE, width=1.8); c.line([(16, 16), (21, 18)], fill=WHITE, width=1.8)
c.finish(f'{OUT}/time_obj_c0.png')
digits = {1: '0', 2: '1', 3: '2', 4: '3', 5: '4', 6: '5', 7: '6', 8: '7', 9: '8', 10: '9', 11: '0', 12: '1', 13: '2', 14: '3'}
for k, dch in digits.items():
    col = (110, 230, 255) if k <= 10 else (255, 100, 110)
    c = Canvas(16, 32, 8)
    c.text((8, 16.5), dch, 20, fill=col, stroke=1.4, stroke_fill=(8, 14, 34))
    c.finish(f'{OUT}/time_obj_c{k}.png')
print('ok')
