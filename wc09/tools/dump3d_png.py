#!/usr/bin/env python3
"""Convert WC09_DUMP3D .rgb6a5 dumps to PNG (and a labeled contact sheet)."""
import sys, os, re
from PIL import Image, ImageDraw
src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
items = []
for f in sorted(os.listdir(src)):
    m = re.match(r'([0-9a-f]{16})_(\d+)x(\d+)(?:_f\d+_[0-9a-f]+)?\.rgb6a5$', f)
    if not m: continue
    h, w, hh = m.group(1), int(m.group(2)), int(m.group(3))
    d = open(f'{src}/{f}', 'rb').read()
    px = bytearray(w * hh * 4)
    for i in range(w * hh):
        r, g, b, a = d[i*4:i*4+4]
        px[i*4:i*4+4] = bytes((r*255//63, g*255//63, b*255//63, a*255//31))
    im = Image.frombytes('RGBA', (w, hh), bytes(px))
    im.save(f'{out}/{h}.png'); items.append((h, im))
if len(sys.argv) > 3:
    cell = 136; cols = 10; rows = (len(items) + cols - 1) // cols
    sheet = Image.new('RGBA', (cols*cell, rows*(cell+12)), (60, 60, 70, 255)); d = ImageDraw.Draw(sheet)
    for k, (h, im) in enumerate(items):
        t = im.copy(); t.thumbnail((128, 128), Image.NEAREST)
        x, y = (k % cols)*cell, (k // cols)*(cell+12)
        sheet.alpha_composite(t, (x+4, y+4)); d.text((x+4, y+cell-2), f'{k}:{h[:6]}', fill=(255,255,0))
    sheet.save(sys.argv[3])
