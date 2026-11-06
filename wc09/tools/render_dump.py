#!/usr/bin/env python3
"""Render a WC09_DUMP file: each engine's 4 BGs (text modes) + sprite list. render_dump.py DUMP OUTPREFIX"""
import struct, sys
from PIL import Image
d = open(sys.argv[1], 'rb').read(); out = sys.argv[2]
o = 0
ABG = d[o:o+0x80000]; o += 0x80000
BBG = d[o:o+0x20000]; o += 0x20000
AOBJ = d[o:o+0x40000]; o += 0x40000
BOBJ = d[o:o+0x20000]; o += 0x20000
PAL = d[o:o+2048]; o += 2048
OAM = d[o:o+2048]; o += 2048
REGS = [struct.unpack_from('<16I', d, o + 64*e) for e in range(2)]
def col(c): return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)
def pal(e, i): return struct.unpack_from('<H', PAL, e*0x400 + i*2)[0]
def render_bg(e, bg):
    r = REGS[e]; disp = r[0]; cnt = r[1+bg]; sx = r[5+bg*2] & 0x1ff; sy = r[6+bg*2] & 0x1ff
    v = ABG if e == 0 else BBG; mask = len(v) - 1
    cb = ((cnt >> 2) & 15) * 0x4000; sb = ((cnt >> 8) & 31) * 0x800
    if e == 0: cb += ((disp >> 24) & 7) * 0x10000; sb += ((disp >> 27) & 7) * 0x10000
    sz = cnt >> 14; bpp8 = cnt & 0x80
    mw = 64 if sz & 1 else 32; mh = 64 if sz & 2 else 32
    im = Image.new('RGBA', (256, 192), (0, 0, 0, 0)); px = im.load()
    for y in range(192):
        for x in range(256):
            X = (x + sx) % (mw*8); Y = (y + sy) % (mh*8); tx, ty = X//8, Y//8
            blk = (tx >> 5) + ((ty >> 5) * (mw >> 5))
            ent = struct.unpack_from('<H', v, (sb + blk*0x800 + ((ty & 31)*32 + (tx & 31))*2) & mask)[0]
            t = ent & 0x3ff; xx = X % 8; yy = Y % 8
            if ent & 0x400: xx = 7 - xx
            if ent & 0x800: yy = 7 - yy
            if bpp8:
                ci = v[(cb + t*64 + yy*8 + xx) & mask]
                if ci: px[x, y] = (ci, ci, ci, 255)
            else:
                b = v[(cb + t*32 + yy*4 + xx//2) & mask]; ci = (b >> 4) if xx & 1 else b & 15
                if ci: px[x, y] = col(pal(e, (ent >> 12)*16 + ci)) + (255,)
    return im
for e in range(2):
    r = REGS[e]
    print('engine', 'AB'[e], 'dispcnt %08x' % r[0], 'bgcnt', ' '.join('%04x' % r[1+b] for b in range(4)),
          'scroll', ' '.join('%d,%d' % (r[5+b*2], r[6+b*2]) for b in range(4)), 'blend %04x eva/evb %04x swap %d' % (r[13], r[14], r[15]))
    s = Image.new('RGBA', (256*4 + 12, 192), (255, 0, 255, 255))
    for bg in range(4):
        if r[0] & (1 << (8+bg)): s.paste(render_bg(e, bg), (bg*259, 0))
    s.save(f'{out}_{"AB"[e]}.png')
    for k in range(128):
        a0, a1, a2 = struct.unpack_from('<HHH', OAM, e*0x400 + k*8)
        if (a0 >> 8) & 3 == 2: continue
        y = a0 & 0xff; x = a1 & 0x1ff
        if x >= 256: x -= 512
        if y >= 192 and y < 256 - 64: continue
        print('  OAM', 'AB'[e], k, 'x', x, 'y', y, 'a0 %04x a1 %04x a2 %04x' % (a0, a1, a2))
