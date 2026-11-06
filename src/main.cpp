#include "app/MainWindow.h"

#include <QApplication>
#include <QDebug>
#include <QInputDevice>

int main(int argc, char** argv)
{
    // Arch's OpenCASCADE is built against GLX, which needs an X11 surface.
    // On Wayland this runs through XWayland; pen and touch still work there.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_glx");
    // Qt logs to journald when stderr is not a tty; keep input diagnostics in the terminal.
    if (qEnvironmentVariableIsSet("SHAPR_DEBUG_INPUT"))
        qputenv("QT_FORCE_STDERR_LOGGING", "1");

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Shapr Linux"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    MainWindow window;
    window.resize(1400, 900);
    window.show();

    // The xcb backend registers input devices only once a window exists.
    if (qEnvironmentVariableIsSet("SHAPR_DEBUG_INPUT")) {
        for (const QInputDevice* dev : QInputDevice::devices())
            qInfo() << "input device:" << dev->name() << dev->type() << dev->capabilities();
    }

    return app.exec();
}
