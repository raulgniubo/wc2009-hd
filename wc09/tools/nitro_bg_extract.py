#!/usr/bin/env python3
"""Render Nitro BGs (NCGR+NCLR+NSCR with the same stem) of RPG2D_2 / RACE to PNG: <set>/<stem>.png"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
import pac, ygocard as Y, nitro_sprite as NS
from PIL import Image
FS = '/home/claude/wc09/fs/Data_arc_pac'
out = sys.argv[1]; n = 0
for setn in ('RPG2D_2', 'RACE'):
    d = open(f'{FS}/{setn}.pac', 'rb').read()
    files = {}
    for name, s, z in pac.parse(d):
        b = d[s:s+z]
        if b[:1] == b'\x10':
            try: b = Y.lz10(b)
            except Exception: continue
        files[name] = b
    for name, sb in files.items():
        if not name.endswith('.lznscr'): continue
        stem = name[:-7]
        g = files.get(stem + '.lzncgr'); c = files.get(stem + '.lznclr')
        if g is None or c is None: continue
        G = NS.ncgr(g); pal = NS.nclr(c)
        body = NS.blocks(sb)[b'NRCS']
        wpx, hpx = struct.unpack_from('<HH', body, 0); msz = struct.unpack_from('<I', body, 8)[0]
        mp = body[12:12+msz]; tw, th = wpx // 8, hpx // 8
        tb = 64 if G['bpp'] == 8 else 32
        px = bytearray(wpx * hpx * 4)
        for t in range(tw * th):
            e = struct.unpack_from('<H', mp, 2*t)[0]
            ti, hf, vf, pb = e & 0x3FF, (e >> 10) & 1, (e >> 11) & 1, e >> 12
            tile = G['data'][ti*tb:(ti+1)*tb]
            if len(tile) < tb: continue
            tx, ty = (t % tw)*8, (t // tw)*8
            for yy in range(8):
                for xx in range(8):
                    sx = 7-xx if hf else xx; sy = 7-yy if vf else yy
                    if tb == 64: ci = tile[sy*8+sx]; k = ci
                    else:
                        bb = tile[sy*4+sx//2]; ci = (bb >> 4) if (sx & 1) else (bb & 0xF); k = pb*16 + ci
                    if ci == 0: continue
                    col = pal[k] if k < len(pal) else (255, 0, 255)
                    o = ((ty+yy)*wpx + tx+xx)*4; px[o:o+4] = bytes((col[0], col[1], col[2], 255))
        os.makedirs(f'{out}/{setn}', exist_ok=True)
        Image.frombytes('RGBA', (wpx, hpx), bytes(px)).save(f'{out}/{setn}/{stem}.png'); n += 1
print(n)
