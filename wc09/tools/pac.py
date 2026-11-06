#!/usr/bin/env python3
"""Konami WC-series .pac container (reverse engineered from WC2009 US).

Layout:
  name sectors (0x200 each): [u16 lo][u8 hi] table_off = lo + hi*0x200, then
      records [u8 len][u8 ?][name], terminated by 0; next name sector starts again with 3 header bytes.
  table @ table_off: 0x200-byte sectors, each = 8 header bytes + 63 x (u32 start, u32 size); start relative to data base.
  data base = align(table_off + 8 + 8N, 0x200)
"""
import struct, sys, os

def parse(d):
    table_off = (d[0] | d[1] << 8) + d[2] * 0x200
    names = []; sec = 0
    while sec < table_off:
        p = sec + 3
        while p < len(d) and d[p] != 0:
            ln = d[p]; names.append(d[p+2:p+2+ln].decode('latin-1')); p += 2 + ln
        sec += 0x200
        if table_off < sec: break
    # table sectors are 0x200 bytes: an 8-byte header then 63 entries each
    ents = []; t = table_off
    while len(ents) < len(names):
        if (t - table_off) % 0x200 == 0: t += 8
        ents.append(struct.unpack_from('<II', d, t)); t += 8
    base = (t + 0x1FF) & ~0x1FF
    return [(n, base + s, z) for n, (s, z) in zip(names, ents)]

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    ents = parse(d)
    for n, s, z in ents: print(f"{s:#010x} {z:10d} {d[s:s+8].hex()} {n}")
    if len(sys.argv) > 2:
        os.makedirs(sys.argv[2], exist_ok=True)
        for n, s, z in ents: open(os.path.join(sys.argv[2], n), 'wb').write(d[s:s+z])
