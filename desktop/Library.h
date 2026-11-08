// The library: every game in the games folders, with box art and full names (GameTDB), custom
// covers and names, favourites first, sorted by recently played, name or a custom order.
#pragma once
#include <QWidget>
#include <QSet>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QImage>
#include <QAbstractListModel>
#include <QStyledItemDelegate>

class QListView;
class QLineEdit;
class QStackedWidget;
class QToolButton;
class QLabel;

struct GameEntry
{
    QString Path, File, Base, Code, BannerTitle, Publisher, Title;
    QImage Icon, Cover;
    QColor Tint;
    bool Favorite = false, HasPack = false, IsPackGame = false;
    qint64 Played = 0;
};

class LibraryModel : public QAbstractListModel
{
    Q_OBJECT
public:
    QList<GameEntry> All;     // everything found
    QList<int> Shown;         // indices into All, in display order
    int rowCount(const QModelIndex& = {}) const override { return Shown.size(); }
    QVariant data(const QModelIndex& i, int role) const override;
    const GameEntry* at(const QModelIndex& i) const { return i.isValid() && i.row() < Shown.size() ? &All[Shown[i.row()]] : nullptr; }
    GameEntry* at(const QModelIndex& i) { return i.isValid() && i.row() < Shown.size() ? &All[Shown[i.row()]] : nullptr; }
    void reset(QList<GameEntry> all, const QString& filter);
    void refilter(const QString& filter);
};

class GameDelegate : public QStyledItemDelegate
{
public:
    QSize Cell{180, 240};
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& i) const override;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return Cell; }
};

class Library : public QWidget
{
    Q_OBJECT
public:
    explicit Library(QWidget* parent = nullptr);
    void reload();                       // rescan the folders
    void refreshIfChanged();             // cheap check (folder contents), rescan only if something changed
    void moveSelection(int dx, int dy);  // controller navigation
    void playSelected();
    QString selectedRom() const;

signals:
    void play(const QString& romPath, bool fresh);
    void openSettings();
    void openGamePage(const QString& romPath);   // per-game enhancements page

public slots:
    void addFolder();
    void openFile();

protected:
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void relayout();
    void contextMenu(const QPoint& at);
    void fetchInfo(int idx);
    QSet<QString> Busy;   // codes with a download under way
    void fetchArt(int idx);
    void applySort();
    void loadCover(GameEntry& g);

    LibraryModel* Model;
    GameDelegate* Delegate;
    QListView* Grid;
    QLineEdit* Search;
    QStackedWidget* Stack;
    QToolButton* SortBtn;
    QLabel* Count;
    QFileSystemWatcher Watcher;   // the games folders: new or removed games show up by themselves
    QTimer ReloadSoon;
    QString LastSignature;
    void watchFolders();
};
