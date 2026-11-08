#!/usr/bin/env python3
"""Card table (texture hashes -> card, card -> passcode) as a data file for the HD pack:
data/cardtable.bin. Usage: cardtable_to_bin.py <WC09CardTable.h> <out.bin>
Format (little endian): "WC9T" u32 version=1, u32 n, n x (u64 hash, u16 card, u8 size, u8 0),
u32 cards, cards x u32 passcode."""
import re, struct, sys
src = open(sys.argv[1]).read()
tex = re.findall(r'\{0x([0-9a-f]+)ULL, (\d+), (\d+)\}', src)
pc_block = src[src.index('Passcodes[NumCards]'):]
pcs = [int(x) for x in re.findall(r'\b(\d+)\b', pc_block[pc_block.index('{'):pc_block.index('};')])]
with open(sys.argv[2], 'wb') as o:
    o.write(b'WC9T'); o.write(struct.pack('<II', 1, len(tex)))
    for h, c, s in tex: o.write(struct.pack('<QHBB', int(h, 16), int(c), int(s), 0))
    o.write(struct.pack('<I', len(pcs)))
    o.write(struct.pack(f'<{len(pcs)}I', *pcs))
print(len(tex), 'hashes', len(pcs), 'cards')
