#!/usr/bin/env python3
"""Index textures inside NSBMD/NSBTX files (RPG3D, RACE, NARC'd monster models/effects) -> hash json."""
import sys, os, json, struct
sys.path.insert(0, os.path.dirname(__file__))
import pac, ygocard as Y, nsbtx as N, tex3d as T
FS = '/home/claude/wc09/fs/Data_arc_pac'
def narc_files(b):
    # NARC: BTAF (fat), BTNF, GMIF
    p = 16; fat = []; gmif = None
    while p + 8 <= len(b):
        m = b[p:p+4]; sz = struct.unpack_from('<I', b, p+4)[0]
        if m == b'BTAF':
            n = struct.unpack_from('<H', b, p+8)[0]
            fat = [struct.unpack_from('<II', b, p+12+8*i) for i in range(n)]
        elif m == b'GMIF': gmif = p + 8
        if sz < 8: break
        p += sz
    for s, e in fat:
        yield b[gmif+s:gmif+e]
def yimd_textures(m):
    """Konami YIMD model: texture table at header +0x48, palettes at +0x50."""
    nt, to, npl, po = struct.unpack_from('<IIII', m, 0x48)
    if nt > 64 or npl > 64: return
    pals = [m[struct.unpack_from('<I', m, po+8*i)[0]: struct.unpack_from('<I', m, po+8*i)[0] + struct.unpack_from('<I', m, po+8*i+4)[0]] for i in range(npl)]
    for i in range(nt):
        e = to + 0x20 * i
        fmt = struct.unpack_from('<I', m, e)[0]; w, h = struct.unpack_from('<HH', m, e+4)
        txo, txs, axo, axs = struct.unpack_from('<IIII', m, e+8)
        if fmt not in range(1, 8) or w not in (8,16,32,64,128,256,512,1024) or h not in (8,16,32,64,128,256,512,1024): continue
        for pal in (pals[i:i+1] or pals[:1] or [b'']):
            for c0t in ((False, True) if fmt in (2, 3, 4) else (False,)):
                yield dict(name=f't{i}', fmt=fmt, w=w, h=h, c0t=c0t, tex=m[txo:txo+txs], aux=m[axo:axo+axs]), pal

def models():
    for f in sorted(os.listdir(FS)):
        if not f.endswith('.pac') or f == 'card.pac': continue
        d = open(f'{FS}/{f}', 'rb').read()
        try: ents = pac.parse(d)
        except Exception: continue
        for n, s, z in ents:
            b = d[s:s+z]
            if b[:1] == b'\x10':
                try: b = Y.lz10(b)
                except Exception: continue
            if b[:4] in (b'BMD0', b'BTX0'): yield f[:-4], n, b
            elif b[:4] == b'NARC':
                for k, sub in enumerate(narc_files(b)):
                    if sub[:1] == b'\x10':
                        try: sub = Y.lz10(sub)
                        except Exception: continue
                    if sub[:4] in (b'BMD0', b'BTX0', b'YIMD'): yield f[:-4], f'{n}#{k}', sub
if __name__ == '__main__':
    only = set(sys.argv[2:])
    out = {}
    for setn, n, b in models():
        if only and setn not in only: continue
        try: pr = list(yimd_textures(b)) if b[:4] == b'YIMD' else N.pairs(b)
        except Exception: continue
        for t, pal in pr:
            if t['fmt'] == 0: continue
            try: buf = T.decode(t['fmt'], t['w'], t['h'], t['tex'], pal or b'', t['aux'], t['c0t'])
            except Exception: continue
            out['%016x' % T.chash(buf, t['w'], t['h'])] = [setn, n, t['name'], t['fmt'], t['w'], t['h'], t['c0t']]
    json.dump(out, open(sys.argv[1], 'w'))
    print(len(out))
