#!/usr/bin/env python3
"""Modern redesign of the duel UI: phase bar, corner buttons, action icons, attack sword,
card highlights. Writes HD replacements into /home/claude/wc09/ui_hd:
  tex3d/<content hash>.png   3D textures (phase bar, B/SELECT/Y, sword, highlight frames)
  duel2d/<name>.png          2D sprites from duel2d.pac (action icons, cursors, sword)
Style: dark navy, rounded corners, light-blue edges, sharp white labels (same as the alerts)."""
import os, math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

OUT = '/home/claude/wc09/ui_hd'
os.makedirs(f'{OUT}/tex3d', exist_ok=True); os.makedirs(f'{OUT}/duel2d', exist_ok=True)
SS = 4                                  # supersampling while drawing
FONT = '/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf'
FONT_B = '/usr/share/fonts/opentype/inter/Inter-Bold.otf'
CARDBACK = Image.open('/home/claude/wc09/cardback.png').convert('RGBA')

NAVY, NAVY2 = (18, 30, 64), (28, 46, 92)
EDGE = (120, 175, 255)
WHITE = (255, 255, 255)

def lerp(a, b, t): return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))

class Canvas:
    """Draw in original-pixel units at K px per unit (plus supersampling)."""
    def __init__(self, w, h, k):
        self.w, self.h, self.k = w, h, k
        self.im = Image.new('RGBA', (w * k * SS, h * k * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)
    def S(self, v): return v * self.k * SS
    def box(self, b): return [self.S(b[0]), self.S(b[1]), self.S(b[2]) - 1, self.S(b[3]) - 1]
    def rrect(self, b, r, fill=None, outline=None, width=0):
        self.d.rounded_rectangle(self.box(b), radius=self.S(r), fill=fill, outline=outline, width=max(1, int(self.S(width))) if width else 0)
    def grad_rrect(self, b, r, top, bot):
        """vertical gradient fill clipped to a rounded rect"""
        x0, y0, x1, y1 = [int(v) for v in self.box(b)]
        g = Image.new('RGBA', (x1 - x0 + 1, y1 - y0 + 1))
        gd = ImageDraw.Draw(g)
        for yy in range(g.height):
            gd.line([0, yy, g.width, yy], fill=lerp(top, bot, yy / max(1, g.height - 1)) + (255,))
        m = Image.new('L', g.size, 0)
        ImageDraw.Draw(m).rounded_rectangle([0, 0, g.width - 1, g.height - 1], radius=self.S(r), fill=255)
        self.im.paste(g, (x0, y0), m)
    def text(self, xy, s, size, fill=WHITE, font=FONT, anchor='mm', stroke=0, stroke_fill=(0, 0, 0)):
        f = ImageFont.truetype(font, int(self.S(size)))
        self.d.text((self.S(xy[0]), self.S(xy[1])), s, font=f, fill=fill, anchor=anchor,
                    stroke_width=int(self.S(stroke)), stroke_fill=stroke_fill)
    def poly(self, pts, fill=None, outline=None, width=0):
        p = [(self.S(x), self.S(y)) for x, y in pts]
        self.d.polygon(p, fill=fill, outline=outline, width=int(self.S(width)) if width else 1)
    def line(self, pts, fill, width):
        self.d.line([(self.S(x), self.S(y)) for x, y in pts], fill=fill, width=max(1, int(self.S(width))), joint='curve')
    def ellipse(self, b, fill=None, outline=None, width=0):
        self.d.ellipse(self.box(b), fill=fill, outline=outline, width=int(self.S(width)) if width else 0)
    def arc(self, b, a0, a1, fill, width):
        self.d.arc(self.box(b), a0, a1, fill=fill, width=max(1, int(self.S(width))))
    def paste(self, img, b):
        x0, y0, x1, y1 = [int(v) for v in self.box(b)]
        t = img.resize((x1 - x0 + 1, y1 - y0 + 1), Image.LANCZOS)
        self.im.alpha_composite(t, (x0, y0))
    def finish(self, path, binary=True):
        im = self.im.resize((self.im.width // SS, self.im.height // SS), Image.LANCZOS)
        if binary:   # the renderers keep texels with alpha >= 0.5: no half-transparent edges
            im.putalpha(im.getchannel('A').point(lambda v: 255 if v >= 128 else 0))
        im.save(path)

# ---------------------------------------------------------------- icons (drawn in a unit box)
def icon(c, kind, b, col=WHITE):
    """Draws icon `kind` inside box b=(x0,y0,x1,y1) in canvas units."""
    x0, y0, x1, y1 = b; w, h = x1 - x0, y1 - y0
    P = lambda u, v: (x0 + u * w, y0 + v * h)
    dark = (12, 18, 40)
    if kind == 'sword':
        c.poly([P(.80, .08), P(.92, .20), P(.42, .70), P(.30, .58)], fill=col)          # blade
        c.line([P(.84, .14), P(.38, .62)], fill=(170, 200, 240), width=w * .03)       # ridge
        c.poly([P(.18, .60), P(.28, .50), P(.50, .72), P(.40, .82)], fill=(255, 200, 70))  # guard
        c.line([P(.30, .70), P(.14, .86)], fill=(255, 200, 70), width=w * .11)          # grip
        c.ellipse((*P(.06, .84), *P(.18, .96)), fill=(255, 200, 70))
    elif kind == 'list':
        for i, (dx, dy) in enumerate([(.0, .0), (.07, .07), (.14, .14)]):
            cx0, cy0 = P(.18 + dx, .12 + dy); cx1, cy1 = P(.66 + dx, .74 + dy)
            c.rrect((cx0, cy0, cx1, cy1), w * .06, fill=col if i == 2 else lerp(col, (60, 160, 90), .45))
        for k in range(3):
            c.line([P(.40, .45 + k * .12), P(.72, .45 + k * .12)], fill=(30, 110, 60), width=h * .06)
    elif kind == 'spark':      # special summon
        cx, cy = P(.5, .52); R, r = w * .42, w * .12
        pts = []
        for i in range(8):
            a = -math.pi / 2 + i * math.pi / 4; rr = R if i % 2 == 0 else r
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        c.poly(pts, fill=col)
        c.ellipse((cx - w * .07, cy - w * .07, cx + w * .07, cy + w * .07), fill=(255, 220, 120))
    elif kind == 'bolt':       # activate
        c.ellipse((*P(.12, .12), *P(.88, .88)), outline=col, width=w * .07)
        c.poly([P(.56, .20), P(.32, .54), P(.48, .54), P(.42, .82), P(.68, .44), P(.52, .44)], fill=(255, 225, 90))
    elif kind in ('summon', 'fsum'):
        c.rrect((*P(.24, .26), *P(.66, .88)), w * .06, fill=col)
        c.rrect((*P(.30, .34), *P(.60, .62)), w * .03, fill=(120, 170, 240))
        if kind == 'summon':
            c.poly([P(.74, .14), P(.92, .38), P(.80, .38), P(.80, .62), P(.68, .62), P(.68, .38), P(.56, .38)], fill=(255, 225, 90))
        else:
            c.arc((*P(.50, .08), *P(.96, .54)), 200, 80, fill=(255, 225, 90), width=w * .08)
            c.poly([P(.84, .40), P(.98, .40), P(.90, .54)], fill=(255, 225, 90))
    elif kind == 'set_h':
        bk = CARDBACK.rotate(90, expand=True)
        c.rrect((*P(.08, .24), *P(.92, .80)), w * .05, fill=dark)
        c.paste(bk, (*P(.11, .27), *P(.89, .77)))
    elif kind == 'set_v':
        c.rrect((*P(.22, .08), *P(.78, .92)), w * .05, fill=dark)
        c.paste(CARDBACK, (*P(.25, .11), *P(.75, .89)))
    elif kind == 'atkp':
        c.rrect((*P(.26, .10), *P(.74, .90)), w * .06, fill=col)
        c.rrect((*P(.32, .18), *P(.68, .52)), w * .03, fill=(255, 190, 90))
    elif kind == 'defp':
        c.rrect((*P(.10, .26), *P(.90, .74)), w * .06, fill=col)
        c.poly([P(.38, .34), P(.62, .34), P(.62, .52), P(.50, .66), P(.38, .52)], fill=(120, 170, 240))
    elif kind == 'flag':
        c.line([P(.28, .10), P(.28, .92)], fill=(230, 230, 235), width=w * .08)
        c.poly([P(.32, .12), P(.84, .20), P(.70, .34), P(.84, .48), P(.32, .52)], fill=col)
    elif kind == 'back':
        c.arc((*P(.22, .22), *P(.86, .82)), 270, 120, fill=col, width=w * .11)
        c.poly([P(.10, .30), P(.40, .08), P(.40, .52)], fill=col)
    elif kind == 'chain':
        c.ellipse((*P(.10, .26), *P(.58, .74)), outline=col, width=w * .08)
        c.ellipse((*P(.42, .26), *P(.90, .74)), outline=col, width=w * .08)
    elif kind == 'detail':
        c.ellipse((*P(.14, .12), *P(.66, .64)), outline=col, width=w * .09)
        c.line([P(.58, .56), P(.86, .86)], fill=col, width=w * .12)
    elif kind == 'ignore':
        c.ellipse((*P(.14, .14), *P(.86, .86)), outline=col, width=w * .09)
        c.line([P(.26, .74), P(.74, .26)], fill=col, width=w * .09)
    elif kind == 'page':
        c.rrect((*P(.26, .12), *P(.78, .90)), w * .06, fill=col)
        for k in range(4):
            c.line([P(.36, .30 + k * .15), P(.68, .30 + k * .15)], fill=(60, 90, 150), width=h * .05)

CATEGORY = {   # tile top/bottom colours
    'magenta': ((214, 70, 150), (140, 30, 100)),
    'green': ((60, 190, 110), (20, 120, 70)),
    'purple': ((150, 90, 230), (90, 45, 170)),
    'blue': ((70, 140, 240), (30, 75, 180)),
    'red': ((235, 80, 95), (160, 30, 50)),
    'navy': ((44, 70, 130), (20, 34, 74)),
}

def tile(c, b, cat, selected=False, r=None):
    top, bot = CATEGORY[cat]
    if selected: top, bot = lerp(top, WHITE, .25), lerp(bot, WHITE, .15)
    x0, y0, x1, y1 = b; s = min(x1 - x0, y1 - y0)
    r = r if r is not None else s * .22
    c.rrect(b, r, fill=(235, 245, 255) if selected else lerp(top, WHITE, .35))
    bw = s * (.07 if selected else .05)
    c.grad_rrect((x0 + bw, y0 + bw, x1 - bw, y1 - bw), r - bw, top, bot)
    # soft gloss on the upper half
    c.grad_rrect((x0 + bw * 2, y0 + bw * 1.6, x1 - bw * 2, y0 + (y1 - y0) * .42), r * .7, lerp(top, WHITE, .28), top)

# ---------------------------------------------------------------- 2D action icons (duel2d.pac)
ICONS = [  # (name in cmd_icon_s order / btn file, icon kind, category)
    ('btn_atk', 'sword', 'magenta'), ('btn_list', 'list', 'green'), ('btn_spsum', 'spark', 'purple'),
    ('btn_act', 'bolt', 'purple'), ('btn_sum', 'summon', 'blue'), ('btn_fsum', 'fsum', 'blue'),
    ('btn_set', 'set_h', 'blue'), ('btn_set2', 'set_v', 'blue'), ('btn_atkp', 'atkp', 'blue'),
    ('btn_defp', 'defp', 'blue'), ('btn_sur', 'flag', 'red')]

def main():
    # small icons strip: 16x176, eleven 16x16 tiles
    c = Canvas(16, 176, 8)
    for i, (_, kind, cat) in enumerate(ICONS):
        y = i * 16
        tile(c, (0.3, y + 0.3, 15.7, y + 15.7), cat)
        icon(c, kind, (2.6, y + 2.6, 13.4, y + 13.4))
    c.finish(f'{OUT}/duel2d/cmd_icon_s.png')

    # big icons: 32x64, normal (top) and selected (bottom), tile at (4,4)-(29,28)
    for name, kind, cat in ICONS:
        c = Canvas(32, 64, 8)
        for half in (0, 1):
            y = half * 32
            tile(c, (4, y + 4, 29, y + 28), cat, selected=bool(half))
            icon(c, kind, (7.5, y + 7, 25.5, y + 25))
        c.finish(f'{OUT}/duel2d/{name}.png')

    # corner buttons, 2D versions (24x22 at the top left of each 32x32 half)
    def corner_button(c, y, kind, label=None, badge=None, selected=False):
        tile(c, (0, y, 24, y + 22), 'navy', selected=selected, r=4.5)
        if kind == 'select':
            c.rrect((6.5, y + 3.2, 17.5, y + 11), 1.6, fill=(70, 150, 255))
            c.rrect((9.6, y + 4.4, 14.4, y + 9.8), .8, fill=(255, 190, 80))
            c.text((12, y + 16.3), 'SELECT', 5.0, font=FONT)
        else:
            icon(c, kind, (6, y + 3, 21, y + 18))
        if badge:
            c.ellipse((1.2, y + 10.8, 10.2, y + 19.8), fill=(235, 242, 255))
            c.text((5.7, y + 15.4), badge, 6.0, fill=(20, 36, 80))

    for name, kind, badge in [('btn_cancel', 'back', 'B'), ('btn_detail', 'detail', 'X'), ('btn_ignore', 'ignore', 'B'),
                              ('btn_chain', 'chain', 'A'), ('btn_select', 'select', None), ('btn_page', 'page', 'Y')]:
        c = Canvas(32, 64, 8)
        for half in (0, 1):
            corner_button(c, half * 32, kind, badge=badge, selected=bool(half))
        c.finish(f'{OUT}/duel2d/{name}.png')

    c = Canvas(32, 64, 8)
    for half in (0, 1):
        y = half * 32
        tile(c, (4, y + 4, 30, y + 30), 'navy', selected=bool(half), r=5)
        icon(c, 'back', (8, y + 8, 26, y + 26))
    c.finish(f'{OUT}/duel2d/backbutton_16.png')

    # ---------------------------------------------------------------- sword pointer
    def sword(c, b, glow=False, ghost=False):
        """vertical sword, tip up, inside box b"""
        x0, y0, x1, y1 = b; w, h = x1 - x0, y1 - y0
        P = lambda u, v: (x0 + u * w, y0 + v * h)
        blade = [P(.5, .0), P(.62, .12), P(.60, .66), P(.40, .66), P(.38, .12)]
        guard = [P(.04, .66), P(.96, .66), P(.90, .75), P(.10, .75)]
        grip = (*P(.42, .75), *P(.58, .93))
        pommel = (*P(.36, .90), *P(.64, 1.0))
        if ghost:
            out = (220, 240, 255)
            c.poly(blade, outline=out, width=w * .07); c.poly(guard, outline=out, width=w * .07)
            c.rrect(grip, w * .04, outline=out, width=w * .07)
            return
        o = (10, 16, 36)
        # dark outline first (slightly larger shapes)
        c.poly([P(.5, -.03), P(.67, .11), P(.65, .69), P(.35, .69), P(.33, .11)], fill=o)
        c.poly([P(-.01, .63), P(1.01, .63), P(.94, .78), P(.06, .78)], fill=o)
        c.rrect((*P(.37, .72), *P(.63, .96)), w * .05, fill=o)
        c.ellipse((*P(.31, .87), *P(.69, 1.03)), fill=o)
        bl = (235, 245, 255) if glow else (200, 215, 240)
        c.poly(blade, fill=bl)
        c.poly([P(.5, .0), P(.62, .12), P(.60, .66), P(.5, .66)], fill=lerp(bl, (110, 150, 210), .45))
        c.line([P(.5, .06), P(.5, .62)], fill=WHITE, width=w * .04)
        gold = (255, 214, 90) if glow else (235, 180, 60)
        c.poly(guard, fill=gold)
        c.rrect(grip, w * .04, fill=(70, 45, 120))
        c.ellipse(pommel, fill=gold)

    # 2D: atk_icon 32x96: normal, lit, outline (cells of 32x32, sword in x 9..23)
    c = Canvas(32, 96, 8)
    sword(c, (9, 3, 23, 29))
    sword(c, (9, 32 + 0, 23, 32 + 28), glow=True)
    sword(c, (9, 64 + 2, 23, 64 + 29), ghost=True)
    c.finish(f'{OUT}/duel2d/atk_icon.png')

    # 3D: 98cbb453 normal sword, d1cb6ec3 outline (32x32, bbox 9,3 - 23,29)
    c = Canvas(32, 32, 8); sword(c, (9, 3, 23, 29)); c.finish(f'{OUT}/tex3d/98cbb453dc580cd2.png')
    c = Canvas(32, 32, 8); sword(c, (9, 4, 23, 29), ghost=True); c.finish(f'{OUT}/tex3d/d1cb6ec352ae0746.png')

    # ---------------------------------------------------------------- highlight frames
    def frame(c, b, col, t):
        """rounded selection outline: bright line with a white inner edge"""
        x0, y0, x1, y1 = b
        c.rrect(b, t * 2.2, outline=lerp(col, (0, 0, 0), .35), width=t)
        c.rrect((x0 + t * .25, y0 + t * .25, x1 - t * .25, y1 - t * .25), t * 2, outline=col, width=t * .7)
        c.rrect((x0 + t * .75, y0 + t * .75, x1 - t * .75, y1 - t * .75), t * 1.6, outline=WHITE, width=t * .22)

    # 3D card highlight (stretched over the card): cyan (yours), pink (opponent), 32x32, 3 px line
    c = Canvas(32, 32, 8); frame(c, (0, 0, 32, 32), (80, 200, 255), 3); c.finish(f'{OUT}/tex3d/dcaa698f0ad22196.png')
    c = Canvas(32, 32, 8); frame(c, (0, 0, 32, 32), (255, 100, 170), 3); c.finish(f'{OUT}/tex3d/e1033ddba20cb76d.png')

    # the same frame in the game's other colours (structure hash + hue class): green = can attack...
    for hue, col in [('b', (80, 200, 255)), ('c', (80, 220, 255)), ('m', (255, 100, 170)), ('r', (255, 90, 110)),
                     ('g', (90, 235, 140)), ('y', (255, 215, 80)), ('n', (220, 230, 245))]:
        c = Canvas(32, 32, 8); frame(c, (0, 0, 32, 32), col, 3); c.finish(f'{OUT}/tex3d/s_8700bf6b0612758e_{hue}.png')

    # 2D cursors (32x64: two frames)
    c = Canvas(32, 64, 8)
    frame(c, (3, 0, 29, 32), (80, 200, 255), 3)
    frame(c, (0, 32, 32, 64), (80, 200, 255), 3)
    c.finish(f'{OUT}/duel2d/cursor.png')
    c = Canvas(32, 64, 8)
    frame(c, (5, 2, 27, 30), (90, 170, 255), 1.4)
    frame(c, (3, 35, 29, 61), (90, 170, 255), 1.4)
    c.finish(f'{OUT}/duel2d/cursor2.png')
    c = Canvas(64, 64, 8)
    frame(c, (0, 0, 34, 26), (150, 225, 255), 2)
    frame(c, (1, 33, 33, 57), (200, 245, 255), 1.6)
    c.finish(f'{OUT}/duel2d/btn_cur.png')
    c = Canvas(64, 64, 8)
    frame(c, (0, 0, 48, 32), (150, 225, 255), 2)
    frame(c, (0, 32, 48, 64), (200, 245, 255), 1.6)
    c.finish(f'{OUT}/duel2d/btn_cur2.png')
    c = Canvas(32, 64, 8)
    for y, col in ((0, (150, 225, 255)), (32, (200, 245, 255))):
        c.poly([(29, y + 1), (29, y + 25), (4, y + 13)], outline=col, width=2.2)
    c.finish(f'{OUT}/duel2d/btn_cur3.png')

    # menu cursor around the big action icon (32x64: two pulse frames) and the small icon cursor
    c = Canvas(32, 64, 8)
    frame(c, (3, 3, 30, 29), (150, 225, 255), 2)
    frame(c, (4, 36, 29, 60), (210, 245, 255), 1.6)
    c.finish(f'{OUT}/duel2d/menu_cur.png')
    c = Canvas(32, 32, 8)
    frame(c, (6, 6, 26, 26), (150, 225, 255), 1.6)
    c.finish(f'{OUT}/duel2d/icon_cur.png')

    # ---------------------------------------------------------------- phase bar (3D, 32x16 each)
    PHASE = {   # content hash -> (label, state)
        '0635269b1d00d666': ('BP', 'off'), '2b0b911106c2ec80': ('DP', 'off'), '7f23dfc74003eb5a': ('M1', 'off'),
        '86cb059b8a7c2231': ('M2', 'off'), 'a817da6f09a5a4a2': ('SP', 'off'), 'f626b01c05cd80f7': ('EP', 'off'),
        '3a97613a41bb0db7': ('M1', 'blue'), '7a0a592b6df29659': ('M1', 'blue'), '90670890e3d57bd7': ('M1', 'blue'),
        '53b54771': ('EP', 'blue'), 'b78165d5': ('EP', 'blue'), 'beec78ea': ('EP', 'blue'), 'c73d3a28': ('EP', 'blue'),
        '2223d030': ('EP', 'hover'), '077580f0b44c8af3': ('EP', 'ready'),
        '213327c8': ('M1', 'red'), '3c1ea247': ('M1', 'red'), '6ca29e4c': ('M1', 'red'),
        '49709e2d': ('DP', 'blue'), '50fdcf71': ('DP', 'blue'),
        '2189502b': ('DP', 'red'), 'ee1fabcc': ('DP', 'red'), '68e45bfb': ('SP', 'red'), '6bcdf9b2': ('SP', 'red'),
        '91caf55c': ('SP', 'red'), 'b9574f96bda0ddca': ('BP', 'red'), 'ef5036eeeb0aed1d': ('BP', 'red'),
    }
    STATE = {   # fill top, fill bottom, edge, text
        'off': ((44, 56, 88), (28, 36, 62), (86, 104, 146), (160, 174, 200)),
        'blue': ((80, 160, 255), (34, 90, 210), (200, 235, 255), WHITE),
        'hover': ((120, 200, 255), (60, 140, 240), (235, 250, 255), WHITE),
        'red': ((245, 95, 110), (175, 35, 60), (255, 205, 210), WHITE),
        'ready': ((60, 200, 160), (20, 130, 100), (190, 255, 230), WHITE),
    }

    def phase_button(label, state):
        c = Canvas(32, 16, 8)
        top, bot, edge, txt = STATE[state]
        c.rrect((0.4, 0.6, 31.6, 15.4), 4.2, fill=edge)
        c.grad_rrect((1.2, 1.4, 30.8, 14.6), 3.5, top, bot)
        c.grad_rrect((2.2, 2.0, 29.8, 7.2), 2.5, lerp(top, WHITE, .22), top)
        c.text((16, 8.3), label, 8.6, fill=txt)
        return c

    # full content hashes of the short keys (seen in this session's dumps)
    FULL = {}
    for f in os.listdir('/tmp/claude-0/all3d') + os.listdir('/tmp/claude-0/cap/long'):
        if f.endswith('.png') and len(f) == 20: FULL.setdefault(f[:8], f[:16])
    for key, (label, state) in PHASE.items():
        h = key if len(key) == 16 else FULL.get(key[:8])
        if not h: print('no full hash for', key); continue
        phase_button(label, state).finish(f'{OUT}/tex3d/{h}.png')

    # corner buttons, 3D versions: B back, SELECT, Y (24x22 in 32x32)
    for h, kind, badge in [('b3d4d1eb92faa1e8', 'back', 'B'), ('81c119b4c7a46a36', 'select', None), ('e69eacc649684b34', 'page', 'Y')]:
        c = Canvas(32, 32, 8); corner_button(c, 0, kind, badge=badge); c.finish(f'{OUT}/tex3d/{h}.png')
    # battle-phase corner buttons (3D): B = skip (crossed), A = chain
    for h, kind, badge in [('52c24964801118db', 'ignore', 'B'), ('a7e2983902575566', 'chain', 'A')]:
        c = Canvas(32, 32, 8); corner_button(c, 0, kind, badge=badge); c.finish(f'{OUT}/tex3d/{h}.png')

    # phase cursor corner piece (64x32 texture, L of 34x17 at the top left; mirrored by the game)
    c = Canvas(64, 32, 8)
    c.line([(0.8, 17), (0.8, 4.5)], fill=(200, 240, 255), width=1.4)
    c.arc((0.1, 0.1, 9, 9), 180, 270, fill=(200, 240, 255), width=1.4)
    c.line([(4.5, 0.8), (34, 0.8)], fill=(200, 240, 255), width=1.4)
    c.finish(f'{OUT}/tex3d/49f74b1ef4e93e52.png')

    # small digits (deck/graveyard counts): mini_num8x8 glyphs by structure hash, any colour
    DIGITS = ['2138da2186ab0a24', '9524e1c83ff2675e', '2656996fa4b89126', '3dca63f2b8d6ab31', '91583d6552d9488e',
              '7140bdbd737b63df', 'af2c3042f1c6e077', '0e1bf16364e782d5', '89d6a4884775f462', '48373df271f28f8a']
    for dgt, sh in enumerate(DIGITS):
        c = Canvas(8, 8, 32)
        c.text((4, 4.3), str(dgt), 8.4, fill=WHITE, stroke=1.1, stroke_fill=(12, 18, 40))
        c.finish(f'{OUT}/tex3d/s_{sh}.png')

    # "Draw" button (3D, 32x32: hand pulling a card, label underneath)
    def draw_button(lit):
        c = Canvas(32, 32, 8)
        c.rrect((9, 1, 23, 19), 2, fill=(12, 18, 40))
        c.paste(CARDBACK, (10, 2, 22, 18))
        c.poly([(26, 4), (30, 9), (27.5, 9), (27.5, 15), (24.5, 15), (24.5, 9), (22, 9)], fill=(255, 225, 90))
        top, bot = ((80, 160, 255), (34, 90, 210)) if lit else ((44, 70, 130), (20, 34, 74))
        c.rrect((1, 20, 31, 31), 4, fill=(200, 235, 255) if lit else (110, 140, 200))
        c.grad_rrect((1.8, 20.8, 30.2, 30.2), 3.4, top, bot)
        c.text((16, 25.6), 'Draw', 7.2)
        return c
    draw_button(False).finish(f'{OUT}/tex3d/110d798fa3e270a0.png')
    draw_button(True).finish(f'{OUT}/tex3d/2c4339fc7ee8fa79.png')
    # ---------------------------------------------------------------- list window (graveyard, extra deck...)
    # frame texture 256x64; rows 4-6, 28-30, 50-52 are stretched by the game, so they are flat
    def list_frame():
        c = Canvas(256, 64, 8)
        X0, X1 = 17, 239
        dark, mid, well = (16, 26, 56), (24, 38, 78), (10, 16, 36)
        # outer body with rounded top/bottom
        c.rrect((X0, 0, X1, 57), 4.5, fill=EDGE)
        c.rrect((X0 + 1, 1, X1 - 1, 56), 3.6, fill=dark)
        c.d.rectangle(c.box((X0 + 1, 4, X1 - 1, 6)), fill=mid)          # title band (stretched)
        c.d.rectangle(c.box((X0 + 1, 28, X1 - 1, 30)), fill=dark)        # card area (stretched)
        c.d.rectangle(c.box((X0 + 1, 50, X1 - 1, 52)), fill=dark)        # bottom area (stretched)
        c.d.rectangle(c.box((X0 + 1, 1.2, X1 - 1, 4)), fill=mid)
        c.rrect((X0 + 1, 1, X1 - 1, 5), 3.6, fill=mid)
        # tab well and slots
        c.rrect((29, 7.5, 226, 26.5), 3, fill=well, outline=(60, 90, 150), width=0.6)
        for k in range(8):
            x = 32 + k * 24
            c.rrect((x + 0.8, 9.2, x + 23.2, 24.8), 2.6, fill=(30, 48, 96))
        # thin separators
        c.line([(X0 + 3, 7), (X1 - 3, 7)], fill=(60, 90, 150), width=0.5)
        c.line([(X0 + 3, 31.2), (X1 - 3, 31.2)], fill=(60, 90, 150), width=0.5)
        # scroll row: arrows boxes and track
        c.rrect((31, 33.5, 56, 46.5), 3, fill=(30, 48, 96), outline=(70, 110, 180), width=0.6)
        c.rrect((199, 33.5, 224, 46.5), 3, fill=(30, 48, 96), outline=(70, 110, 180), width=0.6)
        c.rrect((58, 37.5, 197, 42.5), 2.5, fill=well, outline=(60, 90, 150), width=0.5)
        c.line([(X0 + 3, 48.6), (X1 - 3, 48.6)], fill=(60, 90, 150), width=0.5)
        return c
    list_frame().finish(f'{OUT}/tex3d/6ce7ea0b13171cda.png')

    def tab_icon(kind):
        c = Canvas(16, 16, 16)
        o = (8, 12, 30)
        if kind == 'all':
            for i, col in enumerate([(120, 160, 230), (170, 200, 245), (255, 255, 255)]):
                c.rrect((2.5 + i * 2, 3 + i * 1.2, 10 + i * 2, 13 + i * 1.2 - 2), 1.2, fill=col, outline=o, width=0.6)
        elif kind == 'abc':
            c.text((8, 8.4), 'ABC', 6.4, fill=WHITE, stroke=0.7, stroke_fill=o)
        elif kind == 'atk':
            c.line([(3.5, 12.5), (12.5, 3.5)], fill=o, width=3.0)
            c.line([(4, 12), (12, 4)], fill=(225, 235, 250), width=1.6)
            c.line([(3.2, 9.2), (6.8, 12.8)], fill=(255, 205, 80), width=1.6)
        elif kind == 'def':
            c.poly([(3, 2.5), (13, 2.5), (13, 8.5), (8, 13.5), (3, 8.5)], fill=(225, 235, 250), outline=o, width=0.7)
            c.poly([(8, 3.6), (11.8, 3.6), (11.8, 8.2), (8, 12)], fill=(240, 90, 110))
        elif kind == 'level':
            pts = []
            for i in range(10):
                a = -math.pi / 2 + i * math.pi / 5; r = 6 if i % 2 == 0 else 2.7
                pts.append((8 + r * math.cos(a), 8.4 + r * math.sin(a)))
            c.poly(pts, fill=(255, 210, 70), outline=o, width=0.6)
        elif kind == 'type':
            for i, col in enumerate([(90, 200, 120), (240, 170, 60), (110, 150, 240), (230, 100, 160)]):
                x, y = 2.8 + (i % 2) * 5.6, 2.8 + (i // 2) * 5.6
                c.rrect((x, y, x + 4.6, y + 4.6), 1.1, fill=col, outline=o, width=0.5)
        elif kind == 'attr':
            cols = [(240, 90, 90), (250, 190, 60), (110, 210, 110), (90, 160, 250), (190, 110, 240), (220, 220, 230)]
            for i, col in enumerate(cols):
                a = -math.pi / 2 + i * math.pi / 3
                x, y = 8 + 4.6 * math.cos(a), 8 + 4.6 * math.sin(a)
                c.ellipse((x - 2, y - 2, x + 2, y + 2), fill=col, outline=o, width=0.4)
        elif kind == 'race':
            c.ellipse((5.2, 2.2, 10.8, 7.8), fill=(225, 235, 250), outline=o, width=0.6)
            c.rrect((3.2, 8.4, 12.8, 14), 2.4, fill=(225, 235, 250), outline=o, width=0.6)
        elif kind == 'fusion':
            c.ellipse((1.5, 1.5, 14.5, 14.5), fill=(70, 120, 230), outline=o, width=0.6)
            for r, a0 in ((4.8, 0), (3.2, 120), (1.6, 240)):
                c.arc((8 - r, 8 - r, 8 + r, 8 + r), a0, a0 + 250, fill=WHITE, width=0.9)
        return c
    for h, kind in [('b0abd1c3ebd0f195', 'all'), ('fcdaadf0', 'abc'), ('a72cdc73', 'atk'), ('639654f1', 'def'),
                    ('4be32b69', 'level'), ('140e7f10', 'type'), ('ddc79c38', 'attr'), ('96af3c0ce1bf356e', 'race'),
                    ('74767fc3ef7df568', 'fusion')]:
        full = h if len(h) == 16 else None
        if not full:
            for f in os.listdir('/tmp/claude-0/cap/gyw'):
                if f.startswith(h) and f.endswith('.rgb6a5'): full = f[:16]
        if full: tab_icon(kind).finish(f'{OUT}/tex3d/{full}.png')
        else: print('missing', h)

    # selected tab highlight (32x16, 24x16 used) and the badge pill behind fusion icons (32x16)
    c = Canvas(32, 16, 8)
    c.rrect((0.4, 0.4, 23.6, 15.6), 3, fill=(140, 210, 255))
    c.grad_rrect((1.4, 1.4, 22.6, 14.6), 2.4, (90, 170, 255), (40, 100, 220))
    c.finish(f'{OUT}/tex3d/99b7133f5a34358b.png')
    c = Canvas(32, 16, 8)
    c.rrect((1.5, 1, 31, 15), 7, fill=(44, 74, 150), outline=(120, 175, 255), width=0.8)
    c.finish(f'{OUT}/tex3d/29ca36ca523c0233.png')
    # card cursor in lists (32x32, frame 3..29)
    c = Canvas(32, 32, 8); frame(c, (3, 0, 29, 32), (80, 200, 255), 2.4); c.finish(f'{OUT}/tex3d/a66171319346e873.png')
    # back button (32x64: normal rows 0-24, pressed rows 24-48)
    c = Canvas(32, 64, 8)
    for y0, sel in ((0, False), (24, True)):
        tile(c, (0.5, y0 + 0.5, 31.5, y0 + 23.5), 'navy', selected=sel, r=5)
        icon(c, 'back', (7, y0 + 2.5, 23, y0 + 18.5))
        c.ellipse((21.5, y0 + 13, 29.5, y0 + 21), fill=(235, 242, 255)); c.text((25.5, y0 + 17.2), 'B', 5.6, fill=(20, 36, 80))
    c.finish(f'{OUT}/tex3d/f18c53ddf656c144.png')
    print('ok')


if __name__ == '__main__':
    main()
