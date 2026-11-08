// Dual HD for Mac and Windows.
#include "MainWindow.h"
#include "App.h"
#include "Pads.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QStyleFactory>
#include <QPalette>
#include <QFileOpenEvent>
#include <QFontDatabase>
#include <QPointer>


namespace
{
QPointer<MainWindow> Window;
QString PendingOpen;

class DualHDApp : public QApplication
{
public:
    using QApplication::QApplication;
    bool event(QEvent* e) override
    {
        // a .nds opened from the Finder
        if (e->type() == QEvent::FileOpen)
        {
            QString f = static_cast<QFileOpenEvent*>(e)->file();
            if (Window) Window->openRom(f); else PendingOpen = f;
            return true;
        }
        return QApplication::event(e);
    }
};
}

int main(int argc, char** argv)
{
    QApplication::setApplicationName("Dual HD");
    QApplication::setOrganizationName("Dual HD");
    QApplication::setApplicationVersion("1.0");
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    // OpenGL 3.3 core everywhere (4.1 on Macs); vsync from the settings
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(0);
    fmt.setStencilBufferSize(0);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setSwapInterval(app::On("vsync") ? 1 : 0);
    QSurfaceFormat::setDefaultFormat(fmt);

    DualHDApp a(argc, argv);
    a.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, app::Bg());
    p.setColor(QPalette::WindowText, QColor(0xE6, 0xEC, 0xF4));
    p.setColor(QPalette::Base, QColor(0x0F, 0x14, 0x1E));
    p.setColor(QPalette::AlternateBase, app::Card());
    p.setColor(QPalette::Text, QColor(0xE6, 0xEC, 0xF4));
    p.setColor(QPalette::Button, QColor(0x1B, 0x24, 0x33));
    p.setColor(QPalette::ButtonText, Qt::white);
    p.setColor(QPalette::Highlight, QColor(0x24, 0x42, 0x6E));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, app::Card());
    p.setColor(QPalette::ToolTipText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(0x7C, 0x87, 0x99));
    a.setPalette(p);
    QFontDatabase::addApplicationFont(":/fonts/Inter-Medium.ttf");
    QFontDatabase::addApplicationFont(":/fonts/Inter-Bold.ttf");

    pads::Init();
    MainWindow w;
    Window = &w;
    w.show();
    QStringList args = a.arguments();
    if (args.size() > 1 && args.last().endsWith(".nds", Qt::CaseInsensitive)) w.openRom(args.last());
    else if (!PendingOpen.isEmpty()) w.openRom(PendingOpen);
    int r = a.exec();
    pads::Quit();
    return r;
}
