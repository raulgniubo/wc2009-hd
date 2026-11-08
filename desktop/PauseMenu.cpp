#include "PauseMenu.h"
#include "App.h"
#include <QBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyle>

PauseMenu::PauseMenu(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    CardW = new QWidget(this);
    CardW->setObjectName("card");
    CardW->setStyleSheet("#card{background:rgba(21,27,38,240); border:1px solid #2a3446; border-radius:18px;}"
                         "QPushButton{color:white; background:transparent; border:none; border-radius:10px; padding:10px 16px; font-size:15px; text-align:left;}"
                         "QPushButton[sel=\"true\"]{background:#24426e;}"
                         "QPushButton:hover{background:#1d2a40;}");
    auto* v = new QVBoxLayout(CardW);
    v->setContentsMargins(12, 16, 12, 12); v->setSpacing(2);
    Title = new QLabel;
    Title->setAlignment(Qt::AlignCenter);
    Title->setStyleSheet("color:white; font-size:17px; font-weight:800; padding-bottom:8px;");
    v->addWidget(Title);
    List = new QVBoxLayout; List->setSpacing(2);
    v->addLayout(List);
    hide();
}

void PauseMenu::open(const QString& title, const QList<Item>& items)
{
    Title->setText(title);
    for (QPushButton* b : Buttons) b->deleteLater();
    Buttons.clear();
    Items = items;
    for (int i = 0; i < Items.size(); i++)
    {
        auto* b = new QPushButton(Items[i].Title);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, i] { Sel = i; activate(); });
        List->addWidget(b);
        Buttons << b;
    }
    setGeometry(parentWidget()->rect());
    show();
    raise();
    setFocus();
    select(0);
    resizeEvent(nullptr);
}

void PauseMenu::close() { hide(); if (parentWidget()) parentWidget()->setFocus(); }

void PauseMenu::select(int i)
{
    if (Buttons.isEmpty()) return;
    Sel = (i + Buttons.size()) % Buttons.size();
    for (int k = 0; k < Buttons.size(); k++)
    {
        Buttons[k]->setProperty("sel", k == Sel);
        Buttons[k]->style()->unpolish(Buttons[k]); Buttons[k]->style()->polish(Buttons[k]);
    }
}

void PauseMenu::move(int step) { select(Sel + step); }

void PauseMenu::activate()
{
    if (Sel < 0 || Sel >= Items.size()) return;
    Item it = Items[Sel];
    if (!it.KeepOpen) { hide(); }
    if (it.Run) it.Run();
}

void PauseMenu::back()
{
    if (OnBack) { auto f = OnBack; OnBack = nullptr; f(); return; }
    hide();
    if (OnClose) OnClose();
}

void PauseMenu::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 140));
}

void PauseMenu::keyPressEvent(QKeyEvent* e)
{
    switch (e->key())
    {
    case Qt::Key_Up: move(-1); break;
    case Qt::Key_Down: move(1); break;
    case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space: activate(); break;
    case Qt::Key_Escape: case Qt::Key_Backspace: back(); break;
    default: QWidget::keyPressEvent(e);
    }
}

void PauseMenu::mousePressEvent(QMouseEvent* e)
{
    if (!CardW->geometry().contains(e->position().toPoint())) back();
}

void PauseMenu::resizeEvent(QResizeEvent*)
{
    CardW->adjustSize();
    int w = std::max(320, CardW->sizeHint().width());
    int h = CardW->sizeHint().height();
    CardW->setGeometry((width() - w) / 2, (height() - h) / 2, w, h);
}
