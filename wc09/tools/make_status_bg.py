#!/usr/bin/env python3
"""Duel info backgrounds (status_bg*): stripes and dithering replaced by smooth gradients,
details (pentagon, icons, frames) kept from the AI upscale."""
from PIL import Image, ImageFilter
import numpy as np
from scipy.ndimage import uniform_filter1d, gaussian_filter, binary_erosion
src='/home/claude/wc09/menu_png/duel2d'; hd='/home/claude/wc09/menu_hd/duel2d'; out='/home/claude/wc09/ui_hd/duel2d'
for n in ['status_bgbackparam','status_bgbackdetail','status_bgbacknone','status_bgbackspeed','status_bgcarddetail','status_bgtagparam']:
    im=Image.open(f'{src}/{n}.png').convert('RGBA'); a=np.asarray(im).astype(int)
    H,W=a.shape[:2]
    bgcols=set(map(tuple,a[:,W-24:,:3].reshape(-1,3)))
    mask=np.array([[tuple(a[y,x,:3]) in bgcols for x in range(W)] for y in range(H)])
    mask=binary_erosion(mask,iterations=1,border_value=1)
    rgb=a[...,:3].astype(float); w=mask[...,None].astype(float)
    num=uniform_filter1d(rgb*w,size=31,axis=1,mode='nearest'); den=uniform_filter1d(w,size=31,axis=1,mode='nearest')
    sm=np.where(den>0.05,num/np.maximum(den,1e-6),rgb)
    sm=gaussian_filter(sm,sigma=(2.2,4.0,0))
    S=4
    smh=np.asarray(Image.fromarray(sm.clip(0,255).astype(np.uint8)).resize((W*S,H*S),Image.BICUBIC)).astype(float)
    detail=np.asarray(Image.open(f'{hd}/{n}.png').convert('RGB').resize((W*S,H*S),Image.LANCZOS)).astype(float)
    m=Image.fromarray((mask*255).astype(np.uint8)).resize((W*S,H*S),Image.BILINEAR).filter(ImageFilter.GaussianBlur(2))
    m=np.asarray(m).astype(float)[...,None]/255
    o=Image.fromarray((smh*m+detail*(1-m)).clip(0,255).astype(np.uint8)); o.putalpha(255); o.save(f'{out}/{n}.png')
print('ok')
