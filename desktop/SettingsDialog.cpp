#include "SettingsDialog.h"
#include "App.h"
#include "Pads.h"
#include "wc09_core.h"
#include <QListWidget>
#include <QStackedWidget>
#include <QBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSlider>
#include <QSpinBox>
#include <QPushButton>
#include <QScrollArea>
#include <QKeySequenceEdit>
#include <QKeyEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QClipboard>
#include <QGuiApplication>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QApplication>

namespace
{
const char* kStyle = R"(
QDialog { background:#0a0e16; }
QListWidget#side { background:#0f141e; border:none; border-right:1px solid #1c2433; color:#c8d1de; font-size:14px; outline:0; padding-top:10px; }
QListWidget#side::item { padding:9px 14px; border-radius:8px; margin:1px 8px; }
QListWidget#side::item:selected { background:#1b2840; color:white; }
QLabel { color:#dfe6f0; }
QLabel[role="title"] { color:white; font-size:22px; font-weight:800; }
QLabel[role="section"] { color:#7c8799; font-size:11px; font-weight:800; letter-spacing:1.5px; margin-top:14px; }
QLabel[role="detail"] { color:#8e9aad; font-size:12px; }
QFrame[role="card"] { background:#151b26; border:1px solid #1f2837; border-radius:12px; }
QCheckBox, QRadioButton { color:#e6ecf4; font-size:13px; spacing:8px; }
QCheckBox::indicator { width:20px; height:20px; border:1px solid #3a4558; border-radius:6px; background:#0f141e; }
QCheckBox::indicator:hover { border-color:#4aa3ff; }
QCheckBox::indicator:checked { background:#4aa3ff; border-color:#4aa3ff; image:url(:/check.png); }
QRadioButton::indicator { width:18px; height:18px; border:1px solid #3a4558; border-radius:10px; background:#0f141e; }
QRadioButton::indicator:checked { background:#4aa3ff; border-color:#4aa3ff; image:url(:/dot.png); }
QScrollBar:vertical { background:transparent; width:10px; } QScrollBar::handle:vertical { background:#2a3446; border-radius:5px; min-height:30px; }
QScrollBar::add-line, QScrollBar::sub-line { height:0; }
QComboBox, QSpinBox, QKeySequenceEdit, QLineEdit { background:#0f141e; color:white; border:1px solid #2a3446; border-radius:7px; padding:5px 8px; min-height:20px; }
QComboBox QAbstractItemView { background:#151b26; color:white; selection-background-color:#24426e; }
QPushButton { background:#1b2433; color:white; border:1px solid #2a3446; border-radius:8px; padding:6px 14px; }
QPushButton:hover { background:#24304a; }
QPushButton[role="primary"] { background:#4aa3ff; color:#06111f; font-weight:700; border:none; }
QScrollArea { background:transparent; border:none; }
QSlider::groove:horizontal { height:4px; background:#2a3446; border-radius:2px; }
QSlider::handle:horizontal { background:#4aa3ff; width:16px; margin:-6px 0; border-radius:8px; }
)";

QLabel* Detail(const QString& t) { auto* l = new QLabel(t); l->setProperty("role", "detail"); l->setWordWrap(true); return l; }
QLabel* Section(const QString& t) { auto* l = new QLabel(t.toUpper()); l->setProperty("role", "section"); return l; }

QFrame* Card(QVBoxLayout*& inner)
{
    auto* f = new QFrame; f->setProperty("role", "card");
    inner = new QVBoxLayout(f); inner->setContentsMargins(16, 12, 16, 14); inner->setSpacing(8);
    return f;
}

// a setting with a title, a control on the right and a line of explanation under it
QWidget* Row(const QString& title, QWidget* control, const QString& detail = {})
{
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w); v->setContentsMargins(0, 2, 0, 2); v->setSpacing(2);
    auto* h = new QHBoxLayout; h->setSpacing(12);
    auto* t = new QLabel(title); t->setStyleSheet("font-size:14px; color:white;");
    h->addWidget(t, 1);
    if (control) h->addWidget(control);
    v->addLayout(h);
    if (!detail.isEmpty()) v->addWidget(Detail(detail));
    return w;
}

QCheckBox* Toggle(const QString& key)
{
    auto* c = new QCheckBox;
    c->setChecked(app::On(key));
    QObject::connect(c, &QCheckBox::toggled, [key](bool v) { app::Set(key, v); });
    return c;
}

QComboBox* Choice(const QString& key, const QList<QPair<QString, QVariant>>& items)
{
    auto* c = new QComboBox;
    for (auto& it : items) c->addItem(it.first, it.second);
    int i = c->findData(app::Get(key)); if (i >= 0) c->setCurrentIndex(i);
    QObject::connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), [c, key](int) { app::Set(key, c->currentData()); });
    return c;
}

// captures one key for a DS button
class KeyButton : public QPushButton
{
public:
    int Ds;
    explicit KeyButton(int ds) : Ds(ds) { refresh(); setFixedWidth(150); connect(this, &QPushButton::clicked, this, [this] { setText("Press a key…"); grabKeyboard(); }); }
    void refresh() { int k = app::KeyFor(Ds); setText(k ? QKeySequence(k).toString(QKeySequence::NativeText) : "(none)"); }
protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        if (text() != "Press a key…") { QPushButton::keyPressEvent(e); return; }
        releaseKeyboard();
        if (e->key() != Qt::Key_Escape) app::SetKeyFor(Ds, e->key());
        refresh();
    }
    void focusOutEvent(QFocusEvent* e) override { releaseKeyboard(); refresh(); QPushButton::focusOutEvent(e); }
};
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Settings");
    setStyleSheet(kStyle);
    resize(860, 640);
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0); h->setSpacing(0);
    Side = new QListWidget; Side->setObjectName("side"); Side->setFixedWidth(210);
    for (const char* n : {"Display", "Game Enhancements", "Controls", "Hotkeys", "Sound & Play", "Library", "About & Help"}) Side->addItem(n);
    Pages = new QStackedWidget;
    h->addWidget(Side); h->addWidget(Pages, 1);
    for (int p = 0; p <= About; p++) Pages->addWidget(new QWidget);
    for (int p = 0; p <= About; p++) rebuild(p);
    connect(Side, &QListWidget::currentRowChanged, Pages, &QStackedWidget::setCurrentIndex);
    Side->setCurrentRow(0);
    // pages that show live state
    connect(&app::Notifier::I(), &app::Notifier::changed, this, [this](const QString& k) {
        if (k == "pad" || k == "padLayout") QTimer::singleShot(0, this, [this] { rebuild(Controls); });
        if (k == "keys") QTimer::singleShot(0, this, [this] { rebuild(Controls); });
        if (k == "hotkeys") QTimer::singleShot(0, this, [this] { rebuild(Hotkeys); });
        if (k == "folders") QTimer::singleShot(0, this, [this] { rebuild(LibraryPage); rebuild(Enhancements); });
    });
}

void SettingsDialog::rebuild(int p)
{
    QWidget* w = nullptr;
    switch (p)
    {
    case Display: w = buildDisplay(); break;
    case Enhancements: w = buildEnhancements(); break;
    case Controls: w = buildControls(); break;
    case Hotkeys: w = buildHotkeys(); break;
    case Play: w = buildPlay(); break;
    case LibraryPage: w = buildLibrary(); break;
    case About: w = buildAbout(); break;
    }
    QWidget* old = Pages->widget(p);
    int cur = Pages->currentIndex();
    Pages->insertWidget(p, w);
    Pages->removeWidget(old);
    old->deleteLater();
    Pages->setCurrentIndex(cur);
}

void SettingsDialog::showPage(int p) { Side->setCurrentRow(p); }
void SettingsDialog::showGame(const QString& rom) { FocusRom = rom; rebuild(Enhancements); showPage(Enhancements); }

QWidget* SettingsDialog::page(const QString& title, QVBoxLayout*& body)
{
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true);
    auto* inner = new QWidget; inner->setStyleSheet("background:#0a0e16;");
    body = new QVBoxLayout(inner); body->setContentsMargins(28, 22, 28, 28); body->setSpacing(8);
    auto* t = new QLabel(title); t->setProperty("role", "title");
    body->addWidget(t);
    scroll->setWidget(inner);
    return scroll;
}

QWidget* SettingsDialog::buildDisplay()
{
    QVBoxLayout* b; QWidget* pg = page("Display", b);
    QVBoxLayout* c;
    b->addWidget(Section("Picture"));
    b->addWidget(Card(c));
    c->addWidget(Row("Resolution", Choice("scale", {{"1x (original)", 1}, {"2x", 2}, {"3x", 3}, {"4x (recommended)", 4}, {"5x", 5}, {"6x", 6}, {"8x", 8}}),
        "How sharp the DS screens are drawn: each step renders them at that many times their size. 1x is the original DS picture."));
    c->addWidget(Row("Screen filter", Choice("filter", {{"Smooth", true}, {"Sharp pixels", false}}),
        "Smooth blends the picture to fit your window; Sharp keeps hard pixel edges."));
    c->addWidget(Row("Smooth 2D graphics", Toggle("smooth2d"), "Upscales menus and sprites with smooth edges instead of blocky pixels."));
    c->addWidget(Row("VSync", Toggle("vsync"), "Syncs frames with your display: no tearing, steady motion. Takes effect the next time Dual HD starts."));
    b->addWidget(Section("Screens"));
    b->addWidget(Card(c));
    c->addWidget(Row("Layout", Choice("layout", {{"Stacked (top over bottom)", 0}, {"Side by side", 1}, {"Large top, small bottom", 4}, {"Top screen only", 2}, {"Bottom screen only", 3}}),
        "Also from the View menu, or F10 to go through them."));
    c->addWidget(Row("Swap screens", Toggle("swap"), "Puts the bottom (touch) screen where the top one goes. F9."));
    auto* gap = new QSpinBox; gap->setRange(0, 96); gap->setSuffix(" px"); gap->setValue(app::Int("gap"));
    connect(gap, QOverload<int>::of(&QSpinBox::valueChanged), [](int v) { app::Set("gap", v); });
    c->addWidget(Row("Gap between screens", gap, "Space between the two screens, in DS pixels."));
    c->addWidget(Row("Integer scaling", Toggle("integer"), "Only whole-number zoom levels: every DS pixel the same size, with black borders around."));
    b->addWidget(Section("Performance"));
    b->addWidget(Card(c));
    c->addWidget(Row("Show frame rate", Toggle("perf"), "A small number in the corner of the game. 60 is full speed."));
    c->addWidget(Row("Detailed performance", Toggle("perfDetail"), "Frame timing and graphics stats next to the frame rate."));
    b->addStretch(1);
    return pg;
}

QWidget* SettingsDialog::buildEnhancements()
{
    QVBoxLayout* b; QWidget* pg = page("Game Enhancements", b);
    b->addWidget(Detail("Extra improvements made for specific games. They switch on by themselves when you play that game, if its HD pack sits next to it."));
    for (const app::Pack& pack : app::Packs())
    {
        QVBoxLayout* c;
        b->addWidget(Section(pack.Title));
        b->addWidget(Card(c));
        c->addWidget(Detail(pack.About + " You can switch the look from the Look menu too, even mid-game."));
        auto* grp = new QButtonGroup(c->parentWidget());
        for (const app::Mode& m : app::Modes())
        {
            auto* r = new QRadioButton(m.Name);
            r->setStyleSheet("font-size:14px; font-weight:600;");
            r->setChecked(app::Int("hdMode") == m.Value);
            grp->addButton(r, m.Value);
            c->addWidget(r);
            auto* d = Detail(m.Detail); d->setContentsMargins(26, 0, 0, 6);
            c->addWidget(d);
        }
        connect(grp, &QButtonGroup::idClicked, [](int id) { app::Set("hdMode", id); });

        b->addWidget(Section("What's improved"));
        b->addWidget(Card(c));
        for (const QString& f : pack.Features) c->addWidget(new QLabel(QString::fromUtf8("✓  ") + f));

        b->addWidget(Section("What you need: the " + pack.Folder + " folder"));
        b->addWidget(Card(c));
        // the games in the library, and whether their pack is there
        int found = 0;
        for (const QString& folder : app::Folders())
        {
            QDir d(folder);
            QList<QFileInfo> roms = d.entryInfoList({"*.nds", "*.NDS"}, QDir::Files);
            for (const QFileInfo& sub : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                roms += QDir(sub.absoluteFilePath()).entryInfoList({"*.nds", "*.NDS"}, QDir::Files);
            for (const QFileInfo& rom : roms)
            {
                char code[5] = {0};
                wc09_rom_code(QDir::fromNativeSeparators(rom.absoluteFilePath()).toUtf8().constData(), code);
                if (!QString(code).startsWith(pack.Code)) continue;
                found++;
                bool ok = !app::PackFolderFor(rom.absoluteFilePath(), code).isEmpty();
                auto* l = new QLabel(QString("<b style='color:%1'>%2</b> &nbsp; %3 next to %4")
                    .arg(ok ? "#22c55e" : "#f59e0b", ok ? "Ready" : "HD pack missing", ok ? pack.Folder + " found" : pack.Folder + " not found", rom.fileName().toHtmlEscaped()));
                l->setTextFormat(Qt::RichText);
                c->addWidget(l);
            }
        }
        if (!found) c->addWidget(Detail("This game isn't in your library yet."));
        c->addWidget(Detail(QString("The enhancements use an HD pack: a folder named %1 with the card art, the HD pictures and the redesign (about 1 GB). "
                                    "Put it in the same folder as the game:\n\n    Your games folder\n        %2.nds\n        %1\n\n"
                                    "Keep the folder name exactly \"%1\". Without it the game runs like the original.").arg(pack.Folder, pack.Short)));
    }
    b->addStretch(1);
    return pg;
}

QWidget* SettingsDialog::buildControls()
{
    QVBoxLayout* b; QWidget* pg = page("Controls", b);
    QVBoxLayout* c;
    b->addWidget(Section("Keyboard"));
    b->addWidget(Card(c));
    c->addWidget(Detail("Click a key to change it, then press the new key (Esc cancels). The mouse is the touch screen."));
    auto* grid = new QGridLayout; grid->setHorizontalSpacing(18); grid->setVerticalSpacing(6);
    for (int k = 0; k < app::K_COUNT; k++)
    {
        auto* l = new QLabel(app::DsName(k)); l->setStyleSheet("font-size:14px; color:white;");
        grid->addWidget(l, k % 6, (k / 6) * 2);
        grid->addWidget(new KeyButton(k), k % 6, (k / 6) * 2 + 1);
    }
    c->addLayout(grid);
    auto* rk = new QPushButton("Reset Keys");
    connect(rk, &QPushButton::clicked, [] { app::ResetKeys(); });
    c->addWidget(rk, 0, Qt::AlignLeft);

    b->addWidget(Section("Game controller"));
    b->addWidget(Card(c));
    pads::Poll();
    QString name = pads::Name();
    auto* who = new QLabel(name.isEmpty() ? "No controller connected" : name + "  <span style='color:#22c55e'>connected</span>");
    who->setTextFormat(Qt::RichText);
    who->setStyleSheet("font-size:14px;");
    c->addWidget(who);
    c->addWidget(Detail("Xbox, PlayStation, Switch Pro and most other controllers work as soon as they're connected (USB or Bluetooth)."));
    c->addWidget(Row("Face buttons", Choice("padLayout", {{"Match letters (A is A)", 0}, {"Match DS positions", 1}}),
        "Match DS positions: the right button is A and the bottom one B, as on a DS. Switch-style controllers already match the DS."));
    auto* pgrid = new QGridLayout; pgrid->setHorizontalSpacing(18); pgrid->setVerticalSpacing(6);
    for (int btn = 0; btn < app::PB_COUNT; btn++)
    {
        auto* l = new QLabel(app::PadButtonName(btn)); l->setStyleSheet("font-size:13px; color:white;");
        auto* cb = new QComboBox;
        for (int a = 0; a < app::PA_COUNT; a++) cb->addItem(app::PadActionName(a), a);
        cb->setCurrentIndex(app::PadAction(btn, false));
        connect(cb, QOverload<int>::of(&QComboBox::activated), [btn, cb](int) { app::SetPadAction(btn, cb->currentData().toInt()); });
        pgrid->addWidget(l, btn % 7, (btn / 7) * 2);
        pgrid->addWidget(cb, btn % 7, (btn / 7) * 2 + 1);
    }
    c->addLayout(pgrid);
    c->addWidget(Detail("The D-pad and the left stick are the DS D-pad. By default Home and the left stick click open the pause menu, and holding the right stick click fast-forwards."));
    auto* rp = new QPushButton("Reset Controller Buttons");
    connect(rp, &QPushButton::clicked, [] { app::ResetPad(); });
    c->addWidget(rp, 0, Qt::AlignLeft);
    b->addStretch(1);
    return pg;
}

QWidget* SettingsDialog::buildHotkeys()
{
    QVBoxLayout* b; QWidget* pg = page("Hotkeys", b);
    QVBoxLayout* c;
    b->addWidget(Card(c));
    auto* grid = new QGridLayout; grid->setHorizontalSpacing(18); grid->setVerticalSpacing(8);
    for (int h = 0; h < app::HK_COUNT; h++)
    {
        auto* l = new QLabel(app::HotkeyName(h)); l->setStyleSheet("font-size:14px; color:white;");
        auto* e = new QKeySequenceEdit(app::HotkeySeq(h));
        e->setFixedWidth(180);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        e->setMaximumSequenceLength(1);
#endif
        connect(e, &QKeySequenceEdit::editingFinished, [h, e] { app::SetHotkey(h, QKeySequence(e->keySequence()[0])); });
        grid->addWidget(l, h, 0); grid->addWidget(e, h, 1);
    }
    c->addLayout(grid);
    auto* rh = new QPushButton("Reset Hotkeys");
    connect(rh, &QPushButton::clicked, [] { app::ResetHotkeys(); });
    c->addWidget(rh, 0, Qt::AlignLeft);
    b->addWidget(Section("Always"));
    b->addWidget(Card(c));
#ifdef Q_OS_MACOS
    QString mod = "Cmd";
#else
    QString mod = "Ctrl";
#endif
    for (QString t : {QString("F1 to F8: load save state 1 to 8"), QString("Shift + F1 to F8: save to state 1 to 8"),
                      QString("Esc: pause menu (leaves full screen first)"), mod + " + , : settings", mod + " + 1 to 8: resolution",
                      QString("Double-click outside the touch screen: full screen")})
        c->addWidget(new QLabel(t));
    b->addStretch(1);
    return pg;
}

QWidget* SettingsDialog::buildPlay()
{
    QVBoxLayout* b; QWidget* pg = page("Sound & Play", b);
    QVBoxLayout* c;
    b->addWidget(Section("Sound"));
    b->addWidget(Card(c));
    c->addWidget(Row("Sound", Toggle("sound")));
    auto* vol = new QSlider(Qt::Horizontal); vol->setRange(0, 100); vol->setValue(app::Int("volume")); vol->setFixedWidth(220);
    connect(vol, &QSlider::valueChanged, [](int v) { app::Set("volume", v); });
    c->addWidget(Row("Volume", vol));
    b->addWidget(Section("Playing"));
    b->addWidget(Card(c));
    c->addWidget(Row("Resume where you left off", Toggle("resume"), "Leaving a game saves a resume point; opening it again continues from there. The game's own saves always stay."));
    c->addWidget(Row("Pause in the background", Toggle("pauseUnfocused"), "Pauses the game while Dual HD isn't the active window."));
    c->addWidget(Row("Fast forward speed", Choice("ffSpeed", {{"2x", 2}, {"3x", 3}, {"4x", 4}, {"As fast as possible", 0}}),
        "Hold Tab (or Shift+Tab to turn it on and off)."));
    b->addStretch(1);
    return pg;
}

QWidget* SettingsDialog::buildLibrary()
{
    QVBoxLayout* b; QWidget* pg = page("Library", b);
    QVBoxLayout* c;
    b->addWidget(Section("Games folders"));
    b->addWidget(Card(c));
    QStringList folders = app::Folders();
    if (folders.isEmpty()) c->addWidget(Detail("No folders yet."));
    for (const QString& f : folders)
    {
        auto* h = new QHBoxLayout;
        auto* l = new QLabel(QDir::toNativeSeparators(f)); l->setStyleSheet("font-size:13px;");
        h->addWidget(l, 1);
        auto* rm = new QPushButton("Remove");
        connect(rm, &QPushButton::clicked, [f] { QStringList x = app::Folders(); x.removeAll(f); app::SetFolders(x); });
        h->addWidget(rm);
        c->addLayout(h);
    }
    auto* add = new QPushButton("Add Folder…"); add->setProperty("role", "primary");
    connect(add, &QPushButton::clicked, this, [this] {
        QString d = QFileDialog::getExistingDirectory(this, "Choose a games folder");
        if (d.isEmpty()) return;
        QStringList x = app::Folders(); if (!x.contains(d)) { x << d; app::SetFolders(x); }
    });
    c->addWidget(add, 0, Qt::AlignLeft);
    c->addWidget(Detail("Games in every folder (and one level of sub-folders) show up together. Saves and save states are kept in a Saves folder next to each game, the same as on the phone apps."));
    b->addWidget(Section("Covers"));
    b->addWidget(Card(c));
    c->addWidget(Row("Download box art", Toggle("boxart"), "Real box art and full names from GameTDB, downloaded once. Your own covers always come first (right-click a game)."));
    b->addStretch(1);
    return pg;
}

QWidget* SettingsDialog::buildAbout()
{
    QVBoxLayout* b; QWidget* pg = page("About & Help", b);
    QVBoxLayout* c;
    b->addWidget(Card(c));
    auto* name = new QLabel("<span style='font-size:20px; font-weight:800; color:white'>Dual HD</span>  <span style='color:#8e9aad'>version " + QApplication::applicationVersion() + "</span>");
    name->setTextFormat(Qt::RichText);
    c->addWidget(name);
    c->addWidget(Detail("A DS emulator built on melonDS, with HD rendering and game enhancement packs."));
    b->addWidget(Section("Help"));
    b->addWidget(Card(c));
    auto* copy = new QPushButton("Copy Debug Report");
    connect(copy, &QPushButton::clicked, this, [this] {
        QString head = QString("device %1 %2, %3\nsettings: scale %4x, HD mode %5, smooth 2D %6, filter %7, vsync %8, layout %9\n")
            .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(), "Dual HD " + QApplication::applicationVersion())
            .arg(app::Int("scale")).arg(app::ModeName(app::Int("hdMode")), app::On("smooth2d") ? "on" : "off", app::On("filter") ? "smooth" : "sharp", app::On("vsync") ? "on" : "off")
            .arg(app::Int("layout"));
        int soft = 0;
        QString renderer = QString::fromUtf8(wc09_renderer_report(&soft));
        QGuiApplication::clipboard()->setText(head + QString::fromUtf8(wc09_debug_report()) + "\n" + renderer);
        QMessageBox::information(this, "Debug Report Copied", "It covers the last 15 minutes of play, with the slow moments in detail. Paste it wherever you report the problem.");
    });
    c->addWidget(copy, 0, Qt::AlignLeft);
    c->addWidget(Detail("If a game slows down, keep playing for a bit, then copy the report from here."));
    auto* data = new QPushButton("Open the App Data Folder");
    connect(data, &QPushButton::clicked, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(app::DataDir())); });
    c->addWidget(data, 0, Qt::AlignLeft);
    b->addWidget(Section("Advanced"));
    b->addWidget(Card(c));
    c->addWidget(Row("JIT recompiler", Toggle("jit"), "Runs the DS processors much faster. Turn off only if a game misbehaves. Takes effect the next time a game starts."));
    b->addStretch(1);
    return pg;
}
