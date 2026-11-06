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
    // Under xcb Qt picks the gtk3 platform theme, which doesn't see GNOME's light / dark
    // preference. The portal theme reads it (and still wraps gtk3 for everything else),
    // so the System theme can follow the desktop.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
    // Qt logs to journald when stderr is not a tty; keep input diagnostics in the terminal.
    if (qEnvironmentVariableIsSet("SHAPR_DEBUG_INPUT"))
        qputenv("QT_FORCE_STDERR_LOGGING", "1");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ShaprLinux"));
    QApplication::setApplicationName(QStringLiteral("Shapr Linux"));
    // Fusion honours application palettes everywhere, which the light / dark themes rely on.
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    MainWindow window;
    window.resize(1400, 900);
    window.show();
    // shapr_linux model.shapr opens the project.
    if (const QStringList args = QApplication::arguments(); args.size() > 1)
        window.openPath(args.at(1));

    // The xcb backend registers input devices only once a window exists.
    if (qEnvironmentVariableIsSet("SHAPR_DEBUG_INPUT")) {
        for (const QInputDevice* dev : QInputDevice::devices())
            qInfo() << "input device:" << dev->name() << dev->type() << dev->capabilities();
    }

    return app.exec();
}
