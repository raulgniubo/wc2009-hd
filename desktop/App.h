// Dual HD for Mac and Windows: shared settings, paths and the controller/keyboard mapping.
#pragma once
#include <QString>
#include <QStringList>
#include <QSettings>
#include <QVariant>
#include <QKeySequence>
#include <QColor>
#include <QImage>

namespace app
{
// ---- settings (one QSettings store; keys match the phone apps where they mean the same thing)
QSettings& S();
QVariant Get(const QString& key);                 // with the default below when unset
void Set(const QString& key, const QVariant& v);  // stores it and tells everyone (Changed())
bool On(const QString& key);
int Int(const QString& key);

// sends the key of a changed setting to whoever listens (the game view, the library, …)
class Notifier : public QObject
{
    Q_OBJECT
public:
    static Notifier& I();
signals:
    void changed(const QString& key);
};

// ---- places
QString DataDir();        // settings-adjacent app data (box art cache, …)
QString FontsDir();       // Inter fonts shipped with the app
QStringList Folders();    // the games folders, in order
void SetFolders(const QStringList& f);

// ---- look modes for games with an HD pack (Yu-Gi-Oh! 5D's WC2009)
struct Mode { int Value; QString Name, Detail; };
const QList<Mode>& Modes();                       // in the order they're offered
QString ModeName(int m);

struct Pack
{
    QString Code, Title, Short, About, Folder;    // Folder: the pack folder's name next to the game
    QStringList Features;
};
const QList<Pack>& Packs();
const Pack* PackFor(const QString& gameCode);
// the pack folder for a game, if it's next to the ROM ("" if not)
QString PackFolderFor(const QString& romPath, const QString& gameCode);

// ---- DS buttons and keyboard
enum DsKey { K_A, K_B, K_X, K_Y, K_L, K_R, K_START, K_SELECT, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_COUNT };
unsigned DsMask(int dsKey);                       // WC09_KEY_* bit
QString DsName(int dsKey);
int KeyFor(int dsKey);                            // Qt::Key
void SetKeyFor(int dsKey, int qtKey);
void ResetKeys();

// ---- game controllers (SDL names them by position: A bottom, B right, X left, Y top)
enum PadButton { PB_A, PB_B, PB_X, PB_Y, PB_L1, PB_R1, PB_L2, PB_R2, PB_L3, PB_R3, PB_START, PB_BACK, PB_GUIDE, PB_COUNT };
enum PadAction { PA_NONE, PA_A, PA_B, PA_X, PA_Y, PA_L, PA_R, PA_START, PA_SELECT, PA_MENU, PA_FFWD, PA_COUNT };
QString PadButtonName(int b);
QString PadActionName(int a);
unsigned PadActionMask(int a);                    // WC09_KEY_* for DS actions, 0 otherwise
bool PadByPosition();                             // "Match DS positions" layout
int PadAction(int b, bool nintendoPad);
void SetPadAction(int b, int a);
void ResetPad();

// ---- hotkeys (editable)
enum Hotkey { HK_FULLSCREEN, HK_PAUSE, HK_FFWD_HOLD, HK_FFWD_TOGGLE, HK_RESET, HK_SCREENSHOT, HK_LIBRARY, HK_LOOK, HK_SWAP, HK_LAYOUT, HK_COUNT };
QString HotkeyName(int h);
QKeySequence HotkeySeq(int h);
void SetHotkey(int h, const QKeySequence& k);
void ResetHotkeys();

// ---- misc
QColor Accent();
QColor Bg();
QColor Card();
QImage AppIcon();
}
