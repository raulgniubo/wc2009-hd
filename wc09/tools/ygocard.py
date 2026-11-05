#!/usr/bin/env python3
"""Explore WC2009 card.pac: LZ10 decode + Nitro-like chunk (NTBG/NTTX) parsing."""
import struct, sys

PAC = None
def load(path):
    global PAC
    PAC = open(path, 'rb').read()
    return PAC

def lz10(src, off=0):
    assert src[off] == 0x10, hex(src[off])
    size = src[off+1] | (src[off+2] << 8) | (src[off+3] << 16)
    p = off + 4; out = bytearray()
    while len(out) < size:
        flags = src[p]; p += 1
        for bit in range(8):
            if len(out) >= size: break
            if flags & (0x80 >> bit):
                b1, b2 = src[p], src[p+1]; p += 2
                ln = (b1 >> 4) + 3
                disp = (((b1 & 0xF) << 8) | b2) + 1
                for _ in range(ln):
                    out.append(out[-disp])
            else:
                out.append(src[p]); p += 1
    return bytes(out)

def chunks(buf):
    magic = buf[0:4]
    fsize, hsize, n = struct.unpack_from('<IHH', buf, 8)
    p = hsize; res = {'magic': magic[::-1].decode('latin-1'), 'fsize': fsize, 'chunks': []}
    for _ in range(n):
        cm = buf[p:p+4][::-1].decode('latin-1'); cs = struct.unpack_from('<I', buf, p+4)[0]
        res['chunks'].append((cm, p, cs, buf[p+8:p+cs]))
        p += cs
    return res

def bgr555(c):
    r = (c & 31) << 3; g = ((c >> 5) & 31) << 3; b = ((c >> 10) & 31) << 3
    return (r | r >> 5, g | g >> 5, b | b >> 5)

# card.pac layout (table values + 0x200)
BASE = {'bg128': 0x800, 'bg26': 0x2217c00, 'bg26_idx': 0x2394600,
        'tx128': 0x2397400, 'tx128_idx': 0x3dd9800,
        'tx16': 0x3ddc600, 'tx32': 0x3e6da00, 'tx64': 0x405a800}
NCARDS = 2934

def entry(kind, i):
    if kind in ('bg26', 'tx128'):
        idx = BASE[kind + '_idx']
        a, b = struct.unpack_from('<II', PAC, idx + 4*i)
        return PAC[BASE[kind]+a:BASE[kind]+b]
    raise KeyError(kind)

def parse_nttx(buf):
    pal = None; img = None
    p = 16
    while p + 8 <= len(buf):
        cm = buf[p:p+4]; cs = struct.unpack_from('<I', buf, p+4)[0]
        body = buf[p+8:p+cs]
        if cm == b'PALT':
            n = struct.unpack_from('<I', body, 0)[0]
            pal = [bgr555(struct.unpack_from('<H', body, 4 + 2*k)[0]) for k in range(n)]
        elif cm == b'IMGE':
            fmt, sw, sh, _, w, h, dsz = struct.unpack_from('<BBBBHHI', body, 0)
            img = (fmt, w, h, body[12:12+dsz])
        if cs < 8: break
        p += cs
    return pal, img

def tex4x4_to_rgba(pal, w, h, data):
    nb = (w // 4) * (h // 4)
    tex, pidx = data[:nb*4], data[nb*4:nb*6]
    out = bytearray(w * h * 4)
    def mix(a, b, wa, wb, d):
        return tuple((a[i]*wa + b[i]*wb)//d for i in range(3))
    for bi in range(nb):
        bx, by = (bi % (w//4)) * 4, (bi // (w//4)) * 4
        word = struct.unpack_from('<I', tex, bi*4)[0]
        pv = struct.unpack_from('<H', pidx, bi*2)[0]
        base = (pv & 0x3FFF) * 2; mode = pv >> 14
        g = lambda k: pal[base+k] if base+k < len(pal) else (255, 0, 255)
        c0, c1 = g(0), g(1)
        if mode == 0: cols = [c0, c1, g(2), None]
        elif mode == 1: cols = [c0, c1, mix(c0, c1, 1, 1, 2), None]
        elif mode == 2: cols = [c0, c1, g(2), g(3)]
        else: cols = [c0, c1, mix(c0, c1, 5, 3, 8), mix(c0, c1, 3, 5, 8)]
        for t in range(16):
            ci = (word >> (2*t)) & 3
            x, y = bx + (t % 4), by + (t // 4)
            o = (y*w + x)*4; c = cols[ci]
            if c is None: out[o:o+4] = b'\0\0\0\0'
            else: out[o:o+4] = bytes((c[0], c[1], c[2], 255))
    return bytes(out)

BG128_REC = 12184
def bg128_raw(i):
    s = 0x200 + BG128_REC * i
    return PAC[s:s+BG128_REC]

def parse_ntbg_bgdt(buf):
    """Returns (palette, width_px, height_px, rgba) for an NTBG with PALT+BGDT (8bpp tiles + map)."""
    pal = None; out = None
    p = 16
    while p + 8 <= len(buf):
        cm = buf[p:p+4]; cs = struct.unpack_from('<I', buf, p+4)[0]; body = buf[p+8:p+cs]
        if cm == b'PALT':
            n = struct.unpack_from('<I', body, 0)[0]
            pal = [bgr555(struct.unpack_from('<H', body, 4+2*k)[0]) for k in range(n)]
        elif cm == b'BGDT':
            mapsz = struct.unpack_from('<I', body, 4)[0]
            tw, th = struct.unpack_from('<HH', body, 8)
            chrsz = struct.unpack_from('<I', body, 16)[0]
            mp = body[20:20+mapsz]; ch = body[20+mapsz:20+mapsz+chrsz]
            w, h = tw*8, th*8
            px = bytearray(w*h*4)
            for t in range(tw*th):
                e = struct.unpack_from('<H', mp, 2*t)[0]
                ti, hf, vf = e & 0x3FF, (e >> 10) & 1, (e >> 11) & 1
                tx, ty = (t % tw)*8, (t // tw)*8
                for yy in range(8):
                    for xx in range(8):
                        sx = 7-xx if hf else xx; sy = 7-yy if vf else yy
                        ci = ch[ti*64 + sy*8 + sx]
                        c = pal[ci]
                        o = ((ty+yy)*w + tx+xx)*4
                        px[o:o+4] = bytes((c[0], c[1], c[2], 255))
            out = (w, h, bytes(px))
        if cs < 8: break
        p += cs
    return pal, out
