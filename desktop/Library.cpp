#include "Library.h"
#include "App.h"
#include "wc09_core.h"
#include <QListView>
#include <QLineEdit>
#include <QStackedWidget>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QBoxLayout>
#include <QGridLayout>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTextDocument>
#include <QStandardPaths>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <thread>
#include <algorithm>

static QString Key(const QString& prefix, const QString& file) { return prefix + "/" + QString(file).replace('/', '_'); }
static QString ArtDir() { QString d = app::DataDir() + "/boxart"; QDir().mkpath(d); return d; }
static QString CoverDir() { QString d = app::DataDir() + "/covers"; QDir().mkpath(d); return d; }

static QFont TitleFont() { QFont f; f.setPixelSize(13); f.setWeight(QFont::Medium); return f; }

// "STARDUST ACCELERATOR" -> "Stardust Accelerator" (banner titles are often in capitals)
static QString Pretty(QString t)
{
    t = t.simplified();
    bool caps = t == t.toUpper() && t != t.toLower();
    if (!caps) return t;
    QStringList w = t.toLower().split(' ');
    for (QString& x : w) if (!x.isEmpty()) x[0] = x[0].toUpper();
    return w.join(' ');
}

static void ScanFolder(const QString& dir, QList<GameEntry>& out, int depth)
{
    QDir d(dir);
    for (const QFileInfo& fi : d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot))
    {
        if (fi.suffix().compare("nds", Qt::CaseInsensitive) != 0 || fi.fileName().startsWith('.')) continue;
        GameEntry g;
        g.Path = fi.absoluteFilePath(); g.File = fi.fileName(); g.Base = fi.completeBaseName();
        QByteArray p = QDir::fromNativeSeparators(g.Path).toUtf8();
        char title[512]; unsigned char px[32 * 32 * 4];
        if (wc09_rom_info(p.constData(), title, sizeof(title), px) == 0)
        {
            QStringList lines = QString::fromUtf8(title).split('\n');
            if (lines.size() >= 2) { g.Publisher = lines.takeLast(); g.BannerTitle = Pretty(lines.join(' ')); }
            else g.BannerTitle = Pretty(lines.value(0));
            g.Icon = QImage(px, 32, 32, QImage::Format_RGBA8888).copy();
            // tint from the icon's average colour
            qint64 r = 0, gr = 0, b = 0, n = 0;
            for (int i = 0; i < 32 * 32; i++) if (px[i * 4 + 3] > 128) { r += px[i * 4]; gr += px[i * 4 + 1]; b += px[i * 4 + 2]; n++; }
            g.Tint = n ? QColor(r / n, gr / n, b / n) : app::Accent();
        }
        if (g.BannerTitle.isEmpty()) g.BannerTitle = g.Base;
        char code[5] = {0};
        wc09_rom_code(p.constData(), code);
        g.Code = QString::fromLatin1(code);
        g.IsPackGame = app::PackFor(g.Code) != nullptr;
        g.HasPack = !app::PackFolderFor(g.Path, g.Code).isEmpty();
        out.push_back(g);
    }
    if (depth > 0)
        for (const QFileInfo& sub : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            QString n = sub.fileName();
            if (n == "Saves" || n == "Covers" || n == "HDCards" || QFileInfo::exists(sub.absoluteFilePath() + "/pack.json")) continue;
            ScanFolder(sub.absoluteFilePath(), out, depth - 1);
        }
}

// ---- model

QVariant LibraryModel::data(const QModelIndex& i, int role) const
{
    const GameEntry* g = at(i);
    if (!g) return {};
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) return g->Title;
    return {};
}

void LibraryModel::reset(QList<GameEntry> all, const QString& filter)
{
    beginResetModel();
    All = std::move(all);
    Shown.clear();
    endResetModel();
    refilter(filter);
}

void LibraryModel::refilter(const QString& filter)
{
    beginResetModel();
    Shown.clear();
    QString sort = app::Get("sort").toString();
    QStringList order = app::S().value("order").toStringList();
    for (int i = 0; i < All.size(); i++)
        if (filter.isEmpty() || All[i].Title.contains(filter, Qt::CaseInsensitive) || All[i].File.contains(filter, Qt::CaseInsensitive)) Shown.push_back(i);
    std::stable_sort(Shown.begin(), Shown.end(), [&](int a, int b) {
        const GameEntry &x = All[a], &y = All[b];
        if (x.Favorite != y.Favorite) return x.Favorite;   // favourites first
        if (sort == "recent" && x.Played != y.Played) return x.Played > y.Played;
        if (sort == "custom")
        {
            int ia = order.indexOf(x.File), ib = order.indexOf(y.File);
            if (ia < 0) ia = 1 << 20; if (ib < 0) ib = 1 << 20;
            if (ia != ib) return ia < ib;
        }
        return x.Title.compare(y.Title, Qt::CaseInsensitive) < 0;
    });
    endResetModel();
}

// ---- delegate

void GameDelegate::paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& i) const
{
    const GameEntry* g = static_cast<const LibraryModel*>(i.model())->at(i);
    if (!g) return;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    QRect cell = o.rect.adjusted(6, 6, -6, -6);
    int w = cell.width(), areaH = (int)(w * 0.9);
    QRectF area(cell.x(), cell.y(), w, areaH);
    bool sel = o.state & QStyle::State_Selected, hover = o.state & QStyle::State_MouseOver;
    QRectF art;
    qreal radius;
    if (!g->Cover.isNull())
    {
        // box art at its own shape, sitting on the bottom of the art area (like a shelf)
        QSizeF s = g->Cover.size();
        qreal k = std::min(area.width() / s.width(), area.height() / s.height());
        art = QRectF(area.center().x() - s.width() * k / 2, area.bottom() - s.height() * k, s.width() * k, s.height() * k);
        radius = 6;
    }
    else { art = area; radius = 16; }
    QPainterPath clip; clip.addRoundedRect(art, radius, radius);
    // soft shadow
    for (int s = 6; s > 0; s -= 2)
    {
        QPainterPath sh; sh.addRoundedRect(art.adjusted(-s / 2.0, -s / 4.0, s / 2.0, s), radius + s / 2.0, radius + s / 2.0);
        p->fillPath(sh, QColor(0, 0, 0, 14));
    }
    if (!g->Cover.isNull())
    {
        p->setClipPath(clip);
        p->drawImage(art, g->Cover);
        p->setClipping(false);
    }
    else
    {
        QLinearGradient grad(art.topLeft(), art.bottomRight());
        QColor bg = app::Bg();
        auto mix = [&](qreal t) { return QColor::fromRgbF(g->Tint.redF() * (1 - t) + bg.redF() * t, g->Tint.greenF() * (1 - t) + bg.greenF() * t, g->Tint.blueF() * (1 - t) + bg.blueF() * t); };
        grad.setColorAt(0, mix(0.4)); grad.setColorAt(1, mix(0.82));
        p->fillPath(clip, grad);
        if (!g->Icon.isNull())
        {
            qreal s = std::floor(std::min(art.width(), art.height()) * 0.55 / 32) * 32;
            if (s < 32) s = std::min(art.width(), art.height()) * 0.55;
            p->setRenderHint(QPainter::SmoothPixmapTransform, false);   // crisp pixels
            p->drawImage(QRectF(art.center().x() - s / 2, art.center().y() - s / 2, s, s), g->Icon);
            p->setRenderHint(QPainter::SmoothPixmapTransform, true);
        }
    }
    if (sel || hover)
    {
        p->setPen(QPen(sel ? app::Accent() : QColor(255, 255, 255, 90), sel ? 3 : 2));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(art.adjusted(-3, -3, 3, 3), radius + 3, radius + 3);
    }
    QFont small; small.setPixelSize(10); small.setWeight(QFont::Bold);
    p->setFont(small);
    if (g->Favorite)
    {
        QRectF r(art.x() + 6, art.y() + 6, 22, 22);
        QPainterPath b; b.addEllipse(r); p->fillPath(b, QColor(0, 0, 0, 140));
        p->setPen(QColor(0xF5, 0xC5, 0x42)); p->drawText(r, Qt::AlignCenter, QString::fromUtf8("★"));
    }
    if (g->HasPack)
    {
        QRectF r(art.x() + 6, art.bottom() - 26, 34, 20);
        QPainterPath b; b.addRoundedRect(r, 6, 6); p->fillPath(b, QColor(0xF2, 0xA9, 0x3B));
        p->setPen(QColor(0x06, 0x11, 0x1F)); p->drawText(r, Qt::AlignCenter, "HD");
    }
    // the whole name, wrapped (the grid is tall enough for the longest one)
    p->setFont(TitleFont());
    p->setPen(QColor(255, 255, 255, 220));
    QRectF tr(cell.x(), cell.y() + areaH + 10, w, cell.bottom() - (cell.y() + areaH + 10));
    p->drawText(tr, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, g->Title);
    p->restore();
}

// ---- library widget

Library::Library(QWidget* parent) : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette(); pal.setColor(QPalette::Window, app::Bg()); setPalette(pal);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(28, 22, 28, 0);
    v->setSpacing(14);

    // header: brand, title, search, sort, add, settings
    auto* head = new QHBoxLayout;
    auto* titles = new QVBoxLayout; titles->setSpacing(0);
    auto* brand = new QLabel("DUAL HD");
    brand->setStyleSheet(QString("color:%1; font-size:11px; font-weight:800; letter-spacing:2px;").arg(app::Accent().name()));
    auto* title = new QLabel("Nintendo DS");
    title->setStyleSheet("color:white; font-size:28px; font-weight:800;");
    titles->addWidget(brand); titles->addWidget(title);
    head->addLayout(titles);
    Count = new QLabel;
    Count->setStyleSheet("color:#7c8799; font-size:13px; padding-top:18px;");
    head->addWidget(Count);
    head->addStretch(1);
    Search = new QLineEdit;
    Search->setPlaceholderText("Search");
    Search->setClearButtonEnabled(true);
    Search->setFixedWidth(220);
    Search->setStyleSheet("QLineEdit{background:#151b26; color:white; border:1px solid #232c3b; border-radius:9px; padding:7px 10px;}");
    connect(Search, &QLineEdit::textChanged, this, [this](const QString& t) { Model->refilter(t); relayout(); });
    head->addWidget(Search);
    auto tool = [&](const QString& text, const QString& tip) {
        auto* b = new QToolButton; b->setText(text); b->setToolTip(tip);
        b->setStyleSheet("QToolButton{color:white; background:#151b26; border:1px solid #232c3b; border-radius:9px; padding:6px 12px; font-size:13px;}"
                         "QToolButton:hover{background:#1d2534;} QToolButton::menu-indicator{image:none;}");
        b->setPopupMode(QToolButton::InstantPopup);
        head->addWidget(b);
        return b;
    };
    SortBtn = tool("Sort", "How the games are ordered");
    auto* sortMenu = new QMenu(SortBtn);
    for (auto [key, name] : {std::pair<const char*, const char*>{"recent", "Recently Played"}, {"name", "Name"}, {"custom", "Custom Order"}})
    {
        QAction* a = sortMenu->addAction(name);
        a->setCheckable(true);
        a->setData(QString(key));
        connect(a, &QAction::triggered, this, [this, k = QString(key)] { app::Set("sort", k); applySort(); });
    }
    connect(sortMenu, &QMenu::aboutToShow, this, [sortMenu] {
        for (QAction* a : sortMenu->actions()) a->setChecked(a->data().toString() == app::Get("sort").toString());
    });
    SortBtn->setMenu(sortMenu);
    QToolButton* add = tool("+  Add", "Add games");
    auto* addMenu = new QMenu(add);
    connect(addMenu->addAction("Add Games Folder…"), &QAction::triggered, this, &Library::addFolder);
    connect(addMenu->addAction("Open a Game File…"), &QAction::triggered, this, &Library::openFile);
    add->setMenu(addMenu);
    QToolButton* gear = tool(QString::fromUtf8("⚙"), "Settings");
    gear->setPopupMode(QToolButton::DelayedPopup);
    connect(gear, &QToolButton::clicked, this, &Library::openSettings);
    v->addLayout(head);

    Stack = new QStackedWidget;
    v->addWidget(Stack, 1);

    // grid
    Model = new LibraryModel;
    Model->setParent(this);
    Delegate = new GameDelegate(this);
    Grid = new QListView;
    Grid->setModel(Model);
    Grid->setItemDelegate(Delegate);
    Grid->setViewMode(QListView::IconMode);
    Grid->setResizeMode(QListView::Adjust);
    Grid->setMovement(QListView::Static);
    Grid->setUniformItemSizes(true);
    Grid->setSelectionMode(QAbstractItemView::SingleSelection);
    Grid->setMouseTracking(true);
    Grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    Grid->verticalScrollBar()->setSingleStep(24);
    Grid->setFrameShape(QFrame::NoFrame);
    Grid->setContextMenuPolicy(Qt::CustomContextMenu);
    Grid->setStyleSheet("QListView{background:transparent; outline:0;} QListView::item{background:transparent;}"
                        "QScrollBar:vertical{background:transparent; width:10px;} QScrollBar::handle:vertical{background:#2a3446; border-radius:5px; min-height:30px;}"
                        "QScrollBar::add-line,QScrollBar::sub-line{height:0;}");
    connect(Grid, &QListView::activated, this, [this](const QModelIndex& i) { if (const GameEntry* g = Model->at(i)) emit play(g->Path, false); });
    connect(Grid, &QListView::customContextMenuRequested, this, &Library::contextMenu);
    Stack->addWidget(Grid);

    // empty state
    auto* empty = new QWidget;
    auto* ev = new QVBoxLayout(empty);
    ev->addStretch(1);
    auto* icon = new QLabel;
    icon->setPixmap(QPixmap::fromImage(app::AppIcon()).scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    icon->setAlignment(Qt::AlignCenter);
    ev->addWidget(icon);
    auto* h1 = new QLabel("Your DS games, in HD");
    h1->setAlignment(Qt::AlignCenter);
    h1->setStyleSheet("color:white; font-size:30px; font-weight:800; margin-top:10px;");
    ev->addWidget(h1);
    auto* sub = new QLabel("Choose the folder with your .nds files. Games in its sub-folders show up too.");
    sub->setAlignment(Qt::AlignCenter);
    sub->setStyleSheet("color:#9aa6b8; font-size:14px;");
    ev->addWidget(sub);
    auto* btns = new QHBoxLayout; btns->addStretch(1);
    auto* choose = new QPushButton("Choose Games Folder");
    choose->setStyleSheet(QString("QPushButton{background:%1; color:#06111f; font-weight:700; border-radius:10px; padding:10px 18px; font-size:14px;} QPushButton:hover{background:#6db5ff;}").arg(app::Accent().name()));
    connect(choose, &QPushButton::clicked, this, &Library::addFolder);
    auto* open = new QPushButton("Open a Game File");
    open->setStyleSheet("QPushButton{background:#151b26; color:white; border:1px solid #2a3446; border-radius:10px; padding:10px 18px; font-size:14px;} QPushButton:hover{background:#1d2534;}");
    connect(open, &QPushButton::clicked, this, &Library::openFile);
    btns->addWidget(choose); btns->addSpacing(10); btns->addWidget(open); btns->addStretch(1);
    ev->addSpacing(14);
    ev->addLayout(btns);
    ev->addSpacing(30);
    auto* why = new QLabel("WHY DUAL HD");
    why->setAlignment(Qt::AlignCenter);
    why->setStyleSheet("color:#7c8799; font-size:11px; font-weight:800; letter-spacing:2px;");
    ev->addWidget(why);
    auto* cards = new QHBoxLayout; cards->addStretch(1);
    for (auto [t, d] : {std::pair<const char*, const char*>{"Up to 8x resolution", "Sharp 3D and smooth 2D on any screen."},
                        {"Game enhancements", "HD packs for games like Yu-Gi-Oh! 5D's WC2009."},
                        {"Play your way", "Keyboard, Xbox, PlayStation and Switch controllers."}})
    {
        auto* c = new QLabel(QString("<div style='font-size:14px; font-weight:700; color:white'>%1</div><div style='font-size:12px; color:#9aa6b8; margin-top:4px'>%2</div>").arg(t, d));
        c->setWordWrap(true);
        c->setFixedWidth(210);
        c->setStyleSheet("background:#151b26; border:1px solid #232c3b; border-radius:14px; padding:14px;");
        cards->addWidget(c);
    }
    cards->addStretch(1);
    ev->addLayout(cards);
    ev->addStretch(2);
    Stack->addWidget(empty);

    connect(&app::Notifier::I(), &app::Notifier::changed, this, [this](const QString& k) {
        if (k == "folders" || k == "boxart") reload();
    });
    reload();
}

void Library::loadCover(GameEntry& g)
{
    QString custom = CoverDir() + "/" + g.Base + ".png";
    // custom cover: our copy, or a picture with the game's name next to it / in a Covers folder
    QString dir = QFileInfo(g.Path).absolutePath();
    for (const QString& c : {custom, dir + "/" + g.Base + ".png", dir + "/" + g.Base + ".jpg", dir + "/Covers/" + g.Base + ".png", dir + "/Covers/" + g.Base + ".jpg"})
        if (QFileInfo::exists(c)) { g.Cover = QImage(c); if (!g.Cover.isNull()) return; }
    if (app::On("boxart") && !g.Code.isEmpty())
    {
        QString art = ArtDir() + "/" + g.Code + ".png";
        if (QFileInfo::exists(art)) g.Cover = QImage(art);
    }
}

void Library::reload()
{
    QStringList folders = app::Folders();
    QList<GameEntry> all;
    QSet<QString> seen;
    for (const QString& f : folders)
    {
        QList<GameEntry> found;
        ScanFolder(f, found, 1);
        for (GameEntry& g : found) if (!seen.contains(g.Path)) { seen.insert(g.Path); all.push_back(std::move(g)); }
    }
    for (GameEntry& g : all)
    {
        g.Favorite = app::S().value(Key("fav", g.File)).toBool();
        g.Played = app::S().value(Key("played", g.File)).toLongLong();
        QString custom = app::S().value(Key("name", g.File)).toString();
        QString tdb = app::S().value("tdb/" + g.Code).toString().section('\n', 0, 0);
        g.Title = !custom.isEmpty() ? custom : !tdb.isEmpty() ? tdb : g.BannerTitle;
        loadCover(g);
    }
    Model->reset(all, Search->text());
    for (int i = 0; i < Model->All.size(); i++) { fetchInfo(i); fetchArt(i); }
    relayout();
}

void Library::applySort() { Model->refilter(Search->text()); relayout(); }

void Library::relayout()
{
    bool any = !Model->All.isEmpty();
    Stack->setCurrentIndex(any ? 0 : 1);
    Count->setText(any ? QString("%1 game%2").arg(Model->All.size()).arg(Model->All.size() == 1 ? "" : "s") : "");
    int w = Grid->viewport()->width() - 4;
    if (w < 100) w = width() - 60;
    int cols = std::max(2, w / 188);
    int cellW = w / cols;
    // tall enough for the longest name (two lines at least)
    QFontMetrics fm(TitleFont());
    int titleH = fm.lineSpacing() * 2;
    for (const GameEntry& g : Model->All)
        titleH = std::max(titleH, fm.boundingRect(QRect(0, 0, cellW - 12, 2000), Qt::TextWordWrap | Qt::AlignHCenter, g.Title).height());
    titleH = std::min(titleH, fm.lineSpacing() * 6);
    int cellH = (int)((cellW - 12) * 0.9) + 10 + titleH + 22;
    Delegate->Cell = QSize(cellW, cellH);
    Grid->setGridSize(QSize(cellW, cellH));
    Grid->doItemsLayout();
    Grid->viewport()->update();
}

void Library::resizeEvent(QResizeEvent* e) { QWidget::resizeEvent(e); QTimer::singleShot(0, this, [this] { relayout(); }); }

static QString Region(const QString& code)
{
    QChar r = code.size() >= 4 ? code[3] : 'E';
    switch (r.toLatin1())
    {
    case 'E': return "US"; case 'J': return "JA"; case 'K': return "KO"; case 'F': return "FR"; case 'D': return "DE";
    case 'S': return "ES"; case 'I': return "IT"; case 'H': return "NL"; default: return "EN";
    }
}

void Library::fetchArt(int idx)
{
    const GameEntry& g = Model->All[idx];
    if (!app::On("boxart") || g.Code.size() != 4 || !g.Cover.isNull()) return;
    QString code = g.Code;
    QString out = ArtDir() + "/" + code + ".png";
    if (QFileInfo::exists(out) || app::S().value("noart/" + code).toBool()) return;
    QStringList urls;
    for (const QString& kind : {"coverM", "cover"})
        for (const QString& reg : {Region(code), QString("EN"), QString("US")})
            urls << QString("https://art.gametdb.com/ds/%1/%2/%3.png").arg(kind, reg, code);
    urls.removeDuplicates();
    auto tryNext = std::make_shared<std::function<void(int)>>();
    *tryNext = [this, urls, out, code, tryNext](int n) {
        if (n >= urls.size()) { app::S().setValue("noart/" + code, true); return; }
        QNetworkRequest rq{QUrl(urls[n])};
        rq.setHeader(QNetworkRequest::UserAgentHeader, "DualHD/1.0");
        QNetworkReply* r = Net.get(rq);
        connect(r, &QNetworkReply::finished, this, [this, r, n, out, code, tryNext] {
            r->deleteLater();
            QImage img;
            if (r->error() == QNetworkReply::NoError) img.loadFromData(r->readAll());
            if (img.isNull()) { (*tryNext)(n + 1); return; }
            img.save(out);
            for (GameEntry& g : Model->All) if (g.Code == code && g.Cover.isNull()) loadCover(g);
            Grid->viewport()->update();
        });
    };
    (*tryNext)(0);
}

void Library::fetchInfo(int idx)
{
    const GameEntry& g = Model->All[idx];
    if (g.Code.size() != 4 || app::S().contains("tdb/" + g.Code)) return;
    QString code = g.Code;
    QNetworkRequest rq{QUrl("https://www.gametdb.com/DS/" + code)};
    rq.setHeader(QNetworkRequest::UserAgentHeader, "DualHD/1.0");
    QNetworkReply* r = Net.get(rq);
    connect(r, &QNetworkReply::finished, this, [this, r, code] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) return;
        QString html = QString::fromUtf8(r->readAll());
        auto match = [&](const QString& re) {
            QRegularExpressionMatch m = QRegularExpression(re, QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption).match(html);
            if (!m.hasMatch()) return QString();
            QTextDocument d; d.setHtml(m.captured(1));
            return d.toPlainText().simplified();
        };
        QString title = match("title \\(EN\\)\\s*</t[dh]>\\s*<td[^>]*>(.*?)</td>");
        QString pub = match("publisher\\s*</t[dh]>\\s*<td[^>]*>(.*?)</td>");
        if (title.isEmpty()) return;
        app::S().setValue("tdb/" + code, title + "\n" + pub);
        for (GameEntry& g : Model->All)
            if (g.Code == code && app::S().value(Key("name", g.File)).toString().isEmpty()) g.Title = title;
        applySort();
    });
}

QString Library::selectedRom() const
{
    const GameEntry* g = Model->at(Grid->currentIndex());
    return g ? g->Path : QString();
}

void Library::moveSelection(int dx, int dy)
{
    int n = Model->rowCount();
    if (!n) return;
    int cols = std::max(1, Grid->viewport()->width() / std::max(1, Delegate->Cell.width()));
    int cur = Grid->currentIndex().isValid() ? Grid->currentIndex().row() : -1;
    int next = cur < 0 ? 0 : std::clamp(cur + dx + dy * cols, 0, n - 1);
    QModelIndex i = Model->index(next);
    Grid->setCurrentIndex(i);
    Grid->scrollTo(i);
}

void Library::playSelected()
{
    if (const GameEntry* g = Model->at(Grid->currentIndex())) emit play(g->Path, false);
    else moveSelection(0, 0);
}

void Library::addFolder()
{
    QString d = QFileDialog::getExistingDirectory(this, "Choose a games folder", QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    if (d.isEmpty()) return;
    QStringList f = app::Folders();
    if (!f.contains(d)) { f << d; app::SetFolders(f); }
}

void Library::openFile()
{
    QString p = QFileDialog::getOpenFileName(this, "Open a DS game", QStandardPaths::writableLocation(QStandardPaths::HomeLocation), "DS games (*.nds)");
    if (p.isEmpty()) return;
    // its folder joins the library, so the game shows up next time too
    QString d = QFileInfo(p).absolutePath();
    QStringList f = app::Folders();
    if (!f.contains(d)) { f << d; app::SetFolders(f); }
    emit play(p, false);
}

void Library::contextMenu(const QPoint& at)
{
    QModelIndex idx = Grid->indexAt(at);
    GameEntry* g = Model->at(idx);
    if (!g) return;
    Grid->setCurrentIndex(idx);
    QString file = g->File, path = g->Path, base = g->Base, code = g->Code;
    QMenu m(this);
    connect(m.addAction("Play"), &QAction::triggered, this, [this, path] { emit play(path, false); });
    QString saves = QFileInfo(path).absolutePath() + "/Saves/" + base + ".ml0";
    if (QFileInfo::exists(saves))
        connect(m.addAction("Start Over (ignore the resume point)"), &QAction::triggered, this, [this, path] { emit play(path, true); });
    m.addSeparator();
    connect(m.addAction(g->Favorite ? "Remove from Favorites" : "Add to Favorites"), &QAction::triggered, this, [this, file, fav = g->Favorite] {
        app::S().setValue(Key("fav", file), !fav);
        for (GameEntry& x : Model->All) if (x.File == file) x.Favorite = !fav;
        applySort();
    });
    connect(m.addAction("Rename…"), &QAction::triggered, this, [this, file, title = g->Title] {
        bool ok = false;
        QString n = QInputDialog::getText(this, "Rename", "Name in the library (empty = the game's own name):", QLineEdit::Normal, title, &ok);
        if (!ok) return;
        app::S().setValue(Key("name", file), n.trimmed());
        reload();
    });
    connect(m.addAction("Choose Cover…"), &QAction::triggered, this, [this, base] {
        QString p = QFileDialog::getOpenFileName(this, "Choose a cover picture", QString(), "Pictures (*.png *.jpg *.jpeg *.webp)");
        if (p.isEmpty()) return;
        QImage img(p);
        if (img.isNull()) { QMessageBox::warning(this, "Dual HD", "That picture couldn't be opened."); return; }
        img.save(CoverDir() + "/" + base + ".png");
        reload();
    });
    if (QFileInfo::exists(CoverDir() + "/" + base + ".png"))
        connect(m.addAction("Use the Standard Cover"), &QAction::triggered, this, [this, base] { QFile::remove(CoverDir() + "/" + base + ".png"); reload(); });
    if (app::Get("sort").toString() == "custom")
    {
        m.addSeparator();
        auto move = [this, file](int d) {
            QStringList order;
            for (int i : Model->Shown) order << Model->All[i].File;
            int at = order.indexOf(file);
            int to = std::clamp(at + d, 0, (int)order.size() - 1);
            if (at < 0 || to == at) return;
            order.move(at, to);
            app::S().setValue("order", order);
            applySort();
            for (int r = 0; r < Model->Shown.size(); r++) if (Model->All[Model->Shown[r]].File == file) Grid->setCurrentIndex(Model->index(r));
        };
        connect(m.addAction("Move Earlier"), &QAction::triggered, this, [move] { move(-1); });
        connect(m.addAction("Move Later"), &QAction::triggered, this, [move] { move(1); });
    }
    if (app::PackFor(code))
    {
        m.addSeparator();
        connect(m.addAction("Game Enhancements…"), &QAction::triggered, this, [this, path] { emit openGamePage(path); });
    }
    m.addSeparator();
#ifdef Q_OS_MACOS
    QString reveal = "Show in Finder";
#else
    QString reveal = "Show in Explorer";
#endif
    connect(m.addAction(reveal), &QAction::triggered, this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())); });
    m.exec(Grid->viewport()->mapToGlobal(at));
}
