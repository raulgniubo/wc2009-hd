#!/usr/bin/env python3
"""Decode Konami NTBG/OBJD sprite images (WC2009) -> RGBA. OBJD: u16 flags, u16 ?, u32 mapsz,
u16 tw, u16 th, u32 ?, u16 tw2, u16 th2, u32 chrsz, map (u32 per tile = char offset in 32-byte units), chars."""
import sys, struct, os
sys.path.insert(0, os.path.dirname(__file__))
import ygocard as Y
from PIL import Image

def decode(raw):
    dec = Y.lz10(raw) if raw[:1] == b'\x10' else raw
    if dec[:4] != b'NTBG': return None, None
    p = 16; pal = None; out = None
    while p + 8 <= len(dec):
        cm = dec[p:p+4]; cs = struct.unpack_from('<I', dec, p+4)[0]; body = dec[p+8:p+cs]
        if cm == b'PALT':
            n = struct.unpack_from('<I', body, 0)[0]
            pal = [Y.bgr555(struct.unpack_from('<H', body, 4+2*k)[0]) for k in range(n)]
        elif cm == b'OBJD' and pal:
            flags, unk, mapsz, tw, th = struct.unpack_from('<HHIHH', body, 0)
            chrsz = struct.unpack_from('<I', body, 20)[0]
            mp = body[24:24+mapsz]; ch = body[24+mapsz:24+mapsz+chrsz]
            n = tw * th
            if mapsz < n * 4 or not chrsz: return None, None
            tb = 64 if chrsz >= n * 64 else 32
            W, H = tw * 8, th * 8
            buf = bytearray(W * H * 4)
            tiles = []
            for t in range(n):
                off = struct.unpack_from('<I', mp, 4*t)[0] * 32
                tile = ch[off:off+tb]; tiles.append(tile)
                bx, by = (t % tw) * 8, (t // tw) * 8
                for i in range(64):
                    if tb == 64: ci = tile[i] if i < len(tile) else 0
                    else:
                        b = tile[i >> 1] if (i >> 1) < len(tile) else 0
                        ci = (b >> 4) if (i & 1) else (b & 0xF)
                    if ci == 0 or ci >= len(pal): continue
                    c = pal[ci]; o = ((by + i // 8) * W + bx + i % 8) * 4
                    buf[o:o+4] = bytes((c[0], c[1], c[2], 255))
            out = (Image.frombytes('RGBA', (W, H), bytes(buf)), dict(tw=tw, th=th, tb=tb, tiles=tiles, flags=flags))
        if cs < 8: break
        p += cs
    return out if out else (None, None)
