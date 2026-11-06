#!/usr/bin/env python3
"""DS 3D texture decoding that reproduces melonDS's texcache (RGB6A5) bit-exactly,
plus the WC09 content hash: XXH64(decoded u32 texels, seed=(w<<16)|h)."""
import numpy as np, xxhash, struct

def rgb6(c):
    c = c.astype(np.uint32)
    r = (c & 0x1F) << 1; g = (c & 0x3E0) >> 4; b = (c & 0x7C00) >> 9
    r = np.where(r > 0, r + 1, 0); g = np.where(g > 0, g + 1, 0); b = np.where(b > 0, b + 1, 0)
    return r | (g << 8) | (b << 16)

def pal16(palbytes, n=None):
    a = np.frombuffer(palbytes[:len(palbytes)//2*2], dtype='<u2').astype(np.uint32)
    if n and len(a) < n: a = np.concatenate([a, np.zeros(n - len(a), np.uint32)])
    return a

def decode(fmt, w, h, tex, pal, aux=b'', c0t=False):
    n = w * h
    if fmt == 7:
        v = np.frombuffer(tex[:n*2], dtype='<u2').astype(np.uint32)
        return rgb6(v) | np.where(v & 0x8000, 0x1F000000, 0).astype(np.uint32)
    if fmt in (1, 6):
        X, Y = (3, 5) if fmt == 1 else (5, 3)
        v = np.frombuffer(tex[:n], dtype=np.uint8).astype(np.uint32)
        p = pal16(pal, 1 << Y)
        col = p[v & ((1 << Y) - 1)]
        a = (v >> Y) & ((1 << X) - 1)
        if X != 5: a = a * 4 + a // 2
        return rgb6(col) | (a << 24)
    if fmt in (2, 3, 4):
        bits = {2: 2, 3: 4, 4: 8}[fmt]
        nb = n * bits // 8
        b = np.frombuffer(tex[:nb], dtype=np.uint8)
        if bits == 8: idx = b.astype(np.uint32)
        else:
            per = 8 // bits
            idx = np.stack([(b >> (bits * k)) & ((1 << bits) - 1) for k in range(per)], 1).reshape(-1).astype(np.uint32)
        p = pal16(pal, 1 << bits)
        col = p[idx]
        alpha = np.where(c0t & (idx == 0), 0, 0x1F000000).astype(np.uint32)
        return rgb6(col) | alpha
    if fmt == 5:
        bw, bh = w // 4, h // 4; nbk = bw * bh
        data = np.frombuffer(tex[:nbk*4], dtype='<u4').astype(np.uint32)
        auxd = np.frombuffer(aux[:nbk*2], dtype='<u2').astype(np.uint32)
        if len(auxd) < nbk: auxd = np.concatenate([auxd, np.zeros(nbk - len(auxd), np.uint32)])
        p = pal16(pal)
        p = np.concatenate([p, np.zeros(4, np.uint32)])
        off = (auxd & 0x3FFF) * 2
        def P(k):
            i = off + k
            return np.where(i < len(p), p[np.minimum(i, len(p) - 1)], 0).astype(np.uint32) | 0x8000
        c0, c1, c2, c3 = P(0), P(1), P(2), P(3)
        m = (auxd >> 14) & 3
        def mix(k0, k1, sh):
            r = ((c0 & 0x1F)*k0 + (c1 & 0x1F)*k1) >> sh
            g = (((c0 & 0x3E0)*k0 + (c1 & 0x3E0)*k1) >> sh) & 0x3E0
            b = (((c0 & 0x7C00)*k0 + (c1 & 0x7C00)*k1) >> sh) & 0x7C00
            return r | g | b | 0x8000
        c2 = np.where(m == 1, mix(1, 1, 1), np.where(m == 3, mix(5, 3, 3), c2))
        c3 = np.where(m == 3, mix(3, 5, 3), np.where(m == 2, c3, 0))
        cols = np.stack([c0, c1, c2, c3], 1)                       # nbk x 4
        cv = rgb6(cols) | np.where(cols & 0x8000, 0x1F000000, 0).astype(np.uint32)
        sel = np.stack([(data >> (2*t)) & 3 for t in range(16)], 1)  # nbk x 16
        px = np.take_along_axis(cv, sel.astype(np.int64), 1)         # nbk x 16
        out = px.reshape(bh, bw, 4, 4).transpose(0, 2, 1, 3).reshape(h, w)
        return out.reshape(-1)
    raise ValueError(fmt)

def chash(buf, w, h):
    return xxhash.xxh64(np.ascontiguousarray(buf, dtype='<u4').tobytes(), seed=(w << 16) | h).intdigest()

def to_rgba(buf, w, h):
    from PIL import Image
    b = buf.astype(np.uint32)
    r = (b & 0xFF) * 255 // 63; g = ((b >> 8) & 0xFF) * 255 // 63; bl = ((b >> 16) & 0xFF) * 255 // 63; a = ((b >> 24) & 0xFF) * 255 // 31
    arr = np.stack([r, g, bl, a], 1).astype(np.uint8).reshape(h, w, 4)
    return Image.fromarray(arr, 'RGBA')
