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
def coin_face(c, cx, cy, r, glyph):
    # rim, face gradient, inner ring, emblem
    c.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(150, 98, 10))
    c.ellipse((cx - r * .94, cy - r * .94, cx + r * .94, cy + r * .94), fill=(255, 214, 90))
    # radial-ish shading: stacked ellipses
    for i in range(10):
        t = i / 9; rr = r * (.86 - .5 * t)
        c.ellipse((cx - rr - r * .05 * t, cy - rr - r * .08 * t, cx + rr - r * .05 * t, cy + rr - r * .08 * t),
                  fill=lerp((236, 170, 30), (255, 236, 150), t))
    c.ellipse((cx - r * .8, cy - r * .8, cx + r * .8, cy + r * .8), outline=(190, 128, 20), width=r * .05)
    dark = (120, 70, 0)
    if glyph == '?':
        c.text((cx, cy + r * .04), '?', r * 1.15, fill=(150, 90, 0), font=FONT)
    elif glyph == 'rock':
        # rounded fist: knuckles + thumb
        c.rrect((cx - r * .46, cy - r * .30, cx + r * .46, cy + r * .44), r * .22, fill=dark)
        for k in range(4):
            x = cx - r * .42 + k * r * .23
            c.ellipse((x, cy - r * .42, x + r * .24, cy - r * .12), fill=dark)
        c.rrect((cx - r * .5, cy + r * .02, cx + r * .1, cy + r * .24), r * .1, fill=(205, 150, 40))
    elif glyph == 'scissors':
        c.line([(cx - r * .05, cy + r * .45), (cx - r * .36, cy - r * .55)], fill=dark, width=r * .22)
        c.line([(cx + r * .05, cy + r * .45), (cx + r * .34, cy - r * .55)], fill=dark, width=r * .22)
        c.rrect((cx - r * .34, cy + r * .05, cx + r * .34, cy + r * .58), r * .18, fill=dark)
    elif glyph == 'paper':
        c.rrect((cx - r * .36, cy - r * .05, cx + r * .30, cy + r * .58), r * .2, fill=dark)
        for k, (ang, ln) in enumerate([(-100, .55), (-82, .66), (-64, .62), (-46, .52)]):
            a = math.radians(ang + 8)
            x0 = cx - r * .26 + k * r * .17
            c.line([(x0, cy), (x0 + math.cos(a) * r * ln * .35, cy + math.sin(a) * r * ln)], fill=dark, width=r * .15)
        c.line([(cx - r * .32, cy + r * .2), (cx - r * .62, cy - r * .02)], fill=dark, width=r * .15)

for k, glyph in enumerate(['?', 'rock', 'scissors', 'paper']):
    c = Canvas(48, 48, 8); coin_face(c, 24, 24, 22.5, glyph); c.finish(f'{OUT}/coin_obj_c{k}.png')
# flip frames: the coin seen at an angle (32x48), and edge-on
for k, sq in ((4, .55), (5, .22)):
    c = Canvas(32, 48, 8)
    c.ellipse((16 - 15 * sq - 1.2, 1.5, 16 + 15 * sq + 1.2, 46.5), fill=(150, 98, 10))
    c.ellipse((16 - 15 * sq, 2.5, 16 + 15 * sq, 45.5), fill=(255, 214, 90))
    if sq > .3: c.text((16, 24.5), '?', 22, fill=(150, 90, 0))
    c.finish(f'{OUT}/coin_obj_c{k}.png')

# selection rings (52x52): red set for the opponent (c0-c3), blue for you (c4-c7), pulsing
for k in range(8):
    base = (255, 90, 120) if k < 4 else (80, 200, 255)
    t = (k % 4) / 3
    col = lerp(lerp(base, WHITE, .45), base, t)
    c = Canvas(52, 52, 8)
    c.ellipse((2, 2, 50, 50), outline=lerp(base, (0, 0, 0), .4), width=3.6)
    c.ellipse((2.6, 2.6, 49.4, 49.4), outline=col, width=2.4)
    c.ellipse((3.6, 3.6, 48.4, 48.4), outline=WHITE, width=0.6)
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
