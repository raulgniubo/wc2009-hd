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
// Layout (points): portrait = screens stacked, controls below; landscape = side by side.

struct Layout
{
    CGRect top, bottom, l, r, start, select, menu, ffwd;
    CGFloat dpadX, dpadY, dpadR, abxyX, abxyY, abxyR, btnR;

    void compute(CGSize size, UIEdgeInsets safe, bool controls)
    {
        CGFloat w = size.width, h = size.height;
        if (w <= h)
        {
            CGFloat y0 = safe.top;
            CGFloat minCtrl = controls ? 230 : 0;
            CGFloat sw = w, sh = sw * 0.75;
            CGFloat avail = h - y0 - safe.bottom;
            if (sh * 2 > avail - minCtrl) { sh = MAX((avail - minCtrl) / 2, avail * 0.3); sw = sh / 0.75; }
            CGFloat x0 = (w - sw) / 2;
            top = CGRectMake(x0, y0, sw, sh);
            bottom = CGRectMake(x0, y0 + sh, sw, sh);
            CGFloat areaTop = MIN(CGRectGetMaxY(bottom), h - safe.bottom - minCtrl);
            CGFloat areaH = h - safe.bottom - areaTop;
            CGFloat side = MIN(w, areaH * 2.2);
            CGFloat pad = 18;
            dpadR = MIN(72.0, MIN(areaH * 0.36, side * 0.2));
            btnR = dpadR * 0.36; abxyR = dpadR * 0.88;
            CGFloat cy = areaTop + areaH * 0.52;
            dpadX = pad + dpadR; dpadY = cy;
            abxyX = w - pad - abxyR - btnR; abxyY = cy;
            CGFloat lrW = 86, lrH = 34, lrY = areaTop + 10;
            l = CGRectMake(pad, lrY, lrW, lrH);
            r = CGRectMake(w - pad - lrW, lrY, lrW, lrH);
            CGFloat pillW = 64, pillH = 26, py = h - safe.bottom - pad - pillH;
            select = CGRectMake(w / 2 - 8 - pillW, py, pillW, pillH);
            start = CGRectMake(w / 2 + 8, py, pillW, pillH);
            CGFloat mb = 40, my = areaTop + 10;
            menu = CGRectMake(w / 2 + 6, my, mb, mb);
            ffwd = CGRectMake(w / 2 - 6 - mb, my, mb, mb);
        }
        else
        {
            CGFloat left = MAX(safe.left, 0), right = MAX(safe.right, 0);
            CGFloat margin = controls ? MIN(150.0, w * 0.17) : 0;
            CGFloat availW = w - left - right - 2 * margin;
            CGFloat availH = h - safe.top - safe.bottom;
            CGFloat sw = MIN(availW / 2, availH / 0.75), sh = sw * 0.75;
            CGFloat cx = left + (w - left - right) / 2, y0 = safe.top + (availH - sh) / 2;
            top = CGRectMake(cx - sw, y0, sw, sh);
            bottom = CGRectMake(cx, y0, sw, sh);
            CGFloat m = MAX(margin, 120.0), pad = 14;
            dpadR = MIN(66.0, (m - pad) * 0.5);
            btnR = dpadR * 0.36; abxyR = dpadR * 0.88;
            dpadX = left + pad + dpadR + 4; dpadY = h * 0.55;
            abxyX = w - right - pad - abxyR - btnR; abxyY = h * 0.55;
            CGFloat lrW = MIN(86.0, m - 2 * pad), lrH = 34, t = safe.top + pad;
            l = CGRectMake(left + pad, t, lrW, lrH);
            r = CGRectMake(w - right - pad - lrW, t, lrW, lrH);
            CGFloat pillW = MIN(64.0, m - 2 * pad), pillH = 26, b = h - safe.bottom - pad - pillH;
            select = CGRectMake(left + pad, b, pillW, pillH);
            start = CGRectMake(w - right - pad - pillW, b, pillW, pillH);
            CGFloat mb = 36, my = CGRectGetMaxY(l) + 12;
            menu = CGRectMake(w - right - pad - mb, controls ? my : t, mb, mb);
            ffwd = CGRectMake(left + pad, my, mb, mb);
        }
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
        _showButtons = YES; _opacity = 0.55;
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
        if (CGRectContainsPoint(_layout->menu, p)) { [_delegate controlsMenu]; continue; }
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
    if (k & ~_lastKeys) [_haptic impactOccurred];
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

    // menu button
    [self styleFill:NO alpha:MAX(a, 0.55) ctx:c];
    UIBezierPath* mp = [UIBezierPath bezierPathWithRoundedRect:L.menu cornerRadius:10];
    [mp fill]; [mp stroke];
    CGContextSetRGBStrokeColor(c, 1, 1, 1, 0.9); CGContextSetLineWidth(c, 2.5); CGContextSetLineCap(c, kCGLineCapRound);
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

@interface GameViewController : GLKViewController <ControlsDelegate>
@end

@implementation GameViewController
{
    EAGLContext* _ctx;
    ControlsView* _controls;
    UIView* _setupView;
    Layout _layout;
    BOOL _started, _paused, _ffwd;
    CFTimeInterval _base;
    long _framesRun;
    AVAudioEngine* _engine;
    AVAudioSourceNode* _source;
    NSString* _gameDir;
}

- (NSUserDefaults*)prefs { return NSUserDefaults.standardUserDefaults; }
- (int)scale { NSInteger s = [self.prefs integerForKey:@"scale"]; return s > 0 ? (int)s : 4; }
- (BOOL)buttonsOn { return [self.prefs objectForKey:@"buttons"] ? [self.prefs boolForKey:@"buttons"] : YES; }
- (CGFloat)opacity { return [self.prefs objectForKey:@"opacity"] ? [self.prefs floatForKey:@"opacity"] : 0.55; }

- (void)viewDidLoad
{
    [super viewDidLoad];
    _ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    GLKView* v = (GLKView*)self.view;
    v.context = _ctx;
    v.drawableColorFormat = GLKViewDrawableColorFormatRGBA8888;
    v.drawableDepthFormat = GLKViewDrawableDepthFormatNone;
    v.contentScaleFactor = UIScreen.mainScreen.nativeScale;
    v.backgroundColor = UIColor.blackColor;
    self.preferredFramesPerSecond = 60;
    [EAGLContext setCurrentContext:_ctx];

    _controls = [[ControlsView alloc] initWithFrame:self.view.bounds];
    _controls.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _controls.delegate = self;
    _controls.layout = &_layout;
    _controls.showButtons = self.buttonsOn;
    _controls.opacity = self.opacity;
    [self.view addSubview:_controls];

    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(willResign) name:UIApplicationWillResignActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(didBecomeActive) name:UIApplicationDidBecomeActiveNotification object:nil];
    [self setupAudio];
    [self tryStart];
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    _layout.compute(self.view.bounds.size, self.view.safeAreaInsets, self.buttonsOn);
    [_controls setNeedsDisplay];
}

// ---- finding the game: Documents, or one folder inside it ----

static NSString* FindRomIn(NSString* dir)
{
    for (NSString* f in [NSFileManager.defaultManager contentsOfDirectoryAtPath:dir error:nil])
        if ([f.pathExtension.lowercaseString isEqualToString:@"nds"] && ![f hasPrefix:@"."]) return f;
    return nil;
}

- (NSString*)findGameDir
{
    NSString* docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    if (FindRomIn(docs)) return docs;
    for (NSString* f in [NSFileManager.defaultManager contentsOfDirectoryAtPath:docs error:nil])
    {
        NSString* p = [docs stringByAppendingPathComponent:f];
        BOOL isDir = NO;
        if ([NSFileManager.defaultManager fileExistsAtPath:p isDirectory:&isDir] && isDir && FindRomIn(p)) return p;
    }
    return nil;
}

- (void)tryStart
{
    _gameDir = [self findGameDir];
    if (!_gameDir) { [self showSetup]; return; }
    [_setupView removeFromSuperview]; _setupView = nil;

    [EAGLContext setCurrentContext:_ctx];
    NSString* data = [NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"wc09"];
    [NSFileManager.defaultManager createDirectoryAtPath:data withIntermediateDirectories:YES attributes:nil error:nil];
    wc09_init(_gameDir.fileSystemRepresentation, data.fileSystemRepresentation, NSBundle.mainBundle.resourcePath.fileSystemRepresentation);
    const char* rom = wc09_find_rom();
    if (!rom[0] || wc09_load_rom(rom, self.scale, 0) != 0)
    {
        [self alert:@"Couldn't start" message:[NSString stringWithFormat:@"Couldn't load the game in %@", _gameDir.lastPathComponent]];
        return;
    }
    _started = YES;
    if (wc09_state_exists(kAutoSlot) && wc09_load_state(kAutoSlot) == 0) [self toast:@"Resumed where you left off"];
    _base = 0;
    [_engine startAndReturnError:nil];
}

- (void)showSetup
{
    if (_setupView) return;
    UIScrollView* sv = [[UIScrollView alloc] initWithFrame:self.view.bounds];
    sv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    sv.backgroundColor = UIColor.blackColor;
    UIStackView* st = [[UIStackView alloc] init];
    st.axis = UILayoutConstraintAxisVertical; st.spacing = 18; st.alignment = UIStackViewAlignmentFill;
    st.translatesAutoresizingMaskIntoConstraints = NO;
    UILabel* title = [[UILabel alloc] init];
    title.text = @"Yu-Gi-Oh! 5D's WC2009 HD";
    title.font = [UIFont systemFontOfSize:24 weight:UIFontWeightBold];
    title.textColor = UIColor.whiteColor; title.textAlignment = NSTextAlignmentCenter; title.numberOfLines = 0;
    UILabel* body = [[UILabel alloc] init];
    body.numberOfLines = 0;
    body.textColor = [UIColor colorWithWhite:0.8 alpha:1];
    body.font = [UIFont systemFontOfSize:16];
    body.text = @"Add the game to this app:\n\n"
                 "From a Mac: connect the iPhone, open Finder, click the iPhone, open the Files tab and drag the game folder "
                 "(the .nds file and the HDCards folder) onto WC2009 HD.\n\n"
                 "Or on the iPhone: Files app > On My iPhone > WC2009 HD, and put them there.\n\n"
                 "In-game saves and save states go to a Saves folder next to the game.";
    UIButton* b = [UIButton buttonWithType:UIButtonTypeSystem];
    [b setTitle:@"Check again" forState:UIControlStateNormal];
    b.titleLabel.font = [UIFont systemFontOfSize:18 weight:UIFontWeightSemibold];
    [b addTarget:self action:@selector(tryStart) forControlEvents:UIControlEventTouchUpInside];
    [st addArrangedSubview:title]; [st addArrangedSubview:body]; [st addArrangedSubview:b];
    [sv addSubview:st];
    [NSLayoutConstraint activateConstraints:@[
        [st.leadingAnchor constraintEqualToAnchor:sv.frameLayoutGuide.leadingAnchor constant:28],
        [st.trailingAnchor constraintEqualToAnchor:sv.frameLayoutGuide.trailingAnchor constant:-28],
        [st.topAnchor constraintEqualToAnchor:sv.contentLayoutGuide.topAnchor constant:60],
        [st.bottomAnchor constraintEqualToAnchor:sv.contentLayoutGuide.bottomAnchor constant:-28],
    ]];
    [self.view addSubview:sv];
    _setupView = sv;
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
        float* L = (float*)abl->mBuffers[0].mData;
        float* R = abl->mNumberBuffers > 1 ? (float*)abl->mBuffers[1].mData : nullptr;
        for (int i = 0; i < (int)frames; i++)
        {
            if (i < got) { last[0] = tmp[i * 2] / 32768.f; last[1] = tmp[i * 2 + 1] / 32768.f; }
            else { last[0] *= 0.94f; last[1] *= 0.94f; }
            L[i] = last[0];
            if (R) R[i] = last[1];
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
    if (_ffwd) run = 3;
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
    wc09_save_state(kAutoSlot);
    wc09_flush_save();
    [_engine pause];
}

- (void)didBecomeActive
{
    if (!_started) { [self tryStart]; return; }
    _base = 0;
    [_engine startAndReturnError:nil];
}

// ---- controllers ----

- (void)pollControllers
{
    GCController* c = GCController.controllers.firstObject;
    GCExtendedGamepad* g = c.extendedGamepad;
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
    if (y > 0.5f) k |= WC09_KEY_UP;     // GameController: up is positive
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
    UIAlertController* m = [UIAlertController alertControllerWithTitle:@"WC2009 HD" message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    __weak GameViewController* me = self;
    [m addAction:[UIAlertAction actionWithTitle:@"Save state…" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { [me stateMenu:YES]; }]];
    [m addAction:[UIAlertAction actionWithTitle:@"Load state…" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { [me stateMenu:NO]; }]];
    [m addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"Resolution: %dx", self.scale] style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { [me scaleMenu]; }]];
    [m addAction:[UIAlertAction actionWithTitle:(self.buttonsOn ? @"Hide buttons" : @"Show buttons") style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { [me toggleButtons]; }]];
    [m addAction:[UIAlertAction actionWithTitle:@"Button opacity" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) { [me opacityMenu]; }]];
    [m addAction:[UIAlertAction actionWithTitle:@"Restart from in-game save" style:UIAlertActionStyleDestructive handler:^(UIAlertAction*) { [me restartAsk]; }]];
    [m addAction:[UIAlertAction actionWithTitle:@"Back to the game" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { [me resume]; }]];
    [self present:m];
}

- (void)present:(UIAlertController*)a
{
    if (a.popoverPresentationController)   // iPad
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
        NSString* t = [NSString stringWithFormat:@"Slot %d%@", i, wc09_state_exists(i) ? @"" : @"  (empty)"];
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

- (void)scaleMenu
{
    UIAlertController* m = [UIAlertController alertControllerWithTitle:@"Resolution" message:@"Higher is sharper but needs a faster iPhone." preferredStyle:UIAlertControllerStyleActionSheet];
    __weak GameViewController* me = self;
    int scales[] = {2, 3, 4, 5, 6, 8};
    for (int s : scales)
    {
        NSString* t = [NSString stringWithFormat:@"%dx%@%@", s, s == 4 ? @" (recommended)" : @"", s == self.scale ? @"  ✓" : @""];
        [m addAction:[UIAlertAction actionWithTitle:t style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
            GameViewController* g = me;
            if (!g) return;
            [g.prefs setInteger:s forKey:@"scale"];
            [EAGLContext setCurrentContext:g->_ctx];
            wc09_set_scale(s);
            [g resume];
        }]];
    }
    [m addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { [me resume]; }]];
    [self present:m];
}

- (void)toggleButtons
{
    BOOL on = !self.buttonsOn;
    [self.prefs setBool:on forKey:@"buttons"];
    _controls.showButtons = on;
    [self.view setNeedsLayout];
    [self resume];
}

- (void)opacityMenu
{
    UIAlertController* m = [UIAlertController alertControllerWithTitle:@"Button opacity" message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    __weak GameViewController* me = self;
    float vals[] = {0.25f, 0.4f, 0.55f, 0.75f, 0.95f};
    for (float v : vals)
        [m addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"%d%%", (int)lroundf(v * 100)] style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
            GameViewController* g = me;
            if (!g) return;
            [g.prefs setFloat:v forKey:@"opacity"];
            g->_controls.opacity = v;
            [g->_controls setNeedsDisplay];
            [g resume];
        }]];
    [m addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { [me resume]; }]];
    [self present:m];
}

- (void)restartAsk
{
    UIAlertController* a = [UIAlertController alertControllerWithTitle:@"Restart the game?"
        message:@"Starts again from your last in-game save. Anything after that save is lost (save states stay)."
        preferredStyle:UIAlertControllerStyleAlert];
    __weak GameViewController* me = self;
    [a addAction:[UIAlertAction actionWithTitle:@"Restart" style:UIAlertActionStyleDestructive handler:^(UIAlertAction*) {
        GameViewController* g = me;
        if (!g) return;
        [EAGLContext setCurrentContext:g->_ctx];
        wc09_delete_state(kAutoSlot);
        const char* rom = wc09_find_rom();
        if (rom[0]) wc09_load_rom(rom, g.scale, 0);
        [g resume];
    }]];
    [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) { [me resume]; }]];
    [self presentViewController:a animated:YES completion:nil];
}

- (void)alert:(NSString*)title message:(NSString*)msg
{
    UIAlertController* a = [UIAlertController alertControllerWithTitle:title message:msg preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    dispatch_async(dispatch_get_main_queue(), ^{ [self presentViewController:a animated:YES completion:nil]; });
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

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow* window;
@end

@implementation AppDelegate
- (BOOL)application:(UIApplication*)app didFinishLaunchingWithOptions:(NSDictionary*)opts
{
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[GameViewController alloc] init];
    self.window.backgroundColor = UIColor.blackColor;
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char* argv[])
{
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(AppDelegate.class)); }
}
