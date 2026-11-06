#!/usr/bin/env python3
"""Render every 4bpp NTBG/BGDT image of the menu sets to RGBA PNG (index 0 transparent).
Names match WC09Battle's index: <last path component of set>/<entry stem>."""
import sys, os, struct, json, hashlib
sys.path.insert(0, os.path.dirname(__file__))
import pac, ygocard as Y
from PIL import Image
FS = '/home/claude/wc09/fs/Data_arc_pac'
SETS = ["duel2d/field", "duel2d", "CharaMake", "Closet", "DCustom", "D_race_result", "DataRoom", "Dsimulator",
        "FreeDuel", "FriendMenu", "Getpack", "MainMenu", "Name", "Password", "Record", "Structure",
        "TagSetting", "WiFi", "WiFiDownload", "WiFiRanking", "WordSelect", "janken", "logo", "trunk",
        "Common", "cpuduel"]
out = sys.argv[1]; seen = {}; dups = {}; n = 0
for sp in SETS:
    path = f'{FS}/{sp}.pac'
    if not os.path.exists(path): continue
    d = open(path, 'rb').read(); setn = sp.split('/')[-1]
    for name, s, z in pac.parse(d):
        b = d[s:s+z]
        if b[:1] == b'\x10':
            try: b = Y.lz10(b)
            except Exception: continue
        if b[:4] != b'NTBG': continue
        pal = None; p = 16; img = None
        while p + 8 <= len(b):
            cm = b[p:p+4]; cs = struct.unpack_from('<I', b, p+4)[0]; body = b[p+8:p+cs]
            if cm == b'PALT':
                k = struct.unpack_from('<I', body, 0)[0]
                pal = [Y.bgr555(struct.unpack_from('<H', body, 4+2*i)[0]) for i in range(k)]
            elif cm == b'BGDT' and body[0] == 0: img = body
            if cs < 8: break
            p += cs
        if img is None or pal is None: continue
        mapsz = struct.unpack_from('<I', img, 4)[0]; tw, th = struct.unpack_from('<HH', img, 8)
        chrsz = struct.unpack_from('<I', img, 16)[0]
        mp = img[20:20+mapsz]; ch = img[20+mapsz:20+mapsz+chrsz]
        w, h = tw*8, th*8; px = bytearray(w*h*4)
        for t in range(tw*th):
            e = struct.unpack_from('<H', mp, 2*t)[0]
            ti, hf, vf, pb = e & 0x3FF, (e >> 10) & 1, (e >> 11) & 1, e >> 12
            tx, ty = (t % tw)*8, (t // tw)*8
            tile = ch[ti*32:ti*32+32]
            if len(tile) < 32: continue
            for yy in range(8):
                for xx in range(8):
                    sx = 7-xx if hf else xx; sy = 7-yy if vf else yy
                    bb = tile[sy*4 + sx//2]; ci = (bb >> 4) if (sx & 1) else (bb & 0xF)
                    if ci == 0: continue
                    k = pb*16 + ci
                    c = pal[k] if k < len(pal) else (255, 0, 255)
                    o = ((ty+yy)*w + tx+xx)*4; px[o:o+4] = bytes((c[0], c[1], c[2], 255))
        key = f'{setn}/{name.split(".")[0]}'
        hsh = hashlib.md5(bytes(px) + bytes([w & 255, h & 255])).hexdigest()
        if hsh in seen: dups[key] = seen[hsh]; continue
        seen[hsh] = key
        os.makedirs(f'{out}/{setn}', exist_ok=True)
        Image.frombytes('RGBA', (w, h), bytes(px)).save(f'{out}/{key.split("/")[1]}.png' if False else f'{out}/{key}.png'); n += 1
json.dump(dups, open(f'{out}_dups.json', 'w'))
print(n, 'images,', len(dups), 'dups')
