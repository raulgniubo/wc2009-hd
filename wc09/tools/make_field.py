#!/usr/bin/env python3
"""Duel field redesign: zone atlas (eed9528b68475e5d) and arena floor (0fcadf600e98d99e).
Both are drawn by the game as translucent / repeated (mirrored) 3D textures; the replacements
keep the same layout so every polygon still maps to the same part."""
import os, sys, math
sys.path.insert(0, os.path.dirname(__file__))
from make_duel_ui import Canvas, lerp, WHITE, CARDBACK
from PIL import Image, ImageDraw, ImageFilter

OUT = '/home/claude/wc09/ui_hd/tex3d'
os.makedirs(OUT, exist_ok=True)

# ---------------------------------------------------------------- zone atlas (128x128)
c = Canvas(128, 128, 8)

def zone(b, tint, light):
    """glass panel: tinted fill, glowing rounded rim, corner brackets"""
    x0, y0, x1, y1 = b
    rim = lerp(tint, WHITE, .55 if light else .35)
    fill_top = lerp(tint, WHITE, .12)
    fill_bot = lerp(tint, (0, 0, 0), .08)
    c.rrect(b, 3.2, fill=rim)
    c.grad_rrect((x0 + 1.5, y0 + 1.5, x1 - 1.5, y1 - 1.5), 2.2, fill_top, fill_bot)
    # inner hairline
    c.rrect((x0 + 2.4, y0 + 2.4, x1 - 2.4, y1 - 2.4), 1.8, outline=lerp(tint, WHITE, .3), width=0.35)
    # corner brackets
    L = 5.5; w = 0.9; col = lerp(rim, WHITE, .5)
    for (cx, cy, sx, sy) in ((x0 + 3.4, y0 + 3.4, 1, 1), (x1 - 3.4, y0 + 3.4, -1, 1),
                             (x0 + 3.4, y1 - 3.4, 1, -1), (x1 - 3.4, y1 - 3.4, -1, -1)):
        c.line([(cx, cy + sy * L), (cx, cy), (cx + sx * L, cy)], fill=col, width=w)

BLUE, RED = (0, 165, 238), (238, 85, 101)       # the game's own zone colours
DBLUE, DRED = (0, 117, 165), (190, 68, 85)
zone((0, 0, 31, 31), BLUE, True)
zone((32, 0, 63, 31), RED, True)
# the dark tiles are drawn under the zones, slightly offset, as their thickness/shadow:
# soft dark silhouettes without rims or marks
def shadow(b, tint):
    c.rrect(b, 3.2, fill=lerp(tint, (0, 0, 0), .45))
shadow((64, 0, 95, 31), DBLUE)
shadow((96, 0, 127, 31), DRED)

def slot(b, kind, plain):
    """special zones (22x28) in the game's own colours, redrawn sharp; plain = empty-state version"""
    x0, y0, x1, y1 = b; w, h = x1 - x0, y1 - y0
    cx, cy = x0 + w / 2, y0 + h / 2
    ic = (250, 252, 255)
    base = {'field': (60, 175, 130), 'extra': (130, 130, 140), 'grave': (20, 85, 133),
            'deck': (85, 117, 165), 'banish': (220, 120, 170)}[kind]
    if plain:   # drawn under the special zones as their shadow
        c.rrect(b, 2.6, fill=lerp(base, (0, 0, 0), .45))
        return
    c.rrect(b, 2.6, fill=lerp(base, WHITE, .5))
    if kind == 'extra':
        # the game's diagonal split: grey above, purple below
        c.grad_rrect((x0 + 1, y0 + 1, x1 - 1, y1 - 1), 2, (150, 150, 160), (120, 120, 132))
        c.poly([(x1 - 1, y0 + 4), (x1 - 1, y1 - 1), (x0 + 3, y1 - 1)], fill=(150, 50, 160))
    else:
        c.grad_rrect((x0 + 1, y0 + 1, x1 - 1, y1 - 1), 2, lerp(base, WHITE, .12), lerp(base, (0, 0, 0), .15))
    if plain: return
    if kind == 'field':          # white compass ring
        r = w * .36
        c.ellipse((cx - r, cy - r, cx + r, cy + r), outline=ic, width=1.0)
        for a in range(4):
            ang = a * math.pi / 2 + math.pi / 4
            p = lambda rr, da: (cx + rr * math.cos(ang + da), cy + rr * math.sin(ang + da))
            c.poly([p(r * 1.0, 0), p(r * .3, .55), p(0, 0), p(r * .3, -.55)], fill=ic)
    elif kind == 'extra':        # two spirals (fusion) as in the original
        for (sx, sy) in ((cx - 3.5, cy - 5), (cx + 3, cy + 5)):
            for rr, a0 in ((3.6, 0), (2.2, 120), (0.9, 240)):
                c.arc((sx - rr, sy - rr, sx + rr, sy + rr), a0, a0 + 270, fill=ic, width=0.8)
    elif kind == 'grave':        # tombstone
        c.rrect((cx - 5.2, cy - 7.5, cx + 5.2, cy + 6), 4.8, fill=(205, 225, 245))
        c.d.rectangle(c.box((cx - 5.2, cy - 1.5, cx + 5.2, cy + 6)), fill=(205, 225, 245))
        c.line([(cx - 2.8, cy - 2.6), (cx + 2.8, cy - 2.6)], fill=(20, 85, 133), width=1.0)
        c.line([(cx - 2.8, cy + 0.6), (cx + 2.8, cy + 0.6)], fill=(20, 85, 133), width=1.0)
        c.rrect((cx - 8, cy + 6, cx + 8, cy + 8.2), 0.8, fill=(150, 200, 235))
    elif kind == 'deck':         # card outline like the original, with the HD back inside
        c.rrect((cx - 7, cy - 9.5, cx + 7, cy + 9.5), 1.4, fill=(220, 232, 250))
        c.paste(CARDBACK, (cx - 6, cy - 8.5, cx + 6, cy + 8.5))
    elif kind == 'banish':       # white star burst
        pts = []
        for i in range(16):
            a = -math.pi / 2 + i * math.pi / 8; rr = w * (.38 if i % 2 == 0 else .12)
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        c.poly(pts, fill=ic)

for i, kind in enumerate(['field', 'extra', 'grave', 'deck', 'banish']):
    x = i * 24
    slot((x, 32, x + 22, 60), kind, False)
    slot((x, 61, x + 22, 89), kind, True)
c.finish(f'{OUT}/eed9528b68475e5d.png')

# ---------------------------------------------------------------- arena floor & walls (128x128)
# same colours as the game (sandy floor, blue walls, light strip), just clean and smooth
K = 8; W = 128 * K
im = Image.new('RGBA', (W, W))
d = ImageDraw.Draw(im, 'RGBA')
for y in range(42 * K):                       # walls: the game's blue, soft vertical gradient
    d.line([(0, y), (W, y)], fill=lerp((52, 92, 122), (66, 110, 140), y / (42 * K)) + (255,))
fh = W - 41 * K
floor = Image.new('RGBA', (W, fh))
fd = ImageDraw.Draw(floor, 'RGBA')
for y in range(fh):
    fd.line([(0, y), (W, y)], fill=lerp((178, 161, 129), (166, 150, 118), y / fh) + (255,))
glow = Image.new('L', floor.size, 0)
ImageDraw.Draw(glow).ellipse((int(14 * K), int(26 * K), int(98 * K), int(84 * K)), fill=110)
glow = glow.filter(ImageFilter.GaussianBlur(110))
floor = Image.composite(Image.new('RGBA', floor.size, (205, 190, 158, 255)), floor, glow)
fd = ImageDraw.Draw(floor, 'RGBA')
floor = floor.filter(ImageFilter.GaussianBlur(2))
im.alpha_composite(floor, (0, 41 * K))
d = ImageDraw.Draw(im, 'RGBA')                # the light strip at the top right (stadium lights)
for y in range(0, 34 * K):
    d.line([(70 * K, y), (W, y)], fill=lerp((255, 255, 255), (200, 212, 225), y / (34 * K)) + (255,))
d.rectangle((70 * K, 34 * K, W, 41 * K), fill=(130, 150, 170, 255))
im.putalpha(255)
im.save(f'{OUT}/0fcadf600e98d99e.png')

# side of the deck / graveyard stacks (16x64, card edges): clean layered edges
im = Image.new('RGBA', (16 * K, 64 * K))
d = ImageDraw.Draw(im)
for x in range(16 * K):
    u = (x % (8 * K)) / (8 * K)
    edge = abs(u - .5) < .07
    col = (120, 64, 30) if edge else lerp((236, 196, 150), (214, 160, 110), abs(u - .5) * 2)
    d.line([(x, 0), (x, 64 * K)], fill=col + (255,))
im.save(f'{OUT}/0bdebcbbf7726fc9.png')
print('ok')
