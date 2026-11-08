#include "MainWindow.h"
#include "App.h"
#include "Library.h"
#include "GameView.h"
#include "PauseMenu.h"
#include "SettingsDialog.h"
#include "Pads.h"
#include "wc09_core.h"
#include <QStackedWidget>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QClipboard>
#include <QKeyEvent>
#include <QWindow>
#include <QRegularExpression>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QUrl>

MainWindow::MainWindow()
{
    setWindowTitle("Dual HD");
    setWindowIcon(QIcon(QPixmap::fromImage(app::AppIcon())));
    setMinimumSize(520, 420);
    QPalette pal = palette(); pal.setColor(QPalette::Window, app::Bg()); setPalette(pal);

    setAcceptDrops(true);
    Stack = new QStackedWidget;
    setCentralWidget(Stack);
    Lib = new Library;
    Game = new GameView;
    Stack->addWidget(Lib);
    Stack->addWidget(Game);
    Pause = new PauseMenu(Game);

    connect(Lib, &Library::play, this, [this](const QString& p, bool fresh) { openRom(p, fresh); });
    connect(Lib, &Library::openSettings, this, [this] { openSettings(); });
    connect(Lib, &Library::openGamePage, this, [this](const QString& rom) { openSettings(SettingsDialog::Enhancements, rom); });
    connect(Game, &GameView::startFailed, this, [this](const QString& why) {
        showLibrary();
        QMessageBox::warning(this, "Dual HD", why + "\n\nCheck that the file is a DS game (.nds) and that it isn't damaged.");
    });
    connect(Game, &GameView::started, this, [this] { updateMenus(); updateTitle(); });
    connect(Game, &GameView::menuRequested, this, [this] {
        if (isFullScreen() && !Pause->isOpen()) { toggleFullScreen(); return; }
        togglePauseMenu();
    });
    connect(Game, &GameView::fullScreenToggled, this, [this] { toggleFullScreen(); });

    buildMenus();
    connect(&app::Notifier::I(), &app::Notifier::changed, this, [this](const QString& k) {
        Game->applySettings(k);
        if (k == "hotkeys") applyHotkeys();
        updateMenus();
    });
    // pause while in the background (if so set)
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        Game->setAutoPause(app::On("pauseUnfocused") && s != Qt::ApplicationActive);
        // back in the app: pick up games copied in meanwhile
        if (s == Qt::ApplicationActive && Stack->currentWidget() == Lib) Lib->refreshIfChanged();
    });

    // controllers: navigate the library and the pause menu
    PadTimer.setInterval(16);
    connect(&PadTimer, &QTimer::timeout, this, &MainWindow::pollPads);
    PadTimer.start();

    QByteArray geo = app::S().value("window/geometry").toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo); else resize(1100, 760);
    updateMenus();
}

void MainWindow::buildMenus()
{
    QMenuBar* mb = menuBar();
    QMenu* file = mb->addMenu("&File");
    QAction* open = file->addAction("Open a Game File…", Lib, &Library::openFile);
    open->setShortcut(QKeySequence::Open);
    file->addAction("Add Games Folder…", Lib, &Library::addFolder);
    QAction* refresh = file->addAction("Refresh Library", Lib, &Library::reload);
    refresh->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    file->addSeparator();
    QAction* prefs = file->addAction("Settings…", this, [this] { openSettings(); });
    prefs->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    prefs->setMenuRole(QAction::PreferencesRole);
    file->addSeparator();
    QAction* quit = file->addAction("Quit Dual HD", this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);
    quit->setMenuRole(QAction::QuitRole);

    GameMenu = mb->addMenu("&Game");
    ActPause = GameMenu->addAction("Pause", this, [this] { Game->setPaused(!Game->paused()); updateMenus(); });
    ActPause->setCheckable(true);
    ActFfwd = GameMenu->addAction("Fast Forward", this, [this] { Game->setFastForward(ActFfwd->isChecked()); });
    ActFfwd->setCheckable(true);
    ActReset = GameMenu->addAction("Reset", this, [this] {
        if (QMessageBox::question(this, "Reset", "Restart the game? Progress since your last in-game save is lost.") == QMessageBox::Yes) Game->reset();
    });
    GameMenu->addSeparator();
    SaveMenu = GameMenu->addMenu("Save State");
    LoadMenu = GameMenu->addMenu("Load State");
    for (int s = 1; s <= 8; s++)
    {
        QAction* sv = SaveMenu->addAction(QString("Slot %1").arg(s), this, [this, s] { Game->saveState(s); updateMenus(); });
        sv->setShortcut(QKeySequence(Qt::SHIFT | (Qt::Key)(Qt::Key_F1 + s - 1)));
        QAction* ld = LoadMenu->addAction(QString("Slot %1").arg(s), this, [this, s] { Game->loadState(s); });
        ld->setShortcut(QKeySequence((Qt::Key)(Qt::Key_F1 + s - 1)));
    }
    GameMenu->addSeparator();
    ActShot = GameMenu->addAction("Screenshot", this, &MainWindow::screenshot);
    GameMenu->addSeparator();
    ActLibrary = GameMenu->addAction("Back to Library", this, &MainWindow::showLibrary);

    QMenu* view = mb->addMenu("&View");
    ActFull = view->addAction("Full Screen", this, &MainWindow::toggleFullScreen);
    view->addSeparator();
    QMenu* res = view->addMenu("Resolution");
    ScaleGroup = new QActionGroup(this);
    for (int s : {1, 2, 3, 4, 5, 6, 8})
    {
        QAction* a = res->addAction(s == 1 ? "1x (original)" : QString("%1x").arg(s), this, [s] { app::Set("scale", s); });
        a->setCheckable(true); a->setData(s); ScaleGroup->addAction(a);
        if (s <= 8) a->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key)(Qt::Key_0 + std::min(s, 9))));
    }
    QMenu* lay = view->addMenu("Screen Layout");
    LayoutGroup = new QActionGroup(this);
    for (auto [v, n] : {std::pair<int, const char*>{0, "Stacked"}, {1, "Side by Side"}, {4, "Large Top, Small Bottom"}, {2, "Top Screen Only"}, {3, "Bottom Screen Only"}})
    {
        QAction* a = lay->addAction(n, this, [v] { app::Set("layout", v); });
        a->setCheckable(true); a->setData(v); LayoutGroup->addAction(a);
    }
    lay->addSeparator();
    ActLayoutNext = lay->addAction("Next Layout", this, [] {
        static const int order[] = {0, 1, 4, 2, 3};
        int cur = app::Int("layout"), i = 0;
        while (i < 5 && order[i] != cur) i++;
        app::Set("layout", order[(i + 1) % 5]);
    });
    ActSwap = view->addAction("Swap Screens", this, [] { app::Set("swap", !app::On("swap")); });
    ActSwap->setCheckable(true);
    view->addSeparator();
    ActSmooth = view->addAction("Smooth Screen Filter", this, [] { app::Set("filter", !app::On("filter")); });
    ActSmooth->setCheckable(true);
    ActPerf = view->addAction("Show Frame Rate", this, [] { app::Set("perf", !app::On("perf")); });
    ActPerf->setCheckable(true);

    LookMenu = mb->addMenu("&Look");
    LookGroup = new QActionGroup(this);
    for (const app::Mode& m : app::Modes())
    {
        QAction* a = LookMenu->addAction(m.Name, this, [v = m.Value] { app::Set("hdMode", v); });
        a->setCheckable(true); a->setData(m.Value); a->setToolTip(m.Detail); LookGroup->addAction(a);
    }
    LookMenu->addSeparator();
    ActLookNext = LookMenu->addAction("Next Look", this, &MainWindow::cycleLook);
    LookMenu->addAction("Game Enhancements…", this, [this] { openSettings(SettingsDialog::Enhancements, Game->romPath()); });

    QMenu* help = mb->addMenu("&Help");
    help->addAction("Copy Debug Report", this, [this] {
        int soft = 0;
        QGuiApplication::clipboard()->setText(QString("Dual HD %1 on %2\n").arg(QApplication::applicationVersion(), QSysInfo::prettyProductName())
                                              + QString::fromUtf8(wc09_debug_report()) + "\n" + QString::fromUtf8(wc09_renderer_report(&soft)));
        Game->toast("Debug report copied");
    });
    help->addAction("Controls and Hotkeys…", this, [this] { openSettings(SettingsDialog::Controls); });
    QAction* about = help->addAction("About Dual HD", this, [this] { openSettings(SettingsDialog::About); });
    about->setMenuRole(QAction::AboutRole);

    // hotkeys also work while the menu bar is hidden (full screen on Windows)
    for (QAction* a : {ActPause, ActFfwd, ActReset, ActShot, ActLibrary, ActFull, ActSwap, ActLayoutNext, ActLookNext}) addAction(a);
    for (QAction* a : SaveMenu->actions()) addAction(a);
    for (QAction* a : LoadMenu->actions()) addAction(a);
    for (QAction* a : ScaleGroup->actions()) addAction(a);
    addAction(prefs); addAction(open);
    applyHotkeys();
}

void MainWindow::applyHotkeys()
{
    ActFull->setShortcuts({app::HotkeySeq(app::HK_FULLSCREEN), QKeySequence(Qt::ALT | Qt::Key_Return)});
    ActPause->setShortcut(app::HotkeySeq(app::HK_PAUSE));
    ActFfwd->setShortcut(app::HotkeySeq(app::HK_FFWD_TOGGLE));
    ActReset->setShortcut(app::HotkeySeq(app::HK_RESET));
    ActShot->setShortcut(app::HotkeySeq(app::HK_SCREENSHOT));
    ActLibrary->setShortcut(app::HotkeySeq(app::HK_LIBRARY));
    ActLookNext->setShortcut(app::HotkeySeq(app::HK_LOOK));
    ActSwap->setShortcut(app::HotkeySeq(app::HK_SWAP));
    ActLayoutNext->setShortcut(app::HotkeySeq(app::HK_LAYOUT));
}

void MainWindow::updateMenus()
{
    bool running = Game->running();
    for (QAction* a : {ActPause, ActFfwd, ActReset, ActShot, ActLibrary}) a->setEnabled(running);
    SaveMenu->setEnabled(running); LoadMenu->setEnabled(running);
    ActPause->setChecked(running && Game->paused());
    ActFfwd->setChecked(running && Game->fastForward());
    for (QAction* a : ScaleGroup->actions()) a->setChecked(a->data().toInt() == app::Int("scale"));
    for (QAction* a : LayoutGroup->actions()) a->setChecked(a->data().toInt() == app::Int("layout"));
    for (QAction* a : LookGroup->actions()) a->setChecked(a->data().toInt() == app::Int("hdMode"));
    ActSwap->setChecked(app::On("swap"));
    ActSmooth->setChecked(app::On("filter"));
    ActPerf->setChecked(app::On("perf"));
    LookMenu->menuAction()->setVisible(running && Game->hdGame());
    ActLookNext->setEnabled(running && Game->hdGame());
    if (running)
        for (int s = 1; s <= 8; s++)
        {
            QString p = QFileInfo(Game->romPath()).absolutePath() + "/Saves/" + QFileInfo(Game->romPath()).completeBaseName() + ".ml" + QString::number(s);
            QFileInfo fi(p);
            QString when = fi.exists() ? "  (" + fi.lastModified().toString("MMM d, h:mm ap") + ")" : "  (empty)";
            SaveMenu->actions()[s - 1]->setText(QString("Slot %1%2").arg(s).arg(when));
            LoadMenu->actions()[s - 1]->setText(QString("Slot %1%2").arg(s).arg(when));
            LoadMenu->actions()[s - 1]->setEnabled(fi.exists());
        }
}

void MainWindow::cycleLook()
{
    if (!Game->hdGame()) return;
    const auto& modes = app::Modes();
    int cur = app::Int("hdMode"), i = 0;
    while (i < modes.size() && modes[i].Value != cur) i++;
    const app::Mode& next = modes[(i + 1) % modes.size()];
    app::Set("hdMode", next.Value);
    Game->toast("Look: " + next.Name);
}

void MainWindow::openRom(const QString& path, bool fresh)
{
    QFileInfo fi(path);
    if (!fi.exists()) { QMessageBox::warning(this, "Dual HD", "That game file isn't there anymore."); return; }
    app::S().setValue("played/" + QString(fi.fileName()).replace('/', '_'), QDateTime::currentSecsSinceEpoch());
    QString custom = app::S().value("name/" + QString(fi.fileName()).replace('/', '_')).toString();
    GameTitle = custom;
    if (GameTitle.isEmpty())
    {
        char code[5] = {0};
        wc09_rom_code(QDir::fromNativeSeparators(fi.absoluteFilePath()).toUtf8().constData(), code);
        GameTitle = app::S().value(QString("tdb/") + code).toString().section('\n', 0, 0);
    }
    if (GameTitle.isEmpty())
    {
        // the name in the game's own banner
        char title[512]; unsigned char px[32 * 32 * 4];
        if (wc09_rom_info(QDir::fromNativeSeparators(fi.absoluteFilePath()).toUtf8().constData(), title, sizeof(title), px) == 0)
        {
            QStringList lines = QString::fromUtf8(title).split('\n');
            if (lines.size() >= 2) lines.removeLast();   // the publisher
            // words longer than three letters in capitals get title case, like the Library
            QStringList w = lines.join(' ').simplified().split(' ');
            for (QString& x : w)
                if (x.size() > 3 && x == x.toUpper() && x != x.toLower()) x = x.left(1) + x.mid(1).toLower();
            GameTitle = w.join(' ');
        }
    }
    if (GameTitle.isEmpty()) GameTitle = fi.completeBaseName();
    Stack->setCurrentWidget(Game);
    Game->setFocus();
    Game->start(fi.absoluteFilePath(), fresh);
    updateTitle();
    updateMenus();
}

void MainWindow::showLibrary()
{
    Pause->close();
    Game->stop();
    if (isFullScreen()) toggleFullScreen();
    Stack->setCurrentWidget(Lib);
    Lib->reload();
    updateTitle();
    updateMenus();
}

void MainWindow::updateTitle() { setWindowTitle(Game->running() || Stack->currentWidget() == Game ? GameTitle + " - Dual HD" : "Dual HD"); }

void MainWindow::toggleFullScreen()
{
    if (isFullScreen())
    {
        showNormal();
#ifndef Q_OS_MACOS
        menuBar()->show();
#endif
    }
    else
    {
#ifndef Q_OS_MACOS
        menuBar()->hide();   // the hotkeys still work
#endif
        showFullScreen();
    }
    Game->setFocus();
}

void MainWindow::togglePauseMenu()
{
    if (!Game->running()) return;
    if (Pause->isOpen()) { Pause->close(); Game->setMenuPause(false); Game->setFocus(); return; }
    Game->setMenuPause(true);
    auto resume = [this] { Game->setMenuPause(false); Game->setFocus(); };
    Pause->OnClose = resume;
    auto slotPage = [this](bool save, std::function<void()> backTo) {
        QList<PauseMenu::Item> items;
        for (int s = 1; s <= 8; s++)
        {
            QFileInfo fi(QFileInfo(Game->romPath()).absolutePath() + "/Saves/" + QFileInfo(Game->romPath()).completeBaseName() + ".ml" + QString::number(s));
            QString t = QString("Slot %1   %2").arg(s).arg(fi.exists() ? fi.lastModified().toString("MMM d, h:mm ap") : QString("empty"));
            if (!save && !fi.exists()) continue;
            items.push_back({t, [this, s, save] { if (save) Game->saveState(s); else Game->loadState(s); Game->setMenuPause(false); Game->setFocus(); updateMenus(); }});
        }
        if (items.isEmpty()) items.push_back({"No saved states yet", backTo});
        Pause->OnBack = backTo;
        Pause->open(save ? "Save State" : "Load State", items);
    };
    auto openMain = std::make_shared<std::function<void()>>();
    *openMain = [this, slotPage, resume, openMain] {
        QList<PauseMenu::Item> items;
        items.push_back({"Resume", resume});
        items.push_back({"Save State…", [slotPage, openMain] { slotPage(true, *openMain); }, true});
        items.push_back({"Load State…", [slotPage, openMain] { slotPage(false, *openMain); }, true});
        if (Game->hdGame())
            items.push_back({"Look: " + app::ModeName(app::Int("hdMode")), [this, openMain] {
                QList<PauseMenu::Item> looks;
                for (const app::Mode& m : app::Modes())
                    looks.push_back({(app::Int("hdMode") == m.Value ? QString::fromUtf8("✓  ") : QString("     ")) + m.Name,
                                     [this, v = m.Value, name = m.Name] { app::Set("hdMode", v); Game->toast("Look: " + name); Game->setMenuPause(false); Game->setFocus(); }});
                Pause->OnBack = *openMain;
                Pause->open("Look", looks);
            }, true});
        items.push_back({isFullScreen() ? "Exit Full Screen" : "Full Screen", [this, resume] { toggleFullScreen(); resume(); }});
        items.push_back({"Settings…", [this, resume] { resume(); openSettings(); }});
        items.push_back({"Reset Game", [this] { Game->setMenuPause(false); Game->reset(); }});
        items.push_back({"Back to Library", [this] { showLibrary(); }});
        Pause->OnBack = nullptr;
        Pause->open(GameTitle, items);
    };
    (*openMain)();
}

void MainWindow::openSettings(int page, const QString& rom)
{
    if (!Settings) Settings = new SettingsDialog(this);
    if (!rom.isEmpty()) Settings->showGame(rom); else Settings->showPage(page);
    Settings->show();
    Settings->raise();
    Settings->activateWindow();
}

void MainWindow::screenshot()
{
    if (!Game->running()) return;
    QImage img = Game->screenshot();
    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Dual HD";
    QDir().mkpath(dir);
    QString safe = GameTitle; safe.replace(QRegularExpression("[\\\\/:*?\"<>|]"), " ");
    QString path = dir + "/" + safe + " " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh.mm.ss") + ".png";
    Game->toast(img.save(path) ? "Screenshot saved to Pictures/Dual HD" : "Couldn't save the screenshot");
}

void MainWindow::pollPads()
{
    bool inLibrary = Stack->currentWidget() == Lib && isActiveWindow();
    bool inMenu = Pause->isOpen();
    if (!inLibrary && !inMenu) { PadPrev = ~0u; DirPrev = ~0u; return; }
    pads::State s = pads::Poll();
    unsigned pressed = PadPrev == ~0u ? 0 : s.Held & ~PadPrev;
    unsigned dirs = DirPrev == ~0u ? 0 : s.Dirs & ~DirPrev;
    PadPrev = s.Held; DirPrev = s.Dirs;
    auto act = [&](int b) { return app::PadAction(b, s.Nintendo); };
    bool confirm = false, cancel = false;
    for (int b = 0; b < app::PB_COUNT; b++)
    {
        if (!(pressed & (1u << b))) continue;
        int a = act(b);
        if (a == app::PA_A || a == app::PA_START) confirm = true;
        if (a == app::PA_B) cancel = true;
    }
    if (inMenu)
    {
        if (dirs & WC09_KEY_UP) Pause->move(-1);
        if (dirs & WC09_KEY_DOWN) Pause->move(1);
        if (confirm) Pause->activate();
        else if (cancel) Pause->back();
        return;
    }
    if (dirs & WC09_KEY_LEFT) Lib->moveSelection(-1, 0);
    if (dirs & WC09_KEY_RIGHT) Lib->moveSelection(1, 0);
    if (dirs & WC09_KEY_UP) Lib->moveSelection(0, -1);
    if (dirs & WC09_KEY_DOWN) Lib->moveSelection(0, 1);
    if (confirm) Lib->playSelected();
}

bool MainWindow::eventFilter(QObject* o, QEvent* e) { return QMainWindow::eventFilter(o, e); }

// a .nds dropped on the window: play it (its folder joins the library)
void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && u.toLocalFile().endsWith(".nds", Qt::CaseInsensitive)) { e->acceptProposedAction(); return; }
}

void MainWindow::dropEvent(QDropEvent* e)
{
    for (const QUrl& u : e->mimeData()->urls())
    {
        QString p = u.toLocalFile();
        if (!p.endsWith(".nds", Qt::CaseInsensitive)) continue;
        QString d = QFileInfo(p).absolutePath();
        QStringList f = app::Folders();
        if (!f.contains(d)) { f << d; app::SetFolders(f); }
        openRom(p);
        return;
    }
}

void MainWindow::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::WindowStateChange) updateMenus();
    QMainWindow::changeEvent(e);
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    Pause->close();
    Game->stop();
    if (!isFullScreen()) app::S().setValue("window/geometry", saveGeometry());
    e->accept();
}
