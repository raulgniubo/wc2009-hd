#!/usr/bin/env python3
"""Decode WC2009 BattleMonster *.lz5bg (LZ10 + NTBG: PALT 256 + BGDT 8bpp tiles/map) to RGBA PNG."""
import sys, struct, os
sys.path.insert(0, os.path.dirname(__file__))
import ygocard as Y
from PIL import Image

def decode(raw):
    dec = Y.lz10(raw)
    p = 16; pal = None
    while p + 8 <= len(dec):
        cm = dec[p:p+4]; cs = struct.unpack_from('<I', dec, p+4)[0]; body = dec[p+8:p+cs]
        if cm == b'PALT':
            n = struct.unpack_from('<I', body, 0)[0]
            pal = [Y.bgr555(struct.unpack_from('<H', body, 4+2*k)[0]) for k in range(n)]
        elif cm == b'BGDT':
            mapsz = struct.unpack_from('<I', body, 4)[0]; tw, th = struct.unpack_from('<HH', body, 8)
            chrsz = struct.unpack_from('<I', body, 16)[0]
            mp = body[20:20+mapsz]; ch = body[20+mapsz:20+mapsz+chrsz]
            buf = bytearray(tw*8*th*8*4)
            W = tw*8
            for t in range(tw*th):
                e = struct.unpack_from('<H', mp, 2*t)[0]; ti = e & 0x3FF; hf = (e >> 10) & 1; vf = (e >> 11) & 1
                if ti*64+64 > len(ch): continue
                bx, by = (t % tw)*8, (t // tw)*8
                for yy in range(8):
                    for xx in range(8):
                        sx = 7-xx if hf else xx; sy = 7-yy if vf else yy
                        ci = ch[ti*64+sy*8+sx]
                        if ci == 0: continue
                        c = pal[ci]; o = ((by+yy)*W + bx+xx)*4
                        buf[o:o+4] = bytes((c[0], c[1], c[2], 255))
            return Image.frombytes('RGBA', (W, th*8), bytes(buf)), dict(tw=tw, th=th, mapsz=mapsz, chrsz=chrsz, chars=ch, map=mp)
        if cs < 8: break
        p += cs
    return None, None

if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for f in sorted(os.listdir(src)):
        im, _ = decode(open(os.path.join(src, f), 'rb').read())
        if im: im.save(os.path.join(dst, f.replace('.lz5bg', '.png')))
