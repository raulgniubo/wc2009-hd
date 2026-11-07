#!/usr/bin/env python3
"""Overworld floor stain decals: the game slides these along with the camera; HD made them
crisp enough to notice. Softer and fainter, like the originals at DS resolution."""
from PIL import Image, ImageFilter
import numpy as np, shutil, os
SRC = '/home/claude/livetest/HDCards/tex3d'
OUT = '/home/claude/wc09/ui_hd/tex3d'
for h in ['fdd55c2dc2f8bc32']:
    p = f'{OUT}/{h}.png'
    if not os.path.exists(p): shutil.copy(f'{SRC}/{h}.png', p)
    im = Image.open(f'{SRC}/{h}.png').convert('RGBA')
    im = im.filter(ImageFilter.GaussianBlur(im.width / 64))
    a = np.array(im).astype(float); a[..., 3] *= .6
    Image.fromarray(a.clip(0, 255).astype(np.uint8)).save(p)
print('ok')
