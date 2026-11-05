#!/usr/bin/env python3
"""Minimal NDS ROM header + file system lister/extractor (read-only on the ROM)."""
import struct, sys, os, json

def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u16(b, o): return struct.unpack_from('<H', b, o)[0]

def parse(path):
    rom = open(path, 'rb').read()
    h = {
        'title': rom[0:12].rstrip(b'\0').decode('ascii', 'replace'),
        'gamecode': rom[0x0C:0x10].decode('ascii', 'replace'),
        'maker': rom[0x10:0x12].decode('ascii', 'replace'),
        'version': rom[0x1E],
        'arm9_off': u32(rom, 0x20), 'arm9_ram': u32(rom, 0x28), 'arm9_size': u32(rom, 0x2C),
        'arm7_off': u32(rom, 0x30), 'arm7_size': u32(rom, 0x3C),
        'fnt_off': u32(rom, 0x40), 'fnt_size': u32(rom, 0x44),
        'fat_off': u32(rom, 0x48), 'fat_size': u32(rom, 0x4C),
        'ov9_off': u32(rom, 0x50), 'ov9_size': u32(rom, 0x54),
        'used_size': u32(rom, 0x80), 'header_crc': u16(rom, 0x15E),
    }
    fat_off = h['fat_off']
    nfiles = h['fat_size'] // 8
    fat = [(u32(rom, fat_off + i*8), u32(rom, fat_off + i*8 + 4)) for i in range(nfiles)]
    fnt = h['fnt_off']
    files = {}
    def walk(dir_id, prefix):
        ent = fnt + (dir_id & 0xFFF) * 8
        sub_off = u32(rom, ent); first_id = u16(rom, ent + 4)
        p = fnt + sub_off; fid = first_id
        while True:
            t = rom[p]; p += 1
            if t == 0: break
            ln = t & 0x7F
            name = rom[p:p+ln].decode('latin-1'); p += ln
            if t & 0x80:
                sub = u16(rom, p); p += 2
                walk(sub, prefix + name + '/')
            else:
                files[fid] = prefix + name; fid += 1
    walk(0xF000, '')
    return rom, h, fat, files

if __name__ == '__main__':
    rom, h, fat, files = parse(sys.argv[1])
    mode = sys.argv[2] if len(sys.argv) > 2 else 'list'
    if mode == 'header':
        print(json.dumps(h, indent=1))
    elif mode == 'list':
        for fid in sorted(files):
            s, e = fat[fid]
            print(f"{fid:5d}\t{e-s:10d}\t{rom[s:s+4]!r}\t{files[fid]}")
    elif mode == 'extract':
        out = sys.argv[3]
        for fid, name in files.items():
            s, e = fat[fid]
            dst = os.path.join(out, name)
            os.makedirs(os.path.dirname(dst) or out, exist_ok=True)
            open(dst, 'wb').write(rom[s:e])
        # overlays + arm9
        open(os.path.join(out, '_arm9.bin'), 'wb').write(rom[h['arm9_off']:h['arm9_off']+h['arm9_size']])
        print(f"extracted {len(files)} files")
