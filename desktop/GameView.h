// The game screen: runs the emulator on the OpenGL thread (the GUI thread), draws both DS screens
// in the chosen layout, and takes keyboard, mouse (touch screen) and controller input.
#pragma once
#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <QRect>
#include <QSet>

class GameView : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit GameView(QWidget* parent = nullptr);
    ~GameView() override;

    void start(const QString& romPath, bool fresh = false);   // runs as soon as OpenGL is ready
    void stop();                                               // resume point, save data, shut down
    bool running() const { return Running; }
    QString romPath() const { return Rom; }
    QString gameCode() const { return Code; }
    bool hdGame() const;

    void setPaused(bool p);
    bool paused() const { return Paused || MenuPause || AutoPause; }
    void setMenuPause(bool p);        // paused while the pause menu is open (not shown as "Paused")
    void setAutoPause(bool p);        // the window isn't active and "pause in the background" is on
    void setFastForward(bool on);
    bool fastForward() const { return FFwdToggle || FFwdHeld || PadFfwd; }
    bool saveState(int slot);
    bool loadState(int slot);
    void reset();
    QImage screenshot();
    void toast(const QString& text);
    void applySettings(const QString& key);
    QString perfLine() const;

signals:
    void startFailed(const QString& why);
    void started();
    void menuRequested();             // controller "menu" button or Esc
    void fullScreenToggled();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    bool focusNextPrevChild(bool) override { return false; }   // Tab is fast forward, not focus

private:
    bool doStart();
    void computeLayout(int fbW, int fbH);
    bool touchAt(const QPointF& p, int& x, int& y) const;
    void runFrames();
    unsigned padKeys();

    QString Rom, Code, PendingRom;
    bool PendingFresh = false;
    bool GLReady = false, Running = false, Paused = false, MenuPause = false, AutoPause = false;
    bool FFwdToggle = false, FFwdHeld = false, Grabbing = false;
    QTimer CursorTimer;
    QRect TopRect, BottomRect;        // device pixels
    QSet<int> KeysHeld;
    unsigned KeyMask = 0, PadPrev = 0;
    bool Touching = false, PadFfwd = false;
    // a click can be shorter than a frame (trackpad taps especially): the game only reads the touch
    // screen once per frame, so the release waits until the press has been seen for a few frames
    bool ReleasePending = false;
    int TouchFrames = 0;
    void stepFrame(unsigned keys);
    QElapsedTimer Clock;
    double Base = 0; long FramesRun = 0;
    QTimer Ticker;                    // drives frames when vsync is off
    QString Toast; qint64 ToastUntil = 0;
    qint64 HostShown = 0; double DrawSum = 0; int DrawCount = 0;
};
