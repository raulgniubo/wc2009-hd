"""Outlined flat-vector icon kit (menu icons for the Duelist Menu, deck editor, ...).
Every shape is drawn twice: grown by the outline width in the outline colour, then filled,
so overlapping parts get clean separation lines.  Coordinates are unit-box fractions
(0..1) mapped into a box on a make_duel_ui.Canvas."""
import math
from make_duel_ui import Canvas, lerp, WHITE, FONT, FONT_B

OL = (38, 30, 22)

class Ico:
    def __init__(self, c, b, ow=None, olc=OL):
        self.c = c; self.x0, self.y0, self.x1, self.y1 = b
        self.w, self.h = self.x1 - self.x0, self.y1 - self.y0
        self.ow = ow if ow is not None else max(.55, min(self.w, self.h) * .045)
        self.olc = olc
    def P(self, u, v): return (self.x0 + u * self.w, self.y0 + v * self.h)
    def L(self, v): return v * min(self.w, self.h)
    # --- primitives (fill may be a colour or (top, bottom) gradient for boxes)
    def box(self, u0, v0, u1, v1, r, fill, outline=True):
        a, b = self.P(u0, v0), self.P(u1, v1); r = self.L(r)
        if outline: self.c.rrect((a[0] - self.ow, a[1] - self.ow, b[0] + self.ow, b[1] + self.ow), r + self.ow, fill=self.olc)
        if isinstance(fill[0], tuple): self.c.grad_rrect((*a, *b), r, fill[0], fill[1])
        else: self.c.rrect((*a, *b), r, fill=fill)
    def oval(self, u0, v0, u1, v1, fill, outline=True):
        a, b = self.P(u0, v0), self.P(u1, v1)
        if outline: self.c.ellipse((a[0] - self.ow, a[1] - self.ow, b[0] + self.ow, b[1] + self.ow), fill=self.olc)
        self.c.ellipse((*a, *b), fill=fill)
    def poly(self, pts, fill, outline=True):
        p = [self.P(u, v) for u, v in pts]
        if outline:   # Pillow draws polygon outlines inwards: stroke the closed path instead
            self.c.poly(p, fill=self.olc)
            self.c.line(p + [p[0], p[1]], fill=self.olc, width=self.ow * 2)
            for q in p:
                self.c.ellipse((q[0] - self.ow, q[1] - self.ow, q[0] + self.ow, q[1] + self.ow), fill=self.olc)
        self.c.poly(p, fill=fill)
    def cap(self, u0, v0, u1, v1, r, fill, outline=True):
        (ax, ay), (bx, by) = self.P(u0, v0), self.P(u1, v1); r = self.L(r)
        for grow, col in (((self.ow, self.olc),) if outline else ()) + ((0, fill),):
            rr = r + grow; ang = math.atan2(by - ay, bx - ax); pts = []
            for i in range(13):
                t = ang + math.pi / 2 + i * math.pi / 12; pts.append((ax + rr * math.cos(t), ay + rr * math.sin(t)))
            for i in range(13):
                t = ang - math.pi / 2 + i * math.pi / 12; pts.append((bx + rr * math.cos(t), by + rr * math.sin(t)))
            self.c.poly(pts, fill=col)
    def line(self, pts, col, width):
        self.c.line([self.P(u, v) for u, v in pts], fill=col, width=self.L(width))
    def arcline(self, cu, cv, r, a0, a1, col, width, outline=True):
        """stroked arc with round ends (angles in degrees, clockwise from 3 o'clock)"""
        n = 24
        pts = [(cu + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cv + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
        for grow, cc in (((self.ow, self.olc),) if outline else ()) + ((0, col),):
            wd = self.L(width) + 2 * grow
            self.c.line([self.P(u, v) for u, v in pts], fill=cc, width=wd)
            for (u, v) in (pts[0], pts[-1]):
                x, y = self.P(u, v); self.c.ellipse((x - wd / 2, y - wd / 2, x + wd / 2, y + wd / 2), fill=cc)
    def arc(self, u0, v0, u1, v1, a0, a1, col, width):
        a, b = self.P(u0, v0), self.P(u1, v1)
        self.c.arc((*a, *b), a0, a1, fill=col, width=self.L(width))

GOLD = ((255, 222, 110), (220, 150, 30))
SILVER = ((245, 250, 255), (150, 175, 205))

def draw(c, kind, b, **kw):
    I = Ico(c, b, **kw)
    if kind == 'swords':
        for flip in (1, -1):
            u = lambda x: .5 + (x - .5) * flip
            I.poly([(u(.12), .08), (u(.24), .08), (u(.72), .62), (u(.62), .72), (u(.12), .20)], (205, 225, 250))
            I.poly([(u(.16), .10), (u(.22), .10), (u(.68), .63), (u(.64), .67)], (250, 252, 255), outline=False)
            I.cap(u(.56), .80, u(.80), .56, .045, (255, 205, 70))
            I.cap(u(.74), .74, u(.88), .88, .05, (120, 70, 160))
            I.oval(u(.86) - .05, .86, u(.86) + .05, .96, (255, 205, 70))
    elif kind == 'trophy':
        I.arcline(.24, .30, .14, 90, 270, (250, 200, 70), .07); I.arcline(.76, .30, .14, -90, 90, (250, 200, 70), .07)
        I.poly([(.22, .08), (.78, .08), (.72, .42), (.58, .56), (.42, .56), (.28, .42)], (240, 180, 40))
        I.poly([(.30, .12), (.42, .12), (.40, .40), (.33, .36)], (255, 240, 170), outline=False)
        I.box(.44, .54, .56, .70, .02, (220, 150, 30))
        I.box(.28, .70, .72, .82, .03, ((250, 205, 80), (200, 130, 20)))
        I.poly([(.30, .84), (.50, .78), (.70, .84), (.66, .96), (.50, .90), (.34, .96)], (220, 50, 60))
    elif kind == 'wireless':
        I.box(.26, .34, .74, .96, .07, ((250, 252, 255), (205, 215, 230)))
        I.box(.33, .42, .67, .62, .03, (70, 90, 120))
        I.box(.33, .70, .67, .88, .03, (70, 90, 120))
        for k, r in enumerate((.12, .22, .32)):
            I.arc(.5 - r, .26 - r, .5 + r, .26 + r, 220, 320, (60, 170, 230), .055)
    elif kind == 'person':
        I.oval(.32, .06, .68, .44, ((255, 220, 90)))
        I.box(.18, .46, .82, .96, .2, ((255, 215, 80), (225, 160, 30)))
        I.oval(.40, .18, .46, .26, OL, outline=False); I.oval(.54, .18, .60, .26, OL, outline=False)
    elif kind == 'duelist':     # cards fanned behind a duel disk blade
        for k, (col, rot) in enumerate((((60, 120, 220), -.16), ((230, 70, 80), 0), ((60, 170, 100), .16))):
            cx = .58 + k * .1
            I.poly([(cx - .12, .30 + rot * .2), (cx + .10, .26 - rot * .2), (cx + .16, .86), (cx - .06, .90)], col)
        I.poly([(.04, .40), (.62, .16), (.70, .30), (.14, .62)], ((210, 220, 235)))
        I.poly([(.10, .44), (.60, .24), (.62, .28), (.14, .50)], (255, 255, 255), outline=False)
        I.oval(.16, .40, .40, .64, (90, 100, 120))
        I.oval(.22, .46, .34, .58, (230, 60, 60), outline=False)
    elif kind == 'shop':
        I.box(.14, .40, .86, .94, .03, ((255, 250, 240), (225, 215, 200)))
        I.box(.42, .62, .60, .94, .02, (120, 80, 50))
        I.box(.20, .52, .36, .70, .02, (120, 190, 240))
        I.box(.66, .52, .80, .70, .02, (120, 190, 240))
        I.poly([(.06, .42), (.16, .14), (.84, .14), (.94, .42)], (225, 55, 60))
        for k in range(4):
            u = .16 + k * .18
            I.poly([(u + .02, .16), (u + .1, .16), (u + .12, .40), (u, .40)], (255, 245, 240), outline=False)
    elif kind == 'book':
        I.poly([(.04, .20), (.48, .26), (.48, .90), (.04, .82)], (250, 252, 245))
        I.poly([(.96, .20), (.52, .26), (.52, .90), (.96, .82)], (250, 252, 245))
        I.poly([(.02, .82), (.48, .92), (.52, .92), (.98, .82), (.98, .88), (.52, .98), (.48, .98), (.02, .88)], (40, 150, 110))
        for k in range(3):
            v = .38 + k * .14
            I.line([(.12, v), (.40, v + .04)], (150, 170, 160), .035); I.line([(.60, v + .04), (.88, v)], (150, 170, 160), .035)
    elif kind == 'options':     # folder with a wrench
        I.poly([(.06, .24), (.36, .24), (.42, .32), (.94, .32), (.94, .88), (.06, .88)], (230, 170, 40))
        I.box(.06, .40, .94, .88, .03, ((255, 215, 90), (230, 165, 35)))
        I.cap(.36, .80, .80, .30, .06, (190, 200, 215))
        I.oval(.70, .14, .94, .38, (190, 200, 215))
        I.oval(.79, .18, .90, .29, (255, 215, 90), outline=False)
    elif kind == 'exit':
        I.box(.10, .06, .56, .94, .02, ((220, 200, 160), (180, 150, 110)))
        I.poly([(.14, .10), (.40, .18), (.40, .92), (.14, .90)], ((150, 120, 80)))
        I.oval(.30, .48, .36, .56, (255, 220, 120), outline=False)
        I.poly([(.44, .50), (.66, .30), (.66, .42), (.94, .42), (.94, .60), (.66, .60), (.66, .72)], (60, 210, 110))
    elif kind == 'runner':      # duel runner (bike)
        I.oval(.04, .52, .36, .88, (60, 60, 70)); I.oval(.64, .52, .96, .88, (60, 60, 70))
        I.oval(.12, .60, .28, .80, (200, 200, 210), outline=False); I.oval(.72, .60, .88, .80, (200, 200, 210), outline=False)
        I.poly([(.06, .58), (.30, .30), (.62, .26), (.86, .20), (.98, .40), (.80, .56), (.36, .66)], (220, 50, 60))
        I.poly([(.40, .32), (.62, .30), (.58, .44), (.42, .46)], (255, 140, 140), outline=False)
        I.poly([(.70, .26), (.86, .22), (.90, .32), (.74, .36)], (130, 200, 255), outline=False)
    elif kind == 'back':
        I.arcline(.52, .54, .30, -90, 160, (255, 255, 255), .13)
        I.poly([(.10, .24), (.46, .02), (.46, .46)], (255, 255, 255))
    elif kind in ('up', 'down', 'updown'):
        col = (60, 40, 25)
        if kind in ('up', 'updown'): I.poly([(.5, .06), (.86, .44), (.62, .44), (.62, .52), (.38, .52), (.38, .44), (.14, .44)], col, outline=False)
        if kind in ('down', 'updown'): I.poly([(.5, .94), (.86, .56), (.62, .56), (.62, .48), (.38, .48), (.38, .56), (.14, .56)], col, outline=False)
        if kind == 'up': I.box(.40, .40, .60, .90, .02, col, outline=False)
        if kind == 'down': I.box(.40, .10, .60, .60, .02, col, outline=False)
    else:
        raise ValueError(kind)
