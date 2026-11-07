#!/usr/bin/env python3
"""Deck editor (trunk) sprite cells: buttons restyled (clean faces, smoothed icons, the game's
outline ring kept for selection colours); small icons smoothed."""
import os, sys, glob, re
sys.path.insert(0, os.path.dirname(__file__))
from PIL import Image
from restyle import restyle
from pixsmooth import smooth
NAT = '/home/claude/wc09/nit_png/trunk'
OUT = '/home/claude/wc09/ui_hd/trunk'
os.makedirs(OUT, exist_ok=True)
n = 0
for f in sorted(glob.glob(f'{NAT}/*.png')):
    name = os.path.basename(f)[:-4]
    if re.search(r'_(fr|ge|it|sp|kr|jp)_c\d+$', name): continue
    im = Image.open(f).convert('RGBA')
    w, h = im.size
    bb = im.getchannel('A').getbbox()
    if not bb: continue
    bw, bh = bb[2] - bb[0], bb[3] - bb[1]
    big = bw >= 20 and bh >= 16                  # buttons / plates
    if w >= 100 or h >= 100: continue            # scroll bars, frames: leave to the game
    if big:
        r = restyle(im)
        if r is None: continue
    else:
        r = smooth(im, 8, merge=24)
    r.save(f'{OUT}/{name}.png'); n += 1
print(n, 'cells')
