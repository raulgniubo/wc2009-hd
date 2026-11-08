#include "GameGL.h"
#include "GameView.h"
#include "App.h"
#include "Pads.h"
#include "wc09_core.h"
#include <QOpenGLContext>
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QGuiApplication>
#include <cmath>

static const double kDSFps = 59.8261;
static const int kResumeSlot = 0;

GameView::GameView(QWidget* parent) : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(false);
    Clock.start();
    Ticker.setTimerType(Qt::PreciseTimer);
    Ticker.setInterval(1);
    connect(&Ticker, &QTimer::timeout, this, [this] { if (Running || !PendingRom.isEmpty()) update(); });
    // with vsync the display paces the frames: draw again as soon as the last one is shown
    connect(this, &QOpenGLWidget::frameSwapped, this, [this] { if ((Running || !PendingRom.isEmpty()) && app::On("vsync")) update(); });
    CursorTimer.setSingleShot(true);
    CursorTimer.setInterval(2500);
    connect(&CursorTimer, &QTimer::timeout, this, [this] { if (Running) setCursor(Qt::BlankCursor); });
}

GameView::~GameView()
{
    stop();
}

bool GameView::hdGame() const { return Running && wc09_is_hd_game(); }

void GameView::start(const QString& romPath, bool fresh)
{
    if (Running) stop();
    PendingRom = romPath;
    PendingFresh = fresh;
    if (!app::On("vsync")) Ticker.start();
    update();
}

void GameView::initializeGL()
{
    gamegl::Load([](const char* name) -> void* {
        return (void*)QOpenGLContext::currentContext()->getProcAddress(name);
    });
    GLReady = true;
}

static void ApplyCoreOptions()
{
    wc09_set_option(WC09_OPT_HD_CARDS, 1);
    wc09_set_option(WC09_OPT_HD_TEXT, 1);
    wc09_set_option(WC09_OPT_SMOOTH_2D, app::On("smooth2d") ? 1 : 0);
    wc09_set_option(WC09_OPT_LINEAR_FILTER, app::On("filter") ? 1 : 0);
    wc09_set_option(WC09_OPT_HD_MODE, app::Int("hdMode"));
}

bool GameView::doStart()
{
    QFileInfo fi(Rom);
    QString dataDir = app::DataDir() + "/core";
    QDir().mkpath(dataDir);
    wc09_init(QDir::fromNativeSeparators(fi.absolutePath()).toUtf8().constData(), dataDir.toUtf8().constData(), app::FontsDir().toUtf8().constData());
    ApplyCoreOptions();
    if (wc09_load_rom(QDir::fromNativeSeparators(fi.absoluteFilePath()).toUtf8().constData(), app::Int("scale"), app::On("jit") ? 1 : 0) != 0)
    {
        emit startFailed(QString("Couldn't start %1.").arg(fi.fileName()));
        return false;
    }
    char code[5] = {0};
    wc09_rom_code(QDir::fromNativeSeparators(fi.absoluteFilePath()).toUtf8().constData(), code);
    Code = QString::fromLatin1(code);
    ApplyCoreOptions();
    if (!PendingFresh && app::On("resume") && wc09_state_exists(kResumeSlot)) wc09_load_state(kResumeSlot);
    audio::Start();
    audio::SetVolume(app::Int("volume"));
    audio::SetMuted(!app::On("sound"));
    Running = true;
    Paused = false; FFwdToggle = false; FFwdHeld = false; PadFfwd = false;
    KeyMask = 0; KeysHeld.clear(); PadPrev = ~0u;   // a button still held from the library doesn't count
    Base = 0; FramesRun = 0;
    int soft = 0;
    wc09_renderer_report(&soft);
    if (soft) toast("OpenGL couldn't start: running with the slower software renderer");
    CursorTimer.start();
    emit started();
    return true;
}

void GameView::stop()
{
    Ticker.stop();
    PendingRom.clear();
    if (!Running) return;
    makeCurrent();
    if (app::On("resume")) wc09_save_state(kResumeSlot); else wc09_delete_state(kResumeSlot);
    wc09_flush_save();
    audio::Stop();
    wc09_shutdown();
    doneCurrent();
    Running = false;
    Rom.clear(); Code.clear();
    unsetCursor();
    update();
}

void GameView::setPaused(bool p) { Paused = p; Base = 0; if (Running) toast(p ? "Paused" : ""); update(); }
void GameView::setMenuPause(bool p) { MenuPause = p; Base = 0; update(); }
void GameView::setAutoPause(bool p) { AutoPause = p; Base = 0; update(); }
void GameView::setFastForward(bool on) { FFwdToggle = on; Base = 0; }

bool GameView::saveState(int slot)
{
    if (!Running) return false;
    makeCurrent();
    bool ok = wc09_save_state(slot) == 0;
    toast(ok ? QString("Saved to slot %1").arg(slot) : QString("Couldn't save to slot %1").arg(slot));
    return ok;
}

bool GameView::loadState(int slot)
{
    if (!Running) return false;
    if (!wc09_state_exists(slot)) { toast(QString("Slot %1 is empty").arg(slot)); return false; }
    makeCurrent();
    bool ok = wc09_load_state(slot) == 0;
    toast(ok ? QString("Loaded slot %1").arg(slot) : QString("Couldn't load slot %1").arg(slot));
    Base = 0;
    return ok;
}

void GameView::reset()
{
    if (!Running) return;
    QString rom = Rom;
    makeCurrent();
    wc09_flush_save();
    audio::Stop();
    wc09_shutdown();
    doneCurrent();
    Running = false;
    start(rom, true);
    toast("Game reset");
}

QImage GameView::screenshot()
{
    Grabbing = true;
    QImage img = grabFramebuffer();
    Grabbing = false;
    QRect r = TopRect.united(BottomRect);
    return r.isValid() ? img.copy(r) : img;
}

void GameView::toast(const QString& t) { Toast = t; ToastUntil = Clock.elapsed() + (t.isEmpty() ? 0 : 1800); update(); }

void GameView::applySettings(const QString& key)
{
    if (!Running) return;
    makeCurrent();
    if (key == "scale") wc09_set_scale(app::Int("scale"));
    else if (key == "smooth2d" || key == "filter" || key == "hdMode") ApplyCoreOptions();
    else if (key == "volume") audio::SetVolume(app::Int("volume"));
    else if (key == "sound") audio::SetMuted(!app::On("sound"));
    else if (key == "vsync") { if (app::On("vsync")) Ticker.stop(); else Ticker.start(); }
    update();
}

QString GameView::perfLine() const { return Running ? QString::fromUtf8(wc09_perf_text()) : QString(); }

// ---- layout

void GameView::computeLayout(int W, int H)
{
    int layout = app::Int("layout"), gap = std::max(0, app::Int("gap"));
    bool swap = app::On("swap");
    // content size in DS pixels and where the two screens sit in it
    int cw = 256, ch = 192;
    QRect a, b;   // a: top screen, b: bottom screen (before swapping)
    switch (layout)
    {
    case 1: cw = 512 + gap; ch = 192; a = QRect(0, 0, 256, 192); b = QRect(256 + gap, 0, 256, 192); break;     // side by side
    case 2: a = QRect(0, 0, 256, 192); break;                                                                  // top only
    case 3: b = QRect(0, 0, 256, 192); break;                                                                  // bottom only
    case 4: cw = 768 + gap; ch = 384; a = QRect(0, 0, 512, 384); b = QRect(512 + gap, 192, 256, 192); break;     // hybrid
    default: cw = 256; ch = 384 + gap; a = QRect(0, 0, 256, 192); b = QRect(0, 192 + gap, 256, 192); break;     // vertical
    }
    if (swap && layout != 2 && layout != 3)
    {
        if (layout == 4) { QRect t = a; a = QRect(512 + gap, 192, 256, 192); b = QRect(0, 0, 512, 384); (void)t; }
        else std::swap(a, b);
    }
    else if (swap && layout == 2) { b = a; a = QRect(); }
    else if (swap && layout == 3) { a = b; b = QRect(); }
    double k = std::min((double)W / cw, (double)H / ch);
    if (app::On("integer") && k >= 1) k = std::floor(k);
    int ox = (int)std::round((W - cw * k) / 2), oy = (int)std::round((H - ch * k) / 2);
    auto map = [&](const QRect& r) {
        if (!r.isValid()) return QRect();
        return QRect(ox + (int)std::round(r.x() * k), oy + (int)std::round(r.y() * k), (int)std::round(r.width() * k), (int)std::round(r.height() * k));
    };
    TopRect = map(a); BottomRect = map(b);
}

void GameView::resizeGL(int, int) { update(); }

// ---- frames

unsigned GameView::padKeys()
{
    pads::State s = pads::Poll();
    unsigned held = s.Held;
    unsigned pressed = held & ~PadPrev;
    if (PadPrev == ~0u) pressed = 0;
    PadPrev = held;
    unsigned k = s.Dirs;
    bool ffwd = false;
    for (int b = 0; b < app::PB_COUNT; b++)
    {
        if (!(held & (1u << b))) continue;
        int a = app::PadAction(b, s.Nintendo);
        k |= app::PadActionMask(a);
        if (a == app::PA_FFWD) ffwd = true;
        if (a == app::PA_MENU && (pressed & (1u << b))) QMetaObject::invokeMethod(this, "menuRequested", Qt::QueuedConnection);
    }
    if (ffwd != PadFfwd) { PadFfwd = ffwd; Base = 0; }
    return k;
}

void GameView::runFrames()
{
    unsigned keys = KeyMask | padKeys();
    int run = 0;
    double now = Clock.nsecsElapsed() / 1e9;
    if (paused()) { Base = 0; return; }
    int speed = app::Int("ffSpeed");
    if (fastForward() && speed == 0)
    {
        // as fast as it goes: frames until this display refresh's time is used up
        qint64 until = Clock.nsecsElapsed() + 12000000;
        int n = 0;
        do { wc09_set_keys(keys); wc09_run_frame(); n++; } while (Clock.nsecsElapsed() < until && n < 12);
        Base = 0;
        return;
    }
    double rate = fastForward() ? std::max(2, speed) : 1;
    if (Base == 0) { Base = now; FramesRun = 0; }
    long due = (long)((now - Base) * kDSFps * rate) + 1 - FramesRun;
    if (due > 4 * rate) { Base = now; FramesRun = 0; due = 1; }   // fell behind (window dragged…): don't race to catch up
    run = (int)std::max(0L, due);
    if (rate == 1)
    {
        // keep the sound buffer around 30-90 ms
        int q = wc09_audio_queued();
        if (run == 0 && q < 48000 * 30 / 1000) run = 1;
        else if (run == 2 && q > 48000 * 90 / 1000) run = 1;
    }
    FramesRun += run;
    for (int i = 0; i < run; i++) { wc09_set_keys(keys); wc09_run_frame(); }
}

void GameView::paintGL()
{
    if (!GLReady) return;
    if (!PendingRom.isEmpty())
    {
        Rom = PendingRom;
        PendingRom.clear();
        if (!doStart()) { Ticker.stop(); Rom.clear(); }
    }
    const qreal dpr = devicePixelRatioF();
    const int W = (int)std::round(width() * dpr), H = (int)std::round(height() * dpr);
    if (Running) runFrames();
    qint64 d0 = Clock.nsecsElapsed();
    gamegl::Clear(defaultFramebufferObject(), W, H);
    if (!Running) return;
    computeLayout(W, H);
    if (TopRect.isValid()) wc09_draw_screen(0, TopRect.x(), TopRect.y(), TopRect.width(), TopRect.height(), W, H);
    if (BottomRect.isValid()) wc09_draw_screen(1, BottomRect.x(), BottomRect.y(), BottomRect.width(), BottomRect.height(), W, H);
    gamegl::Bind(defaultFramebufferObject());
    DrawSum += (Clock.nsecsElapsed() - d0) / 1e6; DrawCount++;

    // the app's side for the debug report
    qint64 ms = Clock.elapsed();
    if (ms - HostShown > 1000)
    {
        QScreen* sc = screen();
        wc09_set_host_info(0, 0, DrawCount ? (float)(DrawSum / DrawCount) : 0.f, sc ? (int)std::lround(sc->refreshRate()) : 60);
        HostShown = ms; DrawSum = 0; DrawCount = 0;
    }

    if (Grabbing) return;
    bool showPerf = app::On("perf"), showToast = ms < ToastUntil && !Toast.isEmpty(), showPause = Paused;
    if (!showPerf && !showToast && !showPause) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();
    auto pill = [&](const QString& text, QPointF at, bool center, int px) {
        f.setPixelSize(px); f.setWeight(QFont::DemiBold);
        p.setFont(f);
        QRectF tr = p.fontMetrics().boundingRect(text);
        QRectF r(0, 0, std::max(tr.width() + 16, 30.0), tr.height() + 6);
        r.moveTopLeft(center ? QPointF(at.x() - r.width() / 2, at.y()) : at);
        QPainterPath path; path.addRoundedRect(r, 6, 6);
        p.fillPath(path, QColor(0, 0, 0, 150));
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, text);
    };
    if (showPerf)
        pill(app::On("perfDetail") ? perfLine() : QString::number(wc09_fps()), QPointF(12, 12), false, 12);
    if (showPause) pill("Paused", QPointF(width() / 2.0, height() / 2.0 - 16), true, 16);
    if (showToast) pill(Toast, QPointF(width() / 2.0, height() - 56), true, 14);
}

// ---- input

void GameView::keyPressEvent(QKeyEvent* e)
{
    if (!Running) { QOpenGLWidget::keyPressEvent(e); return; }
    if (e->key() == Qt::Key_Escape) { emit menuRequested(); return; }
    if (e->isAutoRepeat()) return;
    QKeySequence hold = app::HotkeySeq(app::HK_FFWD_HOLD);
    if (!hold.isEmpty() && hold[0].key() == (Qt::Key)e->key() && hold[0].keyboardModifiers() == e->modifiers()) { FFwdHeld = true; Base = 0; return; }
    bool used = false;
    for (int k = 0; k < app::K_COUNT; k++)
        if (app::KeyFor(k) == e->key()) { KeyMask |= app::DsMask(k); used = true; }
    if (used) { KeysHeld.insert(e->key()); return; }
    QOpenGLWidget::keyPressEvent(e);
}

void GameView::keyReleaseEvent(QKeyEvent* e)
{
    if (e->isAutoRepeat()) return;
    QKeySequence hold = app::HotkeySeq(app::HK_FFWD_HOLD);
    if (!hold.isEmpty() && hold[0].key() == (Qt::Key)e->key()) { FFwdHeld = false; Base = 0; }
    for (int k = 0; k < app::K_COUNT; k++)
        if (app::KeyFor(k) == e->key()) KeyMask &= ~app::DsMask(k);
    KeysHeld.remove(e->key());
    QOpenGLWidget::keyReleaseEvent(e);
}

void GameView::focusOutEvent(QFocusEvent* e)
{
    // keys let go while another window had focus would otherwise stay "held"
    KeyMask = 0; KeysHeld.clear(); FFwdHeld = false;
    if (Touching) { wc09_touch(0, 0, 0); Touching = false; }
    QOpenGLWidget::focusOutEvent(e);
}

bool GameView::touchAt(const QPointF& p, int& x, int& y) const
{
    if (!BottomRect.isValid()) return false;
    const qreal dpr = devicePixelRatioF();
    double px = p.x() * dpr, py = p.y() * dpr;
    x = (int)std::floor((px - BottomRect.x()) * 256.0 / BottomRect.width());
    y = (int)std::floor((py - BottomRect.y()) * 192.0 / BottomRect.height());
    return x >= 0 && x < 256 && y >= 0 && y < 192;
}

void GameView::mousePressEvent(QMouseEvent* e)
{
    unsetCursor(); CursorTimer.start();
    int x, y;
    if (Running && e->button() == Qt::LeftButton && touchAt(e->position(), x, y)) { wc09_touch(1, x, y); Touching = true; }
}

void GameView::mouseMoveEvent(QMouseEvent* e)
{
    unsetCursor(); CursorTimer.start();
    if (!Touching) return;
    int x, y;
    touchAt(e->position(), x, y);
    wc09_touch(1, std::clamp(x, 0, 255), std::clamp(y, 0, 191));
}

void GameView::mouseReleaseEvent(QMouseEvent* e)
{
    if (Touching && e->button() == Qt::LeftButton) { wc09_touch(0, 0, 0); Touching = false; }
}

void GameView::mouseDoubleClickEvent(QMouseEvent* e)
{
    // double-click outside the touch screen: full screen (inside it, it's a double tap for the game)
    int x, y;
    if (Running && e->button() == Qt::LeftButton && !touchAt(e->position(), x, y)) { emit fullScreenToggled(); return; }
    mousePressEvent(e);
}
