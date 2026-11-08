// Settings: display, game enhancements, controls, hotkeys, sound & play, library, about.
#pragma once
#include <QDialog>

class QListWidget;
class QStackedWidget;
class QVBoxLayout;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    enum Page { Display, Enhancements, Controls, Hotkeys, Play, LibraryPage, About };
    explicit SettingsDialog(QWidget* parent = nullptr);
    void showPage(int page);
    void showGame(const QString& romPath);   // the enhancements page, for this game

private:
    QWidget* page(const QString& title, QVBoxLayout*& body);
    QWidget* buildDisplay();
    QWidget* buildEnhancements();
    QWidget* buildControls();
    QWidget* buildHotkeys();
    QWidget* buildPlay();
    QWidget* buildLibrary();
    QWidget* buildAbout();
    void rebuild(int page);

    QListWidget* Side;
    QStackedWidget* Pages;
    QString FocusRom;
};
