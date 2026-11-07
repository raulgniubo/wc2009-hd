#!/usr/bin/env python3
"""Duel list / info icons (duel2d sprites): owner pills, attribute badges, card-state, effect and
position icons.  Vector redraws for the ones people look at most (owner pills, attributes, the
spiral and yin-yang effect icons), clean pixel-art smoothing for the rest."""
import os, sys, math
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from make_duel_ui import Canvas, lerp, WHITE
from pixsmooth import smooth
from PIL import Image, ImageFont, ImageDraw

SRC = '/tmp/claude-0/d2d'
OUT = '/home/claude/wc09/ui_hd/duel2d'
K = 8
JP = '/usr/share/fonts/opentype/noto/NotoSansCJK-Black.ttc'

def cells(name, ch=16):
    im = Image.open(f'{SRC}/{name}.png').convert('RGBA')
    return im, [im.crop((0, y, im.width, y + ch)) for y in range(0, im.height, ch)]

# ---- owner pills (32x32: you = blue on top, opponent = pink below)
c = Canvas(32, 32, K)
for y, (top, bot, edge) in ((0, ((150, 215, 255), (40, 130, 230), (225, 245, 255))),
                            (16, ((255, 150, 185), (215, 50, 100), (255, 225, 235)))):
    c.rrect((1.2, y + 0.4, 31.8, y + 15.6), 7.6, fill=lerp(bot, (0, 0, 0), .45))
    c.grad_rrect((2, y + 1.2, 31, y + 14.8), 6.8, top, bot)
    c.grad_rrect((3.4, y + 1.9, 29.6, y + 7.4), 3, lerp(top, WHITE, .5), top)
c.finish(f'{OUT}/card_owner.png', binary=False)

# ---- attributes (16x16 each): coloured badge, white kanji
ATTR = [('光', (250, 200, 40)), ('闇', (150, 50, 190)), ('水', (40, 120, 230)), ('炎', (220, 40, 40)),
        ('地', (60, 56, 52)), ('風', (40, 160, 70)), ('神', (215, 165, 40)), ('魔', (30, 140, 120)), ('罠', (190, 40, 120))]
c = Canvas(16, 16 * len(ATTR), K)
for i, (ch, col) in enumerate(ATTR):
    y = i * 16
    c.ellipse((0.2, y + 0.2, 15.8, y + 15.8), fill=(20, 16, 14))
    c.ellipse((0.9, y + 0.9, 15.1, y + 15.1), fill=lerp(col, (255, 255, 255), .25))
    for k in range(8):   # soft radial shading, light from the top left
        t = k / 7; r = 6.9 * (1 - .55 * t)
        c.ellipse((8 - r - 1.1 * t, y + 8 - r - 1.4 * t, 8 + r - 1.1 * t, y + 8 + r - 1.4 * t), fill=lerp(lerp(col, (0, 0, 0), .25), lerp(col, WHITE, .35), t))
    f = ImageFont.truetype(JP, int(c.S(10.2)))
    c.d.text((c.S(8), c.S(y + 8.1)), ch, font=f, anchor='mm', fill=(255, 255, 255), stroke_width=int(c.S(.9)), stroke_fill=lerp(col, (0, 0, 0), .55))
c.finish(f'{OUT}/attr_obj.png', binary=False)

# ---- card position/location icons (card_posi, 16x16 each): smooth the game's art, redraw spiral and yin-yang
im, cs = cells('card_posi')
sm = smooth(im, K, merge=30)
c = Canvas(16, im.height, K)
c.im.alpha_composite(sm.resize(c.im.size, Image.LANCZOS))
def clear(c, y):
    c.d.rectangle([0, int(c.S(y)), c.im.width, int(c.S(y + 16)) - 1], fill=(0, 0, 0, 0))
def disc(c, y, top, bot):
    c.ellipse((1, y + 1, 15, y + 15), fill=(14, 22, 44))
    for k in range(8):
        t = k / 7; r = 6.3 * (1 - .5 * t)
        c.ellipse((8 - r - t, y + 8 - r - 1.2 * t, 8 + r - t, y + 8 + r - 1.2 * t), fill=lerp(bot, top, t))
# spiral (cell 1)
clear(c, 16); disc(c, 16, (150, 220, 255), (30, 110, 210))
pts = [(8 + .42 * a * math.cos(a), 16 + 8 + .42 * a * math.sin(a)) for a in [i * .12 for i in range(1, 110)]]
c.line(pts, fill=(255, 255, 255), width=1.15)
# yin-yang (cell 3)
y = 48; clear(c, y); c.ellipse((1, y + 1, 15, y + 15), fill=(14, 22, 44))
c.ellipse((1.8, y + 1.8, 14.2, y + 14.2), fill=(235, 245, 255))
half = Canvas(16, 16, K)
half.d.pieslice([half.S(1.8), half.S(1.8), half.S(14.2) - 1, half.S(14.2) - 1], 90, 270, fill=(40, 120, 220))
half.ellipse((4.9, 1.8, 11.1, 8), fill=(40, 120, 220))
c.im.alpha_composite(half.im, (0, int(c.S(y))))
c.ellipse((4.9, y + 8, 11.1, y + 14.2), fill=(235, 245, 255))
c.ellipse((7, y + 3.6, 9, y + 5.6), fill=(235, 245, 255)); c.ellipse((7, y + 10.4, 9, y + 12.4), fill=(40, 120, 220))
c.finish(f'{OUT}/card_posi.png', binary=False)

# ---- the remaining small icon sets: clean smoothing of the game's art
for name in ('card_state', 'affect_icon', 'icon_obj'):
    im = Image.open(f'{SRC}/{name}.png').convert('RGBA')
    smooth(im, K, merge=30).save(f'{OUT}/{name}.png')
print('ok')
