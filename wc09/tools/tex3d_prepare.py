#!/usr/bin/env python3
"""Collect every 3D texture we can name by content hash and write <hash>.png for upscaling:
 - textures inside the ROM's models (NSBMD / Konami YIMD) and NTTX files, decoded offline
 - textures dumped from the running game (WC09_DUMP3D), minus card faces (handled separately)"""
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
import tex3d as T, scan_models as SM, scan_tex as ST, nsbtx as N
out = sys.argv[1]; dumpdir = sys.argv[2] if len(sys.argv) > 2 else None
os.makedirs(out, exist_ok=True)
seen = set(); n = 0
def save(buf, w, h):
    global n
    hh = '%016x' % T.chash(buf, w, h)
    if hh in seen: return
    seen.add(hh)
    im = T.to_rgba(buf, w, h)
    if im.getextrema()[3][1] == 0: return          # fully transparent
    im.save(f'{out}/{hh}.png'); n += 1
for setn, name, b in SM.models():
    try: pr = list(SM.yimd_textures(b)) if b[:4] == b'YIMD' else N.pairs(b)
    except Exception: continue
    for t, pal in pr:
        if t['fmt'] == 0: continue
        try: save(T.decode(t['fmt'], t['w'], t['h'], t['tex'], pal or b'', t['aux'], t['c0t']), t['w'], t['h'])
        except Exception: pass
for setn, name, b in ST.entries():
    if b[:4] != b'NTTX': continue
    pal, img = ST.nttx(b)
    if not img or pal is None: continue
    fmt, sw, sh, _, w, h, dsz = struct.unpack_from('<BBBBHHI', img, 0); data = img[12:12+dsz]
    if fmt == 0 or w * h == 0 or w > 1024 or h > 1024: continue
    for c0t in ((False, True) if fmt in (2, 3, 4) else (False,)):
        try: save(T.decode(fmt, w, h, data, pal, data[w*h//4:] if fmt == 5 else b'', c0t), w, h)
        except Exception: pass
if dumpdir:
    cards = {l.split('\t')[1] for l in open('/home/claude/wc09/texhash_us.tsv') if '\t' in l}
    for f in sorted(os.listdir(dumpdir)):
        m = re.match(r'([0-9a-f]{16})_(\d+)x(\d+)_f(\d)_([0-9a-f]{16})\.rgb6a5$', f)
        if not m: continue
        if m.group(5) in cards or m.group(1) in seen: continue
        w, h = int(m.group(2)), int(m.group(3))
        buf = np.frombuffer(open(f'{dumpdir}/{f}', 'rb').read(), dtype='<u4')
        if len(buf) != w * h: continue
        save(buf, w, h)
print(n, 'textures')
