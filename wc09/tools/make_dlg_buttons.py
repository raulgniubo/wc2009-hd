#!/usr/bin/env python3
"""HD dialog buttons (dlg_btn*.png from duel2d): OK / YES / NO / BACK / Cancel / Select / HEADS / TAILS /
arrows, in the same navy + light-blue style as the alert box.  These are 3D textures; they are matched
by structure hash (s_, whole texture) and top-half hash (h_), computed here from the game's own images,
so every palette (normal, selected, other alerts) gets the redesign."""
import os, math
import numpy as np, xxhash
from PIL import Image, ImageDraw, ImageFont

SRC = '/home/claude/wc09/menu_png/duel2d'
OUT = '/home/claude/wc09/ui_hd/tex3d'
K, SS = 4, 4
FB = '/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf'
WHITE = (255, 255, 255, 255)
STYLES = {   # edge, face top, face bottom
    'navy': ((120, 175, 255), (44, 74, 140), (22, 38, 82)),
    'blue': ((160, 225, 255), (40, 130, 220), (20, 70, 160)),
    'red': ((255, 170, 180), (220, 60, 80), (140, 20, 40)),
    'green': ((170, 255, 210), (40, 170, 110), (16, 100, 60)),
    'amber': ((255, 225, 150), (230, 150, 40), (150, 80, 10)),
}

def S(v): return int(round(v * K * SS))

def ids_of(im, W, H):
    a = np.array(im.convert('RGBA')); h, w = a.shape[:2]
    ids = np.zeros((H, W), np.uint16); seen = {}
    for y in range(min(h, H)):
        for x in range(min(w, W)):
            p = tuple(a[y, x])
            if p[3] == 0: continue
            if p not in seen: seen[p] = len(seen) + 1
            ids[y, x] = seen[p]
    return ids

def hashes(name, W, H):
    ids = ids_of(Image.open(f'{SRC}/{name}.png'), W, H)
    s = xxhash.xxh64(ids.tobytes(), seed=(W << 16) | H).hexdigest()
    h = xxhash.xxh64(ids.tobytes()[:W * H], seed=(W << 16) | H | (1 << 40)).hexdigest()
    return s, h

def plate(d, box, style, selected):
    edge, top, bot = STYLES[style]
    if selected:
        edge = (235, 250, 255)
        top = tuple(min(255, int(c * 1.25 + 20)) for c in top); bot = tuple(min(255, int(c * 1.2 + 12)) for c in bot)
    x0, y0, x1, y1 = box
    d.rounded_rectangle([S(x0), S(y0), S(x1) - 1, S(y1) - 1], radius=S(4), fill=edge + (255,))
    ix0, iy0, ix1, iy1 = S(x0 + 1.2), S(y0 + 1.2), S(x1 - 1.2), S(y1 - 1.2)
    face = Image.new('RGBA', (ix1 - ix0, iy1 - iy0))
    fd = ImageDraw.Draw(face)
    for yy in range(face.height):
        t = yy / max(1, face.height - 1)
        fd.line([0, yy, face.width, yy], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bot)) + (255,))
    m = Image.new('L', face.size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, face.width - 1, face.height - 1], radius=S(3.2), fill=255)
    d._image.paste(face, (ix0, iy0), m)
    # gloss
    gl = Image.new('RGBA', (ix1 - ix0, (iy1 - iy0) // 2), (255, 255, 255, 34))
    gm = Image.new('L', gl.size, 0)
    ImageDraw.Draw(gm).rounded_rectangle([S(.8), S(.6), gl.width - S(.8), gl.height + S(3)], radius=S(2.6), fill=255)
    d._image.alpha_composite(Image.composite(gl, Image.new('RGBA', gl.size), gm), (ix0, iy0))

def text(d, xy, s, size, fill=WHITE):
    f = ImageFont.truetype(FB, S(size))
    d.text((S(xy[0]), S(xy[1])), s, font=f, fill=fill, anchor='mm',
           stroke_width=max(1, S(size * .09)), stroke_fill=(8, 14, 34, 255))

def badge(d, cx, cy, letter, r=3.6):
    d.ellipse([S(cx - r), S(cy - r), S(cx + r), S(cy + r)], fill=(235, 242, 255, 255))
    f = ImageFont.truetype(FB, S(r * 1.45))
    d.text((S(cx), S(cy + .1)), letter, font=f, fill=(20, 36, 80, 255), anchor='mm')

def icon(d, kind, cx, cy, s):
    w = S(s * .16)
    if kind == 'back':
        d.arc([S(cx - s * .38), S(cy - s * .36), S(cx + s * .42), S(cy + s * .4)], 260, 100 + 360, fill=WHITE, width=w)
        d.polygon([(S(cx - s * .55), S(cy - s * .3)), (S(cx - s * .08), S(cy - s * .62)), (S(cx - s * .08), S(cy + .02 * s))], fill=WHITE)
    elif kind == 'check':
        d.line([(S(cx - s * .42), S(cy)), (S(cx - s * .1), S(cy + s * .3)), (S(cx + s * .45), S(cy - s * .35))], fill=WHITE, width=w, joint='curve')
    elif kind == 'cross':
        d.line([(S(cx - s * .33), S(cy - s * .33)), (S(cx + s * .33), S(cy + s * .33))], fill=WHITE, width=w)
        d.line([(S(cx - s * .33), S(cy + s * .33)), (S(cx + s * .33), S(cy - s * .33))], fill=WHITE, width=w)
    elif kind == 'pointer':
        d.polygon([(S(cx - s * .3), S(cy - s * .45)), (S(cx + s * .35), S(cy + s * .05)), (S(cx + s * .02), S(cy + s * .1)),
                   (S(cx - s * .12), S(cy + s * .45))], fill=WHITE)
    elif kind in ('heads', 'tails'):
        r = s * .42
        d.ellipse([S(cx - r), S(cy - r), S(cx + r), S(cy + r)], fill=(250, 206, 70, 255), outline=(130, 80, 6, 255), width=S(.7))
        text(d, (cx, cy + .2), 'H' if kind == 'heads' else 'T', s * .5, fill=(120, 66, 0, 255))

def finish(im, W, H, names):
    im = im.resize((W * K, H * K), Image.LANCZOS)
    a = im.getchannel('A').point(lambda v: 255 if v >= 128 else 0); im.putalpha(a)
    for n in names: im.save(f'{OUT}/{n}.png')

# small buttons: 32x48 (two 24-row halves: normal, selected), texture 32x64
SMALL = {   # name: (label, badge, icon, style)
    'dlg_btn_back': (None, 'B', 'back', 'navy'), 'dlg_btn_decide': (None, 'A', 'check', 'navy'),
    'dlg_btn_ok': ('OK', 'A', None, 'navy'), 'dlg_btn_ok_2': ('OK', None, None, 'navy'),
    'dlg_btn_ok_t': ('OK', 'A', None, 'amber'),
    'dlg_btne_yes': ('YES', 'A', None, 'navy'), 'dlg_btne_no': ('NO', 'B', None, 'navy'),
    'dlg_btne_coin0': ('HEADS', None, 'heads', 'navy'), 'dlg_btne_coin1': ('TAILS', None, 'tails', 'navy'),
}
for name, (label, bdg, ic, style) in SMALL.items():
    im = Image.new('RGBA', (S(32), S(64)), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    for half in (0, 1):
        y = half * 24
        plate(d, (0.5, y + 0.5, 31.5, y + 23.5), style, bool(half))
        if ic in ('heads', 'tails'):
            icon(d, ic, 16, y + 8.6, 11); text(d, (16, y + 18.2), label, 6.2)
        elif ic:
            icon(d, ic, 13 if bdg else 16, y + 11.5, 13)
        elif label:
            text(d, (12.8 if bdg else 16, y + 11.6), label, 9.6 if len(label) <= 2 else 8.2)
        if bdg: badge(d, 26.3, y + 17.6, bdg)
    s, h = hashes(name, 32, 64)
    finish(im, 32, 64, [f's_{s}', f'h_{h}'])

# arrows (32x48: two 24-row frames)
for name, left in (('dlg_btn_left', True), ('dlg_btn_right', False)):
    im = Image.new('RGBA', (S(32), S(64)), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    for half in (0, 1):
        y = half * 24; col = (230, 245, 255, 255) if half else (150, 205, 255, 255)
        pts = [(4, y + 12), (27, y + 2), (27, y + 22)] if left else [(28, y + 12), (5, y + 2), (5, y + 22)]
        d.polygon([(S(x), S(yy)) for x, yy in pts], fill=(10, 20, 50, 255))
        cx = sum(p[0] for p in pts) / 3; cy = y + 12
        inner = [(cx + (x - cx) * .78, cy + (yy - cy) * .78) for x, yy in pts]
        d.polygon([(S(x), S(yy)) for x, yy in inner], fill=col)
    s, h = hashes(name, 32, 64)
    finish(im, 32, 64, [f's_{s}', f'h_{h}'])

# big buttons: 48x64 (two 32-row halves), texture 64x64; icon above the label like the originals
BIG = {
    'dlg_btn_list_bk': ('BACK', 'back', 'navy'), 'dlg_btn_list_ok': ('OK', 'check', 'navy'),
    'dlg_btn_result': ('OK', None, 'red'), 'dlg_btn_space': (None, None, 'navy'),
    'dlg_btne_back': ('BACK', 'back', 'green'), 'dlg_btne_list_bk': ('BACK', 'back', 'navy'),
    'dlg_btne_list_cl': ('Cancel', 'cross', 'red'), 'dlg_btne_list_sl': ('Select', 'pointer', 'navy'),
    'dlg_btne_no2': ('NO', 'cross', 'red'), 'dlg_btne_yes2': ('YES', 'check', 'blue'),
}
for name, (label, ic, style) in BIG.items():
    im = Image.new('RGBA', (S(64), S(64)), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    for half in (0, 1):
        y = half * 32
        plate(d, (0.5, y + 0.5, 47.5, y + 31.5), style, bool(half))
        if ic and label:
            icon(d, ic, 24, y + 11, 13); text(d, (24, y + 24), label, 8.4 if len(label) <= 4 else 7.6)
        elif label:
            text(d, (24, y + 16), label, 13)
    s, h = hashes(name, 64, 64)
    finish(im, 64, 64, [f's_{s}', f'h_{h}'])
print('ok')
