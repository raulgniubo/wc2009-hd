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
    mask=binary_erosion(mask,iterations=1,border_value=1); mask0=mask
    rgb=a[...,:3].astype(float)
    # the background only changes from row to row: per-row colour of the plain pixels, stripes
    # smoothed inside each band, band edges kept sharp
    prof=np.array([np.median(rgb[y][mask[y]],axis=0) if mask[y].sum()>8 else rgb[y].mean(0) for y in range(H)])
    lum=prof.sum(1); cuts=[0]
    for y in range(1,H):
        if abs(lum[y]-lum[y-1])>150 and y-cuts[-1]>2 and (y+1>=H or abs(lum[min(H-1,y+1)]-lum[y-1])>150): cuts.append(y)
    cuts.append(H)
    sp=prof.copy()
    for a0,a1 in zip(cuts[:-1],cuts[1:]):
        seg=prof[a0:a1]
        if len(seg)>=3: sp[a0:a1]=gaussian_filter(seg,sigma=(min(3.0,len(seg)/3),0),mode='nearest')
    sm=np.repeat(sp[:,None,:],W,axis=1)
    # objects (pentagon, icons, frames) = non-background regions that contain pixels far from
    # their row's colour; stripes, faint arcs and dithering become the clean gradient
    from scipy.ndimage import binary_opening, label
    rawprof=np.repeat(prof[:,None,:],W,axis=1)
    far=binary_opening(np.abs(rgb-rawprof).sum(-1)>110,iterations=1)
    lab,nl=label(~mask0)
    keep=np.zeros(nl+1,bool); keep[np.unique(lab[far])]=True; keep[0]=False
    mask=~keep[lab]
    if n=='status_bgbackspeed':   # artwork all over: old per-row x smoothing only
        w=mask0[...,None].astype(float)
        num=uniform_filter1d(rgb*w,size=31,axis=1,mode='nearest'); den=uniform_filter1d(w,size=31,axis=1,mode='nearest')
        sm=np.where(den>0.05,num/np.maximum(den,1e-6),rgb); sm=gaussian_filter(sm,sigma=(2.2,4.0,0)); mask=mask0
    S=4
    smh=np.asarray(Image.fromarray(sm.clip(0,255).astype(np.uint8)).resize((W*S,H*S),Image.NEAREST).filter(ImageFilter.GaussianBlur(0.8))).astype(float)
    detail=np.asarray(Image.open(f'{hd}/{n}.png').convert('RGB').resize((W*S,H*S),Image.LANCZOS)).astype(float)
    m=Image.fromarray((mask*255).astype(np.uint8)).resize((W*S,H*S),Image.BILINEAR).filter(ImageFilter.GaussianBlur(2))
    m=np.asarray(m).astype(float)[...,None]/255
    o=Image.fromarray((smh*m+detail*(1-m)).clip(0,255).astype(np.uint8)); o.putalpha(255); o.save(f'{out}/{n}.png')
print('ok')
