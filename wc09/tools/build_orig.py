#!/usr/bin/env python3
"""HDCards/orig: HD versions that look like the original, for pictures whose main pack entry is a
redesign (the faithful "HD" mode uses these). Usage: build_orig.py <wc09 source dir> <out HDCards dir>"""
import os, sys, io
from PIL import Image
SRC, OUT = sys.argv[1], sys.argv[2]
ui = set()
for r, ds, fs in os.walk(f'{SRC}/ui_hd'):
    for f in fs: ui.add(os.path.relpath(os.path.join(r, f), f'{SRC}/ui_hd'))
n = 0
for t in ['menu_hd', 'spr_hd', 'menu4_hd', 'tex3d_hd', 'nbg_hd', 'rpgspr_hd']:
    for r, ds, fs in os.walk(f'{SRC}/{t}'):
        for f in fs:
            rel = os.path.relpath(os.path.join(r, f), f'{SRC}/{t}')
            if rel not in ui: continue
            img = Image.open(os.path.join(r, f))
            img = img.resize((round(img.width * 0.75), round(img.height * 0.75)), Image.LANCZOS)
            img = img.convert('RGBA').quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
            dst = f'{OUT}/orig/{rel}'
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            img.save(dst, 'PNG', optimize=True); n += 1
print(n, 'files')
