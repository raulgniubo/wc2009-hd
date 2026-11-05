#!/usr/bin/env python3
"""PoC: render an HD card face (text from the game's own card DB) and composite it
over an emulator frame using the 3D polygon the game drew for that card.

Art is a PLACEHOLDER (native art upscaled) until the user supplies real HD art:
  put <passcode>.png / .jpg into an art folder and pass --art DIR.
"""
import csv, struct, sys, os, textwrap
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
sys.path.insert(0, os.path.dirname(__file__))
import ygocard as Y

W, H = 696, 1024           # HD card face = 8x the 87x128 texel card region
FONT_DIR = '/usr/share/fonts/truetype/dejavu/'

def card_db(csv_path, bin2):
    rows = {int(r['index']): r for r in csv.DictReader(open(csv_path))}
    ix = open(bin2 + '/card_indx_e.bin', 'rb').read(); ds = open(bin2 + '/card_desc_e.bin', 'rb').read()
    def desc(i):
        _, b = struct.unpack_from('<II', ix, 8*i); return ds[b:ds.index(b'\0', b)].decode('latin-1')
    return rows, desc

def stats(r):
    a, b = int(r['prop0']), int(r['prop1'])
    return ((a >> 14) & 0x1FF) * 10, ((a >> 23) & 0x1FF) * 10, (b >> 10) & 0xF

def render(idx, native, rows, desc, type_line, art_dir=None):
    r = rows[idx]; atk, dfn, lvl = stats(r)
    # card region of the 128x128 texture (texels 20.5..107.5 wide, 0..128 tall)
    reg = native.crop((20, 0, 108, 128))
    border = np.array(reg.convert('RGB'))[30:100, 3:9].reshape(-1, 3)
    base = tuple(int(c) for c in np.median(border, axis=0))  # frame color from the card's left border
    img = reg.resize((W, H), Image.LANCZOS).filter(ImageFilter.GaussianBlur(6))
    d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W, H), fill=base)
    d.rounded_rectangle((6, 6, W-7, H-7), 18, outline=(60, 50, 40), width=6)
    # name bar
    d.rectangle((40, 40, W-40, 120), fill=tuple(min(255, c+25) for c in base), outline=(90, 70, 40), width=4)
    f_name = ImageFont.truetype(FONT_DIR + 'DejaVuSerif-Bold.ttf', 52)
    d.text((60, 52), r['name'], font=f_name, fill=(20, 15, 10))
    # level stars
    for k in range(lvl):
        cx, cy, rad = W - 85 - k*58, 160, 24
        d.ellipse((cx-rad, cy-rad, cx+rad, cy+rad), fill=(200, 40, 20), outline=(120, 20, 10), width=3)
        pts = [(cx + rad*0.75*np.cos(np.pi/2 + j*np.pi*4/5), cy - rad*0.75*np.sin(np.pi/2 + j*np.pi*4/5)) for j in range(5)]
        d.polygon(pts, fill=(255, 220, 60))
    # art box
    ab = (86, 200, W-86, 200 + (W-172))
    d.rectangle((ab[0]-8, ab[1]-8, ab[2]+8, ab[3]+8), fill=(60, 50, 40))
    art = None
    if art_dir and r['passcode'] != '0':
        for ext in ('png', 'jpg', 'jpeg', 'webp'):
            p = os.path.join(art_dir, f"{r['passcode']}.{ext}")
            if os.path.exists(p): art = Image.open(p).convert('RGB'); break
    if art is None:   # placeholder: native art upscaled
        native_art = native.crop((20 + 11, 26, 20 + 11 + 65, 26 + 65))
        art = native_art.resize((ab[2]-ab[0], ab[3]-ab[1]), Image.LANCZOS)
        img.paste(art, ab[:2])
        f_tag = ImageFont.truetype(FONT_DIR + 'DejaVuSans-Bold.ttf', 22)
        d.rectangle((ab[0], ab[3]-34, ab[2], ab[3]), fill=(0, 0, 0))
        d.text((ab[0]+10, ab[3]-31), 'ART PLACEHOLDER: drop HD art here', font=f_tag, fill=(255, 255, 255))
    else:
        img.paste(art.resize((ab[2]-ab[0], ab[3]-ab[1]), Image.LANCZOS), ab[:2])
    # text box
    tb = (40, ab[3] + 30, W-40, H-40)
    d.rectangle(tb, fill=(245, 235, 210), outline=(90, 70, 40), width=4)
    f_type = ImageFont.truetype(FONT_DIR + 'DejaVuSerif-Bold.ttf', 26)
    f_desc = ImageFont.truetype(FONT_DIR + 'DejaVuSerif.ttf', 23)
    f_stat = ImageFont.truetype(FONT_DIR + 'DejaVuSans-Bold.ttf', 26)
    d.text((tb[0]+14, tb[1]+10), type_line, font=f_type, fill=(20, 15, 10))
    y = tb[1] + 48
    for line in textwrap.wrap(desc(idx), 48):
        d.text((tb[0]+14, y), line, font=f_desc, fill=(20, 15, 10)); y += 28
    d.line((tb[0]+14, tb[3]-46, tb[2]-14, tb[3]-46), fill=(60, 50, 40), width=2)
    s = f'ATK/{atk}  DEF/{dfn}'
    d.text((tb[2]-14 - d.textlength(s, font=f_stat), tb[3]-40), s, font=f_stat, fill=(20, 15, 10))
    return img

def homography(src, dst):
    """coeffs mapping dst(x,y) -> src(u,v) for PIL PERSPECTIVE."""
    A, B = [], []
    for (u, v), (x, y) in zip(src, dst):
        A.append([x, y, 1, 0, 0, 0, -u*x, -u*y]); B.append(u)
        A.append([0, 0, 0, x, y, 1, -v*x, -v*y]); B.append(v)
    return np.linalg.solve(np.array(A, float), np.array(B, float)).tolist()

def composite(frame, poly, hd, scale=4, s0=20.5, s1=107.5):
    """frame: 256x384 emulator output; poly: [(x,y,s,t)] of the card quad (top screen coords)."""
    out = frame.resize((frame.width*scale, frame.height*scale), Image.NEAREST)
    dst = [(x*scale, y*scale) for x, y, s, t in poly]
    src = [((s/16 - s0) / (s1 - s0) * hd.width, (t/16) / 128 * hd.height) for x, y, s, t in poly]
    coeffs = homography(src, dst)
    warped = hd.convert('RGBA').transform(out.size, Image.PERSPECTIVE, coeffs, Image.BICUBIC)
    mask = Image.new('L', out.size, 0); ImageDraw.Draw(mask).polygon(dst, fill=255)
    out.paste(warped, (0, 0), mask)
    return out
