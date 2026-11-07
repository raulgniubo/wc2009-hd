"""Pixel-art -> smooth vector-like upscale.
Every colour becomes a layer; layers are stacked from the most common colour to the rarest, and
each layer's mask is the union of itself and every layer above it (so lower layers run under the
upper ones without gaps).  Masks are upsampled with a smooth kernel and cut with a soft threshold,
which turns pixel staircases into clean anti-aliased curves and straight lines."""
import numpy as np
from PIL import Image, ImageFilter

def _up(mask, K, blur):
    im = Image.fromarray((mask * 255).astype(np.uint8)).resize((mask.shape[1] * K, mask.shape[0] * K), Image.BICUBIC)
    if blur > 0: im = im.filter(ImageFilter.GaussianBlur(blur))
    v = np.asarray(im).astype(np.float32) / 255
    return np.clip((v - .5) * 5 + .5, 0, 1)

def smooth(img, K=8, blur=None, merge=0):
    """img: RGBA PIL image (few colours).  merge: colours closer than this (sum of abs diff) are joined."""
    a = np.asarray(img.convert('RGBA')).astype(np.int32)
    h, w = a.shape[:2]
    blur = K * .42 if blur is None else blur
    opaque = a[..., 3] > 0
    cols = {}
    for y in range(h):
        for x in range(w):
            if opaque[y, x]:
                c = tuple(a[y, x, :3]); cols[c] = cols.get(c, 0) + 1
    order = sorted(cols, key=lambda c: -cols[c])
    if merge:
        reps = []
        for c in order:
            for r in reps:
                if sum(abs(p - q) for p, q in zip(c, r)) < merge: break
            else: reps.append(c)
        lab = np.full((h, w), -1)
        for y in range(h):
            for x in range(w):
                if opaque[y, x]:
                    c = tuple(a[y, x, :3])
                    lab[y, x] = min(range(len(reps)), key=lambda i: sum(abs(p - q) for p, q in zip(c, reps[i])))
        # colour of each merged layer = most common member
        layers = [(reps[i], lab == i) for i in range(len(reps))]
    else:
        layers = [(c, np.all(a[..., :3] == c, axis=-1) & opaque) for c in order]
    out = np.zeros((h * K, w * K, 4), np.float32)
    acc = np.zeros((h, w), bool)
    # union from the top (rarest) down
    unions = []
    for c, m in reversed(layers):
        acc = acc | m; unions.append((c, acc.copy()))
    unions.reverse()
    for c, u in unions:
        al = _up(u.astype(np.float32), K, blur)[..., None]
        out[..., :3] = out[..., :3] * (1 - al) + np.array(c, np.float32) * al
        out[..., 3:] = np.maximum(out[..., 3:], al)
    A = _up(opaque.astype(np.float32), K, blur)
    out[..., 3] = A * 255
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), 'RGBA')
