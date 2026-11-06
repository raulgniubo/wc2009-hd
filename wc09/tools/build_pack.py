#!/usr/bin/env python3
"""Assemble the HD pack: AI-upscaled battle images + game-only card faces -> HDCards layout, zipped."""
import os, sys, zipfile, io
from PIL import Image
SRC = '/home/claude/wc09'
OUT = sys.argv[1]   # zip path
z = zipfile.ZipFile(OUT, 'w', zipfile.ZIP_STORED)
n = 0
written = set()
_ws = z.writestr
def _w(arc, data):
    if arc in written: return
    written.add(arc); z.writestr(arc, data)
def add(img, arc, scale=0.75, quant=True):
    global n, last
    if scale != 1: img = img.resize((round(img.width*scale), round(img.height*scale)), Image.LANCZOS)
    if quant:
        img = img.convert('RGBA').quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    b = io.BytesIO(); img.save(b, 'PNG', optimize=True)
    last = b.getvalue(); _w(arc, last); n += 1
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
        b = io.BytesIO(); img.save(b, 'JPEG', quality=92); _w(f'HDCards/{f[:-4]}.jpg', b.getvalue()); n += 1
import json
# menus and sprites: HDCards/<set>/<name>.png, plus copies for names that share an image
def add_tree(hd, dupfile, flat=None, scale=0.75):
    global n
    if not os.path.isdir(hd): return
    done = {}
    for st in ([None] if flat else sorted(os.listdir(hd))):
        src = hd if flat else f'{hd}/{st}'
        st = flat or st
        for f in sorted(os.listdir(src)):
            if not f.endswith('.png'): continue
            arc = f'HDCards/{st}/{f}'
            add(Image.open(f'{src}/{f}'), arc, scale)
            done[f'{st}/{f[:-4]}'] = last
    if os.path.exists(dupfile):
        for name, canon in json.load(open(dupfile)).items():
            if name != canon and canon in done and name not in done:
                _w(f'HDCards/{name}.png', done[canon]); done[name] = done[canon]; n += 1
add_tree(f'{SRC}/menu_hd', f'{SRC}/menu_dups.json')
add_tree(f'{SRC}/spr_hd', f'{SRC}/spr_dups.json')
add_tree(f'{SRC}/menu4_hd', f'{SRC}/menu4_png_dups.json')
add_tree(f'{SRC}/tex3d_hd', '')
add_tree(f'{SRC}/nbg_hd', '')
add_tree(f'{SRC}/rpgspr_hd', f'{SRC}/rpgspr_png_dups.json')
if len(sys.argv) > 2:   # monster minis go in a second zip to keep each file under 400MB
    z.close(); print(n, 'files', os.path.getsize(OUT) // (1 << 20), 'MB'); n = 0
    OUT = sys.argv[2]; z = zipfile.ZipFile(OUT, 'w', zipfile.ZIP_STORED)
    add_tree(f'{SRC}/mini_hd', '', flat='tex3d', scale=0.5)
    z.close(); print(n, 'files', os.path.getsize(OUT) // (1 << 20), 'MB'); sys.exit()
add_tree(f'{SRC}/mini_hd', '', flat='tex3d', scale=0.5)
z.close()
print(n, 'files', os.path.getsize(OUT) // (1 << 20), 'MB')
