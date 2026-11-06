#!/usr/bin/env python3
"""Compose RPG2D cells (NCER in RPG2D.pac, NCGR/NCLR in RPG2D_2.pac) -> <out>/RPG2D/<stem>_c<cell>.png"""
import sys, os, hashlib, json
sys.path.insert(0, os.path.dirname(__file__))
import pac, ygocard as Y, nitro_sprite as NS
FS = '/home/claude/wc09/fs/Data_arc_pac'
def files(name):
    d = open(f'{FS}/{name}.pac', 'rb').read(); out = {}
    for n, s, z in pac.parse(d):
        b = d[s:s+z]
        out[n] = Y.lz10(b) if b[:1] == b'\x10' else b
    return out
A = files('RPG2D'); B = files('RPG2D_2')
out = sys.argv[1]; os.makedirs(f'{out}/RPG2D', exist_ok=True)
seen = {}; dups = {}; n = 0
for name, ce in A.items():
    if not name.endswith('.lzncer'): continue
    stem = name[:-7]
    if stem.startswith(('f_', 'm_')): continue
    g = B.get(stem + '.lzncgr'); c = B.get(stem + '.lznclr')
    if g is None or c is None: continue
    G = NS.ncgr(g); pal = NS.nclr(c); E = NS.ncer(ce)
    for k, cell in enumerate(E['cells']):
        try: img, org = NS.compose(G, pal, cell, NS.tshift_for(E['mapflags'], False))
        except Exception: continue
        if img is None or img.getextrema()[3][1] == 0: continue
        key = f'RPG2D/{stem}_c{k}'
        h = hashlib.md5(img.tobytes() + bytes(str(img.size), 'ascii')).hexdigest()
        if h in seen: dups[key] = seen[h]; continue
        seen[h] = key; img.save(f'{out}/{key}.png'); n += 1
json.dump(dups, open(f'{out}_dups.json', 'w'))
print(n, len(dups))
