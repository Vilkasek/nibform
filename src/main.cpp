#include "app/Language.h"
#include "app/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QIcon>
#include <QSettings>
#include <QInputDevice>

#ifdef NIBFORM_HAS_SCRIPTING
#include "script/ScriptEngine.h"

#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>
#include <QCoreApplication>

#include <clocale>
#include <cstdio>
#include <cstring>

// `nibform --run script.py [arguments]`: the script runs with no window (sys.argv is the
// script and its arguments; nf.document() a new project), and its exit code is the app's.
static int runHeadless(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Nibform"));
    QCoreApplication::setApplicationName(QStringLiteral("Nibform"));
    std::setlocale(LC_NUMERIC, "C");
    // OpenCASCADE's own reports (STEP writing statistics) would mix with the script's output.
    Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_PrinterOStream));
    if (argc < 3) {
        std::fprintf(stderr, "Usage: nibform --run script.py [arguments]\n");
        return 2;
    }
    ScriptEngine& engine = ScriptEngine::instance();
    std::string error;
    if (!engine.start(&error)) {
        std::fprintf(stderr, "Python didn't start: %s\n", error.c_str());
        return 1;
    }
    engine.trustEverything(true); // the user runs it, and the projects it opens
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i)
        args.emplace_back(argv[i]);
    const auto result = engine.runFile(argv[2], std::make_shared<ScriptDocument>(),
                                       {[](const std::string& s) { std::fputs(s.c_str(), stdout); },
                                        [](const std::string& s) { std::fputs(s.c_str(), stderr); }},
                                       args);
    if (!result.ok && !result.error.empty())
        std::fprintf(stderr, "%s\n", result.error.c_str());
    engine.stop();
    return result.ok ? 0 : (result.exitCode != 0 ? result.exitCode : 1);
}
#endif

int main(int argc, char** argv)
{
#ifdef NIBFORM_HAS_SCRIPTING
    if (argc >= 2 && std::strcmp(argv[1], "--run") == 0)
        return runHeadless(argc, argv);
#endif
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
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
#endif
    // Qt logs to journald when stderr is not a tty; keep input diagnostics in the terminal.
    if (qEnvironmentVariableIsSet("NIBFORM_DEBUG_INPUT"))
        qputenv("QT_FORCE_STDERR_LOGGING", "1");

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Nibform"));
    QApplication::setApplicationName(QStringLiteral("Nibform"));
    // Settings from before the app was renamed come along on the first run.
    if (QSettings settings; settings.allKeys().isEmpty()) {
        const QSettings former(QStringLiteral("ShaprLinux"), QStringLiteral("Shapr Linux"));
        for (const QString& key : former.allKeys())
            settings.setValue(key, former.value(key));
    }
    // Fusion honours application palettes everywhere, which the light / dark themes rely on.
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    // Matches the installed .desktop file: the desktop shows its icon and name for the window.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.vilkasek.Nibform"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("io.github.vilkasek.Nibform"),
                                                 QIcon(QStringLiteral(":/nibform.png"))));

    applyLanguage(savedLanguage()); // before any text is made

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Nibform – pen-first parametric CAD"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("A .nibform project to open"),
                                 QStringLiteral("[project]"));
#ifdef NIBFORM_HAS_SCRIPTING
    parser.setApplicationDescription(QStringLiteral("Nibform – pen-first parametric CAD\n\n"
                                                    "nibform --run script.py [arguments]  runs a Python script with no window"));
#endif
    parser.process(app);

    MainWindow window;
    window.resize(1400, 900);
    // Dev aid: NIBFORM_WINDOW_SIZE=800x1280 tries a portrait (rotated tablet) layout.
    if (const QStringList size = qEnvironmentVariable("NIBFORM_WINDOW_SIZE").split(QLatin1Char('x')); size.size() == 2)
        window.resize(size[0].toInt(), size[1].toInt());
    window.show();
    // Work left by a crash comes first; `nibform model.nibform` then opens the project
    // (unless the restored work is in its place, which would need saving first).
    const bool restored = window.offerRecovery();
    if (const QStringList args = parser.positionalArguments(); !args.isEmpty() && !restored)
        window.openPath(args.first());

    // The xcb backend registers input devices only once a window exists.
    if (qEnvironmentVariableIsSet("NIBFORM_DEBUG_INPUT")) {
        for (const QInputDevice* dev : QInputDevice::devices())
            qInfo() << "input device:" << dev->name() << dev->type() << dev->capabilities();
    }

    const int code = app.exec();
#ifdef NIBFORM_HAS_SCRIPTING
    ScriptEngine::instance().stop(); // before the window's objects go: Python can't end later
#endif
    return code;
}
