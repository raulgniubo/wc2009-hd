#!/usr/bin/env python3
"""Find every NTTX texture in the ROM's .pac files, decode like melonDS, write tex index (hash -> source)."""
import sys, os, struct, json
sys.path.insert(0, os.path.dirname(__file__))
import pac, ygocard as Y, tex3d as T
FS = '/home/claude/wc09/fs/Data_arc_pac'
def nttx(b):
    pal = img = None; p = 16
    while p + 8 <= len(b):
        cm = b[p:p+4]; cs = struct.unpack_from('<I', b, p+4)[0]; body = b[p+8:p+cs]
        if cm == b'PALT': k = struct.unpack_from('<I', body, 0)[0]; pal = body[4:4+k*2]
        if cm == b'IMGE': img = body
        if cs < 8: break
        p += cs
    return pal, img
def entries():
    for f in sorted(os.listdir(FS)):
        if not f.endswith('.pac') or f in ('card.pac',): continue
        d = open(f'{FS}/{f}', 'rb').read()
        try: ents = pac.parse(d)
        except Exception: continue
        for n, s, z in ents:
            b = d[s:s+z]
            if b[:1] == b'\x10':
                try: b = Y.lz10(b)
                except Exception: continue
            yield f[:-4], n, b
if __name__ == '__main__':
    out = {}
    for setn, n, b in entries():
        if b[:4] != b'NTTX': continue
        pal, img = nttx(b)
        if not img or pal is None: continue
        fmt, sw, sh, _, w, h, dsz = struct.unpack_from('<BBBBHHI', img, 0); data = img[12:12+dsz]
        if fmt == 0 or w * h == 0 or w > 1024 or h > 1024: continue
        for c0t in ((False, True) if fmt in (2, 3, 4) else (False,)):
            try: buf = T.decode(fmt, w, h, data, pal, data[w*h//4:] if fmt == 5 else b'', c0t)
            except Exception as e: continue
            out['%016x' % T.chash(buf, w, h)] = [setn, n, fmt, w, h, c0t]
    json.dump(out, open(sys.argv[1], 'w'))
    print(len(out))
