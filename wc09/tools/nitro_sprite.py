#!/usr/bin/env python3
"""Minimal Nitro NCGR/NCLR/NCER decoder: compose each NCER cell into an RGBA image.
Returns per cell: image, origin (x0,y0) and the OAM list with (tile, w, h, x, y, 8bpp, pal, flips)."""
import sys, struct, os
sys.path.insert(0, os.path.dirname(__file__))
import ygocard as Y
from PIL import Image

SIZES = {(0, 0): (8, 8), (0, 1): (16, 16), (0, 2): (32, 32), (0, 3): (64, 64),
         (1, 0): (16, 8), (1, 1): (32, 8), (1, 2): (32, 16), (1, 3): (64, 32),
         (2, 0): (8, 16), (2, 1): (8, 32), (2, 2): (16, 32), (2, 3): (32, 64)}

def unlz(raw):
    return Y.lz10(raw) if raw[:1] == b'\x10' else raw

def blocks(d):
    hs = struct.unpack_from('<H', d, 12)[0]; n = struct.unpack_from('<H', d, 14)[0]
    p = hs; out = {}
    for _ in range(n):
        m = d[p:p+4]; s = struct.unpack_from('<I', d, p+4)[0]
        out[m] = d[p+8:p+s]; p += s
    return out

def ncgr(d):
    b = blocks(d)[b'RAHC']
    th, tw, bpp_code = struct.unpack_from('<HHI', b, 0)
    mapping = struct.unpack_from('<I', b, 8)[0]
    size, off = struct.unpack_from('<II', b, 16)
    data = b[off:off + size]
    return dict(bpp=8 if bpp_code == 4 else 4, data=data, mapping=mapping)

def nclr(d):
    b = blocks(d)[b'TTLP']
    bpp_code, _, size, off = struct.unpack_from('<IIII', b, 0)
    raw = b[off:off + size]
    return [Y.bgr555(struct.unpack_from('<H', raw, 2*i)[0]) for i in range(len(raw) // 2)]

def ncer(d):
    b = blocks(d)[b'KBEC']
    ncell, attrtype, celloff, mapflags = struct.unpack_from('<HHII', b, 0)
    stride = 16 if attrtype == 1 else 8
    cells = []
    base = celloff
    oamstart = base + ncell * stride
    for c in range(ncell):
        nobj, attr, ooff = struct.unpack_from('<HHI', b, base + c * stride)
        objs = []
        for o in range(nobj):
            a0, a1, a2 = struct.unpack_from('<HHH', b, oamstart + ooff + o * 6)
            shape, size = a0 >> 14, a1 >> 14
            w, h = SIZES.get((shape, size), (8, 8))
            y = a0 & 0xFF; y = y - 256 if y >= 128 else y
            x = a1 & 0x1FF; x = x - 512 if x >= 256 else x
            objs.append(dict(tile=a2 & 0x3FF, pal=a2 >> 12, w=w, h=h, x=x, y=y,
                             bpp8=bool(a0 & (1 << 13)), hf=bool(a1 & (1 << 12)) and not (a0 & 0x100),
                             vf=bool(a1 & (1 << 13)) and not (a0 & 0x100)))
        cells.append(objs)
    return dict(cells=cells, mapflags=mapflags)

def tile_bytes(g, obj, tshift):
    tb = 64 if obj['bpp8'] else 32
    n = (obj['w'] // 8) * (obj['h'] // 8)
    start = (obj['tile'] << tshift)
    return g['data'][start:start + n * tb], tb

def compose(g, pal, cell, tshift):
    if not cell: return None, (0, 0)
    x0 = min(o['x'] for o in cell); y0 = min(o['y'] for o in cell)
    x1 = max(o['x'] + o['w'] for o in cell); y1 = max(o['y'] + o['h'] for o in cell)
    W, H = x1 - x0, y1 - y0
    buf = bytearray(W * H * 4)
    for o in reversed(cell):   # first OAM is on top
        data, tb = tile_bytes(g, o, tshift)
        tw = o['w'] // 8
        for t in range((o['w'] // 8) * (o['h'] // 8)):
            tile = data[t*tb:(t+1)*tb]
            if len(tile) < tb: continue
            tx, ty = (t % tw) * 8, (t // tw) * 8
            for i in range(64):
                if tb == 64: ci = tile[i]
                else:
                    bb = tile[i >> 1]; ci = (bb >> 4) if (i & 1) else (bb & 0xF)
                if ci == 0: continue
                ci += 0 if tb == 64 else o['pal'] * 16
                if ci >= len(pal): continue
                px, py = tx + i % 8, ty + i // 8
                if o['hf']: px = o['w'] - 1 - px
                if o['vf']: py = o['h'] - 1 - py
                X, Yy = o['x'] - x0 + px, o['y'] - y0 + py
                c = pal[ci]; k = (Yy * W + X) * 4
                buf[k:k+4] = bytes((c[0], c[1], c[2], 255))
    return Image.frombytes('RGBA', (W, H), bytes(buf)), (x0, y0)

def tshift_for(mapflags, bpp8):
    # 1D mapping boundary from NCER mapping flags (0=32B,1=64,2=128,3=256 byte units)
    return 5 + (mapflags & 3)
