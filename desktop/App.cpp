#include "App.h"
#include "wc09_core.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QHash>

namespace app
{
QSettings& S()
{
    static QSettings s(QSettings::IniFormat, QSettings::UserScope, "Dual HD", "Dual HD");
    return s;
}

static const QHash<QString, QVariant>& Defaults()
{
    static const QHash<QString, QVariant> d = {
        {"scale", 4}, {"filter", true}, {"smooth2d", true}, {"vsync", true}, {"layout", 0}, {"gap", 0},
        {"integer", false}, {"swap", false}, {"perf", false}, {"perfDetail", false}, {"hdMode", 2},
        {"resume", true}, {"sound", true}, {"volume", 100}, {"ffSpeed", 3}, {"language", 1}, {"pauseUnfocused", true},
        {"boxart", true}, {"padLayout", 0}, {"jit", true}, {"sort", "recent"}, {"menuBarFull", false},
    };
    return d;
}

QVariant Get(const QString& key) { return S().value(key, Defaults().value(key)); }
bool On(const QString& key) { return Get(key).toBool(); }
int Int(const QString& key) { return Get(key).toInt(); }
void Set(const QString& key, const QVariant& v)
{
    S().setValue(key, v);
    emit Notifier::I().changed(key);
}

Notifier& Notifier::I() { static Notifier n; return n; }

QString DataDir()
{
    QString d = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(d);
    return d;
}

QString FontsDir()
{
    QByteArray env = qgetenv("DUALHD_FONTS");
    if (!env.isEmpty()) return QString::fromUtf8(env);
    // the fonts for the HD text are built into the app: written out once for the emulator core
    QString dir = DataDir() + "/fonts";
    QDir().mkpath(dir);
    for (const char* f : {"Inter-Medium.ttf", "Inter-Bold.ttf"})
    {
        QString out = dir + "/" + f;
        QFile res(QString(":/fonts/") + f);
        if (!QFileInfo::exists(out) || QFileInfo(out).size() != res.size())
        {
            QFile::remove(out);
            res.copy(out);
            QFile::setPermissions(out, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser);
        }
    }
    return dir;
}

QStringList Folders() { return S().value("folders").toStringList(); }
void SetFolders(const QStringList& f) { S().setValue("folders", f); emit Notifier::I().changed("folders"); }

const QList<Mode>& Modes()
{
    static const QList<Mode> m = {
        {2, "HD", "Everything in high resolution, looking just like the original: cards, characters, backgrounds, 3D world. Recommended."},
        {3, "HD + Redesign", "HD plus a modern redesign: new life point panel, duel buttons and icons, menus, and card text in a clean font. Changes how the game looks."},
        {1, "HD Cards", "The original game with just the card art in HD. Lightest."},
        {0, "Original", "The game exactly as on the DS. Resolution and smoothing settings still apply."},
    };
    return m;
}
QString ModeName(int m) { for (const Mode& x : Modes()) if (x.Value == m) return x.Name; return "HD"; }

const QList<Pack>& Packs()
{
    static const QList<Pack> p = {{
        "CY8", "Yu-Gi-Oh! 5D's World Championship 2009", "Yu-Gi-Oh! 5D's WC2009",
        "An HD makeover for the card game, made just for it.", "WC2009-HD",
        {"HD art for every card, in duels and in the deck editor",
         "High-resolution characters, backgrounds, menus and 3D world",
         "Sharper game text, with the same look",
         "Optional redesign: new life point panel, duel buttons and icons, card text in a clean font"},
    }};
    return p;
}
const Pack* PackFor(const QString& code) { for (const Pack& p : Packs()) if (code.startsWith(p.Code)) return &p; return nullptr; }
QString PackFolderFor(const QString& romPath, const QString& code)
{
    const Pack* p = PackFor(code);
    if (!p) return {};
    QString dir = QFileInfo(romPath).absolutePath() + "/" + p->Folder;
    return QFileInfo::exists(dir + "/pack.json") ? dir : QString();
}

// ---- keyboard
unsigned DsMask(int k)
{
    static const unsigned m[K_COUNT] = {WC09_KEY_A, WC09_KEY_B, WC09_KEY_X, WC09_KEY_Y, WC09_KEY_L, WC09_KEY_R, WC09_KEY_START,
                                        WC09_KEY_SELECT, WC09_KEY_UP, WC09_KEY_DOWN, WC09_KEY_LEFT, WC09_KEY_RIGHT};
    return k >= 0 && k < K_COUNT ? m[k] : 0;
}
QString DsName(int k)
{
    static const char* n[K_COUNT] = {"A", "B", "X", "Y", "L", "R", "START", "SELECT", "Up", "Down", "Left", "Right"};
    return k >= 0 && k < K_COUNT ? n[k] : "";
}
static int DefaultKey(int k)
{
    static const int d[K_COUNT] = {Qt::Key_X, Qt::Key_Z, Qt::Key_S, Qt::Key_A, Qt::Key_Q, Qt::Key_W, Qt::Key_Return,
                                   Qt::Key_Backspace, Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right};
    return d[k];
}
int KeyFor(int k) { return S().value(QString("key/%1").arg(k), DefaultKey(k)).toInt(); }
void SetKeyFor(int k, int key)
{
    // a key does one thing: take it away from any other DS button
    for (int o = 0; o < K_COUNT; o++) if (o != k && KeyFor(o) == key) S().setValue(QString("key/%1").arg(o), 0);
    S().setValue(QString("key/%1").arg(k), key);
    emit Notifier::I().changed("keys");
}
void ResetKeys() { for (int k = 0; k < K_COUNT; k++) S().remove(QString("key/%1").arg(k)); emit Notifier::I().changed("keys"); }

// ---- controllers
QString PadButtonName(int b)
{
    static const char* n[PB_COUNT] = {"A (bottom)", "B (right)", "X (left)", "Y (top)", "L1 / LB", "R1 / RB", "L2 / LT", "R2 / RT",
                                      "Left stick click", "Right stick click", "Start / Menu", "Back / View / Share", "Home / Guide"};
    return b >= 0 && b < PB_COUNT ? n[b] : "";
}
QString PadActionName(int a)
{
    static const char* n[PA_COUNT] = {"Nothing", "A", "B", "X", "Y", "L", "R", "START", "SELECT", "Pause menu", "Fast forward (hold)"};
    return a >= 0 && a < PA_COUNT ? n[a] : "";
}
unsigned PadActionMask(int a)
{
    switch (a)
    {
    case PA_A: return WC09_KEY_A; case PA_B: return WC09_KEY_B; case PA_X: return WC09_KEY_X; case PA_Y: return WC09_KEY_Y;
    case PA_L: return WC09_KEY_L; case PA_R: return WC09_KEY_R; case PA_START: return WC09_KEY_START; case PA_SELECT: return WC09_KEY_SELECT;
    }
    return 0;
}
bool PadByPosition() { return Int("padLayout") == 1; }
static int PadDefault(int b, bool nintendo)
{
    // "Match letters": the action printed on the button. Nintendo pads have A on the right, so for
    // them that's the DS position too.
    bool pos = PadByPosition() || nintendo;
    switch (b)
    {
    case PB_A: return pos ? PA_B : PA_A;
    case PB_B: return pos ? PA_A : PA_B;
    case PB_X: return pos ? PA_Y : PA_X;
    case PB_Y: return pos ? PA_X : PA_Y;
    case PB_L1: case PB_L2: return PA_L;
    case PB_R1: case PB_R2: return PA_R;
    case PB_START: return PA_START;
    case PB_BACK: return PA_SELECT;
    case PB_GUIDE: case PB_L3: return PA_MENU;
    case PB_R3: return PA_FFWD;
    }
    return PA_NONE;
}
int PadAction(int b, bool nintendo)
{
    QVariant v = S().value(QString("pad/%1").arg(b));
    return v.isValid() ? v.toInt() : PadDefault(b, nintendo);
}
void SetPadAction(int b, int a) { S().setValue(QString("pad/%1").arg(b), a); emit Notifier::I().changed("pad"); }
void ResetPad() { for (int b = 0; b < PB_COUNT; b++) S().remove(QString("pad/%1").arg(b)); emit Notifier::I().changed("pad"); }

// ---- hotkeys
QString HotkeyName(int h)
{
    static const char* n[HK_COUNT] = {"Full screen", "Pause / resume", "Fast forward (hold)", "Fast forward (on / off)", "Reset game",
                                      "Screenshot", "Back to library", "Next look (HD games)", "Swap screens", "Next screen layout"};
    return h >= 0 && h < HK_COUNT ? n[h] : "";
}
static QKeySequence DefaultHotkey(int h)
{
    switch (h)
    {
#ifdef Q_OS_MACOS
    case HK_FULLSCREEN: return QKeySequence(Qt::CTRL | Qt::META | Qt::Key_F);
#else
    case HK_FULLSCREEN: return QKeySequence(Qt::Key_F11);
#endif
    case HK_PAUSE: return QKeySequence(Qt::Key_P);
    case HK_FFWD_HOLD: return QKeySequence(Qt::Key_Tab);
    case HK_FFWD_TOGGLE: return QKeySequence(Qt::SHIFT | Qt::Key_Tab);
    case HK_RESET: return QKeySequence(Qt::CTRL | Qt::Key_R);
    case HK_SCREENSHOT: return QKeySequence(Qt::Key_F12);
    case HK_LIBRARY: return QKeySequence(Qt::CTRL | Qt::Key_W);
    case HK_LOOK: return QKeySequence(Qt::CTRL | Qt::Key_L);
    case HK_SWAP: return QKeySequence(Qt::Key_F9);
    case HK_LAYOUT: return QKeySequence(Qt::Key_F10);
    }
    return {};
}
QKeySequence HotkeySeq(int h)
{
    QVariant v = S().value(QString("hotkey/%1").arg(h));
    return v.isValid() ? QKeySequence(v.toString()) : DefaultHotkey(h);
}
void SetHotkey(int h, const QKeySequence& k) { S().setValue(QString("hotkey/%1").arg(h), k.toString()); emit Notifier::I().changed("hotkeys"); }
void ResetHotkeys() { for (int h = 0; h < HK_COUNT; h++) S().remove(QString("hotkey/%1").arg(h)); emit Notifier::I().changed("hotkeys"); }

QColor Accent() { return QColor(0x4A, 0xA3, 0xFF); }
QColor Bg() { return QColor(0x0A, 0x0E, 0x16); }
QColor Card() { return QColor(0x15, 0x1B, 0x26); }
QImage AppIcon() { return QImage(":/icon.png"); }
}
