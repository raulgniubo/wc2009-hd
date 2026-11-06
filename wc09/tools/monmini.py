#!/usr/bin/env python3
"""Mini 3D monsters on the duel field: monmini.pac/monmini_image.bin = concatenated LZ'd NTTX (offsets in
monmini_size.bin), palette from duel3d.pac/monmini.5pl. Yields (index, fmt, w, h, decoded buf)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
import pac, ygocard as Y, tex3d as T
FS = '/home/claude/wc09/fs/Data_arc_pac'
def minis(c0t=True):
    d = open(f'{FS}/monmini.pac', 'rb').read()
    E = {n: (s, z) for n, s, z in pac.parse(d)}
    s, z = E['monmini_image.bin']; img = d[s:s+z]
    ss, sz = E['monmini_size.bin']; sb = d[ss:ss+sz]; sb = Y.lz10(sb) if sb[:1] == b'\x10' else sb
    offs = struct.unpack_from('<%dI' % (len(sb) // 4), sb)
    d3 = open(f'{FS}/duel3d.pac', 'rb').read()
    for n, s3, z3 in pac.parse(d3):
        if 'monmini' in n: pl = d3[s3:s3+z3]; pl = Y.lz10(pl) if pl[:1] == b'\x10' else pl
    k = struct.unpack_from('<I', pl, 0x18)[0]; palt = pl[0x1C:0x1C+2*k]
    done = set()
    for i, o in enumerate(offs):
        if o >= len(img) or o in done: continue
        done.add(o)
        try: b = Y.lz10(img[o:])
        except Exception: continue
        if b[:4] != b'NTTX': continue
        p = 16; im = None
        while p + 8 <= len(b):
            cm = b[p:p+4]; cs = struct.unpack_from('<I', b, p+4)[0]
            if cm == b'IMGE': im = b[p+8:p+cs]
            if cs < 8: break
            p += cs
        if im is None: continue
        fmt, sw, sh, _, w, h, dsz = struct.unpack_from('<BBBBHHI', im, 0)
        yield i, fmt, w, h, T.decode(fmt, w, h, im[12:12+dsz], palt, b'', c0t)
