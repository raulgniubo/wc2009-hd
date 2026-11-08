// Dual HD for iOS: a DS emulator (melonDS + the per-game HD work in mobile/core), touch controls, game
// controllers, AVAudioEngine output. The game files go in the app's Documents folder (visible in
// the Files app and in Finder when the iPhone is connected to a Mac).
#define GLES_SILENCE_DEPRECATION 1
#import <UIKit/UIKit.h>
#include <sys/utsname.h>
#import <GLKit/GLKit.h>
#import <AVFoundation/AVFoundation.h>
#import <GameController/GameController.h>
#import <PhotosUI/PhotosUI.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
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
static void PostChange(NSString* key) { [NSNotificationCenter.defaultCenter postNotificationName:kSettingsChanged object:key]; }

static void MigratePrefs();
static void RegisterDefaults()
{
    [Prefs() registerDefaults:@{
        @"boxart": @YES, @"sort": @"recent", @"perf": @NO, @"perfDetail": @NO, @"hdAll": @YES, @"hdMode": @2, @"scale": @4, @"filter": @YES, @"smooth2d": @YES, @"hdCards": @YES, @"hdText": @YES,
        @"buttons": @YES, @"padHide": @YES, @"padLayout": @0, @"opacity": @0.55f, @"haptics": @YES, @"resume": @YES, @"sound": @YES, @"ffSpeed": @3,
    }];
    MigratePrefs();
    // controllers coming and going: pages and the game screen update themselves
    for (NSNotificationName n in @[GCControllerDidConnectNotification, GCControllerDidDisconnectNotification])
        [NSNotificationCenter.defaultCenter addObserverForName:n object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* note) {
            GCController* c = note.object;
            // the Home button opens the Dual HD menu instead of the system's game overlay
            if (@available(iOS 14.0, *)) if (c.extendedGamepad.buttonHome) c.extendedGamepad.buttonHome.preferredSystemGestureState = GCSystemGestureStateDisabled;
            PostChange(@"pads");
        }];
}

// older versions had one on/off switch for the enhancements: off becomes the Original mode
static void MigratePrefs()
{
    NSUserDefaults* d = Prefs();
    NSDictionary* saved = [d persistentDomainForName:NSBundle.mainBundle.bundleIdentifier];
    if (saved[@"hdMode"] == nil && saved[@"hdAll"] != nil && ![saved[@"hdAll"] boolValue]) [d setInteger:0 forKey:@"hdMode"];
}

// enhancement modes for packs: 0 original, 1 HD cards only, 2 everything in HD with the original
// look, 3 HD plus the redesign
static NSArray* HDModeChoices()
{
    return @[@{@"title": @"HD", @"detail": @"Everything in high resolution, looking just like the original: cards, characters, backgrounds, 3D world. Recommended.", @"value": @2},
             @{@"title": @"HD + Redesign", @"detail": @"HD plus a modern redesign: new life point panel, duel buttons and icons, menus, and card text in a clean font. Changes how the game looks.", @"value": @3},
             @{@"title": @"HD Cards", @"detail": @"The original game with just the card art in HD. Lightest, good for older phones.", @"value": @1},
             @{@"title": @"Original", @"detail": @"The game exactly as on the DS. Resolution and smoothing settings still apply.", @"value": @0}];
}
static NSString* HDModeName(NSInteger m) { return m == 0 ? @"Original" : m == 1 ? @"HD Cards" : m == 3 ? @"HD + Redesign" : @"HD"; }

static void ApplyCoreOptions()
{
    wc09_set_option(WC09_OPT_HD_CARDS, 1);   // the mode decides (an older separate switch is ignored)
    wc09_set_option(WC09_OPT_HD_TEXT, 1);
    wc09_set_option(WC09_OPT_SMOOTH_2D, [Prefs() boolForKey:@"smooth2d"]);
    wc09_set_option(WC09_OPT_LINEAR_FILTER, [Prefs() boolForKey:@"filter"]);
    wc09_set_option(WC09_OPT_HD_MODE, (int)[Prefs() integerForKey:@"hdMode"]);
}

static UIColor* Accent() { return [UIColor colorWithRed:0.29 green:0.64 blue:1.0 alpha:1]; }
static UIColor* AppBackground() { return [UIColor colorWithRed:0.04 green:0.055 blue:0.086 alpha:1]; }
static UIColor* RGB(int hex) { return [UIColor colorWithRed:((hex >> 16) & 255) / 255.0 green:((hex >> 8) & 255) / 255.0 blue:(hex & 255) / 255.0 alpha:1]; }

// ---- game controllers ----
// Controller buttons (as the GameController framework names them: A is the bottom face button,
// B the right one, X the left one, Y the top one) and what they do in the game.
enum { PB_A, PB_B, PB_X, PB_Y, PB_L1, PB_R1, PB_L2, PB_R2, PB_L3, PB_R3, PB_MENU, PB_OPTIONS, PB_HOME, PB_COUNT };
enum { PA_NONE, PA_A, PA_B, PA_X, PA_Y, PA_L, PA_R, PA_START, PA_SELECT, PA_MENU, PA_FFWD, PA_COUNT };
static NSString* PadActionName(int a)
{
    static NSString* const n[PA_COUNT] = {@"Nothing", @"A", @"B", @"X", @"Y", @"L", @"R", @"START", @"SELECT", @"Dual HD Menu", @"Fast Forward (hold)"};
    return a >= 0 && a < PA_COUNT ? n[a] : @"";
}
static GCControllerButtonInput* PadElement(GCExtendedGamepad* g, int b)
{
    switch (b)
    {
    case PB_A: return g.buttonA; case PB_B: return g.buttonB; case PB_X: return g.buttonX; case PB_Y: return g.buttonY;
    case PB_L1: return g.leftShoulder; case PB_R1: return g.rightShoulder; case PB_L2: return g.leftTrigger; case PB_R2: return g.rightTrigger;
    case PB_L3: return g.leftThumbstickButton; case PB_R3: return g.rightThumbstickButton;
    case PB_MENU: return g.buttonMenu; case PB_OPTIONS: return g.buttonOptions; case PB_HOME: return g.buttonHome;
    }
    return nil;
}
// the name printed on the controller (Cross, Circle… on PlayStation pads)
static NSString* PadButtonName(int b)
{
    GCExtendedGamepad* g = GCController.controllers.firstObject.extendedGamepad;
    GCControllerButtonInput* e = g ? PadElement(g, b) : nil;
    if (e.localizedName.length) return e.localizedName;
    static NSString* const n[PB_COUNT] = {@"A", @"B", @"X", @"Y", @"L1", @"R1", @"L2", @"R2", @"Left Stick Click", @"Right Stick Click", @"Menu", @"Options", @"Home"};
    return n[b];
}
// "Match DS positions": the face buttons keep their place on a DS (A right, B bottom, X top, Y left)
static BOOL PadByPosition() { return [Prefs() integerForKey:@"padLayout"] == 1; }
static int PadDefaultAction(int b)
{
    BOOL pos = PadByPosition();
    switch (b)
    {
    case PB_A: return pos ? PA_B : PA_A;
    case PB_B: return pos ? PA_A : PA_B;
    case PB_X: return pos ? PA_Y : PA_X;
    case PB_Y: return pos ? PA_X : PA_Y;
    case PB_L1: case PB_L2: return PA_L;
    case PB_R1: case PB_R2: return PA_R;
    case PB_MENU: return PA_START;
    case PB_OPTIONS: return PA_SELECT;
    case PB_HOME: case PB_L3: return PA_MENU;
    case PB_R3: return PA_FFWD;
    }
    return PA_NONE;
}
static int PadAction(int b)
{
    NSDictionary* m = [Prefs() dictionaryForKey:@"padMap"];
    NSNumber* v = m[[NSString stringWithFormat:@"%d", b]];
    return v ? v.intValue : PadDefaultAction(b);
}
static void PadSetAction(int b, int a)
{
    NSMutableDictionary* m = [[Prefs() dictionaryForKey:@"padMap"] mutableCopy] ?: [NSMutableDictionary dictionary];
    m[[NSString stringWithFormat:@"%d", b]] = @(a);
    [Prefs() setObject:m forKey:@"padMap"];
}
static BOOL PadConnected() { for (GCController* c in GCController.controllers) if (c.extendedGamepad) return YES; return NO; }
// on-screen buttons: off in full screen, and while a controller is connected (if so set)
static BOOL ShowOnScreenButtons() { return [Prefs() boolForKey:@"buttons"] && !([Prefs() boolForKey:@"padHide"] && PadConnected()); }

// Game enhancement packs: extra improvements made for one game, on by themselves when it runs.
static NSArray<NSDictionary*>* EnhancementPacks()
{
    return @[@{
        @"code": @"CY8",
        @"title": @"Yu-Gi-Oh! 5D's World Championship 2009",
        @"short": @"Yu-Gi-Oh! 5D's WC2009",
        @"about": @"An HD makeover for the card game, made just for it.",
        @"features": @[
            @"HD art for every card, in duels and in the deck editor",
            @"High-resolution characters, backgrounds, menus and 3D world",
            @"Sharper game text, with the same look",
            @"Optional redesign: new life point panel, duel buttons and icons, card text in a clean font",
        ],
        @"needs": @"WC2009-HD",
        @"needsTitle": @"HD pack (WC2009-HD folder)",
        @"needsDetail": @"The enhancements use an HD pack: a folder named WC2009-HD with the card art, the HD pictures and the redesign (about 1 GB). It isn't part of the game file, so it has to sit right next to it.",
        @"master": @"hdMode",
        @"options": @[],
    }];
}

static NSDictionary* PackForCode(NSString* code)
{
    for (NSDictionary* p in EnhancementPacks()) if ([code hasPrefix:p[@"code"]]) return p;
    return nil;
}

// Games folders the user picked in Files (security-scoped bookmarks, kept open while the app runs)
static NSMutableArray<NSURL*>* gFolders;

static void LoadLinkedFolders()
{
    gFolders = [NSMutableArray array];
    NSMutableArray* keep = [NSMutableArray array];
    for (NSData* bm0 in [Prefs() arrayForKey:@"folders"] ?: @[])
    {
        NSData* bm = bm0;
        BOOL stale = NO;
        NSURL* u = [NSURL URLByResolvingBookmarkData:bm options:0 relativeToURL:nil bookmarkDataIsStale:&stale error:nil];
        if (!u) continue;
        [u startAccessingSecurityScopedResource];
        if (stale) { NSData* nb = [u bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:nil]; if (nb) bm = nb; }
        [keep addObject:bm];
        [gFolders addObject:u];
    }
    [Prefs() setObject:keep forKey:@"folders"];
}

static BOOL AddLinkedFolder(NSURL* u)
{
    [u startAccessingSecurityScopedResource];
    NSData* bm = [u bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:nil];
    if (!bm) return NO;
    for (NSURL* f in gFolders) if ([f.path isEqualToString:u.path]) return YES;
    NSMutableArray* all = [[Prefs() arrayForKey:@"folders"] ?: @[] mutableCopy];
    [all addObject:bm];
    [Prefs() setObject:all forKey:@"folders"];
    [gFolders addObject:u];
    return YES;
}

static void RemoveLinkedFolder(NSUInteger i)
{
    if (i >= gFolders.count) return;
    [gFolders[i] stopAccessingSecurityScopedResource];
    [gFolders removeObjectAtIndex:i];
    NSMutableArray* all = [[Prefs() arrayForKey:@"folders"] ?: @[] mutableCopy];
    if (i < all.count) [all removeObjectAtIndex:i];
    [Prefs() setObject:all forKey:@"folders"];
}

/** Presents the Files folder picker; the chosen folder joins the library. */
@interface FolderPicker : NSObject <UIDocumentPickerDelegate>
+ (void)presentFrom:(UIViewController*)vc;
@end

@implementation FolderPicker
static FolderPicker* gFolderPicker;
+ (void)presentFrom:(UIViewController*)vc
{
    gFolderPicker = [FolderPicker new];
    UIDocumentPickerViewController* p = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeFolder] asCopy:NO];
    p.delegate = gFolderPicker;
    [vc presentViewController:p animated:YES completion:nil];
}
- (void)documentPicker:(UIDocumentPickerViewController*)picker didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    if (urls.firstObject && AddLinkedFolder(urls.firstObject)) PostChange(@"folders");
    gFolderPicker = nil;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)picker { gFolderPicker = nil; }
@end

// Copies of a pack's game in the library (Documents and games folders, one level deep):
// path -> YES if the pack's folder (e.g. WC2009-HD, with its pack.json) is next to it
static NSDictionary<NSString*, NSNumber*>* PackInstalls(NSDictionary* pack)
{
    NSMutableDictionary* out = [NSMutableDictionary dictionary];
    NSFileManager* fm = NSFileManager.defaultManager;
    NSMutableArray* roots = [NSMutableArray arrayWithObject:NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject];
    for (NSURL* u in gFolders) [roots addObject:u.path];
    NSMutableArray* dirs = [NSMutableArray array];
    for (NSString* r in roots)
    {
        [dirs addObject:r];
        for (NSString* f in [fm contentsOfDirectoryAtPath:r error:nil])
        {
            BOOL isDir = NO;
            NSString* p = [r stringByAppendingPathComponent:f];
            if ([fm fileExistsAtPath:p isDirectory:&isDir] && isDir && ![@[@"Saves", @"HDCards", @"WC2009-HD", @"Covers"] containsObject:f]) [dirs addObject:p];
        }
    }
    for (NSString* d in dirs)
        for (NSString* f in [fm contentsOfDirectoryAtPath:d error:nil])
        {
            if (![f.pathExtension.lowercaseString isEqualToString:@"nds"]) continue;
            NSString* p = [d stringByAppendingPathComponent:f];
            char code[5];
            if (wc09_rom_code(p.fileSystemRepresentation, code) != 0 || ![[NSString stringWithUTF8String:code] hasPrefix:pack[@"code"]]) continue;
            out[p] = @([fm fileExistsAtPath:[[d stringByAppendingPathComponent:pack[@"needs"]] stringByAppendingPathComponent:@"pack.json"]]);
        }
    return out;
}

// SF Symbol in white on a rounded coloured square, like the Settings app
static UIImage* SettingsIcon(NSString* symbol, UIColor* color)
{
    CGFloat s = 29;
    UIGraphicsImageRenderer* r = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(s, s)];
    return [r imageWithActions:^(UIGraphicsImageRendererContext* ctx) {
        [color setFill];
        [[UIBezierPath bezierPathWithRoundedRect:CGRectMake(0, 0, s, s) cornerRadius:7] fill];
        UIImageSymbolConfiguration* cfg = [UIImageSymbolConfiguration configurationWithPointSize:14 weight:UIImageSymbolWeightSemibold];
        UIImage* im = [[UIImage systemImageNamed:symbol withConfiguration:cfg] imageWithTintColor:UIColor.whiteColor renderingMode:UIImageRenderingModeAlwaysOriginal];
        if (im) [im drawInRect:CGRectMake((s - im.size.width) / 2, (s - im.size.height) / 2, im.size.width, im.size.height)];
    }];
}

// ---- a pick-one list (resolution, filter, ...) ----

@interface ChoiceViewController : UITableViewController
- (instancetype)initWithRow:(NSDictionary*)row;
@end

@implementation ChoiceViewController
{
    NSDictionary* _row;
}

- (instancetype)initWithRow:(NSDictionary*)row
{
    if ((self = [super initWithStyle:UITableViewStyleInsetGrouped])) { _row = row; self.title = row[@"title"]; }
    return self;
}

- (NSInteger)tableView:(UITableView*)tv numberOfRowsInSection:(NSInteger)s { return [_row[@"choices"] count]; }
- (NSString*)tableView:(UITableView*)tv titleForFooterInSection:(NSInteger)s { return _row[@"footer"]; }

- (UITableViewCell*)tableView:(UITableView*)tv cellForRowAtIndexPath:(NSIndexPath*)ip
{
    NSDictionary* ch = _row[@"choices"][ip.row];
    UITableViewCell* cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    UIListContentConfiguration* c = [UIListContentConfiguration subtitleCellConfiguration];
    c.text = ch[@"title"];
    c.secondaryText = ch[@"detail"];
    c.secondaryTextProperties.color = UIColor.secondaryLabelColor;
    c.textToSecondaryTextVerticalPadding = 3;
    cell.contentConfiguration = c;
    id cur = [Prefs() objectForKey:_row[@"key"]];
    cell.accessoryType = fabs([cur doubleValue] - [ch[@"value"] doubleValue]) < 0.01 ? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
    return cell;
}

- (void)tableView:(UITableView*)tv didSelectRowAtIndexPath:(NSIndexPath*)ip
{
    [Prefs() setObject:_row[@"choices"][ip.row][@"value"] forKey:_row[@"key"]];
    PostChange(_row[@"key"]);
    [[[UISelectionFeedbackGenerator alloc] init] selectionChanged];
    [tv reloadData];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.25 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self.navigationController popViewControllerAnimated:YES];
    });
}
@end

// ---- a settings page built from sections of rows ----
// row types: switch (key), choice (key, choices), link (open block), info (value)

@interface OptionsViewController : UITableViewController <UIAdaptivePresentationControllerDelegate>
@property (nonatomic, copy) NSArray<NSDictionary*>* (^build)(void);
@property (nonatomic, copy) void (^onDone)(void);
@end

@implementation OptionsViewController
{
    NSArray<NSDictionary*>* _sections;
}

- (instancetype)init { return [super initWithStyle:UITableViewStyleInsetGrouped]; }

- (void)viewWillAppear:(BOOL)animated
{
    [super viewWillAppear:animated];
    [self refresh];
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(changed:) name:kSettingsChanged object:nil];
}

- (void)changed:(NSNotification*)n { if ([@[@"folders", @"padMap", @"pads"] containsObject:n.object]) [self refresh]; }

- (void)refresh
{
    _sections = self.build();
    [self.tableView reloadData];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView*)tv { return _sections.count; }
- (NSInteger)tableView:(UITableView*)tv numberOfRowsInSection:(NSInteger)s { return [_sections[s][@"rows"] count]; }
- (NSString*)tableView:(UITableView*)tv titleForHeaderInSection:(NSInteger)s { return _sections[s][@"title"]; }
- (NSString*)tableView:(UITableView*)tv titleForFooterInSection:(NSInteger)s { return _sections[s][@"footer"]; }

- (BOOL)rowEnabled:(NSDictionary*)row { return !row[@"requires"] || [Prefs() boolForKey:row[@"requires"]]; }

- (UITableViewCell*)tableView:(UITableView*)tv cellForRowAtIndexPath:(NSIndexPath*)ip
{
    NSDictionary* row = _sections[ip.section][@"rows"][ip.row];
    NSString* type = row[@"type"];
    UITableViewCell* cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    BOOL enabled = [self rowEnabled:row];
    UIListContentConfiguration* c;
    if ([type isEqualToString:@"bullet"])
    {
        c = [UIListContentConfiguration cellConfiguration];
        UIImageSymbolConfiguration* sc = [UIImageSymbolConfiguration configurationWithPointSize:15 weight:UIImageSymbolWeightSemibold];
        c.image = [[UIImage systemImageNamed:@"checkmark.circle.fill" withConfiguration:sc] imageWithTintColor:RGB(0x22C55E) renderingMode:UIImageRenderingModeAlwaysOriginal];
    }
    else if ([type isEqualToString:@"choice"] || [type isEqualToString:@"info"] || ([type isEqualToString:@"link"] && row[@"value"]) ||
             ([type isEqualToString:@"action"] && row[@"value"]))
    {
        c = [UIListContentConfiguration valueCellConfiguration];
        if ([type isEqualToString:@"choice"])
        {
            id cur = [Prefs() objectForKey:row[@"key"]];
            for (NSDictionary* ch in row[@"choices"])
                if (fabs([cur doubleValue] - [ch[@"value"] doubleValue]) < 0.01) c.secondaryText = ch[@"short"] ?: ch[@"title"];
        }
        else c.secondaryText = row[@"value"];
    }
    else
    {
        c = [UIListContentConfiguration subtitleCellConfiguration];
        c.secondaryText = row[@"detail"];
        c.secondaryTextProperties.color = UIColor.secondaryLabelColor;
        c.secondaryTextProperties.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
        c.textToSecondaryTextVerticalPadding = 3;
    }
    c.text = row[@"title"];
    c.textProperties.numberOfLines = 0;
    c.secondaryTextProperties.numberOfLines = 0;
    if (!enabled) c.textProperties.color = UIColor.tertiaryLabelColor;
    if (row[@"symbol"]) { c.image = SettingsIcon(row[@"symbol"], row[@"color"] ?: Accent()); c.imageToTextPadding = 14; }
    c.directionalLayoutMargins = NSDirectionalEdgeInsetsMake(11, 0, 11, 0);
    if (row[@"symbol"] && row[@"detail"])
    {
        // icon lined up with the first line of the title (not centred on the whole text)
        UIImageView* iv = [[UIImageView alloc] initWithImage:SettingsIcon(row[@"symbol"], row[@"color"] ?: Accent())];
        UILabel* tl = [UILabel new];
        tl.text = row[@"title"]; tl.numberOfLines = 0;
        tl.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
        tl.textColor = enabled ? UIColor.labelColor : UIColor.tertiaryLabelColor;
        UILabel* dl = [UILabel new];
        dl.text = row[@"detail"]; dl.numberOfLines = 0;
        dl.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
        dl.textColor = UIColor.secondaryLabelColor;
        UIStackView* txt = [[UIStackView alloc] initWithArrangedSubviews:@[tl, dl]];
        txt.axis = UILayoutConstraintAxisVertical; txt.spacing = 3;
        txt.layoutMarginsRelativeArrangement = YES;
        txt.directionalLayoutMargins = NSDirectionalEdgeInsetsMake(MAX(0.0, (29 - tl.font.lineHeight) / 2), 0, 0, 0);
        UIStackView* st = [[UIStackView alloc] initWithArrangedSubviews:@[iv, txt]];
        st.spacing = 14; st.alignment = UIStackViewAlignmentTop;
        st.translatesAutoresizingMaskIntoConstraints = NO;
        [cell.contentView addSubview:st];
        UILayoutGuide* g = cell.contentView.layoutMarginsGuide;
        [NSLayoutConstraint activateConstraints:@[
            [st.leadingAnchor constraintEqualToAnchor:g.leadingAnchor], [st.trailingAnchor constraintEqualToAnchor:g.trailingAnchor],
            [st.topAnchor constraintEqualToAnchor:cell.contentView.topAnchor constant:11],
            [st.bottomAnchor constraintEqualToAnchor:cell.contentView.bottomAnchor constant:-11],
            [iv.widthAnchor constraintEqualToConstant:29], [iv.heightAnchor constraintEqualToConstant:29]]];
    }
    else cell.contentConfiguration = c;

    if ([type isEqualToString:@"switch"])
    {
        UISwitch* sw = [UISwitch new];
        sw.on = [Prefs() boolForKey:row[@"key"]];
        sw.enabled = enabled;
        sw.onTintColor = Accent();
        sw.accessibilityIdentifier = row[@"key"];
        [sw addTarget:self action:@selector(switched:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = sw;
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
    }
    else if ([type isEqualToString:@"info"] || [type isEqualToString:@"bullet"] || [type isEqualToString:@"switchless"]) cell.selectionStyle = UITableViewCellSelectionStyleNone;
    else if ([type isEqualToString:@"action"]) { if (row[@"destructive"]) c.textProperties.color = UIColor.systemRedColor; else if (!row[@"symbol"]) c.textProperties.color = Accent(); cell.contentConfiguration = c; }
    else cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    return cell;
}

- (void)switched:(UISwitch*)sw
{
    [Prefs() setBool:sw.on forKey:sw.accessibilityIdentifier];
    PostChange(sw.accessibilityIdentifier);
    _sections = self.build();
    // rows that depend on this switch
    NSMutableArray* deps = [NSMutableArray array];
    for (NSUInteger s = 0; s < _sections.count; s++)
        for (NSUInteger r = 0; r < [_sections[s][@"rows"] count]; r++)
            if ([_sections[s][@"rows"][r][@"requires"] isEqualToString:sw.accessibilityIdentifier]) [deps addObject:[NSIndexPath indexPathForRow:r inSection:s]];
    if (deps.count) [self.tableView reloadRowsAtIndexPaths:deps withRowAnimation:UITableViewRowAnimationNone];
}

- (void)tableView:(UITableView*)tv didSelectRowAtIndexPath:(NSIndexPath*)ip
{
    NSDictionary* row = _sections[ip.section][@"rows"][ip.row];
    [tv deselectRowAtIndexPath:ip animated:YES];
    if ([row[@"type"] isEqualToString:@"choice"])
        [self.navigationController pushViewController:[[ChoiceViewController alloc] initWithRow:row] animated:YES];
    else if ([row[@"type"] isEqualToString:@"action"])
    {
        void (^act)(UIViewController*) = row[@"action"];
        act(self);
    }
    else if ([row[@"type"] isEqualToString:@"link"])
    {
        UIViewController* (^open)(void) = row[@"open"];
        [self.navigationController pushViewController:open() animated:YES];
    }
}

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
    if (self.onDone) self.navigationController.presentationController.delegate = self;   // swipe down = Done
}

- (void)presentationControllerDidDismiss:(UIPresentationController*)pc { if (self.onDone) self.onDone(); }

- (void)done
{
    void (^cb)(void) = self.onDone;
    [self dismissViewControllerAnimated:YES completion:^{ if (cb) cb(); }];
}
@end

static OptionsViewController* EnhancementsPage(NSDictionary* pack)
{
    OptionsViewController* vc = [OptionsViewController new];
    vc.title = pack[@"short"];
    vc.build = ^NSArray* {
        NSMutableArray* opts = [NSMutableArray array];
        for (NSDictionary* o in pack[@"options"])
            [opts addObject:@{@"type": @"switch", @"key": o[@"key"], @"title": o[@"title"], @"detail": o[@"detail"]}];
        NSMutableArray* feats = [NSMutableArray array];
        for (NSString* f in pack[@"features"]) [feats addObject:@{@"type": @"bullet", @"title": f}];

        // what you need + where it goes + is it there
        NSDictionary* inst = PackInstalls(pack);
        NSMutableArray* need = [NSMutableArray array];
        if (inst.count == 0)
            [need addObject:@{@"type": @"info", @"title": @"Game not in your library yet", @"symbol": @"questionmark.folder.fill", @"color": RGB(0x64748B)}];
        for (NSString* p in inst)
        {
            BOOL ok = [inst[p] boolValue];
            [need addObject:@{@"type": @"switchless", @"title": ok ? @"Ready" : @"HD pack missing",
                              @"detail": [NSString stringWithFormat:@"%@ %@ next to %@", pack[@"needs"], ok ? @"found" : @"not found", p.lastPathComponent],
                              @"symbol": ok ? @"checkmark.seal.fill" : @"exclamationmark.triangle.fill", @"color": ok ? RGB(0x22C55E) : RGB(0xF59E0B)}];
        }
        NSString* where = [NSString stringWithFormat:@"%@\n\nPut it in the same folder as the game, like this:\n\n   📁 Your games folder\n        🎮 %@.nds\n        📁 %@\n\nKeep the folder name exactly \"%@\". Without it the game runs like the original.",
                           pack[@"needsDetail"], pack[@"short"], pack[@"needs"], pack[@"needs"]];
        return @[
            @{@"title": pack[@"title"], @"rows": @[
                @{@"type": @"choice", @"key": pack[@"master"], @"title": @"Mode", @"symbol": @"sparkles", @"color": RGB(0xF2A93B),
                  @"choices": HDModeChoices()}],
              @"footer": [pack[@"about"] stringByAppendingString:@"\n\nYou can switch from the in-game menu too, even mid-game."]},
            @{@"title": @"What's improved", @"rows": feats},
            @{@"title": [@"What you need: " stringByAppendingString:pack[@"needsTitle"]], @"rows": need, @"footer": where},
        ];
    };
    return vc;
}

static NSArray* PercentChoices()
{
    NSMutableArray* a = [NSMutableArray array];
    for (NSNumber* v in @[@0.25f, @0.4f, @0.55f, @0.75f, @0.95f])
        [a addObject:@{@"title": [NSString stringWithFormat:@"%d%%", (int)lroundf(v.floatValue * 100)], @"value": v}];
    return a;
}

static NSString* DebugReportText()
{
    struct utsname u; uname(&u);
    NSUserDefaults* d = Prefs();
    NSString* head = [NSString stringWithFormat:@"device %s, iOS %@, app %@\nsettings: scale %ldx, HD mode %@, smooth 2D %@, filter %@, screen buttons %@\n",
        u.machine, UIDevice.currentDevice.systemVersion, [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"],
        (long)[d integerForKey:@"scale"], HDModeName([d integerForKey:@"hdMode"]), [d boolForKey:@"smooth2d"] ? @"on" : @"off",
        [d boolForKey:@"filter"] ? @"smooth" : @"sharp", [d boolForKey:@"buttons"] ? @"on" : @"off"];
    return [head stringByAppendingString:[NSString stringWithUTF8String:wc09_debug_report()]];
}

/** Game controller options: layout, hiding the on-screen buttons, what each button does. */
static OptionsViewController* ControllerPage()
{
    OptionsViewController* vc = [OptionsViewController new];
    vc.title = @"Game Controllers";
    vc.build = ^NSArray* {
        GCController* c = GCController.controllers.firstObject;
        NSMutableArray* buttons = [NSMutableArray array];
        for (int b = 0; b < PB_COUNT; b++)
        {
            if (b == PB_HOME && c.extendedGamepad && !c.extendedGamepad.buttonHome) continue;
            [buttons addObject:@{@"type": @"action", @"title": PadButtonName(b), @"value": PadActionName(PadAction(b)),
                @"action": [^(UIViewController* host) {
                    UIAlertController* a = [UIAlertController alertControllerWithTitle:PadButtonName(b) message:@"What this button does in the game" preferredStyle:UIAlertControllerStyleActionSheet];
                    int cur = PadAction(b);
                    for (int act = 0; act < PA_COUNT; act++)
                    {
                        NSString* t = act == cur ? [@"✓  " stringByAppendingString:PadActionName(act)] : PadActionName(act);
                        [a addAction:[UIAlertAction actionWithTitle:t style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
                            PadSetAction(b, act);
                            PostChange(@"padMap");
                        }]];
                    }
                    [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
                    a.popoverPresentationController.sourceView = host.view;
                    a.popoverPresentationController.sourceRect = CGRectMake(CGRectGetMidX(host.view.bounds), CGRectGetMidY(host.view.bounds), 1, 1);
                    [host presentViewController:a animated:YES completion:nil];
                } copy]}];
        }
        return @[
            @{@"title": @"Controller", @"rows": @[
                @{@"type": @"info", @"title": c ? (c.vendorName ?: @"Controller") : @"No controller connected",
                  @"symbol": @"gamecontroller.fill", @"color": c ? RGB(0x22C55E) : RGB(0x64748B), @"value": c ? @"Connected" : @""}],
              @"footer": @"Pair a controller in the Settings app under Bluetooth. It works as soon as it's connected."},
            @{@"title": @"Layout", @"rows": @[
                @{@"type": @"choice", @"key": @"padLayout", @"title": @"Face Buttons", @"symbol": @"circle.grid.cross", @"color": RGB(0x8B5CF6),
                  @"choices": @[
                      @{@"title": @"Match Letters", @"detail": @"A is A, B is B. Best for Switch-style controllers", @"value": @0},
                      @{@"title": @"Match DS Positions", @"detail": @"The right button is A, the bottom one is B, as on a DS. Best for Xbox and PlayStation controllers", @"value": @1}]},
                @{@"type": @"switch", @"key": @"padHide", @"title": @"Hide On-Screen Buttons", @"symbol": @"eye.slash", @"color": RGB(0xF97316),
                  @"detail": @"While a controller is connected, the game gets the whole screen. They come back when it disconnects."}]},
            @{@"title": @"Buttons", @"rows": buttons,
              @"footer": @"Tap a button to change what it does. By default the Home button and the left stick click open the Dual HD menu, and holding the right stick click fast-forwards. In the menu, use the D-pad, A to choose and B to go back."},
            @{@"rows": @[@{@"type": @"action", @"title": @"Reset to Defaults", @"destructive": @YES, @"action": [^(UIViewController*) {
                [Prefs() removeObjectForKey:@"padMap"];
                PostChange(@"padMap");
            } copy]}]},
        ];
    };
    return vc;
}

/** The settings, in a navigation controller ready to present. */
static UINavigationController* SettingsNavigation(void (^onDone)(void))
{
    OptionsViewController* vc = [OptionsViewController new];
    vc.title = @"Settings";
    vc.onDone = onDone;
    vc.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:vc action:@selector(done)];
    vc.build = ^NSArray* {
        NSMutableArray* packs = [NSMutableArray array];
        for (NSDictionary* p in EnhancementPacks())
            [packs addObject:@{@"type": @"link", @"title": p[@"short"], @"symbol": @"sparkles", @"color": RGB(0xF2A93B),
                               @"value": HDModeName([Prefs() integerForKey:p[@"master"]]),
                               @"open": [^UIViewController* { return EnhancementsPage(p); } copy]}];
        NSMutableArray* lib = [NSMutableArray array];
        for (NSUInteger i = 0; i < gFolders.count; i++)
        {
            NSURL* u = gFolders[i];
            [lib addObject:@{@"type": @"action", @"title": u.lastPathComponent, @"symbol": @"folder.fill", @"color": RGB(0x3B82F6),
                             @"action": [^(UIViewController* vc) {
                UIAlertController* a = [UIAlertController alertControllerWithTitle:u.lastPathComponent message:@"Stop showing the games in this folder? Nothing is deleted."
                                                                    preferredStyle:UIAlertControllerStyleActionSheet];
                [a addAction:[UIAlertAction actionWithTitle:@"Remove from Library" style:UIAlertActionStyleDestructive handler:^(UIAlertAction*) {
                    NSUInteger idx = [gFolders indexOfObject:u];
                    if (idx != NSNotFound) RemoveLinkedFolder(idx);
                    PostChange(@"folders");
                }]];
                [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
                a.popoverPresentationController.sourceView = vc.view;
                a.popoverPresentationController.sourceRect = CGRectMake(vc.view.bounds.size.width / 2, vc.view.bounds.size.height / 2, 1, 1);
                [vc presentViewController:a animated:YES completion:nil];
            } copy]}];
        }
        [lib addObject:@{@"type": @"action", @"title": @"Add Games Folder…", @"action": [^(UIViewController* vc) { [FolderPicker presentFrom:vc]; } copy]}];
        [lib addObject:@{@"type": @"switch", @"key": @"boxart", @"title": @"Box Art & Game Names", @"symbol": @"photo.on.rectangle.angled", @"color": RGB(0xA855F7),
                         @"detail": @"Real box art and full game names, downloaded once from GameTDB. Your own covers and names always come first."}];
        return @[
            @{@"title": @"Library", @"rows": lib,
              @"footer": @"Pick a folder in Files (On My iPhone, iCloud Drive, a USB drive…) and every game in it shows up in your Library, with its saves kept next to it. Games you import go into Dual HD's own folder."},
            @{@"title": @"Display", @"rows": @[
                @{@"type": @"choice", @"key": @"scale", @"title": @"Resolution", @"symbol": @"tv", @"color": RGB(0x3B82F6),
                  @"footer": @"Higher is sharper. Each step renders the DS screens at that many times their size; 1x is the original DS picture.",
                  @"choices": @[
                      @{@"title": @"1x (Off)", @"short": @"1x", @"detail": @"The DS's own resolution, no upscaling. Lightest of all", @"value": @1},
                      @{@"title": @"2x", @"detail": @"Fast, long battery life", @"value": @2},
                      @{@"title": @"3x", @"detail": @"Sharp on smaller screens", @"value": @3},
                      @{@"title": @"4x", @"detail": @"Recommended. Crisp on every iPhone", @"value": @4},
                      @{@"title": @"5x", @"detail": @"Extra sharp", @"value": @5},
                      @{@"title": @"6x", @"detail": @"Very sharp. Best on recent iPhones", @"value": @6},
                      @{@"title": @"8x", @"detail": @"Sharpest. Uses the most battery", @"value": @8}]},
                @{@"type": @"choice", @"key": @"filter", @"title": @"Screen Filter", @"symbol": @"camera.filters", @"color": RGB(0x8B5CF6),
                  @"choices": @[
                      @{@"title": @"Smooth", @"detail": @"Blends the picture to fit your screen", @"value": @YES},
                      @{@"title": @"Sharp", @"detail": @"Keeps hard pixel edges", @"value": @NO}]},
                @{@"type": @"switch", @"key": @"smooth2d", @"title": @"Smooth 2D Graphics", @"symbol": @"wand.and.stars", @"color": RGB(0x14B8A6),
                  @"detail": @"Upscales menus and sprites with smooth edges instead of blocky pixels."},
                @{@"type": @"switch", @"key": @"perf", @"title": @"Show Frame Rate", @"symbol": @"speedometer", @"color": RGB(0x64748B),
                  @"detail": @"A small number in a corner of the game screen. 60 is full speed."},
                @{@"type": @"switch", @"key": @"perfDetail", @"title": @"Detailed Performance", @"symbol": @"chart.bar.xaxis", @"color": RGB(0x64748B),
                  @"detail": @"Shows frame timing and graphics stats next to the frame rate."}]},
            @{@"title": @"Controls", @"rows": @[
                @{@"type": @"switch", @"key": @"buttons", @"title": @"On-Screen Buttons", @"symbol": @"gamecontroller.fill", @"color": RGB(0xF97316),
                  @"detail": @"Turn off for full screen: just the DS screens and a small menu button."},
                @{@"type": @"choice", @"key": @"opacity", @"title": @"Button Opacity", @"symbol": @"circle.lefthalf.filled", @"color": RGB(0x64748B),
                  @"choices": PercentChoices()},
                @{@"type": @"switch", @"key": @"haptics", @"title": @"Vibration", @"symbol": @"hand.tap.fill", @"color": RGB(0x6366F1),
                  @"detail": @"A light tap when you press a button."},
                @{@"type": @"link", @"title": @"Game Controllers", @"symbol": @"gamecontroller", @"color": RGB(0x0EA5E9),
                  @"value": PadConnected() ? (GCController.controllers.firstObject.vendorName ?: @"Connected") : @"",
                  @"open": [^UIViewController* { return ControllerPage(); } copy]}],
              @"footer": @"Xbox, PlayStation, Switch Pro and MFi controllers work: pair them in the Settings app under Bluetooth."},
            @{@"title": @"Gameplay", @"rows": @[
                @{@"type": @"switch", @"key": @"resume", @"title": @"Resume Where You Left Off", @"symbol": @"play.circle.fill", @"color": RGB(0x22C55E),
                  @"detail": @"Leaving a game saves a resume point; opening it again continues from there."},
                @{@"type": @"switch", @"key": @"sound", @"title": @"Sound", @"symbol": @"speaker.wave.2.fill", @"color": RGB(0xEF4444)},
                @{@"type": @"choice", @"key": @"ffSpeed", @"title": @"Fast Forward Speed", @"symbol": @"forward.fill", @"color": RGB(0xEAB308),
                  @"choices": @[@{@"title": @"2x", @"value": @2}, @{@"title": @"3x", @"value": @3}, @{@"title": @"4x", @"value": @4}]}],
              @"footer": @"Saves and save states are kept in a Saves folder next to your games, in the same format as melonDS on a computer."},
            @{@"title": @"Game Enhancements", @"rows": packs,
              @"footer": @"Extra improvements made for specific games. They switch on by themselves when you play that game; tap one to see what it improves."},
            @{@"title": @"Help", @"rows": @[
                @{@"type": @"action", @"title": @"Copy Debug Report", @"symbol": @"doc.on.clipboard", @"color": RGB(0x64748B),
                  @"action": [^(UIViewController* vc) {
                      UIPasteboard.generalPasteboard.string = DebugReportText();
                      UIAlertController* a = [UIAlertController alertControllerWithTitle:@"Debug Report Copied"
                          message:@"It covers the last 15 minutes of play, with the slow moments in detail. Paste it wherever you report the problem." preferredStyle:UIAlertControllerStyleAlert];
                      [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
                      [vc presentViewController:a animated:YES completion:nil];
                  } copy]}],
              @"footer": @"If a game slows down, keep playing for a bit, then copy the report from here (the in-game menu has Settings too)."},
            @{@"title": @"About", @"rows": @[
                @{@"type": @"info", @"title": @"Dual HD", @"symbol": @"rectangle.split.1x2.fill", @"color": RGB(0x0EA5E9),
                  @"value": [NSString stringWithFormat:@"Version %@", [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"]]}],
              @"footer": @"A DS emulator built on melonDS."},
        ];
    };
    UINavigationController* nav = [[UINavigationController alloc] initWithRootViewController:vc];
    nav.navigationBar.prefersLargeTitles = YES;
    return nav;
}

// ------------------------------------------------------------------------------------------------
// A frosted menu card over the game

@interface MenuPanel : UIView
- (instancetype)initWithTitle:(NSString*)title message:(NSString*)msg items:(NSArray<NSDictionary*>*)items onCancel:(void (^)(void))cancel;
- (void)show;
// game controller navigation
- (void)moveSelection:(int)step;
- (void)activateSelection;
- (void)cancel;
@end

@implementation MenuPanel
{
    UIVisualEffectView* _card;
    void (^_cancel)(void);
    NSMutableArray<UIButton*>* _buttons;
    NSInteger _sel;
}

- (instancetype)initWithTitle:(NSString*)title message:(NSString*)msg items:(NSArray<NSDictionary*>*)items onCancel:(void (^)(void))cancel
{
    if (!(self = [super initWithFrame:CGRectZero])) return nil;
    _cancel = [cancel copy];
    self.backgroundColor = [UIColor colorWithWhite:0 alpha:0.45];
    UITapGestureRecognizer* tap = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(backgroundTap:)];
    [self addGestureRecognizer:tap];

    _card = [[UIVisualEffectView alloc] initWithEffect:[UIBlurEffect effectWithStyle:UIBlurEffectStyleSystemThickMaterialDark]];
    _card.layer.cornerRadius = 24; _card.layer.cornerCurve = kCACornerCurveContinuous; _card.clipsToBounds = YES;
    _card.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_card];

    UIStackView* st = [UIStackView new];
    st.axis = UILayoutConstraintAxisVertical;
    st.translatesAutoresizingMaskIntoConstraints = NO;
    UILabel* t = [UILabel new];
    t.text = title; t.font = [UIFont systemFontOfSize:17 weight:UIFontWeightBold]; t.textColor = UIColor.whiteColor;
    t.textAlignment = NSTextAlignmentCenter; t.numberOfLines = 2;
    [st addArrangedSubview:t];
    [st setCustomSpacing:msg ? 6 : 14 afterView:t];
    if (msg)
    {
        UILabel* m = [UILabel new];
        m.text = msg; m.font = [UIFont systemFontOfSize:14]; m.textColor = UIColor.secondaryLabelColor;
        m.textAlignment = NSTextAlignmentCenter; m.numberOfLines = 0;
        [st addArrangedSubview:m];
        [st setCustomSpacing:16 afterView:m];
    }
    NSMutableArray* all = [items mutableCopy];
    [all addObject:@{@"title": @"Cancel", @"symbol": @"", @"cancel": @YES}];
    _buttons = [NSMutableArray array];
    _sel = -1;
    for (NSDictionary* it in all)
    {
        BOOL isCancel = [it[@"cancel"] boolValue], destructive = [it[@"destructive"] boolValue];
        UIButtonConfiguration* c = isCancel ? [UIButtonConfiguration grayButtonConfiguration] : [UIButtonConfiguration plainButtonConfiguration];
        c.title = it[@"title"];
        if ([it[@"symbol"] length]) c.image = [UIImage systemImageNamed:it[@"symbol"] withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:16 weight:UIImageSymbolWeightMedium]];
        c.imagePadding = 14;
        c.baseForegroundColor = destructive ? UIColor.systemRedColor : UIColor.whiteColor;
        c.contentInsets = NSDirectionalEdgeInsetsMake(13, 16, 13, 16);
        c.cornerStyle = UIButtonConfigurationCornerStyleLarge;
        c.titleTextAttributesTransformer = ^NSDictionary*(NSDictionary* a) {
            NSMutableDictionary* m = [a mutableCopy];
            m[NSFontAttributeName] = [UIFont systemFontOfSize:16 weight:isCancel ? UIFontWeightSemibold : UIFontWeightRegular];
            return m;
        };
        __weak MenuPanel* me = self;
        void (^handler)(void) = it[@"handler"];
        UIButton* b = [UIButton buttonWithConfiguration:c primaryAction:[UIAction actionWithHandler:^(UIAction*) {
            [me hideThen:(isCancel ? (void (^)(void))nil : handler) cancelled:isCancel];
        }]];
        b.contentHorizontalAlignment = isCancel ? UIControlContentHorizontalAlignmentCenter : UIControlContentHorizontalAlignmentLeading;
        if (isCancel) [st setCustomSpacing:10 afterView:st.arrangedSubviews.lastObject];
        b.layer.cornerRadius = 12;
        [st addArrangedSubview:b];
        [_buttons addObject:b];
    }

    UIScrollView* sv = [UIScrollView new];
    sv.translatesAutoresizingMaskIntoConstraints = NO;
    sv.showsVerticalScrollIndicator = NO;
    [sv addSubview:st];
    [_card.contentView addSubview:sv];
    NSLayoutConstraint* fitH = [sv.heightAnchor constraintEqualToAnchor:st.heightAnchor];
    fitH.priority = UILayoutPriorityDefaultHigh;
    NSLayoutConstraint* w = [_card.widthAnchor constraintEqualToConstant:360];
    w.priority = UILayoutPriorityDefaultHigh;
    [NSLayoutConstraint activateConstraints:@[
        [_card.centerXAnchor constraintEqualToAnchor:self.centerXAnchor], [_card.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        w, [_card.widthAnchor constraintLessThanOrEqualToAnchor:self.widthAnchor constant:-40],
        [_card.heightAnchor constraintLessThanOrEqualToAnchor:self.heightAnchor constant:-40],
        [sv.topAnchor constraintEqualToAnchor:_card.contentView.topAnchor constant:18],
        [sv.bottomAnchor constraintEqualToAnchor:_card.contentView.bottomAnchor constant:-12],
        [sv.leadingAnchor constraintEqualToAnchor:_card.contentView.leadingAnchor constant:10],
        [sv.trailingAnchor constraintEqualToAnchor:_card.contentView.trailingAnchor constant:-10],
        fitH,
        [st.topAnchor constraintEqualToAnchor:sv.contentLayoutGuide.topAnchor], [st.bottomAnchor constraintEqualToAnchor:sv.contentLayoutGuide.bottomAnchor],
        [st.leadingAnchor constraintEqualToAnchor:sv.contentLayoutGuide.leadingAnchor], [st.trailingAnchor constraintEqualToAnchor:sv.contentLayoutGuide.trailingAnchor],
        [st.widthAnchor constraintEqualToAnchor:sv.frameLayoutGuide.widthAnchor]]];
    return self;
}

- (void)show
{
    self.alpha = 0;
    _card.transform = CGAffineTransformMakeScale(0.94, 0.94);
    [UIView animateWithDuration:0.28 delay:0 usingSpringWithDamping:0.85 initialSpringVelocity:0 options:0 animations:^{
        self.alpha = 1;
        self->_card.transform = CGAffineTransformIdentity;
    } completion:nil];
}

- (void)hideThen:(void (^)(void))handler cancelled:(BOOL)cancelled
{
    self.userInteractionEnabled = NO;
    void (^cancel)(void) = _cancel;
    [UIView animateWithDuration:0.16 animations:^{ self.alpha = 0; } completion:^(BOOL) {
        [self removeFromSuperview];
        if (cancelled) { if (cancel) cancel(); }
        else if (handler) handler();
    }];
}

- (void)moveSelection:(int)step
{
    if (!_buttons.count) return;
    _sel = _sel < 0 ? 0 : (_sel + step + (NSInteger)_buttons.count) % (NSInteger)_buttons.count;
    for (NSUInteger i = 0; i < _buttons.count; i++)
        _buttons[i].backgroundColor = (NSInteger)i == _sel ? [UIColor colorWithWhite:1 alpha:0.16] : UIColor.clearColor;
    UIButton* b = _buttons[_sel];
    UIScrollView* sv = (UIScrollView*)b.superview.superview;
    if ([sv isKindOfClass:UIScrollView.class]) [sv scrollRectToVisible:[b convertRect:b.bounds toView:sv] animated:YES];
}

- (void)activateSelection
{
    if (_sel < 0) { [self moveSelection:0]; return; }   // first press: show where the selection is
    [_buttons[_sel] sendActionsForControlEvents:UIControlEventPrimaryActionTriggered];
}

- (void)cancel { if (self.userInteractionEnabled) [self hideThen:nil cancelled:YES]; }

- (void)backgroundTap:(UITapGestureRecognizer*)g
{
    if (!CGRectContainsPoint(_card.frame, [g locationInView:self])) [self hideThen:nil cancelled:YES];
}
@end

// ------------------------------------------------------------------------------------------------
// The game screen

@interface GameViewController : GLKViewController <ControlsDelegate>
- (instancetype)initWithRom:(NSString*)rom fresh:(BOOL)fresh;
@property (nonatomic, copy) void (^onClose)(void);
@property (nonatomic, copy) NSString* gameTitle;
@end

@implementation GameViewController
{
    EAGLContext* _ctx;
    ControlsView* _controls;
    Layout _layout;
    NSString* _rom;
    BOOL _fresh, _started, _paused, _ffwd, _flipped;
    UILabel* _perf;
    CFTimeInterval _perfShown, _drawSum, _hostShown, _hostSum;
    int _hostCount;
    int _drawCount;
    CFTimeInterval _base;
    long _framesRun;
    AVAudioEngine* _engine;
    AVAudioSourceNode* _source;
    unsigned _padPrev;               // controller buttons held last frame (bit per PB_)
    BOOL _padHush;                   // a menu just closed: ignore the controller until everything is let go
    __weak MenuPanel* _panel;        // the in-game menu on screen, if any (controller navigation)
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
    _controls.showButtons = ShowOnScreenButtons();
    _controls.opacity = [Prefs() floatForKey:@"opacity"];
    _controls.haptics = [Prefs() boolForKey:@"haptics"];
    [self.view addSubview:_controls];
    _perf = [[UILabel alloc] initWithFrame:CGRectMake(8, 8, 600, 16)];
    _perf.font = [UIFont monospacedDigitSystemFontOfSize:11 weight:UIFontWeightSemibold];
    _perf.textColor = UIColor.whiteColor;
    _perf.textAlignment = NSTextAlignmentCenter;
    _perf.backgroundColor = [UIColor colorWithWhite:0 alpha:0.45];
    _perf.layer.cornerRadius = 5;
    _perf.layer.masksToBounds = YES;
    _perf.hidden = ![Prefs() boolForKey:@"perf"];
    _perf.userInteractionEnabled = NO;
    [self.view addSubview:_perf];

    NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
    [nc addObserver:self selector:@selector(willResign) name:UISceneWillDeactivateNotification object:nil];
    [nc addObserver:self selector:@selector(didBecomeActive) name:UISceneDidActivateNotification object:nil];
    [nc addObserver:self selector:@selector(settingChanged:) name:kSettingsChanged object:nil];
    // iOS doesn't rotate iPhone apps upside down: the game turns itself when the phone is flipped
    [UIDevice.currentDevice beginGeneratingDeviceOrientationNotifications];
    [nc addObserver:self selector:@selector(deviceRotated) name:UIDeviceOrientationDidChangeNotification object:nil];
    gMuted = ![Prefs() boolForKey:@"sound"];
    [self setupAudio];
    [self start];
}

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
    UIApplication.sharedApplication.idleTimerDisabled = YES;   // no dimming or locking while playing
}

- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
    UIApplication.sharedApplication.idleTimerDisabled = NO;
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskAll; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    if (self.view.window.windowScene.interfaceOrientation != UIInterfaceOrientationPortrait && _flipped) [self setFlipped:NO];
    UIScreen* screen = self.view.window.windowScene.screen;
    if (screen && self.view.contentScaleFactor != screen.nativeScale) self.view.contentScaleFactor = screen.nativeScale;
    UIEdgeInsets safe = self.view.safeAreaInsets;
    if (_flipped)   // the insets belong to the other edge now: keep clear of both
    {
        CGFloat v = MAX(safe.top, safe.bottom), h = MAX(safe.left, safe.right);
        safe = UIEdgeInsetsMake(v, h, v, h);
    }
    BOOL buttons = ShowOnScreenButtons();
    if (!buttons)
    {
        // full screen really is the whole display: only the menu button keeps out of the system's areas
        _layout.compute(self.view.bounds.size, UIEdgeInsetsZero, false);
        CGFloat mb = 34, pad = 14, W = self.view.bounds.size.width;   // a little more room: the display's corners are rounded
        _layout.menu = CGRectMake(W - safe.right - pad - mb, safe.top + pad, mb, mb);
    }
    else _layout.compute(self.view.bounds.size, safe, true);
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
    {
        int soft = 0;
        const char* rep = wc09_renderer_report(&soft);
        NSString* report = [NSString stringWithUTF8String:rep ? rep : ""];
        NSLog(@"Dual HD renderer report:\n%@", report);
        if (soft)
            dispatch_async(dispatch_get_main_queue(), ^{
                UIAlertController* a = [UIAlertController alertControllerWithTitle:@"HD graphics couldn't start"
                    message:@"The game runs in basic mode for now. Tap Copy Report and paste it in the chat so it can be fixed." preferredStyle:UIAlertControllerStyleAlert];
                [a addAction:[UIAlertAction actionWithTitle:@"Copy Report" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { UIPasteboard.generalPasteboard.string = report; }]];
                [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleCancel handler:nil]];
                [self presentViewController:a animated:YES completion:nil];
            });
    }
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

- (void)deviceRotated
{
    UIDeviceOrientation o = UIDevice.currentDevice.orientation;
    if (self.view.window.windowScene.interfaceOrientation != UIInterfaceOrientationPortrait) return;
    if (o == UIDeviceOrientationPortraitUpsideDown) [self setFlipped:YES];
    else if (o == UIDeviceOrientationPortrait) [self setFlipped:NO];
}

- (void)setFlipped:(BOOL)f
{
    if (f == _flipped) return;
    _flipped = f;
    [self.view setNeedsLayout];
    [UIView animateWithDuration:0.35 delay:0 options:UIViewAnimationOptionCurveEaseInOut animations:^{
        self.view.transform = f ? CGAffineTransformMakeRotation(M_PI) : CGAffineTransformIdentity;
        [self.view layoutIfNeeded];
    } completion:nil];
}

- (void)settingChanged:(NSNotification*)n
{
    NSString* key = n.object;
    if ([key isEqualToString:@"scale"]) { if (_started) { [EAGLContext setCurrentContext:_ctx]; wc09_set_scale(PrefScale()); } }
    else if ([@[@"filter", @"smooth2d", @"hdCards", @"hdText", @"hdAll", @"hdMode"] containsObject:key]) ApplyCoreOptions();
    else if ([@[@"buttons", @"padHide", @"pads"] containsObject:key]) { _controls.showButtons = ShowOnScreenButtons(); [self.view setNeedsLayout]; }
    else if ([key isEqualToString:@"opacity"]) { _controls.opacity = [Prefs() floatForKey:@"opacity"]; [_controls setNeedsDisplay]; }
    else if ([key isEqualToString:@"haptics"]) _controls.haptics = [Prefs() boolForKey:@"haptics"];
    else if ([key isEqualToString:@"sound"]) gMuted = ![Prefs() boolForKey:@"sound"];
    else if ([key isEqualToString:@"perf"]) _perf.hidden = ![Prefs() boolForKey:@"perf"];
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
    CFTimeInterval t0 = CACurrentMediaTime();
    [self drawScreens:view];
    _drawSum += CACurrentMediaTime() - t0; _drawCount++;
    _hostSum += CACurrentMediaTime() - t0; _hostCount++;
    if (t0 - _hostShown > 1.0)
    {
        _hostShown = t0;
        NSProcessInfo* pi = NSProcessInfo.processInfo;
        wc09_set_host_info((int)pi.thermalState, pi.isLowPowerModeEnabled ? 1 : 0, _hostCount ? (float)(_hostSum / _hostCount * 1000) : 0, (int)self.framesPerSecond);
        _hostSum = 0; _hostCount = 0;
    }
    if (!_perf.hidden && t0 - _perfShown > 0.5)
    {
        _perfShown = t0;
        if ([Prefs() boolForKey:@"perfDetail"])
            _perf.text = [NSString stringWithFormat:@" %s  ·  draw %.1f ms ", wc09_perf_text(), _drawCount ? _drawSum / _drawCount * 1000 : 0];
        else
            _perf.text = [NSString stringWithFormat:@"%d", wc09_fps()];
        [_perf sizeToFit];
        CGRect f = _perf.frame;
        f.size.width = MAX(f.size.width + 10, 28); f.size.height = MAX(f.size.height + 4, 18);
        f.origin = CGPointMake(self.view.safeAreaInsets.left + 14, self.view.safeAreaInsets.top + 12);
        _perf.frame = f;
        _drawSum = 0; _drawCount = 0;
    }
}

- (void)drawScreens:(GLKView*)view
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
    GCExtendedGamepad* g = nil;
    for (GCController* c in GCController.controllers) if ((g = c.extendedGamepad)) break;
    if (!g) { if (_padPrev) { _padPrev = 0; gInput.setPadKeys(0); } return; }
    unsigned held = 0;
    for (int b = 0; b < PB_COUNT; b++) { GCControllerButtonInput* e = PadElement(g, b); if (e.pressed) held |= 1u << b; }
    float x = g.dpad.xAxis.value + g.leftThumbstick.xAxis.value, y = g.dpad.yAxis.value + g.leftThumbstick.yAxis.value;
    unsigned dirs = 0;
    if (x < -0.5f) dirs |= WC09_KEY_LEFT;
    if (x > 0.5f) dirs |= WC09_KEY_RIGHT;
    if (y > 0.5f) dirs |= WC09_KEY_UP;
    if (y < -0.5f) dirs |= WC09_KEY_DOWN;
    unsigned pressed = held & ~_padPrev;
    static unsigned prevDirs = 0;
    unsigned newDirs = dirs & ~prevDirs;
    prevDirs = dirs;
    _padPrev = held;

    // the in-game menu is open: the controller drives it
    if (_panel)
    {
        _padHush = YES;
        gInput.setPadKeys(0);
        if (newDirs & WC09_KEY_UP) [_panel moveSelection:-1];
        if (newDirs & WC09_KEY_DOWN) [_panel moveSelection:1];
        for (int b = 0; b < PB_COUNT; b++)
        {
            if (!(pressed & (1u << b))) continue;
            int a = PadAction(b);
            if (a == PA_A || (b == PB_A && !PadByPosition()) || (b == PB_B && PadByPosition())) { [_panel activateSelection]; break; }
            if (a == PA_B || a == PA_MENU) { [_panel cancel]; break; }
        }
        return;
    }
    if (_padHush)
    {
        if (held || dirs) { gInput.setPadKeys(0); return; }
        _padHush = NO;
    }
    unsigned k = dirs;
    BOOL ffwd = NO;
    for (int b = 0; b < PB_COUNT; b++)
    {
        if (!(held & (1u << b))) continue;
        switch (PadAction(b))
        {
        case PA_A: k |= WC09_KEY_A; break; case PA_B: k |= WC09_KEY_B; break;
        case PA_X: k |= WC09_KEY_X; break; case PA_Y: k |= WC09_KEY_Y; break;
        case PA_L: k |= WC09_KEY_L; break; case PA_R: k |= WC09_KEY_R; break;
        case PA_START: k |= WC09_KEY_START; break; case PA_SELECT: k |= WC09_KEY_SELECT; break;
        case PA_FFWD: ffwd = YES; break;
        case PA_MENU: if (pressed & (1u << b)) { gInput.setPadKeys(0); [self controlsMenu]; return; } break;
        }
    }
    static BOOL padFfwd = NO;
    if (ffwd != padFfwd) { padFfwd = ffwd; [self controlsFastForward:ffwd]; }
    gInput.setPadKeys(k);
}

// ---- menu ----

- (void)controlsFastForward:(BOOL)on { _ffwd = on; _base = 0; }

- (void)resume { _paused = NO; _base = 0; }

// The in-game menus are drawn by the app (not system alerts), so they turn with the game when
// the phone is held upside down.
- (void)panel:(NSString*)title message:(NSString*)msg items:(NSArray<NSDictionary*>*)items
{
    __weak GameViewController* me = self;
    MenuPanel* p = [[MenuPanel alloc] initWithTitle:title message:msg items:items onCancel:^{ [me resume]; }];
    _panel = p;
    p.frame = self.view.bounds;
    p.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:p];
    [p show];
}

static NSDictionary* Item(NSString* title, NSString* symbol, BOOL destructive, void (^handler)(void))
{
    return @{@"title": title, @"symbol": symbol ?: @"", @"destructive": @(destructive), @"handler": [handler copy]};
}

- (void)controlsMenu
{
    if (!_started) return;
    _paused = YES;
    __weak GameViewController* me = self;
    BOOL full = ![Prefs() boolForKey:@"buttons"];
    NSMutableArray* items = [NSMutableArray array];
    [items addObject:Item(@"Save State", @"square.and.arrow.down", NO, ^{ [me stateMenu:YES]; })];
    [items addObject:Item(@"Load State", @"clock.arrow.circlepath", NO, ^{ [me stateMenu:NO]; })];
    [items addObject:Item(full ? @"Exit Full Screen" : @"Full Screen", full ? @"arrow.down.right.and.arrow.up.left" : @"arrow.up.left.and.arrow.down.right", NO, ^{
        [Prefs() setBool:full forKey:@"buttons"];
        PostChange(@"buttons");
        [me resume];
    })];
    if (wc09_is_hd_game())
    {
        NSString* cur = HDModeName([Prefs() integerForKey:@"hdMode"]);
        [items addObject:Item([@"Look: " stringByAppendingString:cur], @"sparkles", NO, ^{ [me hdModeMenu]; })];
    }
    [items addObject:Item(@"Settings", @"gearshape", NO, ^{ [me openSettings]; })];
    [items addObject:Item(@"Restart from In-Game Save", @"arrow.counterclockwise", NO, ^{ [me restartAsk]; })];
    [items addObject:Item(@"Back to Library", @"square.grid.2x2", YES, ^{ [me close]; })];
    [self panel:(self.gameTitle ?: _rom.lastPathComponent.stringByDeletingPathExtension) message:nil items:items];
}

- (void)openSettings
{
    __weak GameViewController* me = self;
    [self presentViewController:SettingsNavigation(^{ [me resume]; }) animated:YES completion:nil];
}

- (void)hdModeMenu
{
    __weak GameViewController* me = self;
    NSInteger cur = [Prefs() integerForKey:@"hdMode"];
    NSMutableArray* items = [NSMutableArray array];
    for (NSDictionary* ch in HDModeChoices())
    {
        NSInteger m = [ch[@"value"] integerValue];
        [items addObject:Item(ch[@"title"], m == cur ? @"checkmark.circle.fill" : @"circle", NO, ^{
            [Prefs() setInteger:m forKey:@"hdMode"];
            ApplyCoreOptions();
            [me toast:HDModeName(m)];
            [me resume];
        })];
    }
    [self panel:@"Look" message:@"HD keeps the original look. HD + Redesign changes the duel screen, menus and card text." items:items];
}

- (void)stateMenu:(BOOL)save
{
    __weak GameViewController* me = self;
    NSString* base = [[_rom.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"Saves"] stringByAppendingPathComponent:_rom.lastPathComponent.stringByDeletingPathExtension];
    NSDateFormatter* df = [NSDateFormatter new];
    df.doesRelativeDateFormatting = YES;
    df.dateStyle = NSDateFormatterShortStyle; df.timeStyle = NSDateFormatterShortStyle;
    NSMutableArray* items = [NSMutableArray array];
    for (int i = 1; i <= 8; i++)
    {
        NSDictionary* at = [NSFileManager.defaultManager attributesOfItemAtPath:[base stringByAppendingFormat:@".ml%d", i] error:nil];
        NSString* when = at ? [df stringFromDate:at.fileModificationDate] : @"Empty";
        [items addObject:Item([NSString stringWithFormat:@"Slot %d  ·  %@", i, when], at ? @"square.stack.fill" : @"square.dashed", NO, ^{
            GameViewController* s = me;
            if (!s) return;
            [EAGLContext setCurrentContext:s->_ctx];
            int r = save ? wc09_save_state(i) : wc09_load_state(i);
            [s toast:r == 0 ? [NSString stringWithFormat:(save ? @"Saved to slot %d" : @"Loaded slot %d"), i]
                            : (save ? @"Couldn't save" : [NSString stringWithFormat:@"Slot %d is empty", i])];
            [s resume];
        })];
    }
    [self panel:(save ? @"Save State" : @"Load State") message:nil items:items];
}

- (void)restartAsk
{
    __weak GameViewController* me = self;
    [self panel:@"Restart the game?"
        message:@"Starts again from your last in-game save. Anything after it is lost; save states are kept."
          items:@[Item(@"Restart", @"arrow.counterclockwise", YES, ^{
              GameViewController* g = me;
              if (!g) return;
              [EAGLContext setCurrentContext:g->_ctx];
              wc09_delete_state(kAutoSlot);
              wc09_load_rom(g->_rom.fileSystemRepresentation, PrefScale(), 0);
              [g resume];
          })]];
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
// The library: every .nds in Documents (and one folder deep). Favourites first, then the rest,
// most recently played first. Per-game extras (favourite, custom name) live in NSUserDefaults
// under "meta"; custom covers are pictures in Documents/Covers.

static NSString* DocumentsDir() { return NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject; }
static NSString* CoversDir() { return [DocumentsDir() stringByAppendingPathComponent:@"Covers"]; }

static NSDictionary* Meta(NSString* file) { return [Prefs() dictionaryForKey:@"meta"][file] ?: @{}; }
static void SetMeta(NSString* file, NSString* key, id value)
{
    NSMutableDictionary* all = [[Prefs() dictionaryForKey:@"meta"] ?: @{} mutableCopy];
    NSMutableDictionary* m = [all[file] ?: @{} mutableCopy];
    if (value) m[key] = value; else [m removeObjectForKey:key];
    all[file] = m;
    [Prefs() setObject:all forKey:@"meta"];
}

@interface GameEntry : NSObject
@property (nonatomic, copy) NSString *path, *file, *title, *publisher, *base, *code;
@property (nonatomic, strong) UIImage *icon, *cover;
@property (nonatomic, strong) UIColor* tint;
@property (nonatomic) BOOL resume, favorite, enhanced;
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

static NSArray<NSString*>* CoverCandidates(NSString* dir, NSString* base)
{
    NSMutableArray* a = [NSMutableArray array];
    for (NSString* d in @[CoversDir(), dir, [dir stringByAppendingPathComponent:@"Covers"]])
        for (NSString* ext in @[@"jpg", @"jpeg", @"png", @"heic"])
            [a addObject:[[d stringByAppendingPathComponent:base] stringByAppendingPathExtension:ext]];
    return a;
}

// ---- box art: downloaded once from GameTDB by the game's 4-letter code, kept in Application Support

static NSString* BoxArtDir()
{
    NSString* d = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"boxart"];
    [NSFileManager.defaultManager createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
    return d;
}
static NSString* BoxArtPath(NSString* code) { return [[BoxArtDir() stringByAppendingPathComponent:code] stringByAppendingPathExtension:@"jpg"]; }
static NSString* const kBoxArtLoaded = @"WC09BoxArtLoaded";

static NSArray<NSString*>* BoxArtRegions(NSString* code)
{
    NSDictionary* m = @{@"E": @"US", @"P": @"EN", @"J": @"JA", @"D": @"DE", @"F": @"FR", @"S": @"ES", @"I": @"IT", @"K": @"KO", @"U": @"AU", @"H": @"NL", @"X": @"EN", @"Y": @"EN"};
    NSString* r = code.length >= 4 ? m[[code substringWithRange:NSMakeRange(3, 1)]] : nil;
    NSMutableArray* a = [NSMutableArray array];
    if (r) [a addObject:r];
    for (NSString* x in @[@"US", @"EN", @"JA"]) if (![a containsObject:x]) [a addObject:x];
    return a;
}

// tries each URL in turn until one gives a picture
static void TryBoxArt(NSString* code, NSArray<NSURL*>* urls, NSUInteger i)
{
    if (i >= urls.count)
    {
        [Prefs() setDouble:NSDate.date.timeIntervalSince1970 forKey:[@"boxartMiss:" stringByAppendingString:code]];
        return;
    }
    [[NSURLSession.sharedSession dataTaskWithURL:urls[i] completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err) {
        UIImage* im = data ? [UIImage imageWithData:data] : nil;
        if (!err && [(NSHTTPURLResponse*)resp statusCode] == 200 && im && im.size.width >= 64)
        {
            [UIImageJPEGRepresentation(im, 0.92) writeToFile:BoxArtPath(code) atomically:YES];
            dispatch_async(dispatch_get_main_queue(), ^{ [NSNotificationCenter.defaultCenter postNotificationName:kBoxArtLoaded object:code]; });
            return;
        }
        TryBoxArt(code, urls, i + 1);
    }] resume];
}

// ---- full game names (and publishers) from GameTDB, cached in "tdb"

static NSString* HTMLText(NSString* s)
{
    s = [s stringByReplacingOccurrencesOfString:@"<[^>]+>" withString:@"" options:NSRegularExpressionSearch range:NSMakeRange(0, s.length)];
    NSDictionary* ents = @{@"&amp;": @"&", @"&#039;": @"'", @"&#39;": @"'", @"&quot;": @"\"", @"&lt;": @"<", @"&gt;": @">", @"&nbsp;": @" ", @"&eacute;": @"é", @"&#233;": @"é"};
    for (NSString* k in ents) s = [s stringByReplacingOccurrencesOfString:k withString:ents[k]];
    return [s stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
}

static NSString* FirstMatch(NSString* html, NSString* pattern)
{
    NSRegularExpression* re = [NSRegularExpression regularExpressionWithPattern:pattern options:NSRegularExpressionCaseInsensitive | NSRegularExpressionDotMatchesLineSeparators error:nil];
    NSTextCheckingResult* m = [re firstMatchInString:html options:0 range:NSMakeRange(0, html.length)];
    return m && m.numberOfRanges > 1 && [m rangeAtIndex:1].location != NSNotFound ? HTMLText([html substringWithRange:[m rangeAtIndex:1]]) : nil;
}

static NSString* const kGameInfoLoaded = @"WC09GameInfoLoaded";

static NSDictionary* GameInfo(NSString* code) { return [Prefs() dictionaryForKey:@"tdb"][code]; }

static void FetchGameInfo(NSString* code)
{
    static NSMutableSet* busy;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ busy = [NSMutableSet set]; });
    if (code.length != 4 || GameInfo(code)) return;
    @synchronized (busy) { if ([busy containsObject:code]) return; [busy addObject:code]; }
    double missed = [Prefs() doubleForKey:[@"tdbMiss:" stringByAppendingString:code]];
    if (missed > 0 && NSDate.date.timeIntervalSince1970 - missed < 7 * 86400) return;
    NSURL* u = [NSURL URLWithString:[@"https://www.gametdb.com/DS/" stringByAppendingString:code]];
    [[NSURLSession.sharedSession dataTaskWithURL:u completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err) {
        NSString* html = data ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : nil;
        NSString* title = nil, *pub = nil;
        if (html && [(NSHTTPURLResponse*)resp statusCode] == 200)
        {
            title = FirstMatch(html, @"title \\(EN\\)\\s*</t[dh]>\\s*<td[^>]*>(.*?)</td>");
            if (!title.length)
            {
                NSString* t = FirstMatch(html, @"<title>(.*?)</title>");
                NSRange dash = [t rangeOfString:@" - "];
                if (dash.location != NSNotFound) t = [t substringFromIndex:dash.location + 3];
                NSRange bar = [t rangeOfString:@" | "];
                if (bar.location != NSNotFound) t = [t substringToIndex:bar.location];
                if (t.length && ![t.lowercaseString containsString:@"gametdb"]) title = t;
            }
            pub = FirstMatch(html, @"publisher\\s*</t[dh]>\\s*<td[^>]*>(.*?)</td>");
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (title.length)
            {
                NSMutableDictionary* all = [[Prefs() dictionaryForKey:@"tdb"] ?: @{} mutableCopy];
                all[code] = @{@"title": title, @"publisher": pub ?: @""};
                [Prefs() setObject:all forKey:@"tdb"];
                [NSNotificationCenter.defaultCenter postNotificationName:kGameInfoLoaded object:code];
            }
            else [Prefs() setDouble:NSDate.date.timeIntervalSince1970 forKey:[@"tdbMiss:" stringByAppendingString:code]];
        });
    }] resume];
}

static void FetchBoxArt(NSString* code)
{
    static NSMutableSet* busy;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ busy = [NSMutableSet set]; });
    if (code.length != 4 || ![Prefs() boolForKey:@"boxart"]) return;
    @synchronized (busy) { if ([busy containsObject:code]) return; [busy addObject:code]; }
    // a code that wasn't found is tried again after a week
    double missed = [Prefs() doubleForKey:[@"boxartMiss:" stringByAppendingString:code]];
    if (missed > 0 && NSDate.date.timeIntervalSince1970 - missed < 7 * 86400) return;

    NSMutableArray<NSURL*>* urls = [NSMutableArray array];
    for (NSString* region in BoxArtRegions(code))
        for (NSString* kind in @[@"coverHQ/%@/%@.jpg", @"coverM/%@/%@.jpg", @"cover/%@/%@.jpg", @"coverS/%@/%@.png"])
            [urls addObject:[NSURL URLWithString:[@"https://art.gametdb.com/ds/" stringByAppendingFormat:kind, region, code]]];
    TryBoxArt(code, urls, 0);
}

static GameEntry* LoadEntry(NSString* path)
{
    GameEntry* g = [GameEntry new];
    g.path = path;
    g.file = path.lastPathComponent;
    g.base = g.file.stringByDeletingPathExtension;
    char title[512], code[5];
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
    g.code = wc09_rom_code(path.fileSystemRepresentation, code) == 0 ? [NSString stringWithUTF8String:code] : @"";
    g.enhanced = PackForCode(g.code) != nil;
    NSDictionary* info = GameInfo(g.code);
    if ([info[@"title"] length]) { g.title = info[@"title"]; if ([info[@"publisher"] length]) g.publisher = info[@"publisher"]; }
    else if ([Prefs() boolForKey:@"boxart"]) FetchGameInfo(g.code);

    NSDictionary* meta = Meta(g.file);
    if ([meta[@"name"] length]) g.title = meta[@"name"];
    g.favorite = [meta[@"fav"] boolValue];

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef bc = CGBitmapContextCreate(px, 32, 32, 8, 128, cs, kCGImageAlphaPremultipliedLast);
    long r = 0, gg = 0, b = 0, n = 0;
    for (int i = 0; i < 32 * 32; i++)
        if (px[i * 4 + 3]) { r += px[i * 4]; gg += px[i * 4 + 1]; b += px[i * 4 + 2]; n++; }
    CGImageRef img = CGBitmapContextCreateImage(bc);
    g.icon = [UIImage imageWithCGImage:img];
    CGImageRelease(img); CGContextRelease(bc); CGColorSpaceRelease(cs);
    g.tint = n ? [UIColor colorWithRed:r / n / 255.0 green:gg / n / 255.0 blue:b / n / 255.0 alpha:1] : RGB(0x2A3346);

    NSString* dir = path.stringByDeletingLastPathComponent;
    for (NSString* f in CoverCandidates(dir, g.base))
    {
        UIImage* im = [UIImage imageWithContentsOfFile:f];
        if (!im) continue;
        CGFloat k = 600 / MAX(1.0, MIN(im.size.width, im.size.height));
        g.cover = k < 1 ? [im imageByPreparingThumbnailOfSize:CGSizeMake(im.size.width * k, im.size.height * k)] : im;
        if (!g.cover) g.cover = im;
        break;
    }
    if (!g.cover && g.code.length == 4)
    {
        UIImage* im = [UIImage imageWithContentsOfFile:BoxArtPath(g.code)];
        if (im) g.cover = im;
        else FetchBoxArt(g.code);
    }
    g.resume = [NSFileManager.defaultManager fileExistsAtPath:[[dir stringByAppendingPathComponent:@"Saves"] stringByAppendingPathComponent:[g.base stringByAppendingString:@".ml0"]]];
    g.lastPlayed = [Prefs() doubleForKey:[@"played:" stringByAppendingString:g.file]];
    return g;
}

static UIColor* Mix(UIColor* a, UIColor* b, CGFloat t)
{
    CGFloat r1, g1, b1, a1, r2, g2, b2, a2;
    [a getRed:&r1 green:&g1 blue:&b1 alpha:&a1]; [b getRed:&r2 green:&g2 blue:&b2 alpha:&a2];
    return [UIColor colorWithRed:r1 + (r2 - r1) * t green:g1 + (g2 - g1) * t blue:b1 + (b2 - b1) * t alpha:1];
}

@interface GradientView : UIView
@end
@implementation GradientView
+ (Class)layerClass { return CAGradientLayer.class; }
@end

// ---- tile ----

@interface GameCell : UICollectionViewCell
@property (nonatomic, strong) GameEntry* game;
@end

static UIFont* GameTitleFont() { return [UIFont systemFontOfSize:13.5 weight:UIFontWeightMedium]; }
// height of a game's name wrapped to the cell width
static CGFloat GameTitleHeight(NSString* t, CGFloat w)
{
    if (!t.length) return 0;
    CGRect r = [t boundingRectWithSize:CGSizeMake(w - 4, CGFLOAT_MAX) options:NSStringDrawingUsesLineFragmentOrigin
                            attributes:@{NSFontAttributeName: GameTitleFont()} context:nil];
    return ceil(r.size.height);
}

@implementation GameCell
{
    UIView *_art, *_shine;
    CAGradientLayer* _grad;
    UIImageView *_icon, *_cover, *_star;
    UILabel *_title, *_sub, *_badge, *_hd;
}

static UILabel* Badge(NSString* text, UIColor* color)
{
    UILabel* l = [UILabel new];
    l.text = text;
    l.font = [UIFont systemFontOfSize:10.5 weight:UIFontWeightBold];
    l.textColor = color; l.textAlignment = NSTextAlignmentCenter;
    l.backgroundColor = [UIColor colorWithRed:0.04 green:0.055 blue:0.086 alpha:0.82];
    l.layer.cornerRadius = 10; l.layer.cornerCurve = kCACornerCurveContinuous; l.clipsToBounds = YES;
    return l;
}

- (instancetype)initWithFrame:(CGRect)f
{
    if ((self = [super initWithFrame:f]))
    {
        _art = [UIView new];
        _art.layer.cornerRadius = 18; _art.layer.cornerCurve = kCACornerCurveContinuous; _art.clipsToBounds = YES;
        _art.layer.borderWidth = 1; _art.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.07].CGColor;
        _grad = [CAGradientLayer layer];
        _grad.startPoint = CGPointMake(0, 0); _grad.endPoint = CGPointMake(1, 1);
        [_art.layer addSublayer:_grad];
        _icon = [UIImageView new];
        _icon.layer.magnificationFilter = kCAFilterNearest;
        _cover = [UIImageView new];
        _cover.contentMode = UIViewContentModeScaleAspectFill; _cover.clipsToBounds = YES;
        [_art addSubview:_icon]; [_art addSubview:_cover];
        _badge = Badge(@"RESUME", Accent());
        _hd = Badge(@"✦ HD", RGB(0xF2C14E));
        UIImageSymbolConfiguration* sc = [UIImageSymbolConfiguration configurationWithPointSize:11 weight:UIImageSymbolWeightBold];
        _star = [[UIImageView alloc] initWithImage:[[UIImage systemImageNamed:@"star.fill" withConfiguration:sc] imageWithTintColor:RGB(0xFACC15) renderingMode:UIImageRenderingModeAlwaysOriginal]];
        _star.contentMode = UIViewContentModeCenter;
        _star.backgroundColor = [UIColor colorWithRed:0.04 green:0.055 blue:0.086 alpha:0.82];
        _star.layer.cornerRadius = 11; _star.clipsToBounds = YES;
        for (UIView* v in @[_badge, _hd, _star]) [_art addSubview:v];
        _title = [UILabel new];
        _title.font = GameTitleFont();
        _title.textColor = [UIColor colorWithWhite:1 alpha:0.86]; _title.numberOfLines = 0;
        _title.lineBreakMode = NSLineBreakByWordWrapping;
        _title.textAlignment = NSTextAlignmentCenter;
        _sub = [UILabel new];
        _sub.font = [UIFont systemFontOfSize:12.5];
        _sub.textColor = [UIColor colorWithRed:0.6 green:0.65 blue:0.72 alpha:1];
        for (UIView* v in @[_art, _title, _sub]) [self.contentView addSubview:v];
        self.layer.shadowColor = UIColor.blackColor.CGColor;
        self.layer.shadowOpacity = 0.35; self.layer.shadowRadius = 10; self.layer.shadowOffset = CGSizeMake(0, 6);
    }
    return self;
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    CGFloat w = self.contentView.bounds.size.width, areaH = floor(w * 0.9);
    CGRect art;
    if (_game.cover)
    {
        // box art at its own shape, sitting on the bottom of the art area (like a shelf)
        CGSize is = _game.cover.size;
        CGFloat k = MIN(w / MAX(1.0, is.width), areaH / MAX(1.0, is.height));
        CGFloat aw = floor(is.width * k), ah = floor(is.height * k);
        art = CGRectMake((w - aw) / 2, areaH - ah, aw, ah);
        _art.layer.cornerRadius = 7;
    }
    else
    {
        art = CGRectMake(0, 0, w, areaH);
        _art.layer.cornerRadius = 18;
    }
    _art.frame = art;
    self.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:art cornerRadius:_art.layer.cornerRadius].CGPath;
    _grad.frame = _art.bounds;
    _cover.frame = _art.bounds;
    CGFloat s = floor(MIN(art.size.width, art.size.height) * 0.6 / 32) * 32;
    if (s < 32) s = MIN(art.size.width, art.size.height) * 0.6;
    _icon.frame = CGRectMake((art.size.width - s) / 2, (art.size.height - s) / 2, s, s);
    CGFloat bw = art.size.width;
    _badge.frame = CGRectMake(bw - 6 - 62, 6, 62, 20);
    _star.frame = CGRectMake(6, 6, 22, 22);
    _hd.frame = CGRectMake(6, art.size.height - 6 - 20, 42, 20);
    // the whole name, however many lines it takes (the grid makes every row tall enough)
    CGSize ts = [_title sizeThatFits:CGSizeMake(w - 4, CGFLOAT_MAX)];
    _title.frame = CGRectMake(2, areaH + 10, w - 4, MIN(ts.height, self.contentView.bounds.size.height - areaH - 10));
    _sub.frame = CGRectZero;
}

- (void)setGame:(GameEntry*)g
{
    _game = g;
    _grad.colors = @[(id)Mix(g.tint, AppBackground(), 0.4).CGColor, (id)Mix(g.tint, AppBackground(), 0.82).CGColor];
    _icon.image = g.icon;
    _cover.image = g.cover;
    _cover.hidden = g.cover == nil;
    _icon.hidden = g.cover != nil;
    _grad.hidden = g.cover != nil;
    _title.text = g.title;
    _sub.text = g.publisher;
    _badge.hidden = !g.resume;
    _star.hidden = !g.favorite;
    _hd.hidden = !g.enhanced;
    [self setNeedsLayout];
}

- (void)setHighlighted:(BOOL)h
{
    [super setHighlighted:h];
    [UIView animateWithDuration:0.18 delay:0 usingSpringWithDamping:0.8 initialSpringVelocity:0 options:UIViewAnimationOptionAllowUserInteraction
                     animations:^{ self.transform = h ? CGAffineTransformMakeScale(0.95, 0.95) : CGAffineTransformIdentity; } completion:nil];
}
@end

@interface SectionHeader : UICollectionReusableView
@property (nonatomic, strong) UILabel* label;
@end
@implementation SectionHeader
- (instancetype)initWithFrame:(CGRect)f
{
    if ((self = [super initWithFrame:f]))
    {
        _label = [[UILabel alloc] initWithFrame:CGRectInset(self.bounds, 2, 0)];
        _label.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        _label.font = [UIFont systemFontOfSize:20 weight:UIFontWeightBold];
        _label.textColor = UIColor.whiteColor;
        [self addSubview:_label];
    }
    return self;
}
@end

// ---- library screen ----

@interface LibraryViewController : UIViewController <UICollectionViewDataSource, UICollectionViewDelegate, UICollectionViewDragDelegate, UICollectionViewDropDelegate, UISearchResultsUpdating, UIDocumentPickerDelegate, PHPickerViewControllerDelegate>
@property (nonatomic, strong) UICollectionView* collectionView;
@end

@implementation LibraryViewController
{
    NSArray<GameEntry*>* _all;
    NSArray<NSArray<GameEntry*>*>* _sections;
    NSArray<NSString*>* _sectionTitles;
    NSString* _search;
    GameEntry* _coverTarget;   // the game whose cover is being picked
    BOOL _pickingCover;
    UICollectionViewLayout* _grid;
    UIView* _empty;
    UIView* _hud;
    UISearchController* _searchController;
    UIBarButtonItem* _sortItem;
}

- (UIMenu*)sortMenu
{
    __weak LibraryViewController* me = self;
    NSString* cur = [Prefs() stringForKey:@"sort"];
    NSArray* opts = @[@[@"recent", @"Recently Played", @"clock"], @[@"name", @"Name", @"textformat"], @[@"custom", @"Custom Order", @"hand.draw"]];
    NSMutableArray* acts = [NSMutableArray array];
    for (NSArray* o in opts)
    {
        UIAction* a = [UIAction actionWithTitle:o[1] image:[UIImage systemImageNamed:o[2]] identifier:nil handler:^(UIAction*) {
            [Prefs() setObject:o[0] forKey:@"sort"];
            [me sortChanged];
        }];
        a.state = [cur isEqualToString:o[0]] ? UIMenuElementStateOn : UIMenuElementStateOff;
        [acts addObject:a];
    }
    return [UIMenu menuWithTitle:@"Sort by  (drag a game to reorder)" children:acts];
}

- (void)sortChanged
{
    _sortItem.menu = [self sortMenu];
    [self sortAll];
    [self rebuild];
}

- (void)sortAll
{
    NSString* sort = [Prefs() stringForKey:@"sort"];
    NSArray* order = [Prefs() arrayForKey:@"order"] ?: @[];
    _all = [_all sortedArrayUsingComparator:^NSComparisonResult(GameEntry* a, GameEntry* b) {
        if ([sort isEqualToString:@"name"]) return [a.title localizedStandardCompare:b.title];
        if ([sort isEqualToString:@"custom"])
        {
            NSUInteger ia = [order indexOfObject:a.file], ib = [order indexOfObject:b.file];
            if (ia != ib) return ia < ib ? NSOrderedAscending : NSOrderedDescending;   // unknown ones go last
            return [a.title localizedStandardCompare:b.title];
        }
        if (a.lastPlayed != b.lastPlayed) return a.lastPlayed > b.lastPlayed ? NSOrderedAscending : NSOrderedDescending;
        return [a.title localizedStandardCompare:b.title];
    }];
}

// ---- drag to reorder (switches to Custom Order) ----

- (NSArray<UIDragItem*>*)collectionView:(UICollectionView*)cv itemsForBeginningDragSession:(id<UIDragSession>)session atIndexPath:(NSIndexPath*)ip
{
    if (_search.length) return @[];
    UIDragItem* item = [[UIDragItem alloc] initWithItemProvider:[NSItemProvider new]];
    item.localObject = _sections[ip.section][ip.item];
    return @[item];
}

- (UICollectionViewDropProposal*)collectionView:(UICollectionView*)cv dropSessionDidUpdate:(id<UIDropSession>)session withDestinationIndexPath:(NSIndexPath*)dest
{
    if (!session.localDragSession || !dest) return [[UICollectionViewDropProposal alloc] initWithDropOperation:UIDropOperationForbidden];
    GameEntry* g = session.localDragSession.items.firstObject.localObject;
    BOOL same = dest.section < (NSInteger)_sections.count && [_sections[dest.section] containsObject:g];
    return [[UICollectionViewDropProposal alloc] initWithDropOperation:same ? UIDropOperationMove : UIDropOperationForbidden
                                                                intent:UICollectionViewDropIntentInsertAtDestinationIndexPath];
}

- (void)collectionView:(UICollectionView*)cv performDropWithCoordinator:(id<UICollectionViewDropCoordinator>)coord
{
    id<UICollectionViewDropItem> it = coord.items.firstObject;
    NSIndexPath* src = it.sourceIndexPath, *dst = coord.destinationIndexPath;
    if (!src || !dst || src.section != dst.section) return;
    NSMutableArray* sec = [_sections[src.section] mutableCopy];
    GameEntry* g = sec[src.item];
    [sec removeObjectAtIndex:src.item];
    NSInteger to = MIN((NSInteger)sec.count, dst.item);
    [sec insertObject:g atIndex:to];
    NSMutableArray* secs = [_sections mutableCopy];
    secs[src.section] = sec;
    _sections = secs;
    [cv performBatchUpdates:^{ [cv moveItemAtIndexPath:src toIndexPath:[NSIndexPath indexPathForItem:to inSection:src.section]]; } completion:nil];
    [coord dropItem:it.dragItem toItemAtIndexPath:[NSIndexPath indexPathForItem:to inSection:src.section]];
    // the new order becomes the custom order
    NSMutableArray* order = [NSMutableArray array];
    for (NSArray* sct in _sections) for (GameEntry* e in sct) [order addObject:e.file];
    for (GameEntry* e in _all) if (![order containsObject:e.file]) [order addObject:e.file];
    [Prefs() setObject:order forKey:@"order"];
    if (![[Prefs() stringForKey:@"sort"] isEqualToString:@"custom"])
    {
        [Prefs() setObject:@"custom" forKey:@"sort"];
        _sortItem.menu = [self sortMenu];
    }
    NSMutableArray* all = [NSMutableArray array];
    for (NSString* f in order) for (GameEntry* e in _all) if ([e.file isEqualToString:f]) [all addObject:e];
    _all = all;
    [[[UISelectionFeedbackGenerator alloc] init] selectionChanged];
}

static UICollectionViewLayout* GridLayout(BOOL (^hasHeader)(NSInteger), CGFloat (^titleHeight)(CGFloat cellW))
{
    return [[UICollectionViewCompositionalLayout alloc] initWithSectionProvider:^NSCollectionLayoutSection*(NSInteger s, id<NSCollectionLayoutEnvironment> env) {
        CGFloat w = env.container.effectiveContentSize.width;
        NSInteger cols = MAX(2, (NSInteger)((w - 40) / 168));
        CGFloat cellW = floor((w - 40 - (cols - 1) * 16) / cols);
        // every cell tall enough for the longest name in the library (two lines at least)
        CGFloat cellH = floor(cellW * 0.9) + 10 + MAX(titleHeight(cellW), ceil(GameTitleFont().lineHeight * 2)) + 12;
        NSCollectionLayoutItem* item = [NSCollectionLayoutItem itemWithLayoutSize:
            [NSCollectionLayoutSize sizeWithWidthDimension:[NSCollectionLayoutDimension absoluteDimension:cellW] heightDimension:[NSCollectionLayoutDimension absoluteDimension:cellH]]];
        NSCollectionLayoutGroup* group = [NSCollectionLayoutGroup horizontalGroupWithLayoutSize:
            [NSCollectionLayoutSize sizeWithWidthDimension:[NSCollectionLayoutDimension fractionalWidthDimension:1] heightDimension:[NSCollectionLayoutDimension absoluteDimension:cellH]]
            repeatingSubitem:item count:cols];
        group.interItemSpacing = [NSCollectionLayoutSpacing fixedSpacing:16];
        NSCollectionLayoutSection* sec = [NSCollectionLayoutSection sectionWithGroup:group];
        sec.interGroupSpacing = 20;
        sec.contentInsets = NSDirectionalEdgeInsetsMake(6, 20, 26, 20);
        if (hasHeader(s))
        {
            NSCollectionLayoutBoundarySupplementaryItem* h = [NSCollectionLayoutBoundarySupplementaryItem
                boundarySupplementaryItemWithLayoutSize:[NSCollectionLayoutSize sizeWithWidthDimension:[NSCollectionLayoutDimension fractionalWidthDimension:1]
                                                                                          heightDimension:[NSCollectionLayoutDimension absoluteDimension:34]]
                                            elementKind:UICollectionElementKindSectionHeader alignment:NSRectAlignmentTop];
            h.contentInsets = NSDirectionalEdgeInsetsMake(0, 20, 0, 20);
            sec.boundarySupplementaryItems = @[h];
        }
        return sec;
    }];
}

- (instancetype)init
{
    if (!(self = [super initWithNibName:nil bundle:nil])) return nil;
    __weak LibraryViewController* weakSelf = self;
    _grid = GridLayout(^BOOL(NSInteger s) {
        LibraryViewController* me = weakSelf;
        return me && me->_sectionTitles.count > (NSUInteger)s && me->_sectionTitles[s].length > 0;
    }, ^CGFloat(CGFloat cellW) {
        LibraryViewController* me = weakSelf;
        CGFloat h = 0;
        if (me) for (NSArray<GameEntry*>* sec in me->_sections) for (GameEntry* e in sec) h = MAX(h, GameTitleHeight(e.title, cellW));
        return MIN(h, ceil(GameTitleFont().lineHeight * 6));
    });
    return self;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskAll; }

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.title = @"Nintendo DS";
    // the app's name under the big title (iOS 26+), or as a small label in the bar before that
    if ([self.navigationItem respondsToSelector:NSSelectorFromString(@"setSubtitle:")])
        [self.navigationItem setValue:@"Dual HD" forKey:@"subtitle"];
    else
    {
        UILabel* brand = [UILabel new];
        brand.attributedText = [[NSAttributedString alloc] initWithString:@"DUAL HD" attributes:@{
            NSFontAttributeName: [UIFont systemFontOfSize:12 weight:UIFontWeightHeavy], NSKernAttributeName: @2.5,
            NSForegroundColorAttributeName: Accent()}];
        [brand sizeToFit];
        self.navigationItem.leftBarButtonItem = [[UIBarButtonItem alloc] initWithCustomView:brand];
    }
    self.view.backgroundColor = AppBackground();
    self.collectionView = [[UICollectionView alloc] initWithFrame:self.view.bounds collectionViewLayout:_grid];
    self.collectionView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.collectionView.dataSource = self;
    self.collectionView.delegate = self;
    [self.view addSubview:self.collectionView];
    self.collectionView.backgroundColor = AppBackground();
    self.collectionView.alwaysBounceVertical = YES;
    [self.collectionView registerClass:GameCell.class forCellWithReuseIdentifier:@"g"];
    [self.collectionView registerClass:SectionHeader.class forSupplementaryViewOfKind:UICollectionElementKindSectionHeader withReuseIdentifier:@"h"];

    UIBarButtonItem* gear = [[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"gearshape"] style:UIBarButtonItemStylePlain target:self action:@selector(openSettings)];
    __weak LibraryViewController* me = self;
    UIMenu* add = [UIMenu menuWithChildren:@[
        [UIAction actionWithTitle:@"Import Games…" image:[UIImage systemImageNamed:@"square.and.arrow.down"] identifier:nil handler:^(UIAction*) { [me importGames]; }],
        [UIAction actionWithTitle:@"Add Games Folder…" image:[UIImage systemImageNamed:@"folder.badge.plus"] identifier:nil handler:^(UIAction*) { [FolderPicker presentFrom:me]; }],
    ]];
    UIBarButtonItem* plus = [[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"plus"] menu:add];
    _sortItem = [[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"arrow.up.arrow.down"] menu:[self sortMenu]];
    self.navigationItem.rightBarButtonItems = @[gear, plus, _sortItem];
    self.collectionView.dragDelegate = self;
    self.collectionView.dropDelegate = self;
    self.collectionView.dragInteractionEnabled = YES;

    UISearchController* sc = [[UISearchController alloc] initWithSearchResultsController:nil];
    sc.searchResultsUpdater = self;
    sc.obscuresBackgroundDuringPresentation = NO;
    sc.searchBar.placeholder = @"Search games";
    _searchController = sc;
    self.navigationItem.hidesSearchBarWhenScrolling = YES;

    UIRefreshControl* rc = [UIRefreshControl new];
    [rc addTarget:self action:@selector(pulled:) forControlEvents:UIControlEventValueChanged];
    self.collectionView.refreshControl = rc;
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(reload) name:UISceneDidActivateNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(reload) name:kSettingsChanged object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(boxArtLoaded) name:kBoxArtLoaded object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(boxArtLoaded) name:kGameInfoLoaded object:nil];
}

// several covers arrive together: reload once
- (void)boxArtLoaded
{
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(reload) object:nil];
    [self performSelector:@selector(reload) withObject:nil afterDelay:0.3];
}

- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; [self reload]; }
- (void)pulled:(UIRefreshControl*)rc { [self reload]; [rc endRefreshing]; }

- (void)reload
{
    NSString* docs = DocumentsDir();
    NSArray<NSURL*>* folders = [gFolders copy];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSMutableArray<NSString*>* roms = [NSMutableArray array];
        NSFileManager* fm = NSFileManager.defaultManager;
        void (^scan)(NSString*) = ^(NSString* dir) {
            for (NSString* f in [fm contentsOfDirectoryAtPath:dir error:nil])
            {
                NSString* p = [dir stringByAppendingPathComponent:f];
                if ([f.pathExtension.lowercaseString isEqualToString:@"nds"] && ![f hasPrefix:@"."]) { if (![roms containsObject:p]) [roms addObject:p]; }
                else if ([f hasPrefix:@"."] && [f.lowercaseString hasSuffix:@".nds.icloud"])   // in iCloud, not downloaded yet
                    [fm startDownloadingUbiquitousItemAtURL:[NSURL fileURLWithPath:p] error:nil];
            }
        };
        NSMutableArray* roots = [NSMutableArray arrayWithObject:docs];
        for (NSURL* u in folders) [roots addObject:u.path];
        for (NSString* root in roots)
        {
            scan(root);
            for (NSString* f in [fm contentsOfDirectoryAtPath:root error:nil])
            {
                NSString* p = [root stringByAppendingPathComponent:f];
                BOOL isDir = NO;
                if ([fm fileExistsAtPath:p isDirectory:&isDir] && isDir && ![@[@"Saves", @"HDCards", @"WC2009-HD", @"Covers", @"Inbox"] containsObject:f] &&
                    ![fm fileExistsAtPath:[p stringByAppendingPathComponent:@"pack.json"]]) scan(p);
            }
        }
        NSMutableArray<GameEntry*>* games = [NSMutableArray array];
        for (NSString* r in roms) [games addObject:LoadEntry(r)];
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_all = games;
            [self sortAll];
            [self rebuild];
        });
    });
}

- (void)rebuild
{
    NSMutableArray *fav = [NSMutableArray array], *rest = [NSMutableArray array];
    for (GameEntry* g in _all)
    {
        if (_search.length && [g.title rangeOfString:_search options:NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch].location == NSNotFound) continue;
        [(g.favorite ? fav : rest) addObject:g];
    }
    if (fav.count) { _sections = rest.count ? @[fav, rest] : @[fav]; _sectionTitles = rest.count ? @[@"Favorites", @"All Games"] : @[@"Favorites"]; }
    else { _sections = @[rest]; _sectionTitles = @[@""]; }
    [self.collectionView.collectionViewLayout invalidateLayout];
    [self.collectionView reloadData];
    if (!_empty)
    {
        _empty = [self emptyView];
        _empty.translatesAutoresizingMaskIntoConstraints = NO;
        [self.view addSubview:_empty];
        UILayoutGuide* g = self.view.safeAreaLayoutGuide;
        [NSLayoutConstraint activateConstraints:@[
            [_empty.topAnchor constraintEqualToAnchor:g.topAnchor], [_empty.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
            [_empty.leadingAnchor constraintEqualToAnchor:g.leadingAnchor], [_empty.trailingAnchor constraintEqualToAnchor:g.trailingAnchor]]];
    }
    _empty.hidden = _all.count > 0;
    self.collectionView.hidden = _all.count == 0;
    self.navigationItem.searchController = _all.count ? _searchController : nil;
}

- (void)updateSearchResultsForSearchController:(UISearchController*)sc { _search = sc.searchBar.text; [self rebuild]; }

// ---- empty state ----

- (UIView*)emptyView
{
    UIView* v = [UIView new];
    UIScrollView* sv = [UIScrollView new];
    sv.translatesAutoresizingMaskIntoConstraints = NO;
    [v addSubview:sv];

    // the app's two screens, drawn big, on a soft glow
    UIView* art = [UIView new];
    art.translatesAutoresizingMaskIntoConstraints = NO;
    GradientView* glow = [GradientView new];
    CAGradientLayer* gl = (CAGradientLayer*)glow.layer;
    gl.type = kCAGradientLayerRadial;
    gl.colors = @[(id)[Accent() colorWithAlphaComponent:0.28].CGColor, (id)[Accent() colorWithAlphaComponent:0].CGColor];
    gl.startPoint = CGPointMake(0.5, 0.5); gl.endPoint = CGPointMake(1, 1);
    UIView* body = [UIView new];
    body.backgroundColor = RGB(0x0B1730); body.layer.cornerRadius = 12;
    body.layer.borderWidth = 1; body.layer.borderColor = [UIColor colorWithWhite:1 alpha:0.12].CGColor;
    for (UIView* x in @[glow, body]) { x.translatesAutoresizingMaskIntoConstraints = NO; [art addSubview:x]; }
    NSArray* screens = @[@[RGB(0x7CC4FF), RGB(0x2F7BEA)], @[RGB(0x46E0D0), RGB(0x1A8FB0)]];
    for (int i = 0; i < 2; i++)
    {
        CAGradientLayer* sl = [CAGradientLayer layer];
        sl.colors = @[(id)[screens[i][0] CGColor], (id)[screens[i][1] CGColor]];
        sl.startPoint = CGPointMake(0, 0); sl.endPoint = CGPointMake(1, 1);
        sl.frame = CGRectMake(7, 8 + i * 48, 56, 40);
        sl.cornerRadius = 4;
        [body.layer addSublayer:sl];
    }
    [NSLayoutConstraint activateConstraints:@[
        [body.centerXAnchor constraintEqualToAnchor:art.centerXAnchor], [body.topAnchor constraintEqualToAnchor:art.topAnchor],
        [body.widthAnchor constraintEqualToConstant:70], [body.heightAnchor constraintEqualToConstant:104],
        [glow.centerXAnchor constraintEqualToAnchor:body.centerXAnchor], [glow.centerYAnchor constraintEqualToAnchor:body.centerYAnchor],
        [glow.widthAnchor constraintEqualToConstant:180], [glow.heightAnchor constraintEqualToConstant:180]]];

    UILabel* brand = [UILabel new];
    brand.attributedText = [[NSAttributedString alloc] initWithString:@"DUAL HD" attributes:@{
        NSFontAttributeName: [UIFont systemFontOfSize:13 weight:UIFontWeightHeavy], NSKernAttributeName: @3.5,
        NSForegroundColorAttributeName: Accent()}];
    brand.textAlignment = NSTextAlignmentCenter;
    UILabel* title = [UILabel new];
    title.text = @"Your DS games, in HD";
    title.font = [UIFont systemFontOfSize:28 weight:UIFontWeightBold];
    title.textColor = UIColor.whiteColor; title.textAlignment = NSTextAlignmentCenter; title.numberOfLines = 0;
    UILabel* sub = [UILabel new];
    sub.text = @"Add your games to get started. Pick a folder that has them (On My iPhone, iCloud Drive, a USB drive…), or import .nds files one by one.";
    sub.font = [UIFont systemFontOfSize:16]; sub.numberOfLines = 0;
    sub.textColor = UIColor.secondaryLabelColor; sub.textAlignment = NSTextAlignmentCenter;

    __weak LibraryViewController* me = self;
    UIButtonConfiguration* bc = [UIButtonConfiguration filledButtonConfiguration];
    bc.title = @"Choose Games Folder";
    bc.image = [UIImage systemImageNamed:@"folder.badge.plus"];
    bc.imagePadding = 8; bc.cornerStyle = UIButtonConfigurationCornerStyleLarge;
    bc.baseBackgroundColor = Accent(); bc.baseForegroundColor = RGB(0x06111F);
    bc.contentInsets = NSDirectionalEdgeInsetsMake(14, 20, 14, 20);
    UIButton* imp = [UIButton buttonWithConfiguration:bc primaryAction:[UIAction actionWithHandler:^(UIAction*) { [FolderPicker presentFrom:me]; }]];

    UIButtonConfiguration* fc = [UIButtonConfiguration tintedButtonConfiguration];
    fc.title = @"Import Games";
    fc.image = [UIImage systemImageNamed:@"square.and.arrow.down"];
    fc.imagePadding = 8; fc.cornerStyle = UIButtonConfigurationCornerStyleLarge;
    fc.baseForegroundColor = Accent();
    fc.contentInsets = NSDirectionalEdgeInsetsMake(14, 20, 14, 20);
    UIButton* files = [UIButton buttonWithConfiguration:fc primaryAction:[UIAction actionWithHandler:^(UIAction*) { [me importGames]; }]];

    UILabel* why = [UILabel new];
    why.text = @"WHY DUAL HD";
    why.font = [UIFont systemFontOfSize:12.5 weight:UIFontWeightSemibold];
    why.textColor = UIColor.secondaryLabelColor;
    UIView* card = [self tipCard:@"sparkles.tv" title:@"Up to 8x resolution"
                            text:@"Sharp HD upscaling for every game, from 2x to 8x, with smooth 2D graphics."];
    UIView* card1 = [self tipCard:@"rectangle.split.1x2.fill" title:@"Made for big screens"
                             text:@"Layouts for portrait, landscape and foldables, full screen mode and game controllers."];
    UIView* card3 = [self tipCard:@"clock.arrow.circlepath" title:@"Never lose progress"
                             text:@"Save states, and every game picks up right where you left off."];
    UIView* card2 = [self tipCard:@"wand.and.stars" title:@"Game enhancements"
                             text:@"Some games get extra improvements made just for them. See Settings > Game Enhancements."];

    UIStackView* st = [[UIStackView alloc] initWithArrangedSubviews:@[art, brand, title, sub, imp, files, why, card, card1, card3, card2]];
    st.axis = UILayoutConstraintAxisVertical; st.spacing = 14; st.alignment = UIStackViewAlignmentFill;
    [st setCustomSpacing:22 afterView:art];
    [st setCustomSpacing:6 afterView:brand];
    [st setCustomSpacing:8 afterView:title];
    [st setCustomSpacing:26 afterView:sub];
    [st setCustomSpacing:34 afterView:files];
    [st setCustomSpacing:8 afterView:why];
    st.translatesAutoresizingMaskIntoConstraints = NO;
    [sv addSubview:st];
    [NSLayoutConstraint activateConstraints:@[
        [sv.topAnchor constraintEqualToAnchor:v.topAnchor], [sv.bottomAnchor constraintEqualToAnchor:v.bottomAnchor],
        [sv.leadingAnchor constraintEqualToAnchor:v.leadingAnchor], [sv.trailingAnchor constraintEqualToAnchor:v.trailingAnchor],
        [art.heightAnchor constraintEqualToConstant:104],
        [st.widthAnchor constraintLessThanOrEqualToConstant:440],
        [st.widthAnchor constraintLessThanOrEqualToAnchor:sv.frameLayoutGuide.widthAnchor constant:-56],
        [st.centerXAnchor constraintEqualToAnchor:sv.frameLayoutGuide.centerXAnchor],
        [st.topAnchor constraintEqualToAnchor:sv.contentLayoutGuide.topAnchor constant:56],
        [st.bottomAnchor constraintEqualToAnchor:sv.contentLayoutGuide.bottomAnchor constant:-40],
    ]];
    NSLayoutConstraint* wide = [st.widthAnchor constraintEqualToConstant:440];
    wide.priority = UILayoutPriorityDefaultHigh;
    wide.active = YES;
    return v;
}

- (UIView*)tipCard:(NSString*)symbol title:(NSString*)t text:(NSString*)text
{
    UIView* card = [UIView new];
    card.backgroundColor = [UIColor colorWithRed:0.09 green:0.115 blue:0.165 alpha:1];
    card.layer.cornerRadius = 16; card.layer.cornerCurve = kCACornerCurveContinuous;
    UIImageView* iv = [[UIImageView alloc] initWithImage:SettingsIcon(symbol, [Accent() colorWithAlphaComponent:0.9])];
    UILabel* tl = [UILabel new];
    tl.text = t; tl.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold]; tl.textColor = UIColor.whiteColor;
    UILabel* bl = [UILabel new];
    bl.text = text; bl.font = [UIFont systemFontOfSize:14]; bl.textColor = UIColor.secondaryLabelColor; bl.numberOfLines = 0;
    UIStackView* txt = [[UIStackView alloc] initWithArrangedSubviews:@[tl, bl]];
    txt.axis = UILayoutConstraintAxisVertical; txt.spacing = 3;
    UIStackView* row = [[UIStackView alloc] initWithArrangedSubviews:@[iv, txt]];
    row.spacing = 14; row.alignment = UIStackViewAlignmentTop;
    row.translatesAutoresizingMaskIntoConstraints = NO;
    [card addSubview:row];
    [NSLayoutConstraint activateConstraints:@[
        [row.leadingAnchor constraintEqualToAnchor:card.leadingAnchor constant:16], [row.trailingAnchor constraintEqualToAnchor:card.trailingAnchor constant:-16],
        [row.topAnchor constraintEqualToAnchor:card.topAnchor constant:14], [row.bottomAnchor constraintEqualToAnchor:card.bottomAnchor constant:-14],
        [iv.widthAnchor constraintEqualToConstant:29], [iv.heightAnchor constraintEqualToConstant:29]]];
    return card;
}

// ---- grid ----

- (NSInteger)numberOfSectionsInCollectionView:(UICollectionView*)cv { return _sections.count; }
- (NSInteger)collectionView:(UICollectionView*)cv numberOfItemsInSection:(NSInteger)s { return _sections[s].count; }

- (UICollectionViewCell*)collectionView:(UICollectionView*)cv cellForItemAtIndexPath:(NSIndexPath*)ip
{
    GameCell* c = [cv dequeueReusableCellWithReuseIdentifier:@"g" forIndexPath:ip];
    c.game = _sections[ip.section][ip.item];
    return c;
}

- (UICollectionReusableView*)collectionView:(UICollectionView*)cv viewForSupplementaryElementOfKind:(NSString*)kind atIndexPath:(NSIndexPath*)ip
{
    SectionHeader* h = [cv dequeueReusableSupplementaryViewOfKind:kind withReuseIdentifier:@"h" forIndexPath:ip];
    h.label.text = _sectionTitles[ip.section];
    return h;
}

- (void)collectionView:(UICollectionView*)cv didSelectItemAtIndexPath:(NSIndexPath*)ip { [self play:_sections[ip.section][ip.item] fresh:NO]; }

- (UIContextMenuConfiguration*)collectionView:(UICollectionView*)cv contextMenuConfigurationForItemAtIndexPath:(NSIndexPath*)ip point:(CGPoint)p
{
    GameEntry* g = _sections[ip.section][ip.item];
    __weak LibraryViewController* me = self;
    return [UIContextMenuConfiguration configurationWithIdentifier:nil previewProvider:nil actionProvider:^UIMenu*(NSArray<UIMenuElement*>*) {
        NSMutableArray* top = [NSMutableArray array];
        [top addObject:[UIAction actionWithTitle:@"Play" image:[UIImage systemImageNamed:@"play.fill"] identifier:nil handler:^(UIAction*) { [me play:g fresh:NO]; }]];
        if (g.resume)
            [top addObject:[UIAction actionWithTitle:@"Start from In-Game Save" image:[UIImage systemImageNamed:@"arrow.counterclockwise"] identifier:nil handler:^(UIAction*) { [me play:g fresh:YES]; }]];
        UIAction* fav = [UIAction actionWithTitle:(g.favorite ? @"Remove from Favorites" : @"Add to Favorites") image:[UIImage systemImageNamed:(g.favorite ? @"star.slash" : @"star")] identifier:nil handler:^(UIAction*) {
            SetMeta(g.file, @"fav", g.favorite ? nil : @YES);
            [me reload];
        }];
        UIMenu* cover = [UIMenu menuWithTitle:@"Change Cover" image:[UIImage systemImageNamed:@"photo"] identifier:nil options:0 children:@[
            [UIAction actionWithTitle:@"From Photos" image:[UIImage systemImageNamed:@"photo.on.rectangle"] identifier:nil handler:^(UIAction*) { [me pickCoverFromPhotos:g]; }],
            [UIAction actionWithTitle:@"From Files" image:[UIImage systemImageNamed:@"folder"] identifier:nil handler:^(UIAction*) { [me pickCoverFromFiles:g]; }],
            [UIAction actionWithTitle:@"Use Default Cover" image:[UIImage systemImageNamed:@"arrow.uturn.backward"] identifier:nil handler:^(UIAction*) { [me removeCover:g]; }],
        ]];
        UIAction* rename = [UIAction actionWithTitle:@"Rename" image:[UIImage systemImageNamed:@"pencil"] identifier:nil handler:^(UIAction*) { [me rename:g]; }];
        NSMutableArray* more = [NSMutableArray arrayWithObjects:fav, cover, rename, nil];
        NSMutableArray* danger = [NSMutableArray array];
        if (g.resume)
        {
            UIAction* r = [UIAction actionWithTitle:@"Remove Resume Point" image:[UIImage systemImageNamed:@"bookmark.slash"] identifier:nil handler:^(UIAction*) {
                [NSFileManager.defaultManager removeItemAtPath:[[[g.path.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"Saves"] stringByAppendingPathComponent:g.base] stringByAppendingString:@".ml0"] error:nil];
                [me reload];
            }];
            [danger addObject:r];
        }
        UIAction* del = [UIAction actionWithTitle:@"Delete Game" image:[UIImage systemImageNamed:@"trash"] identifier:nil handler:^(UIAction*) { [me confirmDelete:g]; }];
        del.attributes = UIMenuElementAttributesDestructive;
        [danger addObject:del];
        return [UIMenu menuWithTitle:g.title children:@[
            [UIMenu menuWithTitle:@"" image:nil identifier:nil options:UIMenuOptionsDisplayInline children:top],
            [UIMenu menuWithTitle:@"" image:nil identifier:nil options:UIMenuOptionsDisplayInline children:more],
            [UIMenu menuWithTitle:@"" image:nil identifier:nil options:UIMenuOptionsDisplayInline children:danger]]];
    }];
}

// ---- actions ----

- (void)play:(GameEntry*)g fresh:(BOOL)fresh
{
    [Prefs() setDouble:NSDate.date.timeIntervalSince1970 forKey:[@"played:" stringByAppendingString:g.file]];
    GameViewController* vc = [[GameViewController alloc] initWithRom:g.path fresh:fresh];
    vc.gameTitle = g.title;
    vc.modalPresentationStyle = UIModalPresentationFullScreen;
    vc.modalTransitionStyle = UIModalTransitionStyleCrossDissolve;
    __weak LibraryViewController* me = self;
    vc.onClose = ^{ [me reload]; };
    [self presentViewController:vc animated:YES completion:nil];
}

- (void)openSettings { [self presentViewController:SettingsNavigation(nil) animated:YES completion:nil]; }

- (void)openFiles
{
    NSURLComponents* u = [NSURLComponents componentsWithURL:[NSURL fileURLWithPath:DocumentsDir()] resolvingAgainstBaseURL:NO];
    u.scheme = @"shareddocuments";
    [UIApplication.sharedApplication openURL:u.URL options:@{} completionHandler:nil];
}

- (void)importGames
{
    UTType* nds = [UTType typeWithFilenameExtension:@"nds"] ?: UTTypeData;
    UIDocumentPickerViewController* p = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[nds, UTTypeFolder] asCopy:NO];
    p.allowsMultipleSelection = YES;
    p.delegate = self;
    _pickingCover = NO;
    [self presentViewController:p animated:YES completion:nil];
}

- (void)showHud:(NSString*)text
{
    UIView* hud = [UIView new];
    hud.backgroundColor = [UIColor colorWithWhite:0 alpha:0.55];
    hud.frame = self.view.window.bounds;
    UIView* box = [UIView new];
    box.backgroundColor = [UIColor colorWithRed:0.09 green:0.115 blue:0.165 alpha:1];
    box.layer.cornerRadius = 18;
    UIActivityIndicatorView* spin = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleLarge];
    spin.color = UIColor.whiteColor;
    [spin startAnimating];
    UILabel* l = [UILabel new];
    l.text = text; l.textColor = UIColor.whiteColor; l.font = [UIFont systemFontOfSize:16 weight:UIFontWeightSemibold];
    UIStackView* st = [[UIStackView alloc] initWithArrangedSubviews:@[spin, l]];
    st.axis = UILayoutConstraintAxisVertical; st.spacing = 14; st.alignment = UIStackViewAlignmentCenter;
    st.translatesAutoresizingMaskIntoConstraints = NO; box.translatesAutoresizingMaskIntoConstraints = NO;
    [box addSubview:st]; [hud addSubview:box];
    [NSLayoutConstraint activateConstraints:@[
        [box.centerXAnchor constraintEqualToAnchor:hud.centerXAnchor], [box.centerYAnchor constraintEqualToAnchor:hud.centerYAnchor],
        [st.leadingAnchor constraintEqualToAnchor:box.leadingAnchor constant:30], [st.trailingAnchor constraintEqualToAnchor:box.trailingAnchor constant:-30],
        [st.topAnchor constraintEqualToAnchor:box.topAnchor constant:24], [st.bottomAnchor constraintEqualToAnchor:box.bottomAnchor constant:-24]]];
    [self.view.window addSubview:hud];
    _hud = hud;
}

- (void)documentPicker:(UIDocumentPickerViewController*)picker didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    if (_pickingCover)
    {
        GameEntry* g = _coverTarget;
        NSURL* u = urls.firstObject;
        BOOL scoped = [u startAccessingSecurityScopedResource];
        UIImage* im = [UIImage imageWithContentsOfFile:u.path];
        if (scoped) [u stopAccessingSecurityScopedResource];
        if (im && g) [self saveCover:im for:g];
        return;
    }
    [self showHud:@"Importing…"];
    NSString* docs = DocumentsDir();
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSFileManager* fm = NSFileManager.defaultManager;
        int copied = 0;
        for (NSURL* u in urls)
        {
            BOOL scoped = [u startAccessingSecurityScopedResource];
            NSString* dst = [docs stringByAppendingPathComponent:u.lastPathComponent];
            if (![fm fileExistsAtPath:dst] && [fm copyItemAtURL:u toURL:[NSURL fileURLWithPath:dst] error:nil]) copied++;
            if (scoped) [u stopAccessingSecurityScopedResource];
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_hud removeFromSuperview];
            self->_hud = nil;
            [[[UINotificationFeedbackGenerator alloc] init] notificationOccurred:copied ? UINotificationFeedbackTypeSuccess : UINotificationFeedbackTypeWarning];
            [self reload];
        });
    });
}

- (void)pickCoverFromPhotos:(GameEntry*)g
{
    _coverTarget = g;
    PHPickerConfiguration* cfg = [PHPickerConfiguration new];
    cfg.filter = PHPickerFilter.imagesFilter;
    cfg.selectionLimit = 1;
    PHPickerViewController* p = [[PHPickerViewController alloc] initWithConfiguration:cfg];
    p.delegate = self;
    [self presentViewController:p animated:YES completion:nil];
}

- (void)picker:(PHPickerViewController*)picker didFinishPicking:(NSArray<PHPickerResult*>*)results
{
    [picker dismissViewControllerAnimated:YES completion:nil];
    NSItemProvider* ip = results.firstObject.itemProvider;
    GameEntry* g = _coverTarget;
    if (!ip || !g || ![ip canLoadObjectOfClass:UIImage.class]) return;
    [ip loadObjectOfClass:UIImage.class completionHandler:^(id<NSItemProviderReading> obj, NSError* err) {
        UIImage* im = (UIImage*)obj;
        if (!im) return;
        dispatch_async(dispatch_get_main_queue(), ^{ [self saveCover:im for:g]; });
    }];
}

- (void)pickCoverFromFiles:(GameEntry*)g
{
    _coverTarget = g;
    _pickingCover = YES;
    UIDocumentPickerViewController* p = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeImage] asCopy:YES];
    p.delegate = self;
    [self presentViewController:p animated:YES completion:nil];
}

- (void)saveCover:(UIImage*)im for:(GameEntry*)g
{
    [self removeCoverFiles:g];
    [NSFileManager.defaultManager createDirectoryAtPath:CoversDir() withIntermediateDirectories:YES attributes:nil error:nil];
    CGFloat k = 1024 / MAX(im.size.width, im.size.height);
    if (k < 1)
    {
        UIGraphicsImageRenderer* r = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(im.size.width * k, im.size.height * k)];
        im = [r imageWithActions:^(UIGraphicsImageRendererContext*) { [im drawInRect:CGRectMake(0, 0, im.size.width * k, im.size.height * k)]; }];
    }
    [UIImageJPEGRepresentation(im, 0.9) writeToFile:[[CoversDir() stringByAppendingPathComponent:g.base] stringByAppendingPathExtension:@"jpg"] atomically:YES];
    [self reload];
}

- (void)removeCoverFiles:(GameEntry*)g
{
    for (NSString* f in CoverCandidates(g.path.stringByDeletingLastPathComponent, g.base))
        [NSFileManager.defaultManager removeItemAtPath:f error:nil];
}

- (void)removeCover:(GameEntry*)g { [self removeCoverFiles:g]; [self reload]; }

- (void)rename:(GameEntry*)g
{
    UIAlertController* a = [UIAlertController alertControllerWithTitle:@"Rename" message:nil preferredStyle:UIAlertControllerStyleAlert];
    [a addTextFieldWithConfigurationHandler:^(UITextField* tf) {
        tf.text = g.title; tf.clearButtonMode = UITextFieldViewModeWhileEditing; tf.autocapitalizationType = UITextAutocapitalizationTypeWords;
    }];
    __weak LibraryViewController* me = self;
    __weak UIAlertController* wa = a;
    [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [a addAction:[UIAlertAction actionWithTitle:@"Reset" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { SetMeta(g.file, @"name", nil); [me reload]; }]];
    [a addAction:[UIAlertAction actionWithTitle:@"Save" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
        NSString* t = [wa.textFields.firstObject.text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        SetMeta(g.file, @"name", t.length ? t : nil);
        [me reload];
    }]];
    a.preferredAction = a.actions.lastObject;
    [self presentViewController:a animated:YES completion:nil];
}

- (void)confirmDelete:(GameEntry*)g
{
    UIAlertController* a = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Delete %@?", g.title]
        message:@"The game file is removed from this iPhone. Your saves stay in the Saves folder." preferredStyle:UIAlertControllerStyleAlert];
    __weak LibraryViewController* me = self;
    [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [a addAction:[UIAlertAction actionWithTitle:@"Delete" style:UIAlertActionStyleDestructive handler:^(UIAlertAction*) {
        [NSFileManager.defaultManager removeItemAtPath:g.path error:nil];
        [me reload];
    }]];
    [self presentViewController:a animated:YES completion:nil];
}
@end

// ------------------------------------------------------------------------------------------------

@interface NavigationController : UINavigationController
@end
@implementation NavigationController
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskAll; }
- (UIStatusBarStyle)preferredStatusBarStyle { return UIStatusBarStyleLightContent; }
@end

// scene-based life cycle (required by the current iOS SDK)
@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication*)app didFinishLaunchingWithOptions:(NSDictionary*)opts
{
    RegisterDefaults();
    LoadLinkedFolders();
    return YES;
}

- (UIInterfaceOrientationMask)application:(UIApplication*)app supportedInterfaceOrientationsForWindow:(UIWindow*)window
{
    return UIInterfaceOrientationMaskAll;
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
    NavigationController* nav = [[NavigationController alloc] initWithRootViewController:[LibraryViewController new]];
    nav.navigationBar.prefersLargeTitles = YES;
    self.window.rootViewController = nav;
    self.window.backgroundColor = AppBackground();
    [self.window makeKeyAndVisible];
}
@end

int main(int argc, char* argv[])
{
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(AppDelegate.class)); }
}
