// WC2009 HD for iOS: GLKit view running the WC2009 HD core (mobile/core), touch controls, game
// controllers, AVAudioEngine output. The game files go in the app's Documents folder (visible in
// the Files app and in Finder when the iPhone is connected to a Mac).
#define GLES_SILENCE_DEPRECATION 1
#import <UIKit/UIKit.h>
#import <GLKit/GLKit.h>
#import <AVFoundation/AVFoundation.h>
#import <GameController/GameController.h>
#include <OpenGLES/ES3/gl.h>
#include <mutex>
#include <atomic>
#include <cmath>
#include "wc09_core.h"

static const double kDSFps = 59.8261;
static const int kAutoSlot = 0;   // "resume where you left off"

// ------------------------------------------------------------------------------------------------
// Input shared between touch/controllers (main thread) and the emulator (also the main thread here,
// but taps shorter than a frame still need holding for a couple of frames)

struct InputState
{
    unsigned screenKeys = 0, padKeys = 0, latched = 0;
    int latchFrames = 0;
    bool touchDown = false, touchLatched = false, lastSent = false;
    int tx = 0, ty = 0, touchFrames = 0;

    void setScreenKeys(unsigned k) { latch(k & ~screenKeys); screenKeys = k; }
    void setPadKeys(unsigned k) { latch(k & ~padKeys); padKeys = k; }
    void latch(unsigned pressed) { if (pressed) { latched |= pressed; latchFrames = 2; } }
    void touch(bool down, int x, int y)
    {
        if (down) { if (!touchDown) { touchLatched = true; touchFrames = 2; } tx = x; ty = y; }
        touchDown = down;
    }
    void apply()
    {
        wc09_set_keys(screenKeys | padKeys | latched);
        if (latchFrames > 0 && --latchFrames == 0) latched = 0;
        bool down = touchDown || touchLatched;
        if (down) wc09_touch(1, tx, ty);
        else if (lastSent) wc09_touch(0, 0, 0);
        lastSent = down;
        if (touchFrames > 0 && --touchFrames == 0) touchLatched = false;
    }
    void releaseAll() { screenKeys = padKeys = latched = 0; touchDown = touchLatched = false; }
};
static InputState gInput;

// ------------------------------------------------------------------------------------------------
// Layout (points), same rules as the Android app:
//  - tall iPhone, portrait: screens stacked at the top, controls underneath
//  - squarer screen, portrait (iPhone Duo open, iPad): "DS mode", top screen on the top half,
//    bottom screen on the bottom half (meeting at the fold), controls beside the bottom screen
//  - wide iPhone, landscape: screens side by side, controls in the side margins
//  - squarer screen, landscape: screens side by side at the top, controls underneath
//  - full screen (no buttons): screens as big as they fit, a small menu button in a corner

struct Layout
{
    CGRect top, bottom, l, r, start, select, menu, ffwd;
    CGFloat dpadX, dpadY, dpadR, abxyX, abxyY, abxyR, btnR;
    bool landscape, fullscreen;
    static constexpr CGFloat kTall = 1.75;

    void compute(CGSize size, UIEdgeInsets safe, bool controls)
    {
        CGFloat W = size.width - safe.left - safe.right, H = size.height - safe.top - safe.bottom;
        landscape = W > H;
        fullscreen = !controls;
        if (fullscreen) full(W, H);
        else if (!landscape) { if (H / W >= kTall) phonePortrait(W, H); else foldPortrait(W, H); }
        else { if (W / H >= kTall || !wideLandscape(W, H)) phoneLandscape(W, H); }
        CGFloat dx = safe.left, dy = safe.top;
        for (CGRect* rc : {&top, &bottom, &l, &r, &start, &select, &menu, &ffwd}) *rc = CGRectOffset(*rc, dx, dy);
        dpadX += dx; dpadY += dy; abxyX += dx; abxyY += dy;
    }

    static CGRect R(CGFloat x0, CGFloat y0, CGFloat x1, CGFloat y1) { return CGRectMake(x0, y0, x1 - x0, y1 - y0); }

    void full(CGFloat W, CGFloat H)
    {
        if (!landscape)
        {
            if (H / W < kTall)
            {
                CGFloat half = H / 2, sh = MIN(half, W * 0.75), sw = sh / 0.75;
                top = R((W - sw) / 2, half - sh, (W + sw) / 2, half);
                bottom = R((W - sw) / 2, half, (W + sw) / 2, half + sh);
            }
            else
            {
                CGFloat sw = MIN(W, H / 1.5), sh = sw * 0.75, y0 = (H - 2 * sh) / 2;
                top = R((W - sw) / 2, y0, (W + sw) / 2, y0 + sh);
                bottom = R((W - sw) / 2, y0 + sh, (W + sw) / 2, y0 + 2 * sh);
            }
        }
        else
        {
            CGFloat sw = MIN(W / 2, H / 0.75), sh = sw * 0.75, y0 = (H - sh) / 2;
            top = R(W / 2 - sw, y0, W / 2, y0 + sh);
            bottom = R(W / 2, y0, W / 2 + sw, y0 + sh);
        }
        CGFloat mb = 34, pad = 6;
        menu = R(W - pad - mb, pad, W - pad, pad + mb);
        ffwd = CGRectMake(-100, -100, 0, 0);
        dpadR = 0; dpadX = dpadY = abxyX = abxyY = -1000; btnR = abxyR = 0;
        l = r = start = select = CGRectMake(-100, -100, 0, 0);
    }

    void phonePortrait(CGFloat W, CGFloat H)
    {
        CGFloat minCtrl = 230, sw = W, sh = sw * 0.75;
        if (sh * 2 > H - minCtrl) { sh = MAX((H - minCtrl) / 2, H * 0.3); sw = sh / 0.75; }
        CGFloat x0 = (W - sw) / 2;
        top = R(x0, 0, x0 + sw, sh);
        bottom = R(x0, sh, x0 + sw, 2 * sh);
        areaControls(0, MIN(CGRectGetMaxY(bottom), H - minCtrl), W, H);
    }

    void foldPortrait(CGFloat W, CGFloat H)
    {
        CGFloat half = H / 2, sh = MIN(half, W * 0.75), sw = sh / 0.75;
        top = R((W - sw) / 2, half - sh, (W + sw) / 2, half);
        CGFloat m = MAX(110.0, MIN(170.0, W * 0.2));
        CGFloat bw = MIN(W - 2 * m, half / 0.75), bh = bw * 0.75;
        bottom = R((W - bw) / 2, half, (W + bw) / 2, half + bh);
        marginControls(half, H, 0, m, W - m, W);
    }

    void phoneLandscape(CGFloat W, CGFloat H)
    {
        CGFloat m = MIN(150.0, W * 0.17);
        CGFloat sw = MIN((W - 2 * m) / 2, H / 0.75), sh = sw * 0.75, y0 = (H - sh) / 2;
        top = R(W / 2 - sw, y0, W / 2, y0 + sh);
        bottom = R(W / 2, y0, W / 2 + sw, y0 + sh);
        CGFloat mm = MAX(m, 120.0);
        marginControls(0, H, 0, mm, W - mm, W);
    }

    bool wideLandscape(CGFloat W, CGFloat H)
    {
        CGFloat sw = MIN(W / 2, (H - 170) / 0.75), sh = sw * 0.75;
        if (H - sh < 150) return false;
        top = R(W / 2 - sw, 0, W / 2, sh);
        bottom = R(W / 2, 0, W / 2 + sw, sh);
        areaControls(0, sh, W, H);
        return true;
    }

    void areaControls(CGFloat x0, CGFloat y0, CGFloat x1, CGFloat y1)
    {
        CGFloat aw = x1 - x0, ah = y1 - y0, side = MIN(aw, ah * 2.2), pad = 18;
        dpadR = MIN(72.0, MIN(ah * 0.36, side * 0.2));
        btnR = dpadR * 0.36; abxyR = dpadR * 0.88;
        CGFloat cy = y0 + ah * 0.52;
        dpadX = x0 + pad + dpadR; dpadY = cy;
        abxyX = x1 - pad - abxyR - btnR; abxyY = cy;
        CGFloat lrW = 86, lrH = 34, lrY = y0 + 10;
        l = R(x0 + pad, lrY, x0 + pad + lrW, lrY + lrH);
        r = R(x1 - pad - lrW, lrY, x1 - pad, lrY + lrH);
        CGFloat pillW = 64, pillH = 26, py = y1 - pad - pillH, cx = (x0 + x1) / 2;
        select = R(cx - 8 - pillW, py, cx - 8, py + pillH);
        start = R(cx + 8, py, cx + 8 + pillW, py + pillH);
        CGFloat mb = 40, my = y0 + 10;
        menu = R(cx + 6, my, cx + 6 + mb, my + mb);
        ffwd = R(cx - 6 - mb, my, cx - 6, my + mb);
    }

    void marginControls(CGFloat y0, CGFloat y1, CGFloat lx0, CGFloat lx1, CGFloat rx0, CGFloat rx1)
    {
        CGFloat m = lx1 - lx0, pad = 14, ah = y1 - y0;
        dpadR = MIN(66.0, MIN((m - pad) * 0.5, ah * 0.22));
        btnR = dpadR * 0.36; abxyR = dpadR * 0.88;
        CGFloat cy = y0 + ah * 0.55;
        dpadX = lx0 + pad + dpadR + 4; dpadY = cy;
        abxyX = rx1 - pad - abxyR - btnR; abxyY = cy;
        CGFloat lrW = MIN(86.0, m - 2 * pad), lrH = 34, t = y0 + pad;
        l = R(lx0 + pad, t, lx0 + pad + lrW, t + lrH);
        r = R(rx1 - pad - lrW, t, rx1 - pad, t + lrH);
        CGFloat pillW = MIN(64.0, m - 2 * pad), pillH = 26, b = y1 - pad - pillH;
        select = R(lx0 + pad, b, lx0 + pad + pillW, b + pillH);
        start = R(rx1 - pad - pillW, b, rx1 - pad, b + pillH);
        CGFloat mb = 36, my = CGRectGetMaxY(l) + 12;
        menu = R(rx1 - pad - mb, my, rx1 - pad, my + mb);
        ffwd = R(lx0 + pad, my, lx0 + pad + mb, my + mb);
    }
};

// ------------------------------------------------------------------------------------------------

@protocol ControlsDelegate <NSObject>
- (void)controlsMenu;
- (void)controlsFastForward:(BOOL)on;
@end

@interface ControlsView : UIView
@property (nonatomic, weak) id<ControlsDelegate> delegate;
@property (nonatomic) BOOL showButtons;
@property (nonatomic) CGFloat opacity;
@property (nonatomic) BOOL ffwdOn;
@property (nonatomic) BOOL haptics;
@property (nonatomic) Layout* layout;
@end

@implementation ControlsView
{
    NSMapTable<UITouch*, NSNumber*>* _held;   // touch -> key mask, or -1 = touch screen
    UITouch* _screenTouch;
    unsigned _lastKeys;
    UIImpactFeedbackGenerator* _haptic;
}

- (instancetype)initWithFrame:(CGRect)f
{
    if ((self = [super initWithFrame:f]))
    {
        self.multipleTouchEnabled = YES;
        self.backgroundColor = UIColor.clearColor;
        self.opaque = NO;
        _held = [NSMapTable weakToStrongObjectsMapTable];
        _haptic = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
        _showButtons = YES; _opacity = 0.55; _haptics = YES;
    }
    return self;
}

- (CGPoint)face:(int)i
{
    Layout& L = *_layout;
    switch (i) { case 0: return CGPointMake(L.abxyX + L.abxyR, L.abxyY); case 1: return CGPointMake(L.abxyX, L.abxyY + L.abxyR);
                 case 2: return CGPointMake(L.abxyX, L.abxyY - L.abxyR); default: return CGPointMake(L.abxyX - L.abxyR, L.abxyY); }
}

- (unsigned)keysAt:(CGPoint)p
{
    if (!_showButtons) return 0;
    Layout& L = *_layout;
    CGFloat dx = p.x - L.dpadX, dy = p.y - L.dpadY, dist = hypot(dx, dy);
    if (dist < L.dpadR * 1.25 && dist > L.dpadR * 0.15)
    {
        double a = atan2(dy, dx) * 180 / M_PI;
        unsigned k = 0;
        if (a > -67.5 && a < 67.5) k |= WC09_KEY_RIGHT;
        if (a > 112.5 || a < -112.5) k |= WC09_KEY_LEFT;
        if (a > 22.5 && a < 157.5) k |= WC09_KEY_DOWN;
        if (a < -22.5 && a > -157.5) k |= WC09_KEY_UP;
        return k;
    }
    unsigned bits[4] = {WC09_KEY_A, WC09_KEY_B, WC09_KEY_X, WC09_KEY_Y}, k = 0;
    for (int i = 0; i < 4; i++)
    {
        CGPoint c = [self face:i];
        if (hypot(p.x - c.x, p.y - c.y) < L.btnR * 1.35) k |= bits[i];
    }
    if (k) return k;
    if (CGRectContainsPoint(CGRectInset(L.l, -8, -8), p)) return WC09_KEY_L;
    if (CGRectContainsPoint(CGRectInset(L.r, -8, -8), p)) return WC09_KEY_R;
    if (CGRectContainsPoint(CGRectInset(L.start, -8, -8), p)) return WC09_KEY_START;
    if (CGRectContainsPoint(CGRectInset(L.select, -8, -8), p)) return WC09_KEY_SELECT;
    return 0;
}

- (void)sendTouch:(CGPoint)p down:(BOOL)down
{
    CGRect b = _layout->bottom;
    int x = (int)((p.x - b.origin.x) * 256 / b.size.width), y = (int)((p.y - b.origin.y) * 192 / b.size.height);
    gInput.touch(down, MAX(0, MIN(255, x)), MAX(0, MIN(191, y)));
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)e
{
    for (UITouch* t in touches)
    {
        CGPoint p = [t locationInView:self];
        if (CGRectContainsPoint(CGRectInset(_layout->menu, -10, -10), p)) { [_delegate controlsMenu]; continue; }
        if (_showButtons && CGRectContainsPoint(CGRectInset(_layout->ffwd, -8, -8), p))
        {
            _ffwdOn = !_ffwdOn; [_delegate controlsFastForward:_ffwdOn]; [self setNeedsDisplay]; continue;
        }
        unsigned k = [self keysAt:p];
        if (k) [_held setObject:@(k) forKey:t];
        else if (CGRectContainsPoint(_layout->bottom, p) && !_screenTouch)
        {
            _screenTouch = t; [_held setObject:@(-1) forKey:t];
            [self sendTouch:p down:YES];
        }
    }
    [self updateKeys];
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)e
{
    for (UITouch* t in touches)
    {
        if (![_held objectForKey:t]) continue;
        CGPoint p = [t locationInView:self];
        if (t == _screenTouch) [self sendTouch:p down:YES];
        else [_held setObject:@([self keysAt:p]) forKey:t];
    }
    [self updateKeys];
}

- (void)endTouches:(NSSet<UITouch*>*)touches
{
    for (UITouch* t in touches)
    {
        if (t == _screenTouch) { [self sendTouch:[t locationInView:self] down:NO]; _screenTouch = nil; }
        [_held removeObjectForKey:t];
    }
    [self updateKeys];
}
- (void)touchesEnded:(NSSet<UITouch*>*)t withEvent:(UIEvent*)e { [self endTouches:t]; }
- (void)touchesCancelled:(NSSet<UITouch*>*)t withEvent:(UIEvent*)e { [self endTouches:t]; }

- (void)updateKeys
{
    unsigned k = 0;
    for (UITouch* t in _held) { int v = [[_held objectForKey:t] intValue]; if (v > 0) k |= (unsigned)v; }
    if (_haptics && (k & ~_lastKeys)) [_haptic impactOccurred];
    if (k != _lastKeys) { _lastKeys = k; gInput.setScreenKeys(k); [self setNeedsDisplay]; }
}

- (void)releaseAll { [_held removeAllObjects]; _screenTouch = nil; _lastKeys = 0; [self setNeedsDisplay]; }

// drawing

- (void)styleFill:(BOOL)pressed alpha:(CGFloat)a ctx:(CGContextRef)c
{
    if (pressed) CGContextSetRGBFillColor(c, 1, 1, 1, MIN(1.0, a + 0.15));
    else CGContextSetRGBFillColor(c, 0.125, 0.14, 0.17, a * 0.75);
    CGContextSetRGBStrokeColor(c, 1, 1, 1, a * 0.8);
    CGContextSetLineWidth(c, 2);
}

- (void)label:(NSString*)s at:(CGPoint)p size:(CGFloat)sz pressed:(BOOL)pressed alpha:(CGFloat)a
{
    UIColor* col = pressed ? [UIColor colorWithRed:0.06 green:0.075 blue:0.094 alpha:1] : [UIColor colorWithWhite:1 alpha:MIN(1.0, a + 0.25)];
    NSDictionary* at = @{NSFontAttributeName: [UIFont systemFontOfSize:sz weight:UIFontWeightBold], NSForegroundColorAttributeName: col};
    CGSize ts = [s sizeWithAttributes:at];
    [s drawAtPoint:CGPointMake(p.x - ts.width / 2, p.y - ts.height / 2) withAttributes:at];
}

- (void)pill:(CGRect)r text:(NSString*)s pressed:(BOOL)pr alpha:(CGFloat)a size:(CGFloat)sz ctx:(CGContextRef)c
{
    [self styleFill:pr alpha:a ctx:c];
    UIBezierPath* p = [UIBezierPath bezierPathWithRoundedRect:r cornerRadius:r.size.height / 2];
    [p fill]; [p stroke];
    [self label:s at:CGPointMake(CGRectGetMidX(r), CGRectGetMidY(r)) size:sz pressed:pr alpha:a];
}

- (void)drawRect:(CGRect)rect
{
    CGContextRef c = UIGraphicsGetCurrentContext();
    Layout& L = *_layout;
    CGFloat a = _opacity;

    // menu button (full screen: a quiet chip in the corner)
    [self styleFill:NO alpha:(_showButtons ? MAX(a, 0.55) : 0.37) ctx:c];
    UIBezierPath* mp = [UIBezierPath bezierPathWithRoundedRect:L.menu cornerRadius:10];
    [mp fill]; [mp stroke];
    CGContextSetRGBStrokeColor(c, 1, 1, 1, _showButtons ? 0.9 : 0.6); CGContextSetLineWidth(c, 2.5); CGContextSetLineCap(c, kCGLineCapRound);
    for (int i = -1; i <= 1; i++)
    {
        CGFloat y = CGRectGetMidY(L.menu) + i * L.menu.size.height * 0.2, hw = L.menu.size.width * 0.26;
        CGContextMoveToPoint(c, CGRectGetMidX(L.menu) - hw, y); CGContextAddLineToPoint(c, CGRectGetMidX(L.menu) + hw, y);
        CGContextStrokePath(c);
    }
    if (!_showButtons) return;

    // d-pad
    CGFloat x = L.dpadX, y = L.dpadY, r = L.dpadR, t = r * 0.36;
    [self styleFill:NO alpha:a ctx:c];
    UIBezierPath* cross = [UIBezierPath bezierPath];
    CGPoint pts[12] = {{x - t, y - r}, {x + t, y - r}, {x + t, y - t}, {x + r, y - t}, {x + r, y + t}, {x + t, y + t},
                       {x + t, y + r}, {x - t, y + r}, {x - t, y + t}, {x - r, y + t}, {x - r, y - t}, {x - t, y - t}};
    [cross moveToPoint:pts[0]];
    for (int i = 1; i < 12; i++) [cross addLineToPoint:pts[i]];
    [cross closePath];
    [cross fill];
    CGContextSetRGBFillColor(c, 1, 1, 1, MIN(1.0, a + 0.15));
    if (_lastKeys & WC09_KEY_UP) CGContextFillRect(c, CGRectMake(x - t, y - r, 2 * t, r - t));
    if (_lastKeys & WC09_KEY_DOWN) CGContextFillRect(c, CGRectMake(x - t, y + t, 2 * t, r - t));
    if (_lastKeys & WC09_KEY_LEFT) CGContextFillRect(c, CGRectMake(x - r, y - t, r - t, 2 * t));
    if (_lastKeys & WC09_KEY_RIGHT) CGContextFillRect(c, CGRectMake(x + t, y - t, r - t, 2 * t));
    CGContextSetRGBStrokeColor(c, 1, 1, 1, a * 0.8); CGContextSetLineWidth(c, 2);
    [cross stroke];
    CGContextSetRGBFillColor(c, 1, 1, 1, MIN(1.0, a + 0.15));
    CGFloat s = t * 0.55, d = r - t * 0.55;
    CGPoint dirs[4] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    for (auto dv : dirs)
    {
        CGFloat cx = x + dv.x * d, cy = y + dv.y * d;
        CGContextMoveToPoint(c, cx + dv.x * s * 0.6, cy + dv.y * s * 0.6);
        CGContextAddLineToPoint(c, cx - dv.y * s * 0.6 - dv.x * s * 0.4, cy + dv.x * s * 0.6 - dv.y * s * 0.4);
        CGContextAddLineToPoint(c, cx + dv.y * s * 0.6 - dv.x * s * 0.4, cy - dv.x * s * 0.6 - dv.y * s * 0.4);
        CGContextClosePath(c); CGContextFillPath(c);
    }

    // face buttons
    NSArray* names = @[@"A", @"B", @"X", @"Y"];
    unsigned bits[4] = {WC09_KEY_A, WC09_KEY_B, WC09_KEY_X, WC09_KEY_Y};
    for (int i = 0; i < 4; i++)
    {
        CGPoint p = [self face:i];
        BOOL pr = (_lastKeys & bits[i]) != 0;
        [self styleFill:pr alpha:a ctx:c];
        UIBezierPath* circ = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(p.x - L.btnR, p.y - L.btnR, 2 * L.btnR, 2 * L.btnR)];
        [circ fill]; [circ stroke];
        [self label:names[i] at:p size:L.btnR * 0.95 pressed:pr alpha:a];
    }
    [self pill:L.l text:@"L" pressed:(_lastKeys & WC09_KEY_L) != 0 alpha:a size:L.btnR * 0.75 ctx:c];
    [self pill:L.r text:@"R" pressed:(_lastKeys & WC09_KEY_R) != 0 alpha:a size:L.btnR * 0.75 ctx:c];
    [self pill:L.select text:@"SELECT" pressed:(_lastKeys & WC09_KEY_SELECT) != 0 alpha:a size:11 ctx:c];
    [self pill:L.start text:@"START" pressed:(_lastKeys & WC09_KEY_START) != 0 alpha:a size:11 ctx:c];

    // fast forward
    [self styleFill:_ffwdOn alpha:a ctx:c];
    UIBezierPath* fp = [UIBezierPath bezierPathWithRoundedRect:L.ffwd cornerRadius:10];
    [fp fill]; [fp stroke];
    if (_ffwdOn) CGContextSetRGBFillColor(c, 0.06, 0.075, 0.094, 0.9); else CGContextSetRGBFillColor(c, 1, 1, 1, 0.9);
    CGFloat fs = L.ffwd.size.height * 0.2, fcx = CGRectGetMidX(L.ffwd), fcy = CGRectGetMidY(L.ffwd);
    for (int i = 0; i < 2; i++)
    {
        CGFloat ox = fcx - fs * 0.9 + i * fs;
        CGContextMoveToPoint(c, ox, fcy - fs); CGContextAddLineToPoint(c, ox + fs, fcy); CGContextAddLineToPoint(c, ox, fcy + fs);
        CGContextClosePath(c); CGContextFillPath(c);
    }
}
@end


// ------------------------------------------------------------------------------------------------
// Settings (NSUserDefaults; same keys as the Android app)

static NSString* const kSettingsChanged = @"WC09SettingsChanged";

static NSUserDefaults* Prefs() { return NSUserDefaults.standardUserDefaults; }
static int PrefScale() { return (int)[Prefs() integerForKey:@"scale"]; }

static void RegisterDefaults()
{
    [Prefs() registerDefaults:@{
        @"hdAll": @YES, @"scale": @4, @"filter": @YES, @"smooth2d": @YES, @"hdCards": @YES, @"hdText": @YES,
        @"buttons": @YES, @"opacity": @0.55f, @"haptics": @YES, @"resume": @YES, @"sound": @YES, @"ffSpeed": @3,
    }];
}

static void ApplyCoreOptions()
{
    wc09_set_option(WC09_OPT_HD_CARDS, [Prefs() boolForKey:@"hdCards"]);
    wc09_set_option(WC09_OPT_HD_TEXT, [Prefs() boolForKey:@"hdText"]);
    wc09_set_option(WC09_OPT_SMOOTH_2D, [Prefs() boolForKey:@"smooth2d"]);
    wc09_set_option(WC09_OPT_LINEAR_FILTER, [Prefs() boolForKey:@"filter"]);
    wc09_set_option(WC09_OPT_HD_ALL, [Prefs() boolForKey:@"hdAll"]);
}

static UIColor* Accent() { return [UIColor colorWithRed:0.29 green:0.64 blue:1.0 alpha:1]; }

@interface SettingsViewController : UITableViewController <UIAdaptivePresentationControllerDelegate>
@property (nonatomic, copy) void (^onDone)(void);
@end

@implementation SettingsViewController
{
    NSArray<NSDictionary*>* _sections;
}

- (instancetype)init
{
    if ((self = [super initWithStyle:UITableViewStyleInsetGrouped]))
    {
        self.title = @"Settings";
        _sections = @[
            @{@"title": @"Graphics", @"rows": @[
                @{@"type": @"seg", @"key": @"scale", @"label": @"Resolution", @"items": @[@"2x", @"3x", @"4x", @"5x", @"6x", @"8x"], @"values": @[@2, @3, @4, @5, @6, @8]},
                @{@"type": @"seg", @"key": @"filter", @"label": @"Screen filter", @"items": @[@"Smooth", @"Sharp"], @"values": @[@YES, @NO]},
                @{@"type": @"switch", @"key": @"smooth2d", @"label": @"Smooth 2D graphics"}],
              @"footer": @"Higher resolution is sharper. 4x looks great on most iPhones; 6x and 8x need a recent one and use more battery. Smooth 2D upscales menus and sprites with smooth edges instead of blocky pixels."},
            @{@"title": @"Yu-Gi-Oh! 5D's WC2009 HD", @"rows": @[
                @{@"type": @"switch", @"key": @"hdAll", @"label": @"HD improvements"},
                @{@"type": @"switch", @"key": @"hdCards", @"label": @"HD card art"},
                @{@"type": @"switch", @"key": @"hdText", @"label": @"HD text and menus"}],
              @"footer": @"Turn HD improvements off to play exactly like the original DS (you can also switch it from the in-game menu to compare). Card art uses the HDCards folder next to the game."},
            @{@"title": @"Controls", @"rows": @[
                @{@"type": @"switch", @"key": @"buttons", @"label": @"On-screen buttons (off = full screen)"},
                @{@"type": @"seg", @"key": @"opacity", @"label": @"Button opacity", @"items": @[@"25%", @"40%", @"55%", @"75%", @"95%"], @"values": @[@0.25f, @0.4f, @0.55f, @0.75f, @0.95f]},
                @{@"type": @"switch", @"key": @"haptics", @"label": @"Vibration"}],
              @"footer": @"Game controllers (Xbox, PlayStation, MFi) work as well."},
            @{@"title": @"Gameplay", @"rows": @[
                @{@"type": @"switch", @"key": @"resume", @"label": @"Resume where you left off"},
                @{@"type": @"switch", @"key": @"sound", @"label": @"Sound"},
                @{@"type": @"seg", @"key": @"ffSpeed", @"label": @"Fast forward speed", @"items": @[@"2x", @"3x", @"4x"], @"values": @[@2, @3, @4]}],
              @"footer": @"Saves and save states are kept in a Saves folder next to each game, in the same format as the computer version.\n\nCustom cover art: put a .png or .jpg with the same name as the game next to it (or in a Covers folder)."},
        ];
    }
    return self;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(done)];
    self.tableView.rowHeight = UITableViewAutomaticDimension;
}

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
    self.navigationController.presentationController.delegate = self;   // swipe down = Done
}

- (void)done
{
    void (^cb)(void) = self.onDone;
    [self dismissViewControllerAnimated:YES completion:^{ if (cb) cb(); }];
}

- (void)presentationControllerDidDismiss:(UIPresentationController*)pc { if (self.onDone) self.onDone(); }

- (NSInteger)numberOfSectionsInTableView:(UITableView*)tv { return _sections.count; }
- (NSInteger)tableView:(UITableView*)tv numberOfRowsInSection:(NSInteger)s { return [_sections[s][@"rows"] count]; }
- (NSString*)tableView:(UITableView*)tv titleForHeaderInSection:(NSInteger)s { return _sections[s][@"title"]; }
- (NSString*)tableView:(UITableView*)tv titleForFooterInSection:(NSInteger)s { return _sections[s][@"footer"]; }

- (UITableViewCell*)tableView:(UITableView*)tv cellForRowAtIndexPath:(NSIndexPath*)ip
{
    NSDictionary* row = _sections[ip.section][@"rows"][ip.row];
    UITableViewCell* cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    NSString* key = row[@"key"];
    if ([row[@"type"] isEqualToString:@"switch"])
    {
        cell.textLabel.text = row[@"label"];
        UISwitch* sw = [[UISwitch alloc] init];
        sw.on = [Prefs() boolForKey:key];
        sw.onTintColor = Accent();
        sw.accessibilityIdentifier = key;
        [sw addTarget:self action:@selector(switched:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = sw;
    }
    else
    {
        UILabel* l = [[UILabel alloc] init];
        l.text = row[@"label"];
        l.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
        UISegmentedControl* seg = [[UISegmentedControl alloc] initWithItems:row[@"items"]];
        seg.accessibilityIdentifier = [NSString stringWithFormat:@"%ld.%ld", (long)ip.section, (long)ip.row];
        NSArray* values = row[@"values"];
        id cur = [Prefs() objectForKey:key];
        seg.selectedSegmentIndex = 0;
        for (NSUInteger i = 0; i < values.count; i++)
            if (fabs([values[i] doubleValue] - [cur doubleValue]) < 0.01) seg.selectedSegmentIndex = i;
        seg.selectedSegmentTintColor = Accent();
        [seg setTitleTextAttributes:@{NSForegroundColorAttributeName: UIColor.blackColor} forState:UIControlStateSelected];
        [seg addTarget:self action:@selector(segmented:) forControlEvents:UIControlEventValueChanged];
        UIStackView* st = [[UIStackView alloc] initWithArrangedSubviews:@[l, seg]];
        st.axis = UILayoutConstraintAxisVertical; st.spacing = 10;
        st.translatesAutoresizingMaskIntoConstraints = NO;
        [cell.contentView addSubview:st];
        UILayoutGuide* g = cell.contentView.layoutMarginsGuide;
        [NSLayoutConstraint activateConstraints:@[
            [st.leadingAnchor constraintEqualToAnchor:g.leadingAnchor], [st.trailingAnchor constraintEqualToAnchor:g.trailingAnchor],
            [st.topAnchor constraintEqualToAnchor:cell.contentView.topAnchor constant:12],
            [st.bottomAnchor constraintEqualToAnchor:cell.contentView.bottomAnchor constant:-12]]];
    }
    return cell;
}

- (void)switched:(UISwitch*)sw
{
    [Prefs() setBool:sw.on forKey:sw.accessibilityIdentifier];
    [NSNotificationCenter.defaultCenter postNotificationName:kSettingsChanged object:sw.accessibilityIdentifier];
}

- (void)segmented:(UISegmentedControl*)seg
{
    NSArray* parts = [seg.accessibilityIdentifier componentsSeparatedByString:@"."];
    NSDictionary* row = _sections[[parts[0] integerValue]][@"rows"][[parts[1] integerValue]];
    [Prefs() setObject:row[@"values"][seg.selectedSegmentIndex] forKey:row[@"key"]];
    [NSNotificationCenter.defaultCenter postNotificationName:kSettingsChanged object:row[@"key"]];
}
@end

// ------------------------------------------------------------------------------------------------
// The game screen

@interface GameViewController : GLKViewController <ControlsDelegate>
- (instancetype)initWithRom:(NSString*)rom fresh:(BOOL)fresh;
@property (nonatomic, copy) void (^onClose)(void);
@end

@implementation GameViewController
{
    EAGLContext* _ctx;
    ControlsView* _controls;
    Layout _layout;
    NSString* _rom;
    BOOL _fresh, _started, _paused, _ffwd;
    CFTimeInterval _base;
    long _framesRun;
    AVAudioEngine* _engine;
    AVAudioSourceNode* _source;
}

static std::atomic<bool> gMuted{false};

- (instancetype)initWithRom:(NSString*)rom fresh:(BOOL)fresh
{
    if ((self = [super initWithNibName:nil bundle:nil])) { _rom = rom; _fresh = fresh; }
    return self;
}

- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }

- (void)viewDidLoad
{
    [super viewDidLoad];
    _ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    GLKView* v = (GLKView*)self.view;
    v.context = _ctx;
    v.drawableColorFormat = GLKViewDrawableColorFormatRGBA8888;
    v.drawableDepthFormat = GLKViewDrawableDepthFormatNone;
    v.contentScaleFactor = self.traitCollection.displayScale > 0 ? self.traitCollection.displayScale : 3;
    v.backgroundColor = UIColor.blackColor;
    self.preferredFramesPerSecond = 60;
    [EAGLContext setCurrentContext:_ctx];

    _controls = [[ControlsView alloc] initWithFrame:self.view.bounds];
    _controls.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _controls.delegate = self;
    _controls.layout = &_layout;
    _controls.showButtons = [Prefs() boolForKey:@"buttons"];
    _controls.opacity = [Prefs() floatForKey:@"opacity"];
    _controls.haptics = [Prefs() boolForKey:@"haptics"];
    [self.view addSubview:_controls];

    NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
    [nc addObserver:self selector:@selector(willResign) name:UISceneWillDeactivateNotification object:nil];
    [nc addObserver:self selector:@selector(didBecomeActive) name:UISceneDidActivateNotification object:nil];
    [nc addObserver:self selector:@selector(settingChanged:) name:kSettingsChanged object:nil];
    gMuted = ![Prefs() boolForKey:@"sound"];
    [self setupAudio];
    [self start];
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    UIScreen* screen = self.view.window.windowScene.screen;
    if (screen && self.view.contentScaleFactor != screen.nativeScale) self.view.contentScaleFactor = screen.nativeScale;
    _layout.compute(self.view.bounds.size, self.view.safeAreaInsets, [Prefs() boolForKey:@"buttons"]);
    [_controls setNeedsDisplay];
}

- (void)start
{
    [EAGLContext setCurrentContext:_ctx];
    NSString* data = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"wc09"];
    [NSFileManager.defaultManager createDirectoryAtPath:data withIntermediateDirectories:YES attributes:nil error:nil];
    wc09_init(_rom.stringByDeletingLastPathComponent.fileSystemRepresentation, data.fileSystemRepresentation,
              NSBundle.mainBundle.resourcePath.fileSystemRepresentation);
    ApplyCoreOptions();
    if (wc09_load_rom(_rom.fileSystemRepresentation, PrefScale(), 0) != 0)
    {
        UIAlertController* a = [UIAlertController alertControllerWithTitle:@"Couldn't start the game"
            message:[NSString stringWithFormat:@"%@ couldn't be loaded.", _rom.lastPathComponent] preferredStyle:UIAlertControllerStyleAlert];
        __weak GameViewController* me = self;
        [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { [me close]; }]];
        dispatch_async(dispatch_get_main_queue(), ^{ [self presentViewController:a animated:YES completion:nil]; });
        return;
    }
    _started = YES;
    if (!_fresh && [Prefs() boolForKey:@"resume"] && wc09_state_exists(kAutoSlot) && wc09_load_state(kAutoSlot) == 0)
        dispatch_async(dispatch_get_main_queue(), ^{ [self toast:@"Resumed where you left off"]; });
    _base = 0;
    [_engine startAndReturnError:nil];
}

/** Saves the resume point, shuts the emulator down and goes back to the library. */
- (void)close
{
    if (_started)
    {
        [EAGLContext setCurrentContext:_ctx];
        if ([Prefs() boolForKey:@"resume"]) wc09_save_state(kAutoSlot); else wc09_delete_state(kAutoSlot);
        wc09_flush_save();
        wc09_shutdown();
        _started = NO;
    }
    [_engine stop];
    gInput.releaseAll();
    self.paused = YES;
    void (^cb)(void) = self.onClose;
    [self.presentingViewController dismissViewControllerAnimated:YES completion:^{ if (cb) cb(); }];
}

- (void)settingChanged:(NSNotification*)n
{
    NSString* key = n.object;
    if ([key isEqualToString:@"scale"]) { if (_started) { [EAGLContext setCurrentContext:_ctx]; wc09_set_scale(PrefScale()); } }
    else if ([@[@"filter", @"smooth2d", @"hdCards", @"hdText", @"hdAll"] containsObject:key]) ApplyCoreOptions();
    else if ([key isEqualToString:@"buttons"]) { _controls.showButtons = [Prefs() boolForKey:@"buttons"]; [self.view setNeedsLayout]; }
    else if ([key isEqualToString:@"opacity"]) { _controls.opacity = [Prefs() floatForKey:@"opacity"]; [_controls setNeedsDisplay]; }
    else if ([key isEqualToString:@"haptics"]) _controls.haptics = [Prefs() boolForKey:@"haptics"];
    else if ([key isEqualToString:@"sound"]) gMuted = ![Prefs() boolForKey:@"sound"];
}

// ---- audio ----

- (void)setupAudio
{
    [AVAudioSession.sharedInstance setCategory:AVAudioSessionCategoryAmbient error:nil];
    [AVAudioSession.sharedInstance setPreferredIOBufferDuration:0.01 error:nil];
    [AVAudioSession.sharedInstance setActive:YES error:nil];
    _engine = [[AVAudioEngine alloc] init];
    AVAudioFormat* fmt = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:48000 channels:2 interleaved:NO];
    _source = [[AVAudioSourceNode alloc] initWithFormat:fmt renderBlock:^OSStatus(BOOL* silence, const AudioTimeStamp* ts, AVAudioFrameCount frames, AudioBufferList* abl) {
        static short tmp[4096 * 2];
        static float last[2] = {0, 0};
        int n = (int)MIN(frames, (AVAudioFrameCount)4096);
        int got = wc09_audio_read(tmp, n);
        bool mute = gMuted;
        float* L = (float*)abl->mBuffers[0].mData;
        float* R = abl->mNumberBuffers > 1 ? (float*)abl->mBuffers[1].mData : nullptr;
        for (int i = 0; i < (int)frames; i++)
        {
            if (i < got) { last[0] = tmp[i * 2] / 32768.f; last[1] = tmp[i * 2 + 1] / 32768.f; }
            else { last[0] *= 0.94f; last[1] *= 0.94f; }
            L[i] = mute ? 0 : last[0];
            if (R) R[i] = mute ? 0 : last[1];
        }
        return noErr;
    }];
    [_engine attachNode:_source];
    [_engine connect:_source to:_engine.mainMixerNode format:fmt];
    [_engine prepare];
}

// ---- running ----

- (void)update
{
    [self pollControllers];
    if (!_started || _paused) return;
    [EAGLContext setCurrentContext:_ctx];
    int run;
    if (_ffwd) run = (int)MAX(2, MIN(4, [Prefs() integerForKey:@"ffSpeed"]));
    else
    {
        CFTimeInterval now = CACurrentMediaTime();
        if (_base == 0) { _base = now; _framesRun = 0; }
        long due = (long)((now - _base) * kDSFps) + 1 - _framesRun;
        if (due > 4) { _base = now; _framesRun = 0; due = 1; }
        run = (int)MAX(0L, due);
        int q = wc09_audio_queued();
        if (run == 0 && q < 48000 * 30 / 1000) run = 1;
        else if (run == 2 && q > 48000 * 90 / 1000) run = 1;
        _framesRun += run;
    }
    for (int i = 0; i < run; i++) { gInput.apply(); wc09_run_frame(); }
}

- (void)glkView:(GLKView*)view drawInRect:(CGRect)rect
{
    [view bindDrawable];
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!_started) return;
    CGFloat s = view.contentScaleFactor;
    int fbw = (int)view.drawableWidth, fbh = (int)view.drawableHeight;
    wc09_draw_screen(0, (int)lround(_layout.top.origin.x * s), (int)lround(_layout.top.origin.y * s),
                     (int)lround(_layout.top.size.width * s), (int)lround(_layout.top.size.height * s), fbw, fbh);
    [view bindDrawable];
    wc09_draw_screen(1, (int)lround(_layout.bottom.origin.x * s), (int)lround(_layout.bottom.origin.y * s),
                     (int)lround(_layout.bottom.size.width * s), (int)lround(_layout.bottom.size.height * s), fbw, fbh);
}

- (void)willResign
{
    if (!_started) return;
    gInput.releaseAll();
    [_controls releaseAll];
    [EAGLContext setCurrentContext:_ctx];
    if ([Prefs() boolForKey:@"resume"]) wc09_save_state(kAutoSlot);
    wc09_flush_save();
    [_engine pause];
}

- (void)didBecomeActive
{
    if (!_started) return;
    _base = 0;
    [_engine startAndReturnError:nil];
}

// ---- controllers ----

- (void)pollControllers
{
    GCExtendedGamepad* g = GCController.controllers.firstObject.extendedGamepad;
    if (!g) return;
    unsigned k = 0;
    if (g.buttonA.pressed) k |= WC09_KEY_A;
    if (g.buttonB.pressed) k |= WC09_KEY_B;
    if (g.buttonX.pressed) k |= WC09_KEY_X;
    if (g.buttonY.pressed) k |= WC09_KEY_Y;
    if (g.leftShoulder.pressed || g.leftTrigger.pressed) k |= WC09_KEY_L;
    if (g.rightShoulder.pressed || g.rightTrigger.pressed) k |= WC09_KEY_R;
    if (g.buttonMenu.pressed) k |= WC09_KEY_START;
    if (g.buttonOptions.pressed) k |= WC09_KEY_SELECT;
    float x = g.dpad.xAxis.value + g.leftThumbstick.xAxis.value, y = g.dpad.yAxis.value + g.leftThumbstick.yAxis.value;
    if (x < -0.5f) k |= WC09_KEY_LEFT;
    if (x > 0.5f) k |= WC09_KEY_RIGHT;
    if (y > 0.5f) k |= WC09_KEY_UP;
    if (y < -0.5f) k |= WC09_KEY_DOWN;
    gInput.setPadKeys(k);
}

// ---- menu ----

- (void)controlsFastForward:(BOOL)on { _ffwd = on; _base = 0; }

- (void)resume { _paused = NO; _base = 0; }

- (void)controlsMenu
{
    if (!_started) return;
    _paused = YES;
    UIAlertController* m = [UIAlertController alertControllerWithTitle:_rom.lastPathComponent.stringByDeletingPathExtension message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    __weak GameViewController* me = self;
    BOOL full = ![Prefs() boolForKey:@"buttons"];
    [m addAction:[UIAlertAction actionWithTitle:(full ? @"Exit full screen" : @"Full screen (hide buttons)") style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
        GameViewController* g = me;
        if (!g) return;
        [Prefs() setBool:full forKey:@"buttons"];
        [NSNotificationCenter.defaultCenter postNotificationName:kSettingsChanged object:@"buttons"];
        [g resume];
    }]];
    [m addAction:[UIAlertAction actionWithTitle:@"Save state" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { [me stateMenu:YES]; }]];
    [m addAction:[UIAlertAction actionWithTitle:@"Load state" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { [me stateMenu:NO]; }]];
    if (wc09_is_hd_game())
    {
        BOOL hd = [Prefs() boolForKey:@"hdAll"];
        [m addAction:[UIAlertAction actionWithTitle:(hd ? @"Switch to original look" : @"Switch to HD look") style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
            GameViewController* g = me;
            if (!g) return;
            [Prefs() setBool:!hd forKey:@"hdAll"];
            ApplyCoreOptions();
            [g toast:hd ? @"Original look" : @"HD look"];
            [g resume];
        }]];
    }
    [m addAction:[UIAlertAction actionWithTitle:@"Settings" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { [me openSettings]; }]];
    [m addAction:[UIAlertAction actionWithTitle:@"Restart from in-game save" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { [me restartAsk]; }]];
    [m addAction:[UIAlertAction actionWithTitle:@"Back to library" style:UIAlertActionStyleDestructive handler:^(UIAlertAction*) { [me close]; }]];
    [m addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { [me resume]; }]];
    [self present:m];
}

- (void)openSettings
{
    SettingsViewController* s = [[SettingsViewController alloc] init];
    __weak GameViewController* me = self;
    s.onDone = ^{ [me resume]; };
    UINavigationController* nav = [[UINavigationController alloc] initWithRootViewController:s];
    [self presentViewController:nav animated:YES completion:nil];
}

- (void)present:(UIAlertController*)a
{
    if (a.popoverPresentationController)
    {
        a.popoverPresentationController.sourceView = self.view;
        a.popoverPresentationController.sourceRect = _layout.menu;
    }
    [self presentViewController:a animated:YES completion:nil];
}

- (void)stateMenu:(BOOL)save
{
    UIAlertController* m = [UIAlertController alertControllerWithTitle:(save ? @"Save state" : @"Load state") message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    __weak GameViewController* me = self;
    for (int i = 1; i <= 8; i++)
    {
        NSString* t = [NSString stringWithFormat:@"Slot %d%@", i, wc09_state_exists(i) ? @"" : @"   (empty)"];
        [m addAction:[UIAlertAction actionWithTitle:t style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
            GameViewController* s = me;
            if (!s) return;
            [EAGLContext setCurrentContext:s->_ctx];
            int r = save ? wc09_save_state(i) : wc09_load_state(i);
            [s toast:r == 0 ? [NSString stringWithFormat:(save ? @"Saved to slot %d" : @"Loaded slot %d"), i]
                            : (save ? @"Couldn't save" : [NSString stringWithFormat:@"Couldn't load slot %d", i])];
            [s resume];
        }]];
    }
    [m addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { [me resume]; }]];
    [self present:m];
}

- (void)restartAsk
{
    UIAlertController* a = [UIAlertController alertControllerWithTitle:@"Restart the game?"
        message:@"Starts again from your last in-game save. Anything after it is lost (save states are kept)."
        preferredStyle:UIAlertControllerStyleAlert];
    __weak GameViewController* me = self;
    [a addAction:[UIAlertAction actionWithTitle:@"Restart" style:UIAlertActionStyleDestructive handler:^(UIAlertAction*) {
        GameViewController* g = me;
        if (!g) return;
        [EAGLContext setCurrentContext:g->_ctx];
        wc09_delete_state(kAutoSlot);
        wc09_load_rom(g->_rom.fileSystemRepresentation, PrefScale(), 0);
        [g resume];
    }]];
    [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { [me resume]; }]];
    [self presentViewController:a animated:YES completion:nil];
}

- (void)toast:(NSString*)text
{
    UILabel* l = [[UILabel alloc] init];
    l.text = text; l.textColor = UIColor.whiteColor; l.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    l.textAlignment = NSTextAlignmentCenter; l.numberOfLines = 0;
    l.backgroundColor = [UIColor colorWithWhite:0.1 alpha:0.85];
    l.layer.cornerRadius = 12; l.clipsToBounds = YES;
    CGSize sz = [l sizeThatFits:CGSizeMake(self.view.bounds.size.width - 80, 200)];
    l.frame = CGRectMake((self.view.bounds.size.width - sz.width - 32) / 2, self.view.safeAreaInsets.top + 24, sz.width + 32, sz.height + 20);
    [self.view addSubview:l];
    [UIView animateWithDuration:0.4 delay:2.2 options:0 animations:^{ l.alpha = 0; } completion:^(BOOL) { [l removeFromSuperview]; }];
}
@end

// ------------------------------------------------------------------------------------------------
// The library: every .nds in Documents (and one folder deep), most recently played first

@interface GameEntry : NSObject
@property (nonatomic, copy) NSString *path, *title, *publisher, *base;
@property (nonatomic, strong) UIImage *icon, *cover;
@property (nonatomic, strong) UIColor* tint;
@property (nonatomic) BOOL resume;
@property (nonatomic) double lastPlayed;
@end
@implementation GameEntry
@end

static NSString* PrettyTitle(NSString* s)
{
    NSMutableArray* words = [NSMutableArray array];
    for (NSString* w in [s componentsSeparatedByString:@" "])
    {
        BOOL caps = w.length > 3 && [w isEqualToString:w.uppercaseString] && ![w isEqualToString:w.lowercaseString];
        [words addObject:caps ? [[w substringToIndex:1] stringByAppendingString:[w substringFromIndex:1].lowercaseString] : w];
    }
    return [words componentsJoinedByString:@" "];
}

static GameEntry* LoadEntry(NSString* path)
{
    GameEntry* g = [GameEntry new];
    g.path = path;
    g.base = path.lastPathComponent.stringByDeletingPathExtension;
    char title[512];
    unsigned char px[32 * 32 * 4];
    NSArray<NSString*>* lines = @[];
    if (wc09_rom_info(path.fileSystemRepresentation, title, sizeof(title), px) == 0)
        lines = [[NSString stringWithUTF8String:title] componentsSeparatedByString:@"\n"];
    if (lines.count >= 2)
    {
        g.title = PrettyTitle([[lines subarrayWithRange:NSMakeRange(0, lines.count - 1)] componentsJoinedByString:@" "]);
        g.publisher = lines.lastObject;
    }
    else { g.title = lines.count == 1 && lines[0].length ? PrettyTitle(lines[0]) : g.base; g.publisher = @""; }

    // icon -> UIImage, and its average colour for the tile background
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef bc = CGBitmapContextCreate(px, 32, 32, 8, 128, cs, kCGImageAlphaPremultipliedLast);
    long r = 0, gg = 0, b = 0, n = 0;
    for (int i = 0; i < 32 * 32; i++)
        if (px[i * 4 + 3]) { r += px[i * 4]; gg += px[i * 4 + 1]; b += px[i * 4 + 2]; n++; }
    CGImageRef img = CGBitmapContextCreateImage(bc);
    g.icon = [UIImage imageWithCGImage:img];
    CGImageRelease(img); CGContextRelease(bc); CGColorSpaceRelease(cs);
    g.tint = n ? [UIColor colorWithRed:r / n / 255.0 green:gg / n / 255.0 blue:b / n / 255.0 alpha:1] : [UIColor colorWithRed:0.16 green:0.2 blue:0.27 alpha:1];

    NSString* dir = path.stringByDeletingLastPathComponent;
    for (NSString* d in @[dir, [dir stringByAppendingPathComponent:@"Covers"]])
        for (NSString* ext in @[@"png", @"jpg", @"jpeg", @"heic"])
        {
            if (g.cover) break;
            NSString* f = [[d stringByAppendingPathComponent:g.base] stringByAppendingPathExtension:ext];
            UIImage* im = [UIImage imageWithContentsOfFile:f];
            if (im) g.cover = [im imageByPreparingThumbnailOfSize:CGSizeMake(512, 512 * im.size.height / MAX(1.0, im.size.width))] ?: im;
        }
    g.resume = [NSFileManager.defaultManager fileExistsAtPath:[[dir stringByAppendingPathComponent:@"Saves"] stringByAppendingPathComponent:[g.base stringByAppendingString:@".ml0"]]];
    g.lastPlayed = [Prefs() doubleForKey:[@"played:" stringByAppendingString:path.lastPathComponent]];
    return g;
}

@interface GameCell : UICollectionViewCell
@property (nonatomic, strong) GameEntry* game;
@end

@implementation GameCell
{
    UIView* _art;
    CAGradientLayer* _grad;
    UIImageView *_icon, *_cover;
    UILabel *_title, *_sub, *_badge;
}

- (instancetype)initWithFrame:(CGRect)f
{
    if ((self = [super initWithFrame:f]))
    {
        self.contentView.backgroundColor = [UIColor colorWithRed:0.082 green:0.106 blue:0.153 alpha:1];
        self.contentView.layer.cornerRadius = 16;
        self.contentView.layer.cornerCurve = kCACornerCurveContinuous;
        self.contentView.clipsToBounds = YES;
        _art = [UIView new];
        _grad = [CAGradientLayer layer];
        _grad.startPoint = CGPointMake(0, 0); _grad.endPoint = CGPointMake(1, 1);
        [_art.layer addSublayer:_grad];
        _art.clipsToBounds = YES;
        _icon = [UIImageView new];
        _icon.layer.magnificationFilter = kCAFilterNearest;
        _icon.layer.shadowColor = UIColor.blackColor.CGColor; _icon.layer.shadowOpacity = 0.4; _icon.layer.shadowRadius = 8; _icon.layer.shadowOffset = CGSizeMake(0, 4);
        _cover = [UIImageView new];
        _cover.contentMode = UIViewContentModeScaleAspectFill;
        _cover.clipsToBounds = YES;
        [_art addSubview:_icon]; [_art addSubview:_cover];
        _title = [UILabel new];
        _title.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
        _title.textColor = UIColor.whiteColor; _title.numberOfLines = 2;
        _sub = [UILabel new];
        _sub.font = [UIFont systemFontOfSize:12.5];
        _sub.textColor = [UIColor colorWithRed:0.6 green:0.65 blue:0.72 alpha:1];
        _badge = [UILabel new];
        _badge.text = @"RESUME";
        _badge.font = [UIFont systemFontOfSize:10.5 weight:UIFontWeightBold];
        _badge.textColor = Accent(); _badge.textAlignment = NSTextAlignmentCenter;
        _badge.backgroundColor = [UIColor colorWithRed:0.04 green:0.055 blue:0.086 alpha:0.9];
        _badge.layer.cornerRadius = 11; _badge.clipsToBounds = YES;
        for (UIView* v in @[_art, _title, _sub, _badge]) [self.contentView addSubview:v];
    }
    return self;
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    CGFloat w = self.contentView.bounds.size.width;
    _art.frame = CGRectMake(0, 0, w, w);
    _grad.frame = _art.bounds;
    _cover.frame = _art.bounds;
    CGFloat s = floor(w * 0.56 / 32) * 32;
    if (s < 32) s = w * 0.56;
    _icon.frame = CGRectMake((w - s) / 2, (w - s) / 2, s, s);
    _title.frame = CGRectMake(12, w + 9, w - 24, 40);
    [_title sizeToFit];
    _title.frame = CGRectMake(12, w + 9, w - 24, _title.frame.size.height);
    _sub.frame = CGRectMake(12, CGRectGetMaxY(_title.frame) + 2, w - 24, 16);
    _badge.frame = CGRectMake(w - 8 - 64, 8, 64, 22);
}

static UIColor* Mix(UIColor* a, UIColor* b, CGFloat t)
{
    CGFloat r1, g1, b1, a1, r2, g2, b2, a2;
    [a getRed:&r1 green:&g1 blue:&b1 alpha:&a1]; [b getRed:&r2 green:&g2 blue:&b2 alpha:&a2];
    return [UIColor colorWithRed:r1 + (r2 - r1) * t green:g1 + (g2 - g1) * t blue:b1 + (b2 - b1) * t alpha:1];
}

- (void)setGame:(GameEntry*)g
{
    _game = g;
    UIColor* bg = [UIColor colorWithRed:0.04 green:0.055 blue:0.086 alpha:1];
    _grad.colors = @[(id)Mix(g.tint, bg, 0.45).CGColor, (id)Mix(g.tint, bg, 0.8).CGColor];
    _icon.image = g.icon;
    _cover.image = g.cover;
    _cover.hidden = g.cover == nil;
    _icon.hidden = g.cover != nil;
    _title.text = g.title;
    _sub.text = g.publisher;
    _badge.hidden = !g.resume;
    [self setNeedsLayout];
}

- (void)setHighlighted:(BOOL)h
{
    [super setHighlighted:h];
    [UIView animateWithDuration:0.15 animations:^{ self.transform = h ? CGAffineTransformMakeScale(0.96, 0.96) : CGAffineTransformIdentity; }];
}
@end

@interface LibraryViewController : UICollectionViewController
@end

@implementation LibraryViewController
{
    NSArray<GameEntry*>* _games;
    UILabel* _empty;
}

static UICollectionViewLayout* GridLayout()
{
    return [[UICollectionViewCompositionalLayout alloc] initWithSectionProvider:^NSCollectionLayoutSection*(NSInteger s, id<NSCollectionLayoutEnvironment> env) {
        CGFloat w = env.container.effectiveContentSize.width;
        NSInteger cols = MAX(2, (NSInteger)((w - 32) / 170));
        CGFloat cellW = (w - 32 - (cols - 1) * 14) / cols;
        NSCollectionLayoutSize* is = [NSCollectionLayoutSize sizeWithWidthDimension:[NSCollectionLayoutDimension absoluteDimension:cellW]
                                                                    heightDimension:[NSCollectionLayoutDimension absoluteDimension:cellW + 72]];
        NSCollectionLayoutItem* item = [NSCollectionLayoutItem itemWithLayoutSize:is];
        NSCollectionLayoutSize* gs = [NSCollectionLayoutSize sizeWithWidthDimension:[NSCollectionLayoutDimension fractionalWidthDimension:1]
                                                                    heightDimension:[NSCollectionLayoutDimension absoluteDimension:cellW + 72]];
        NSCollectionLayoutGroup* group = [NSCollectionLayoutGroup horizontalGroupWithLayoutSize:gs repeatingSubitem:item count:cols];
        group.interItemSpacing = [NSCollectionLayoutSpacing fixedSpacing:14];
        NSCollectionLayoutSection* sec = [NSCollectionLayoutSection sectionWithGroup:group];
        sec.interGroupSpacing = 16;
        sec.contentInsets = NSDirectionalEdgeInsetsMake(8, 16, 24, 16);
        return sec;
    }];
}

- (instancetype)init { return [super initWithCollectionViewLayout:GridLayout()]; }

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.title = @"Library";
    self.navigationController.navigationBar.prefersLargeTitles = YES;
    self.collectionView.backgroundColor = [UIColor colorWithRed:0.04 green:0.055 blue:0.086 alpha:1];
    [self.collectionView registerClass:GameCell.class forCellWithReuseIdentifier:@"g"];
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"gearshape"]
                                                                              style:UIBarButtonItemStylePlain target:self action:@selector(openSettings)];
    UIRefreshControl* rc = [UIRefreshControl new];
    [rc addTarget:self action:@selector(pulled:) forControlEvents:UIControlEventValueChanged];
    self.collectionView.refreshControl = rc;
    _empty = [UILabel new];
    _empty.numberOfLines = 0; _empty.textAlignment = NSTextAlignmentCenter;
    _empty.textColor = [UIColor colorWithRed:0.6 green:0.65 blue:0.72 alpha:1];
    _empty.font = [UIFont systemFontOfSize:16];
    _empty.text = @"No games yet\n\n"
                   "From a Mac: plug in the iPhone, open Finder, click the iPhone, open the Files tab and drag your .nds files "
                   "(and for Yu-Gi-Oh! 5D's WC2009, its HDCards folder) onto WC2009 HD.\n\n"
                   "Or use the Files app: On My iPhone > WC2009 HD.\n\n"
                   "Custom cover art: add a .png or .jpg with the same name as the game.";
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(reload) name:UISceneDidActivateNotification object:nil];
    [self reload];
}

- (UIStatusBarStyle)preferredStatusBarStyle { return UIStatusBarStyleLightContent; }

- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; [self reload]; }

- (void)pulled:(UIRefreshControl*)rc { [self reload]; [rc endRefreshing]; }

- (void)reload
{
    NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSMutableArray<NSString*>* roms = [NSMutableArray array];
        NSFileManager* fm = NSFileManager.defaultManager;
        void (^scan)(NSString*) = ^(NSString* dir) {
            for (NSString* f in [fm contentsOfDirectoryAtPath:dir error:nil])
                if ([f.pathExtension.lowercaseString isEqualToString:@"nds"] && ![f hasPrefix:@"."]) [roms addObject:[dir stringByAppendingPathComponent:f]];
        };
        scan(docs);
        for (NSString* f in [fm contentsOfDirectoryAtPath:docs error:nil])
        {
            NSString* p = [docs stringByAppendingPathComponent:f];
            BOOL isDir = NO;
            if ([fm fileExistsAtPath:p isDirectory:&isDir] && isDir && ![@[@"Saves", @"HDCards", @"Covers"] containsObject:f]) scan(p);
        }
        NSMutableArray<GameEntry*>* games = [NSMutableArray array];
        for (NSString* r in roms) [games addObject:LoadEntry(r)];
        [games sortUsingComparator:^NSComparisonResult(GameEntry* a, GameEntry* b) {
            if (a.lastPlayed != b.lastPlayed) return a.lastPlayed > b.lastPlayed ? NSOrderedAscending : NSOrderedDescending;
            return [a.title localizedCaseInsensitiveCompare:b.title];
        }];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_games = games;
            [self.collectionView reloadData];
            self.collectionView.backgroundView = games.count ? nil : [self emptyView];
        });
    });
}

- (UIView*)emptyView
{
    UIView* v = [UIView new];
    _empty.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:_empty];
    [NSLayoutConstraint activateConstraints:@[
        [_empty.leadingAnchor constraintEqualToAnchor:v.leadingAnchor constant:32],
        [_empty.trailingAnchor constraintEqualToAnchor:v.trailingAnchor constant:-32],
        [_empty.centerYAnchor constraintEqualToAnchor:v.centerYAnchor constant:-40]]];
    return v;
}

- (NSInteger)collectionView:(UICollectionView*)cv numberOfItemsInSection:(NSInteger)s { return _games.count; }

- (UICollectionViewCell*)collectionView:(UICollectionView*)cv cellForItemAtIndexPath:(NSIndexPath*)ip
{
    GameCell* c = [cv dequeueReusableCellWithReuseIdentifier:@"g" forIndexPath:ip];
    c.game = _games[ip.item];
    return c;
}

- (void)collectionView:(UICollectionView*)cv didSelectItemAtIndexPath:(NSIndexPath*)ip { [self play:_games[ip.item] fresh:NO]; }

- (UIContextMenuConfiguration*)collectionView:(UICollectionView*)cv contextMenuConfigurationForItemAtIndexPath:(NSIndexPath*)ip point:(CGPoint)p
{
    GameEntry* g = _games[ip.item];
    __weak LibraryViewController* me = self;
    return [UIContextMenuConfiguration configurationWithIdentifier:nil previewProvider:nil actionProvider:^UIMenu*(NSArray<UIMenuElement*>*) {
        NSMutableArray* items = [NSMutableArray arrayWithObject:[UIAction actionWithTitle:@"Play" image:[UIImage systemImageNamed:@"play.fill"] identifier:nil handler:^(UIAction*) { [me play:g fresh:NO]; }]];
        if (g.resume)
        {
            [items addObject:[UIAction actionWithTitle:@"Start from in-game save" image:[UIImage systemImageNamed:@"arrow.counterclockwise"] identifier:nil handler:^(UIAction*) { [me play:g fresh:YES]; }]];
            UIAction* del = [UIAction actionWithTitle:@"Remove resume point" image:[UIImage systemImageNamed:@"trash"] identifier:nil handler:^(UIAction*) {
                NSString* f = [[[g.path.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"Saves"] stringByAppendingPathComponent:g.base] stringByAppendingString:@".ml0"];
                [NSFileManager.defaultManager removeItemAtPath:f error:nil];
                [me reload];
            }];
            del.attributes = UIMenuElementAttributesDestructive;
            [items addObject:del];
        }
        return [UIMenu menuWithTitle:g.title children:items];
    }];
}

- (void)play:(GameEntry*)g fresh:(BOOL)fresh
{
    [Prefs() setDouble:NSDate.date.timeIntervalSince1970 forKey:[@"played:" stringByAppendingString:g.path.lastPathComponent]];
    GameViewController* vc = [[GameViewController alloc] initWithRom:g.path fresh:fresh];
    vc.modalPresentationStyle = UIModalPresentationFullScreen;
    vc.modalTransitionStyle = UIModalTransitionStyleCrossDissolve;
    __weak LibraryViewController* me = self;
    vc.onClose = ^{ [me reload]; };
    [self presentViewController:vc animated:YES completion:nil];
}

- (void)openSettings
{
    UINavigationController* nav = [[UINavigationController alloc] initWithRootViewController:[SettingsViewController new]];
    [self presentViewController:nav animated:YES completion:nil];
}
@end

// ------------------------------------------------------------------------------------------------

// scene-based life cycle (required by the current iOS SDK)
@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication*)app didFinishLaunchingWithOptions:(NSDictionary*)opts
{
    RegisterDefaults();
    return YES;
}

- (UISceneConfiguration*)application:(UIApplication*)app configurationForConnectingSceneSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options
{
    UISceneConfiguration* c = [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:session.role];
    c.delegateClass = NSClassFromString(@"SceneDelegate");
    return c;
}
@end

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property (nonatomic, strong) UIWindow* window;
@end

@implementation SceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options
{
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
    self.window.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.window.tintColor = Accent();
    UINavigationController* nav = [[UINavigationController alloc] initWithRootViewController:[LibraryViewController new]];
    nav.navigationBar.prefersLargeTitles = YES;
    self.window.rootViewController = nav;
    self.window.backgroundColor = UIColor.blackColor;
    [self.window makeKeyAndVisible];
}
@end

int main(int argc, char* argv[])
{
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(AppDelegate.class)); }
}
