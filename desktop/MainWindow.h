// The window: the library, the game, the menus and hotkeys, full screen and the pause menu.
#pragma once
#include <QMainWindow>
#include <QTimer>
#include <QPointer>

class Library;
class GameView;
class QStackedWidget;
class QAction;
class QActionGroup;
class QMenu;
class PauseMenu;
class SettingsDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    void openRom(const QString& path, bool fresh = false);

protected:
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void buildMenus();
    void applyHotkeys();
    void showLibrary();
    void toggleFullScreen();
    void togglePauseMenu();
    void openSettings(int page = 0, const QString& rom = {});
    void screenshot();
    void updateTitle();
    void updateMenus();
    void pollPads();
    void cycleLook();

    QStackedWidget* Stack;
    Library* Lib;
    GameView* Game;
    PauseMenu* Pause;
    QPointer<SettingsDialog> Settings;
    QTimer PadTimer;
    unsigned PadPrev = 0, DirPrev = 0;
    QString GameTitle;
    QMenu *GameMenu, *LookMenu, *SaveMenu, *LoadMenu;
    QAction *ActFull, *ActPause, *ActReset, *ActFfwd, *ActShot, *ActLibrary, *ActSwap, *ActLayoutNext, *ActLookNext, *ActPerf, *ActSmooth;
    QActionGroup *ScaleGroup, *LayoutGroup, *LookGroup;
};
