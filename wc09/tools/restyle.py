"""Generic button restyle for menu sprite cells: keeps the game's own outline ring (alpha-marker
pass-through, so palette-driven selection colours still show), redraws the face as a clean
rounded gradient in the face's own colour and smooths the icon drawn on it."""
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage
from pixsmooth import smooth

MARK = 77

def _lerp(a, b, t): return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))

def restyle(cell, K=8, ring=2, face_tol=40, radius=3.5):
    src = np.asarray(cell.convert('RGBA')).astype(int)
    h, w = src.shape[:2]
    A = src[..., 3] > 0
    if A.sum() < 16: return None
    inner = ndimage.binary_erosion(A, iterations=ring)
    if inner.sum() < 9: return None
    px = src[inner][:, :3]
    # face colour = most common colour of the inner area
    keys, counts = np.unique(px, axis=0, return_counts=True)
    face = keys[counts.argmax()]
    near = np.abs(src[..., :3] - face).sum(-1) <= face_tol
    icon = inner & ~near
    out = Image.new('RGBA', (w * K, h * K), (0, 0, 0, 0))
    # ring: game pixels
    ringm = Image.fromarray(((A & ~inner) * 255).astype(np.uint8)).resize(out.size, Image.NEAREST)
    out.paste(Image.new('RGBA', out.size, tuple(int(v) for v in face) + (MARK,)), (0, 0), ringm)
    # face: rounded rect over the inner area, soft vertical gradient around the face colour
    ys, xs = np.nonzero(inner)
    x0, y0, x1, y1 = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
    fc = tuple(int(v) for v in face)
    top, bot = _lerp(fc, (255, 255, 255), .22), _lerp(fc, (0, 0, 0), .10)
    g = Image.new('RGBA', ((x1 - x0) * K, (y1 - y0) * K))
    gd = ImageDraw.Draw(g)
    for yy in range(g.height):
        gd.line([0, yy, g.width, yy], fill=_lerp(top, bot, yy / max(1, g.height - 1)) + (255,))
    m = Image.new('L', g.size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, g.width - 1, g.height - 1], radius=int(radius * K), fill=255)
    # the face must still cover the inner area exactly where the game draws (corners included)
    inner_m = Image.fromarray((inner[y0:y1, x0:x1] * 255).astype(np.uint8)).resize(g.size, Image.NEAREST)
    m = Image.fromarray(np.maximum(np.asarray(m), np.asarray(inner_m.filter(ImageFilter.MinFilter(int(K * 1.5) | 1)))))
    out.paste(g, (x0 * K, y0 * K), m)
    # icon: the game's icon pixels, smoothed
    if icon.any():
        ic = src.copy().astype(np.uint8); ic[~icon] = 0
        sm = smooth(Image.fromarray(ic), K, merge=24)
        out.alpha_composite(sm)
    return out
