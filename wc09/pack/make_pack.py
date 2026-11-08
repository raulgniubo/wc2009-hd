#!/usr/bin/env python3
"""Turns an old-style HDCards folder into the WC2009-HD pack layout:

  WC2009-HD/
    pack.json          which game it is for
    cards/             card art (passcode.jpg, card_N.jpg, cardback.png)
    hd/                HD pictures that look like the original game
    redesign/          the modern redesign (only used in the "HD + Redesign" mode)
    data/cardtable.bin which texture is which card

Usage: make_pack.py <HDCards folder> <new pack folder> [--move]
--move moves the files instead of copying them (fast, same disk). Leftovers of older updates in
the HDCards folder (HDCards/, r2/, zips) are not part of the pack and are left where they are."""
import os, sys, shutil, json
here = os.path.dirname(os.path.abspath(__file__))
src, dst = sys.argv[1], sys.argv[2]
move = '--move' in sys.argv
redesign = set(open(os.path.join(here, 'redesign_names.txt')).read().split())
skip_dirs = {'HDCards', 'r2', '__MACOSX'}
counts = {'cards': 0, 'hd': 0, 'redesign': 0}
def put(path, rel):
    out = os.path.join(dst, rel)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if move: os.replace(path, out)
    else: shutil.copy2(path, out)
for root, dirs, files in os.walk(src):
    relroot = os.path.relpath(root, src)
    if relroot == '.':
        dirs[:] = [d for d in dirs if d not in skip_dirs]
    for f in files:
        if f.startswith('.') or f.endswith('.zip') or f.endswith('.txt'): continue
        path = os.path.join(root, f)
        rel = f if relroot == '.' else os.path.join(relroot, f).replace(os.sep, '/')
        stem = os.path.splitext(rel)[0]
        if relroot == '.':
            kind = 'cards'                                   # card art lives at the top
        elif rel.startswith('orig/'):
            kind, rel = 'hd', rel[5:]                        # original-look versions of redesigned pictures
        elif rel.startswith('tiles/') or stem in redesign:
            kind = 'redesign'
        else:
            kind = 'hd'
        if kind == 'cards' and not (stem.isdigit() or stem == 'cardback' or stem.startswith('card_')): continue
        put(path, f'{kind}/{rel}')
        counts[kind] += 1
os.makedirs(os.path.join(dst, 'data'), exist_ok=True)
shutil.copy2(os.path.join(here, 'cardtable.bin'), os.path.join(dst, 'data', 'cardtable.bin'))
json.dump({'name': "Yu-Gi-Oh! 5D's World Championship 2009 HD", 'game': 'CY8', 'version': 2,
           'folders': {'cards': 'card art', 'hd': 'HD pictures with the original look',
                       'redesign': 'modern redesign (HD + Redesign mode)'}},
          open(os.path.join(dst, 'pack.json'), 'w'), indent=2)
print(counts)
