#!/usr/bin/env python3
"""Modern HD replacements for the duel alert box, YES/NO buttons and selection frame (tex3d)."""
from PIL import Image, ImageDraw, ImageFont
import os
OUT = '/home/claude/wc09/ui_hd/tex3d'
K = 4          # output texels per original texel
SS = 4         # supersampling
FB = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
NAVY = (16, 28, 62, 255); NAVY2 = (24, 40, 84, 255)
EDGE = (120, 175, 255, 255); HI = (52, 82, 150, 255)

def canvas(w, h): return Image.new('RGBA', (w * K * SS, h * K * SS), (0, 0, 0, 0))
def S(v): return int(round(v * K * SS))
def finish(im, name):
    im = im.resize((im.width // SS, im.height // SS), Image.LANCZOS)
    # the renderer keeps texels with alpha >= 0.5: make edges clean instead of half-transparent
    a = im.getchannel('A').point(lambda v: 255 if v >= 128 else 0)
    im.putalpha(a)
    im.save(f'{OUT}/{name}.png')

def rrect(d, box, r, fill=None, outline=None, width=0):
    d.rounded_rectangle([S(box[0]), S(box[1]), S(box[2]) - 1, S(box[3]) - 1], radius=S(r), fill=fill, outline=outline, width=int(S(width)))

# alert box: 256x32 texture, panel at x 17..239, rows 0..24 (top 0-8, middle 8-16 stretched, bottom 16-24)
im = canvas(256, 32); d = ImageDraw.Draw(im)
rrect(d, (17, 0, 239, 24), 5, fill=EDGE)
rrect(d, (18.5, 1.5, 237.5, 22.5), 3.8, fill=NAVY)
d.line([S(22), S(2.6), S(234), S(2.6)], fill=HI, width=int(S(0.6)))   # soft top highlight
finish(im, 'ef42d746f513ab21')

# buttons: 32x64 textures, the button face is rows 0..24; rows 24..64 keep a plain face
def button(label, letter, name):
    im = canvas(32, 64); d = ImageDraw.Draw(im)
    for y0 in (0, 24):
        rrect(d, (0.5, y0 + 0.5, 31.5, y0 + 23.5), 4, fill=(90, 140, 220, 255))
        # vertical gradient face
        face = Image.new('RGBA', (S(30), S(21)))
        fd = ImageDraw.Draw(face)
        for yy in range(face.height):
            t = yy / face.height
            c = tuple(int(a + (b - a) * t) for a, b in zip((44, 74, 140), (22, 38, 82))) + (255,)
            fd.line([0, yy, face.width, yy], fill=c)
        mask = Image.new('L', face.size, 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, face.width - 1, face.height - 1], radius=S(3.2), fill=255)
        im.paste(face, (S(1), S(y0 + 1.5)), mask)
        f = ImageFont.truetype(FB, S(8.6))
        tw = d.textlength(label, font=f)
        d.text((S(12.5) - tw / 2, S(y0 + 10.5)), label, font=f, fill=(255, 255, 255, 255), anchor='lm')
        # controller-style badge
        cx, cy, r = 26.2, y0 + 17.8, 3.7
        d.ellipse([S(cx - r), S(cy - r), S(cx + r), S(cy + r)], fill=(235, 242, 255, 255))
        fl = ImageFont.truetype(FB, S(5.4))
        d.text((S(cx), S(cy + 0.2)), letter, font=fl, fill=(20, 36, 80, 255), anchor='mm')
    finish(im, name)

button('YES', 'A', 'c76b23565af043b7')
button('NO', 'B', '54fe4b2f58dc7f53')

# selection frame: 64x32 texture, 34x26 outline at the top left, drawn 1 px around the button
im = canvas(64, 32); d = ImageDraw.Draw(im)
rrect(d, (0, 0, 34, 26), 5, outline=(160, 230, 255, 255), width=1.6)
rrect(d, (1.2, 1.2, 32.8, 24.8), 4, outline=(255, 255, 255, 255), width=0.5)
finish(im, '32e24a7a2c65518a')
# the frame pulses between that and a tighter one (32x24 at 1,1)
im = canvas(64, 32); d = ImageDraw.Draw(im)
rrect(d, (1, 1, 33, 25), 4.5, outline=(200, 245, 255, 255), width=1.4)
finish(im, 'c25a665b3197e948')
# the same pictures in other colours (palette swaps per alert, the pulsing frame): structure hashes
import shutil
for h, sh in [('32e24a7a2c65518a', '430e6e41cffb9445'), ('c25a665b3197e948', 'c01cdbddac0ff9db'),
              ('c76b23565af043b7', '7562d1e87fdf8be5'), ('54fe4b2f58dc7f53', 'ef6be0e4d53192d1'),
              ('ef42d746f513ab21', '607a2933bd109728')]:
    shutil.copy(f'{OUT}/{h}.png', f'{OUT}/s_{sh}.png')
for h, hh in [('c76b23565af043b7', '949819e021789a33'), ('54fe4b2f58dc7f53', '44c99e494393ca71')]:
    shutil.copy(f'{OUT}/{h}.png', f'{OUT}/h_{hh}.png')
print('ok')
