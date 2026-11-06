#include "app/MainWindow.h"

#include "sketch/SketchEditor.h"
#include "viewer/OcctViewerWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QSettings>
#include <QStyleHints>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Shapr Linux"));

    m_viewer = new OcctViewerWidget(this);
    setCentralWidget(m_viewer);

    m_editor = new SketchEditor(m_viewer, this);
    m_viewer->setInteraction(m_editor);

    m_hintLabel = new QLabel(this);
    m_dofLabel = new QLabel(this);
    m_inputLabel = new QLabel(this);
    m_glLabel = new QLabel(this);
    statusBar()->addWidget(m_hintLabel, 1);
    statusBar()->addPermanentWidget(m_dofLabel);
    statusBar()->addPermanentWidget(m_inputLabel);
    statusBar()->addPermanentWidget(m_glLabel);

    buildActions();


    connect(m_editor, &SketchEditor::hintChanged, m_hintLabel, &QLabel::setText);
    connect(m_editor, &SketchEditor::statusChanged, m_dofLabel, &QLabel::setText);
    connect(m_editor, &SketchEditor::message, this, [this](const QString& text) {
        statusBar()->showMessage(text, 4000);
    });
    connect(m_editor, &SketchEditor::toolChanged, this, [this](ToolKind kind) {
        if (QAction* action = m_toolActions[kind])
            action->setChecked(true);
    });
    connect(m_editor, &SketchEditor::historyChanged, this, &MainWindow::updateEditActions);
    connect(m_editor, &SketchEditor::selectionChanged, this, &MainWindow::updateEditActions);
    // Remembered theme; System also follows the desktop when it switches.
    const QString saved = QSettings().value(QStringLiteral("theme")).toString();
    setTheme(saved == QLatin1String("light") ? ThemeMode::Light
             : saved == QLatin1String("dark") ? ThemeMode::Dark
                                              : ThemeMode::System);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_theme == ThemeMode::System)
            applyTheme();
    });

    m_editor->setTool(ToolKind::Line);
    updateEditActions();

    connect(m_viewer, &OcctViewerWidget::glReady, this, [this](const QString& info) {
        m_glLabel->setText(info);
        // Defer until the first frame has sized the view, otherwise FitAll sees a 0x0 viewport.
        QTimer::singleShot(0, this, [this] { m_viewer->setProjection(V3d_XposYnegZpos); });

        // Dev aid: SHAPR_SCREENSHOT=/path.png renders one frame, saves it and quits.
        const QString shotPath = qEnvironmentVariable("SHAPR_SCREENSHOT");
        if (!shotPath.isEmpty()) {
            QTimer::singleShot(1500, this, [this, shotPath] {
                grab().save(shotPath);
                QApplication::quit();
            });
        }
    });
    connect(m_viewer, &OcctViewerWidget::glFailed, this, [this](const QString& reason) {
        QMessageBox::critical(this, QStringLiteral("Błąd OpenGL"), reason);
    });
    connect(m_viewer, &OcctViewerWidget::penInput, this, &MainWindow::onPenInput);
}

void MainWindow::buildActions()
{
    // --- Edit menu -------------------------------------------------------------------
    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Edycja"));
    m_undoAction = edit->addAction(QStringLiteral("Cofnij"), m_editor, &SketchEditor::undo);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = edit->addAction(QStringLiteral("Ponów"), m_editor, &SketchEditor::redo);
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    edit->addSeparator();
    m_deleteAction = edit->addAction(QStringLiteral("Usuń zaznaczone"), m_editor, &SketchEditor::deleteSelection);
    m_deleteAction->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    edit->addAction(QStringLiteral("Wyczyść szkic"), m_editor, &SketchEditor::clearActiveSketch);
    edit->addSeparator();
    QAction* cancel = edit->addAction(QStringLiteral("Przerwij / odznacz"), m_editor, &SketchEditor::cancel);
    cancel->setShortcut(QKeySequence(Qt::Key_Escape));
    QAction* confirm = edit->addAction(QStringLiteral("Zakończ / edytuj wymiar"), m_editor, &SketchEditor::confirm);
    confirm->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});

    // --- Tool bar ----------------------------------------------------------------------
    QToolBar* bar = addToolBar(QStringLiteral("Narzędzia"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto* tools = new QActionGroup(this);
    auto addTool = [&](ToolKind kind, const QString& name, Qt::Key key) {
        QAction* action = bar->addAction(name);
        action->setCheckable(true);
        action->setShortcut(QKeySequence(key));
        action->setToolTip(QStringLiteral("%1 (%2)").arg(name, QKeySequence(key).toString()));
        tools->addAction(action);
        connect(action, &QAction::triggered, this, [this, kind] { m_editor->setTool(kind); });
        m_toolActions[kind] = action;
    };
    addTool(ToolKind::Select, QStringLiteral("Zaznacz"), Qt::Key_S);
    addTool(ToolKind::Line, QStringLiteral("Linia"), Qt::Key_L);
    addTool(ToolKind::Rectangle, QStringLiteral("Prostokąt"), Qt::Key_R);
    addTool(ToolKind::Circle, QStringLiteral("Okrąg"), Qt::Key_C);
    addTool(ToolKind::Arc, QStringLiteral("Łuk"), Qt::Key_A);
    addTool(ToolKind::Trim, QStringLiteral("Przytnij"), Qt::Key_X);
    addTool(ToolKind::Offset, QStringLiteral("Odsunięcie"), Qt::Key_U);

    bar->addSeparator();
    bar->addWidget(new QLabel(QStringLiteral(" Płaszczyzna: "), bar));
    auto* plane = new QComboBox(bar);
    plane->setFocusPolicy(Qt::NoFocus);
    plane->addItem(QStringLiteral("Podłoże (XY)"), static_cast<int>(SketchPlane::XY));
    plane->addItem(QStringLiteral("Przód (XZ)"), static_cast<int>(SketchPlane::XZ));
    plane->addItem(QStringLiteral("Prawo (YZ)"), static_cast<int>(SketchPlane::YZ));
    connect(plane, &QComboBox::currentIndexChanged, this, [this, plane](int index) {
        m_editor->setPlane(static_cast<SketchPlane>(plane->itemData(index).toInt()));
    });
    bar->addWidget(plane);

    bar->addSeparator();
    QAction* fit = bar->addAction(QStringLiteral("Dopasuj"), m_viewer, &OcctViewerWidget::fitAll);
    fit->setShortcut(QKeySequence(Qt::Key_F));
    fit->setToolTip(QStringLiteral("Dopasuj widok (F)"));

    // --- Views: digits = faces, Ctrl+digit = corners, Shift+digit = edges --------------
    QMenu* views = menuBar()->addMenu(QStringLiteral("Wi&dok"));
    views->addAction(fit);

    QMenu* themes = views->addMenu(QStringLiteral("Motyw"));
    auto* themeGroup = new QActionGroup(this);
    for (const auto& [mode, name] : {std::pair(ThemeMode::System, QStringLiteral("Systemowy")),
                                     std::pair(ThemeMode::Light, QStringLiteral("Jasny")),
                                     std::pair(ThemeMode::Dark, QStringLiteral("Ciemny"))}) {
        QAction* action = themes->addAction(name, this, [this, mode] { setTheme(mode); });
        action->setCheckable(true);
        themeGroup->addAction(action);
        m_themeActions[mode] = action;
    }
    auto addView = [this, views](const QString& name, V3d_TypeOfOrientation orientation, QKeySequence key) {
        QAction* action = views->addAction(name, this, [this, orientation] { m_viewer->setProjection(orientation); });
        action->setShortcut(key);
        action->setToolTip(QStringLiteral("%1 (%2)").arg(name, key.toString(QKeySequence::NativeText)));
        return action;
    };
    const Qt::Key digits[] = {Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5,
                              Qt::Key_6, Qt::Key_7, Qt::Key_8, Qt::Key_9, Qt::Key_0};

    views->addSection(QStringLiteral("Ściany"));
    const std::pair<const char*, V3d_TypeOfOrientation> faces[] = {
        {"Góra", V3d_Zpos}, {"Przód", V3d_Yneg}, {"Prawo", V3d_Xpos},
        {"Tył", V3d_Ypos},  {"Lewo", V3d_Xneg},  {"Dół", V3d_Zneg},
    };
    std::vector<QAction*> faceActions;
    for (int i = 0; i < 6; ++i)
        faceActions.push_back(addView(QString::fromUtf8(faces[i].first), faces[i].second, QKeySequence(digits[i])));

    // Top ring first, then the bottom one, both going front-right -> back-right -> back-left -> front-left.
    views->addSection(QStringLiteral("Narożniki"));
    const std::pair<const char*, V3d_TypeOfOrientation> corners[] = {
        {"Góra-przód-prawo", V3d_XposYnegZpos}, {"Góra-tył-prawo", V3d_XposYposZpos},
        {"Góra-tył-lewo", V3d_XnegYposZpos},    {"Góra-przód-lewo", V3d_XnegYnegZpos},
        {"Dół-przód-prawo", V3d_XposYnegZneg},  {"Dół-tył-prawo", V3d_XposYposZneg},
        {"Dół-tył-lewo", V3d_XnegYposZneg},     {"Dół-przód-lewo", V3d_XnegYnegZneg},
    };
    std::vector<QAction*> cornerActions;
    for (int i = 0; i < 8; ++i) {
        cornerActions.push_back(addView(QString::fromUtf8(corners[i].first), corners[i].second,
                                        QKeySequence(Qt::CTRL | digits[i])));
    }

    // Twelve edges but ten digits: the two least used bottom edges go to Ctrl+Shift+9 / 0.
    views->addSection(QStringLiteral("Krawędzie"));
    const std::pair<const char*, V3d_TypeOfOrientation> edges[] = {
        {"Góra-przód", V3d_YnegZpos},   {"Góra-prawo", V3d_XposZpos},
        {"Góra-tył", V3d_YposZpos},     {"Góra-lewo", V3d_XnegZpos},
        {"Przód-prawo", V3d_XposYneg},  {"Tył-prawo", V3d_XposYpos},
        {"Tył-lewo", V3d_XnegYpos},     {"Przód-lewo", V3d_XnegYneg},
        {"Dół-przód", V3d_YnegZneg},    {"Dół-tył", V3d_YposZneg},
    };
    for (int i = 0; i < 10; ++i)
        addView(QString::fromUtf8(edges[i].first), edges[i].second, QKeySequence(Qt::SHIFT | digits[i]));
    addView(QStringLiteral("Dół-prawo"), V3d_XposZneg, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_9));
    addView(QStringLiteral("Dół-lewo"), V3d_XnegZneg, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_0));

    // The toolbar keeps its quick buttons, sharing the menu's actions (and shortcuts).
    QAction* iso = bar->addAction(QStringLiteral("Izometria"));
    iso->setToolTip(cornerActions[0]->toolTip());
    connect(iso, &QAction::triggered, cornerActions[0], &QAction::trigger);
    bar->addAction(faceActions[0]);
    bar->addAction(faceActions[1]);
    bar->addAction(faceActions[2]);

    // --- Constraints: select geometry, then pick what should hold -----------------------
    addToolBarBreak();
    QToolBar* constraints = addToolBar(QStringLiteral("Więzy"));
    m_constraintBar = constraints;
    constraints->setMovable(false);
    constraints->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QMenu* constraintMenu = menuBar()->addMenu(QStringLiteral("&Więzy"));
    auto addConstraint = [&](ConstraintCommand command, const QString& name, Qt::Key key, const QString& help) {
        QAction* action = constraints->addAction(name);
        constraintMenu->addAction(action);
        action->setShortcut(QKeySequence(key));
        action->setToolTip(QStringLiteral("%1 (%2)\n%3").arg(name, QKeySequence(key).toString(), help));
        connect(action, &QAction::triggered, this, [this, command] { m_editor->applyConstraint(command); });
        m_constraintActions[command] = action;
    };
    addConstraint(ConstraintCommand::Dimension, QStringLiteral("Wymiar"), Qt::Key_D,
                  QStringLiteral("Linia lub 2 punkty: odległość · punkt i linia: odległość od linii · "
                                 "2 równoległe linie: odstęp · 2 inne linie: kąt · okrąg: średnica · łuk: promień"));
    constraints->addSeparator();
    constraintMenu->addSeparator();
    addConstraint(ConstraintCommand::Horizontal, QStringLiteral("Poziomo"), Qt::Key_H,
                  QStringLiteral("Linie albo 2 punkty"));
    addConstraint(ConstraintCommand::Vertical, QStringLiteral("Pionowo"), Qt::Key_V,
                  QStringLiteral("Linie albo 2 punkty"));
    addConstraint(ConstraintCommand::Parallel, QStringLiteral("Równoległe"), Qt::Key_P, QStringLiteral("2+ linie"));
    addConstraint(ConstraintCommand::Perpendicular, QStringLiteral("Prostopadłe"), Qt::Key_N,
                  QStringLiteral("2 linie"));
    addConstraint(ConstraintCommand::Equal, QStringLiteral("Równe"), Qt::Key_E,
                  QStringLiteral("2+ linie (długość) albo 2+ okręgi/łuki (promień)"));
    addConstraint(ConstraintCommand::Tangent, QStringLiteral("Styczne"), Qt::Key_T,
                  QStringLiteral("Linia i okrąg/łuk albo 2 okręgi/łuki"));
    addConstraint(ConstraintCommand::Concentric, QStringLiteral("Współśrodkowe"), Qt::Key_O,
                  QStringLiteral("2+ okręgi/łuki"));
    addConstraint(ConstraintCommand::Coincident, QStringLiteral("Wspólny punkt"), Qt::Key_I,
                  QStringLiteral("2 punkty (łączy je) albo punkt i krzywa (punkt na krzywej)"));
    addConstraint(ConstraintCommand::Midpoint, QStringLiteral("Środek"), Qt::Key_M,
                  QStringLiteral("Punkt i linia"));
    addConstraint(ConstraintCommand::Fix, QStringLiteral("Zablokuj"), Qt::Key_Z,
                  QStringLiteral("Punkty albo krzywe: przypina je w obecnym miejscu"));
}

void MainWindow::setTheme(ThemeMode mode)
{
    m_theme = mode;
    m_themeActions[mode]->setChecked(true);
    QSettings().setValue(QStringLiteral("theme"), mode == ThemeMode::Light  ? QStringLiteral("light")
                                                  : mode == ThemeMode::Dark ? QStringLiteral("dark")
                                                                            : QStringLiteral("system"));
    applyTheme();
}

void MainWindow::applyTheme()
{
    // Changing the palette can make Qt re-guess the color scheme and call back in here.
    if (m_applyingTheme)
        return;
    const QScopedValueRollback guard(m_applyingTheme, true);
    const bool dark = resolvesToDark(m_theme);
    qApp->setPalette(themePalette(dark));
    m_viewer->setDark(dark);
    m_editor->setDark(dark);
}

void MainWindow::updateEditActions()
{
    m_undoAction->setEnabled(m_editor->canUndo());
    m_redoAction->setEnabled(m_editor->canRedo());
    m_deleteAction->setEnabled(!m_editor->selection().empty());
    // Grey out the buttons only: a disabled action would also swallow its shortcut
    // silently, while the enabled one explains why the constraint doesn't fit.
    for (const auto& [command, action] : m_constraintActions) {
        if (QWidget* button = m_constraintBar->widgetForAction(action))
            button->setEnabled(m_editor->canApply(command));
    }
}

void MainWindow::onPenInput(const PenSample& sample)
{
    m_inputLabel->setText(QStringLiteral("Rysik%1: nacisk %2%3")
                              .arg(sample.eraser ? QStringLiteral(" [gumka]") : QString())
                              .arg(sample.pressure, 0, 'f', 2)
                              .arg(sample.barrelButton ? QStringLiteral(" [przycisk]") : QString()));
}
