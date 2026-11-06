#!/usr/bin/env python3
"""Assemble the HD pack: AI-upscaled battle images + game-only card faces -> HDCards layout, zipped."""
import os, sys, zipfile, io
from PIL import Image
SRC = '/home/claude/wc09'
OUT = sys.argv[1]   # zip path
z = zipfile.ZipFile(OUT, 'w', zipfile.ZIP_STORED)
n = 0
def add(img, arc, scale=0.75, quant=True):
    global n
    if scale != 1: img = img.resize((round(img.width*scale), round(img.height*scale)), Image.LANCZOS)
    if quant:
        img = img.convert('RGBA').quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    b = io.BytesIO(); img.save(b, 'PNG', optimize=True)
    z.writestr(arc, b.getvalue()); n += 1
for s in ('1', '2'):
    d = f'{SRC}/bm_hd/R{s}'
    for f in sorted(os.listdir(d)):
        add(Image.open(f'{d}/{f}'), f'HDCards/BattleMonsterR{s}/{f}')
d = f'{SRC}/bm_hd/Lonly'
if os.path.isdir(d):
    for f in sorted(os.listdir(d)):
        s, name = f[1], f[3:]
        add(Image.open(f'{d}/{f}'), f'HDCards/BattleMonsterL{s}/{name}')
d = f'{SRC}/gonly_hd'
if os.path.isdir(d):
    for f in sorted(os.listdir(d)):
        img = Image.open(f'{d}/{f}').convert('RGB')
        b = io.BytesIO(); img.save(b, 'JPEG', quality=92); z.writestr(f'HDCards/{f[:-4]}.jpg', b.getvalue()); n += 1
z.close()
print(n, 'files', os.path.getsize(OUT) // (1 << 20), 'MB')
