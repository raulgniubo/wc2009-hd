// The pause menu over the game (Esc, or the controller's Home button): works with the mouse,
// the keyboard and a controller.
#pragma once
#include <QWidget>
#include <functional>

class QVBoxLayout;
class QPushButton;
class QLabel;

class PauseMenu : public QWidget
{
    Q_OBJECT
public:
    struct Item { QString Title; std::function<void()> Run; bool KeepOpen = false; };
    explicit PauseMenu(QWidget* parent);
    void open(const QString& title, const QList<Item>& items);   // replaces what's shown
    void close();
    bool isOpen() const { return isVisible(); }
    void move(int step);
    void activate();
    void back();
    std::function<void()> OnClose;   // the menu was dismissed (Resume / Esc / B)
    std::function<void()> OnBack;    // a sub-page wants to go back to the main page (nullptr = close)

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void select(int i);
    QWidget* CardW;
    QVBoxLayout* List;
    QLabel* Title;
    QList<QPushButton*> Buttons;
    QList<Item> Items;
    int Sel = 0;
};
