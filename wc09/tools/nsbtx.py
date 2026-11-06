#!/usr/bin/env python3
"""Nitro TEX0 (in BMD0/BTX0) reader: textures + palettes + names, and MDL0 material pairing."""
import struct

def dict_names(d, off):
    # 3D info dictionary: u8 dummy, u8 count, u16 size; unknown block 8+4n; data block 4 + es*n; names 16*n
    cnt = d[off+1]
    unk_size = struct.unpack_from('<H', d, off+6)[0]
    p = off + unk_size
    es, ds = struct.unpack_from('<HH', d, p)
    data = [d[p+4+i*es:p+4+(i+1)*es] for i in range(cnt)]
    np_ = p + ds
    names = [d[np_+16*i:np_+16*(i+1)].split(b'\0')[0].decode('latin-1') for i in range(cnt)]
    return names, data

def tex0(d, base):
    assert d[base:base+4] == b'TEX0'
    texinfo = struct.unpack_from('<H', d, base+0x0E)[0]
    texdata = struct.unpack_from('<I', d, base+0x14)[0]
    t4data = struct.unpack_from('<I', d, base+0x24)[0]
    t4pidx = struct.unpack_from('<I', d, base+0x28)[0]
    palsize = struct.unpack_from('<H', d, base+0x30)[0] << 3
    palinfo = struct.unpack_from('<I', d, base+0x34)[0]
    paldata = struct.unpack_from('<I', d, base+0x38)[0]
    tn, td = dict_names(d, base+texinfo)
    pn, pd = dict_names(d, base+palinfo)
    texs = []
    for name, e in zip(tn, td):
        if len(e) < 4: continue
        param = struct.unpack_from('<I', e, 0)[0]
        fmt = (param >> 26) & 7; w = 8 << ((param >> 20) & 7); h = 8 << ((param >> 23) & 7)
        c0t = bool(param & (1 << 29)); off = (param & 0xFFFF) << 3
        bpp = {1: 8, 2: 2, 3: 4, 4: 8, 5: 2, 6: 8, 7: 16}.get(fmt, 0)
        size = w * h * bpp // 8
        if fmt == 5:
            tex = d[base+t4data+off: base+t4data+off+size]
            aux = d[base+t4pidx+off//2: base+t4pidx+off//2+size//2]
        else:
            tex = d[base+texdata+off: base+texdata+off+size]; aux = b''
        texs.append(dict(name=name, fmt=fmt, w=w, h=h, c0t=c0t, tex=tex, aux=aux))
    pals = {}
    allpal = d[base+paldata: base+paldata+palsize]
    for name, e in zip(pn, pd):
        if len(e) < 2: continue
        off = struct.unpack_from('<H', e, 0)[0] << 3
        pals[name] = allpal[off:]
    return texs, pals

def blocks(d):
    n = struct.unpack_from('<H', d, 0x0E)[0]
    return {d[o:o+4]: o for o in (struct.unpack_from('<I', d, 0x10 + 4*i)[0] for i in range(n))}

def pairs(d):
    """(texture, palette-bytes-or-None) pairs as used by materials; falls back to name matching."""
    b = blocks(d)
    if b'TEX0' not in b: return []
    texs, pals = tex0(d, b[b'TEX0'])
    used = {}
    if b'MDL0' in b:
        try:
            m = b[b'MDL0']
            mn, md = dict_names(d, m + 8)
            for mi, e in enumerate(md):
                mo = m + struct.unpack_from('<I', e, 0)[0]
                matoff = mo + struct.unpack_from('<I', d, mo + 0x08)[0]
                tdo, pdo = struct.unpack_from('<HH', d, matoff)
                ttn, ttd = dict_names(d, matoff + tdo)
                ppn, ppd = dict_names(d, matoff + pdo)
                tmat = {}
                for nm, ee in zip(ttn, ttd):
                    lo, cnt = struct.unpack_from('<HB', ee, 0)
                    for k in range(cnt): tmat.setdefault(d[matoff + lo + k], set()).add(nm)
                for nm, ee in zip(ppn, ppd):
                    lo, cnt = struct.unpack_from('<HB', ee, 0)
                    for k in range(cnt):
                        for t in tmat.get(d[matoff + lo + k], ()): used.setdefault(t, set()).add(nm)
        except Exception:
            used = {}
    out = []
    for t in texs:
        if t['fmt'] == 7: out.append((t, None)); continue
        names = used.get(t['name']) or ({t['name'] + '_pl'} & pals.keys()) or set(pals.keys())
        for pnm in names:
            if pnm in pals: out.append((t, pals[pnm]))
    return out
