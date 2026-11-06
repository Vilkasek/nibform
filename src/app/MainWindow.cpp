#include "app/MainWindow.h"

#include "app/Icons.h"
#include "model/Export.h"
#include "sketch/SketchEditor.h"
#include "viewer/OcctViewerWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QSettings>
#include <QStyleHints>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

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
    m_dofLabel->setObjectName(QStringLiteral("statusChip"));
    m_inputLabel = new QLabel(this);
    m_glLabel = new QLabel(this);
    statusBar()->addWidget(m_hintLabel, 1);
    statusBar()->addPermanentWidget(m_dofLabel);
    statusBar()->addPermanentWidget(m_inputLabel);
    statusBar()->addPermanentWidget(m_glLabel);

    buildActions();
    buildContextBar(); // before the theme, which gives it icons
    m_timelineBar = new QToolBar(QStringLiteral("Historia"), this);
    m_timelineBar->setObjectName(QStringLiteral("timeline"));
    m_timelineBar->setMovable(false);
    m_timelineBar->setIconSize(QSize(18, 18));
    addToolBar(Qt::BottomToolBarArea, m_timelineBar);


    // Messages take the hint's place for a moment (a status bar message would be
    // painted over the hint, not instead of it).
    connect(m_editor, &SketchEditor::hintChanged, this, [this](const QString& hint) {
        m_hint = hint;
        if (!m_messageTimer.isActive())
            m_hintLabel->setText(hint);
    });
    m_messageTimer.setSingleShot(true);
    connect(&m_messageTimer, &QTimer::timeout, this, [this] {
        m_hintLabel->setProperty("message", false);
        m_hintLabel->setText(m_hint);
        m_hintLabel->style()->polish(m_hintLabel);
    });
    connect(m_editor, &SketchEditor::statusChanged, m_dofLabel, &QLabel::setText);
    connect(m_editor, &SketchEditor::message, this, &MainWindow::showMessage);
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

    connect(m_editor, &SketchEditor::modeChanged, this, &MainWindow::updateMode);
    connect(m_editor, &SketchEditor::historyChanged, this, &MainWindow::updateTimeline);
    connect(m_editor, &SketchEditor::modeChanged, this, &MainWindow::updateTimeline); // rollback shading
    connect(m_editor, &SketchEditor::historyChanged, this, &MainWindow::updateTitle);
    for (auto signal : {&SketchEditor::selectionChanged, &SketchEditor::historyChanged})
        connect(m_editor, signal, this, &MainWindow::updateContextBar);
    connect(m_editor, &SketchEditor::toolChanged, this, &MainWindow::updateContextBar);
    connect(m_editor, &SketchEditor::modeChanged, this, &MainWindow::updateContextBar);
    connect(m_editor, &SketchEditor::hintChanged, this, &MainWindow::updateContextBar); // session open / closed
    m_editor->setTool(ToolKind::Select);
    updateMode(m_editor->mode());
    updateTimeline();
    updateTitle();
    updateEditActions();

    connect(m_viewer, &OcctViewerWidget::glReady, this, [this](const QString& info) {
        m_glLabel->setText(info);
        // Defer until the first frame has sized the view, otherwise FitAll sees a 0x0 viewport.
        QTimer::singleShot(0, this, [this] { m_viewer->setProjection(V3d_XposYnegZpos, false); });

        // Dev aid: SHAPR_DEMO=1 builds a small model to look at (with SHAPR_SCREENSHOT, too).
        if (qEnvironmentVariableIsSet("SHAPR_DEMO"))
            QTimer::singleShot(0, this, &MainWindow::loadDemo);

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
    // --- File menu -------------------------------------------------------------------
    QMenu* file = menuBar()->addMenu(QStringLiteral("&Plik"));
    file->addAction(QStringLiteral("Nowy"), QKeySequence::New, this, &MainWindow::newFile);
    file->addAction(QStringLiteral("Otwórz…"), QKeySequence::Open, this, &MainWindow::openFile);
    file->addSeparator();
    file->addAction(QStringLiteral("Zapisz"), QKeySequence::Save, this, &MainWindow::save);
    file->addAction(QStringLiteral("Zapisz jako…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this,
                    &MainWindow::saveAs);
    file->addSeparator();
    m_exportStepAction = file->addAction(QStringLiteral("Eksportuj STEP…"), this, [this] { exportBodies(true); });
    m_exportStlAction = file->addAction(QStringLiteral("Eksportuj STL…"), this, [this] { exportBodies(false); });
    file->addSeparator();
    file->addAction(QStringLiteral("Zakończ"), QKeySequence::Quit, this, &QWidget::close);

    // --- Edit menu -------------------------------------------------------------------
    QMenu* edit = menuBar()->addMenu(QStringLiteral("&Edycja"));
    m_undoAction = edit->addAction(QStringLiteral("Cofnij"), m_editor, &SketchEditor::undo);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = edit->addAction(QStringLiteral("Ponów"), m_editor, &SketchEditor::redo);
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    edit->addSeparator();
    m_deleteAction = edit->addAction(QStringLiteral("Usuń zaznaczone"), m_editor, &SketchEditor::deleteSelection);
    m_deleteAction->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    m_clearSketchAction = edit->addAction(QStringLiteral("Wyczyść szkic"), m_editor, &SketchEditor::clearActiveSketch);
    edit->addSeparator();
    QAction* cancel = edit->addAction(QStringLiteral("Przerwij / odznacz"), m_editor, &SketchEditor::cancel);
    cancel->setShortcut(QKeySequence(Qt::Key_Escape));
    QAction* confirm = edit->addAction(QStringLiteral("Zakończ / edytuj wymiar"), m_editor, &SketchEditor::confirm);
    confirm->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});

    // --- Tool bars: one for the model, one for the open sketch, one for the view ------
    auto makeBar = [this](const QString& name) {
        QToolBar* bar = addToolBar(name);
        bar->setMovable(false);
        bar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        bar->setIconSize(QSize(24, 24));
        return bar;
    };
    m_modelBar = makeBar(QStringLiteral("Bryła"));
    m_sketchBar = makeBar(QStringLiteral("Szkic"));
    QToolBar* bar = makeBar(QStringLiteral("Widok"));

    // Which mode the window is in, at the start of its bar.
    auto badge = [](QToolBar* toolbar, const QString& text, const char* mode) {
        auto* label = new QLabel(text, toolbar);
        label->setObjectName(QStringLiteral("modeBadge"));
        label->setProperty("mode", QString::fromLatin1(mode)); // colored by the style sheet
        toolbar->addWidget(label);
    };
    badge(m_modelBar, QStringLiteral("BRYŁA"), "model");
    badge(m_sketchBar, QStringLiteral("SZKIC"), "sketch");

    auto* tools = new QActionGroup(this);
    auto makeTool = [&](ToolKind kind, const QString& name, const QKeySequence& key, Icon icon) {
        auto* action = new QAction(name, this);
        iconize(action, icon);
        action->setCheckable(true);
        action->setShortcut(key);
        action->setToolTip(QStringLiteral("%1 (%2)").arg(name, key.toString(QKeySequence::NativeText)));
        tools->addAction(action);
        connect(action, &QAction::triggered, this, [this, kind] { m_editor->setTool(kind); });
        m_toolActions[kind] = action;
        return action;
    };
    QAction* select = makeTool(ToolKind::Select, QStringLiteral("Zaznacz"), QKeySequence(Qt::Key_S), Icon::Select);

    // Model: start / reopen sketches, pull them into bodies, round edges.
    m_modelBar->addAction(select);
    m_modelBar->addSeparator();
    m_newSketchAction = new QAction(QStringLiteral("Stwórz szkic"), this);
    m_newSketchAction->setShortcut(QKeySequence(Qt::Key_K));
    iconize(m_newSketchAction, Icon::NewSketch);
    m_newSketchAction->setToolTip(QStringLiteral("Stwórz szkic (K) – na zaznaczonej płaskiej ścianie, "
                                                 "inaczej na podłożu; strzałka obok: wybór płaszczyzny"));
    connect(m_newSketchAction, &QAction::triggered, this, [this] {
        if (m_editor->canSketchOnFace())
            m_editor->createSketchOnFace();
        else
            m_editor->createSketch(SketchEditor::basePlane(SketchPlane::XY));
    });
    auto* planes = new QMenu(this);
    m_faceSketchAction = planes->addAction(QStringLiteral("Na zaznaczonej ścianie"), m_editor,
                                           &SketchEditor::createSketchOnFace);
    for (const auto& [plane, name] : {std::pair(SketchPlane::XY, QStringLiteral("Podłoże (XY)")),
                                      std::pair(SketchPlane::XZ, QStringLiteral("Przód (XZ)")),
                                      std::pair(SketchPlane::YZ, QStringLiteral("Prawo (YZ)"))}) {
        planes->addAction(name, this, [this, plane] { m_editor->createSketch(SketchEditor::basePlane(plane)); });
    }
    auto* newSketch = new QToolButton(m_modelBar);
    newSketch->setDefaultAction(m_newSketchAction);
    newSketch->setMenu(planes);
    newSketch->setPopupMode(QToolButton::MenuButtonPopup);
    newSketch->setFocusPolicy(Qt::NoFocus);
    newSketch->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_modelBar->addWidget(newSketch);
    m_editSketchAction = m_modelBar->addAction(QStringLiteral("Edytuj szkic"), m_editor, &SketchEditor::editSketch);
    m_editSketchAction->setToolTip(QStringLiteral("Otwiera szkic, którego obszar jest zaznaczony"));
    iconize(m_editSketchAction, Icon::EditSketch);
    m_modelBar->addSeparator();
    m_modelBar->addAction(
        makeTool(ToolKind::PushPull, QStringLiteral("Wyciągnij"), QKeySequence(Qt::Key_W), Icon::Extrude));
    m_modelBar->addAction(
        makeTool(ToolKind::Fillet, QStringLiteral("Zaokrąglij"), QKeySequence(Qt::Key_G), Icon::Fillet));
    m_modelBar->addAction(
        makeTool(ToolKind::Chamfer, QStringLiteral("Fazuj"), QKeySequence(Qt::Key_B), Icon::Chamfer));
    m_modelBar->addAction(
        makeTool(ToolKind::Shell, QStringLiteral("Skorupa"), QKeySequence(Qt::Key_J), Icon::Shell));

    // What extruding does to the bodies it meets.
    m_modelBar->addSeparator();
    m_modelBar->addWidget(new QLabel(QStringLiteral(" Bryła: "), m_modelBar));
    auto* operation = new QComboBox(m_modelBar);
    operation->setFocusPolicy(Qt::NoFocus);
    operation->setToolTip(QStringLiteral("Automatycznie: wciśnięcie w bryłę odejmuje, wyciągnięcie z niej "
                                         "albo przy niej dodaje,\na z dala od brył powstaje nowa bryła"));
    operation->addItem(QStringLiteral("Automatycznie"), static_cast<int>(BodyOperation::Auto));
    operation->addItem(QStringLiteral("Nowa bryła"), static_cast<int>(BodyOperation::NewBody));
    operation->addItem(QStringLiteral("Dodaj"), static_cast<int>(BodyOperation::Join));
    operation->addItem(QStringLiteral("Odejmij"), static_cast<int>(BodyOperation::Cut));
    connect(operation, &QComboBox::currentIndexChanged, this, [this, operation](int index) {
        m_editor->setBodyOperation(static_cast<BodyOperation>(operation->itemData(index).toInt()));
    });
    m_modelBar->addWidget(operation);

    // Sketch: drawing tools, then the way back to the model.
    m_sketchBar->addAction(select);
    m_sketchBar->addSeparator();
    m_sketchBar->addAction(makeTool(ToolKind::Line, QStringLiteral("Linia"), QKeySequence(Qt::Key_L), Icon::Line));
    m_sketchBar->addAction(
        makeTool(ToolKind::Rectangle, QStringLiteral("Prostokąt"), QKeySequence(Qt::Key_R), Icon::Rectangle));
    m_sketchBar->addAction(makeTool(ToolKind::Circle, QStringLiteral("Okrąg"), QKeySequence(Qt::Key_C), Icon::Circle));
    m_sketchBar->addAction(makeTool(ToolKind::Arc, QStringLiteral("Łuk"), QKeySequence(Qt::Key_A), Icon::Arc));
    m_sketchBar->addAction(makeTool(ToolKind::Trim, QStringLiteral("Przytnij"), QKeySequence(Qt::Key_X), Icon::Trim));
    m_sketchBar->addAction(
        makeTool(ToolKind::Offset, QStringLiteral("Odsunięcie"), QKeySequence(Qt::Key_U), Icon::Offset));
    m_constructionAction = m_sketchBar->addAction(QStringLiteral("Konstrukcyjna"), m_editor,
                                                  &SketchEditor::toggleConstruction);
    m_constructionAction->setShortcut(QKeySequence(Qt::Key_Q));
    m_constructionAction->setToolTip(QStringLiteral("Konstrukcyjna (Q) – zaznaczone krzywe stają się pomocnicze "
                                                    "(przerywane, nie tworzą obszarów) albo z powrotem zwykłe"));
    iconize(m_constructionAction, Icon::Construction);
    m_sketchBar->addSeparator();
    m_closeSketchAction = m_sketchBar->addAction(QStringLiteral("Zamknij szkic"), m_editor, &SketchEditor::closeSketch);
    m_closeSketchAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    m_closeSketchAction->setToolTip(QStringLiteral("Wraca do brył (Ctrl+Enter)"));
    iconize(m_closeSketchAction, Icon::CloseSketch);
    if (QWidget* button = m_sketchBar->widgetForAction(m_closeSketchAction))
        button->setObjectName(QStringLiteral("closeSketch")); // the accent button of the sketch bar

    bar->addSeparator();
    QAction* fit = bar->addAction(QStringLiteral("Dopasuj"), m_viewer, &OcctViewerWidget::fitAll);
    fit->setShortcut(QKeySequence(Qt::Key_F));
    fit->setToolTip(QStringLiteral("Dopasuj widok (F)"));
    iconize(fit, Icon::Fit);

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
    iconize(iso, Icon::Iso);
    iconize(faceActions[0], Icon::Top);
    iconize(faceActions[1], Icon::Front);
    iconize(faceActions[2], Icon::Right);
    connect(iso, &QAction::triggered, cornerActions[0], &QAction::trigger);
    bar->addAction(faceActions[0]);
    bar->addAction(faceActions[1]);
    bar->addAction(faceActions[2]);

    // --- Constraints: select geometry, then pick what should hold -----------------------
    addToolBarBreak();
    QToolBar* constraints = addToolBar(QStringLiteral("Więzy"));
    m_constraintBar = constraints;
    constraints->setMovable(false);
    constraints->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    constraints->setIconSize(QSize(20, 20));
    // --- Bodies: picked through their faces ----------------------------------------------
    QMenu* bodies = menuBar()->addMenu(QStringLiteral("&Bryły"));
    m_bodiesMenu = bodies;
    auto addBoolean = [&](BooleanKind kind, const QString& name, const QString& help) {
        QAction* action = bodies->addAction(name, this, [this, kind] { m_editor->combineBodies(kind); });
        action->setToolTip(help);
        m_bodyActions.push_back(action);
    };
    addBoolean(BooleanKind::Union, QStringLiteral("Połącz"), QStringLiteral("Zaznaczone bryły w jedną"));
    addBoolean(BooleanKind::Subtract, QStringLiteral("Odejmij"),
               QStringLiteral("Od pierwszej zaznaczonej bryły odejmuje pozostałe"));
    addBoolean(BooleanKind::Intersect, QStringLiteral("Część wspólna"),
               QStringLiteral("Zostawia tylko to, co zaznaczone bryły mają wspólne"));
    bodies->setToolTipsVisible(true);

    QMenu* constraintMenu = menuBar()->addMenu(QStringLiteral("&Więzy"));
    m_constraintMenu = constraintMenu;
    auto addConstraint = [&](ConstraintCommand command, const QString& name, Qt::Key key, const QString& help) {
        QAction* action = constraints->addAction(name);
        constraintMenu->addAction(action);
        action->setShortcut(QKeySequence(key));
        action->setToolTip(QStringLiteral("%1 (%2)\n%3").arg(name, QKeySequence(key).toString(), help));
        connect(action, &QAction::triggered, this, [this, command] { m_editor->applyConstraint(command); });
        m_constraintActions[command] = action;
        iconize(action, command);
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

void MainWindow::applyStyle(bool dark)
{
    struct Tones
    {
        const char *bar, *border, *text, *hover, *pressed, *checkedText, *disabled, *panel, *base, *accent;
    };
    const Tones t = dark ? Tones{"#26282c", "#3a3e44", "#e3e6eb", "#33363b", "#23395f", "#8ab4ff", "#6b7079",
                                 "#2c2f34", "#1f2125", "#3b82f6"}
                         : Tones{"#f6f7f9", "#dde1e6", "#1f2329", "#e8ebef", "#dbe6fd", "#1d4ed8", "#a3a9b1",
                                 "#ffffff", "#ffffff", "#2563eb"};
    QString sheet = QStringLiteral(R"(
QToolBar { background: @bar; border: none; border-bottom: 1px solid @border; padding: 3px 6px; spacing: 2px; }
QToolBar::separator { background: @border; width: 1px; margin: 8px 6px; }
QToolButton { color: @text; border: none; border-radius: 8px; padding: 3px 7px; }
QToolButton:hover { background: @hover; }
QToolButton:pressed, QToolButton:checked { background: @pressed; color: @checkedText; }
QToolButton:disabled { color: @disabled; }
QToolButton::menu-button { border: none; width: 14px; }
QLabel#modeBadge { color: white; border-radius: 10px; padding: 3px 12px; margin: 0 8px 0 2px; font-weight: 600; }
QLabel#modeBadge[mode="model"] { background: #2563eb; }
QLabel#modeBadge[mode="sketch"] { background: #ea580c; }
QToolButton#closeSketch { background: #ea580c; color: white; font-weight: 600; padding: 3px 12px; }
QToolButton#closeSketch:hover { background: #f97316; }
QFrame#valueEditor, QFrame#contextBar { background: @panel; border: 1px solid @border; border-radius: 12px; }
QFrame#valueEditor QLabel { color: @text; padding-left: 4px; }
QFrame#valueEditor QLineEdit { border: 1px solid @accent; border-radius: 6px; padding: 3px 6px;
                               background: @base; color: @text; selection-background-color: @accent; }
QToolButton#valueAccept { background: @accent; color: white; }
QStatusBar { background: @bar; border-top: 1px solid @border; }
QLabel[message="true"] { color: @checkedText; font-weight: 600; }
QLabel#statusChip { background: @pressed; color: @checkedText; border-radius: 9px; padding: 2px 10px; }
QToolBar#timeline { border-bottom: none; border-top: 1px solid @border; padding: 4px 8px; spacing: 6px; }
QLabel#timelineTitle { color: @disabled; font-weight: 600; padding: 0 8px 0 2px; }
QToolButton#timelineChip { background: @panel; border: 1px solid @border; border-radius: 10px; padding: 3px 8px; }
QToolButton#timelineChip:hover { border-color: @accent; }
QToolButton#timelineChip:checked { border-color: @accent; background: @pressed; color: @checkedText; }
QToolButton#timelineChip[rolledBack="true"] { color: @disabled; border-style: dashed; }
QToolButton#timelineChip[failed="true"] { border-color: #e5484d; color: #e5484d; }
QMenu { background: @panel; border: 1px solid @border; border-radius: 8px; padding: 4px; }
QMenu::item { padding: 5px 22px 5px 22px; border-radius: 6px; color: @text; }
QMenu::item:selected { background: @hover; }
QMenu::item:disabled { color: @disabled; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
)");
    const std::pair<const char*, const char*> tones[] = {
        {"@checkedText", t.checkedText}, {"@disabled", t.disabled}, {"@pressed", t.pressed}, {"@border", t.border},
        {"@accent", t.accent},           {"@hover", t.hover},       {"@panel", t.panel},     {"@text", t.text},
        {"@base", t.base},               {"@bar", t.bar},
    };
    for (const auto& [key, value] : tones)
        sheet.replace(QLatin1String(key), QLatin1String(value));
    setStyleSheet(sheet);

    // Icons follow the text color; the accent marks what each tool adds or changes.
    const QColor ink(t.text);
    const QColor accent(dark ? "#5b9cff" : "#2563eb");
    m_accent = accent.name();
    for (const auto& [action, make] : m_icons) {
        const bool onAccent = action == m_closeSketchAction; // drawn on the orange button
        action->setIcon(onAccent ? make(Qt::white, Qt::white) : make(ink, accent));
    }
    for (const auto& [button, icon] : m_contextButtons)
        button->setIcon(makeIcon(icon, ink, accent));
    if (m_timelineBar)
        updateTimeline(); // chips are rebuilt with the new colors
}

void MainWindow::applyTheme()
{
    // Changing the palette can make Qt re-guess the color scheme and call back in here.
    if (m_applyingTheme)
        return;
    const QScopedValueRollback guard(m_applyingTheme, true);
    const bool dark = resolvesToDark(m_theme);
    qApp->setPalette(themePalette(dark));
    applyStyle(dark);
    m_viewer->setDark(dark);
    m_editor->setDark(dark);
}

void MainWindow::loadDemo()
{
    // A plate (the rectangle and both discs in it pulled together), a boss pulled
    // further from one disc (joins), a through hole from the other (cuts), a rounded
    // edge, and a closed region left on the sketch, picked so its arrow shows.
    m_editor->createSketch(SketchEditor::basePlane(SketchPlane::XY));
    m_editor->commit([](Sketch& s) {
        const PointId a = s.addPoint({0, 0}), b = s.addPoint({60, 0});
        const PointId c = s.addPoint({60, 40}), d = s.addPoint({0, 40});
        s.addLine(a, b);
        s.addLine(b, c);
        s.addLine(c, d);
        s.addLine(d, a);
        s.addCircle(s.addPoint({45, 20}), 8.0);
        s.addCircle(s.addPoint({15, 20}), 6.0);
        s.addCircle(s.addPoint({-25, 20}), 10.0);
    });
    m_editor->closeSketch();
    const auto plate = m_editor->regionAt(0, {30, 5});
    const auto boss = m_editor->regionAt(0, {45, 20});
    const auto hole = m_editor->regionAt(0, {15, 20});
    if (!plate || !boss || !hole)
        return;
    m_editor->extrudeRegions({*plate, *boss, *hole}, 12.0);
    m_editor->extrudeRegions({*boss}, 25.0);
    m_editor->extrudeRegions({*hole}, 12.0);
    m_viewer->setProjection(V3d_XposYnegZpos, false);
    if (const auto loose = m_editor->regionAt(0, {-25, 20}))
        m_editor->selectRegion(*loose);

    // SHAPR_DEMO=sketch: then open a new sketch above it, with a rectangle drawn.
    if (qEnvironmentVariable("SHAPR_DEMO") == QLatin1String("sketch")) {
        m_editor->createSketch(gp_Ax3(gp_Pnt(0, 0, 37), gp::DZ(), gp::DX()));
        m_editor->commit([](Sketch& s) {
            const PointId a = s.addPoint({35, 10}), b = s.addPoint({55, 10});
            const PointId c = s.addPoint({55, 30}), d = s.addPoint({35, 30});
            s.addLine(a, b);
            s.addLine(b, c);
            s.addLine(c, d);
            s.addLine(d, a);
        });
    }
}

void MainWindow::updateTimeline()
{
    // One chip per history entry, oldest on the left. A tap picks it, a double tap edits
    // it (reopens the sketch / retypes the value, the model following live); the arrow
    // menu offers both and removal.
    m_timelineBar->clear();
    auto* title = new QLabel(QStringLiteral("Historia"), m_timelineBar);
    title->setObjectName(QStringLiteral("timelineTitle"));
    m_timelineBar->addWidget(title);
    const std::vector<TimelineItem> items = m_editor->timeline();
    if (items.empty()) {
        auto* empty = new QLabel(QStringLiteral("pusta – stwórz szkic i wyciągnij go"), m_timelineBar);
        empty->setEnabled(false);
        m_timelineBar->addWidget(empty);
    }
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const TimelineItem& item = items[i];
        auto* chip = new QToolButton(m_timelineBar);
        chip->setObjectName(QStringLiteral("timelineChip"));
        chip->setProperty("failed", !item.error.isEmpty()); // red by the style sheet
        chip->setProperty("rolledBack", item.rolledBack);   // dimmed while a sketch before it is edited
        chip->setText(item.detail.isEmpty() ? item.name : QStringLiteral("%1  ·  %2").arg(item.name, item.detail));
        const bool editable = item.sketch || !item.detail.isEmpty();
        chip->setToolTip(!item.error.isEmpty() ? item.error
                         : item.sketch         ? QStringLiteral("Dwuklik: edytuj szkic")
                         : editable            ? QStringLiteral("Dwuklik: zmień wartość")
                                               : QString());
        chip->setCheckable(true);
        chip->setChecked(i == m_timelinePick);
        chip->setProperty("timelineIndex", i);
        chip->installEventFilter(this); // double taps
        chip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        chip->setFocusPolicy(Qt::NoFocus);
        const Icon icon = item.sketch                                    ? Icon::EditSketch
                        : item.name.startsWith(QStringLiteral("Wyciąg")) ? Icon::Extrude
                        : item.name.startsWith(QStringLiteral("Zaokr"))  ? Icon::Fillet
                        : item.name.startsWith(QStringLiteral("Faza"))   ? Icon::Chamfer
                        : item.name.startsWith(QStringLiteral("Skorup")) ? Icon::Shell
                                                                         : Icon::Coincident;
        chip->setIcon(makeIcon(icon, palette().color(QPalette::WindowText), QColor(m_accent)));
        connect(chip, &QToolButton::clicked, this, [this, i] {
            m_timelinePick = i;
            for (QToolButton* other : m_timelineBar->findChildren<QToolButton*>(QStringLiteral("timelineChip")))
                other->setChecked(other->property("timelineIndex").toInt() == i);
        });
        auto* menu = new QMenu(chip);
        menu->addAction(QStringLiteral("Edytuj"), this, [this, i] { m_editor->activateTimelineItem(i); })
            ->setEnabled(editable);
        menu->addAction(QStringLiteral("Usuń"), this, [this, i] {
            m_timelinePick = -1;
            m_editor->removeTimelineItem(i);
        });
        chip->setMenu(menu);
        chip->setPopupMode(QToolButton::MenuButtonPopup);
        m_timelineBar->addWidget(chip);
    }
    const bool any = m_editor->bodyCount() > 0;
    m_exportStepAction->setEnabled(any);
    m_exportStlAction->setEnabled(any);
}

void MainWindow::showMessage(const QString& text)
{
    m_hintLabel->setText(text);
    m_hintLabel->setProperty("message", true); // accent color, from the style sheet
    m_hintLabel->style()->polish(m_hintLabel);
    m_messageTimer.start(4000);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Double click / double tap on a history chip edits that entry.
    if (event->type() == QEvent::MouseButtonDblClick && watched->objectName() == QLatin1String("timelineChip")) {
        const int index = watched->property("timelineIndex").toInt();
        m_timelinePick = index;
        // Deferred: the chips are rebuilt as the edit starts, this one included.
        QTimer::singleShot(0, this, [this, index] {
            updateTimeline(); // shows the chip as picked
            m_editor->activateTimelineItem(index);
        });
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateTitle()
{
    const QString name = m_path.isEmpty() ? QStringLiteral("Bez nazwy") : QFileInfo(m_path).fileName();
    setWindowTitle(QStringLiteral("%1%2 – Shapr Linux").arg(name, m_editor->isModified() ? QStringLiteral("*") : QString()));
}

QString MainWindow::lastDirectory() const
{
    return QSettings().value(QStringLiteral("lastDirectory"), QDir::homePath()).toString();
}

void MainWindow::rememberDirectory(const QString& path)
{
    QSettings().setValue(QStringLiteral("lastDirectory"), QFileInfo(path).absolutePath());
}

bool MainWindow::maybeSave()
{
    if (!m_editor->isModified())
        return true;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Niezapisane zmiany"), QStringLiteral("Zapisać zmiany w projekcie?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::newFile()
{
    if (!maybeSave())
        return;
    m_editor->newProject();
    m_path.clear();
    updateTitle();
}

void MainWindow::openFile()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Otwórz projekt"), lastDirectory(),
                                                      QStringLiteral("Projekty Shapr Linux (*.shapr)"));
    if (!path.isEmpty())
        openPath(path);
}

void MainWindow::openPath(const QString& path)
{
    QString error;
    auto project = ::loadProject(path, &error);
    if (!project) {
        QMessageBox::warning(this, QStringLiteral("Nie można otworzyć"), error);
        return;
    }
    m_editor->loadProject(std::move(*project));
    m_path = path;
    rememberDirectory(path);
    updateTitle();
    m_viewer->fitAll();
    showMessage(QStringLiteral("Otwarto %1").arg(QFileInfo(path).fileName()));
}

bool MainWindow::save()
{
    if (m_path.isEmpty())
        return saveAs();
    const QString error = saveProject(m_path, m_editor->project());
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Nie można zapisać"), error);
        return false;
    }
    m_editor->markSaved();
    showMessage(QStringLiteral("Zapisano %1").arg(QFileInfo(m_path).fileName()));
    return true;
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Zapisz projekt"),
                                                m_path.isEmpty() ? lastDirectory() : m_path,
                                                QStringLiteral("Projekty Shapr Linux (*.shapr)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QStringLiteral(".shapr")))
        path += QStringLiteral(".shapr");
    m_path = path;
    rememberDirectory(path);
    return save();
}

void MainWindow::exportBodies(bool step)
{
    const QString filter = step ? QStringLiteral("STEP (*.step *.stp)") : QStringLiteral("STL (*.stl)");
    const QString suffix = step ? QStringLiteral(".step") : QStringLiteral(".stl");
    const QString suggested = m_path.isEmpty() ? lastDirectory() + QStringLiteral("/model") + suffix
                                               : QFileInfo(m_path).absolutePath() + QLatin1Char('/')
                                                     + QFileInfo(m_path).completeBaseName() + suffix;
    QString path = QFileDialog::getSaveFileName(this, step ? QStringLiteral("Eksportuj STEP")
                                                           : QStringLiteral("Eksportuj STL"),
                                                suggested, filter);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += suffix;
    rememberDirectory(path);
    const std::string error = step ? exportStep(m_editor->bodies(), path.toStdString())
                                   : exportStl(m_editor->bodies(), path.toStdString());
    if (!error.empty())
        QMessageBox::warning(this, QStringLiteral("Eksport nieudany"), QString::fromStdString(error));
    else
        showMessage(QStringLiteral("Wyeksportowano %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

void MainWindow::iconize(QAction* action, Icon icon)
{
    m_icons.push_back({action, [icon](const QColor& ink, const QColor& accent) { return makeIcon(icon, ink, accent); }});
}

void MainWindow::iconize(QAction* action, ConstraintCommand command)
{
    std::function<QIcon(const QColor&, const QColor&)> make;
    auto glyph = [](ConstraintKind kind) {
        return [kind](const QColor& ink, const QColor&) { return makeConstraintIcon(kind, ink); };
    };
    switch (command) {
    case ConstraintCommand::Dimension:
        make = [](const QColor& ink, const QColor& accent) { return makeIcon(Icon::Dimension, ink, accent); };
        break;
    case ConstraintCommand::Coincident:
        make = [](const QColor& ink, const QColor& accent) { return makeIcon(Icon::Coincident, ink, accent); };
        break;
    case ConstraintCommand::Horizontal: make = glyph(ConstraintKind::Horizontal); break;
    case ConstraintCommand::Vertical: make = glyph(ConstraintKind::Vertical); break;
    case ConstraintCommand::Parallel: make = glyph(ConstraintKind::Parallel); break;
    case ConstraintCommand::Perpendicular: make = glyph(ConstraintKind::Perpendicular); break;
    case ConstraintCommand::Equal: make = glyph(ConstraintKind::Equal); break;
    case ConstraintCommand::Tangent: make = glyph(ConstraintKind::Tangent); break;
    case ConstraintCommand::Concentric: make = glyph(ConstraintKind::Concentric); break;
    case ConstraintCommand::Midpoint: make = glyph(ConstraintKind::Midpoint); break;
    case ConstraintCommand::Fix: make = glyph(ConstraintKind::Fixed); break;
    }
    m_icons.push_back({action, make});
}

void MainWindow::buildContextBar()
{
    // Floats next to what is picked in the model, offering what can be done with it.
    m_contextBar = new QFrame(m_viewer);
    m_contextBar->setObjectName(QStringLiteral("contextBar"));
    auto* layout = new QHBoxLayout(m_contextBar);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);
    auto add = [&](const QString& text, Icon icon, const std::function<void()>& run) {
        auto* button = new QToolButton(m_contextBar);
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIconSize(QSize(20, 20));
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, this, run);
        layout->addWidget(button);
        m_contextButtons.push_back({button, icon});
        return button;
    };
    m_ctxFillet = add(QStringLiteral("Zaokrąglij"), Icon::Fillet, [this] { m_editor->setTool(ToolKind::Fillet); });
    m_ctxChamfer = add(QStringLiteral("Fazuj"), Icon::Chamfer, [this] { m_editor->setTool(ToolKind::Chamfer); });
    m_ctxSketch = add(QStringLiteral("Szkic na ścianie"), Icon::NewSketch, [this] { m_editor->createSketchOnFace(); });
    m_ctxPull = add(QStringLiteral("Wyciągnij"), Icon::Extrude, [this] { m_editor->setTool(ToolKind::PushPull); });
    m_ctxShell = add(QStringLiteral("Skorupa"), Icon::Shell, [this] { m_editor->setTool(ToolKind::Shell); });
    m_contextBar->hide();
}

void MainWindow::updateContextBar()
{
    if (!m_contextBar)
        return;
    const bool active = m_editor->mode() == EditorMode::Model && m_editor->tool() == ToolKind::Select
                     && !m_editor->valueSessionOpen();
    const bool edges = active && m_editor->hasPickedEdges();
    const bool face = active && m_editor->canSketchOnFace();
    const bool faces = active && m_editor->hasPickedFaces();
    m_ctxFillet->setVisible(edges);
    m_ctxChamfer->setVisible(edges);
    m_ctxSketch->setVisible(face);
    m_ctxPull->setVisible(faces);
    m_ctxShell->setVisible(faces);
    if (!edges && !faces) {
        m_contextBar->hide();
        return;
    }
    // Below and right of where the last pick happened, kept inside the view.
    m_contextBar->adjustSize();
    const QPointF at = m_editor->pointerPos() + QPointF(18, 18);
    m_contextBar->move(std::clamp(qRound(at.x()), 4, std::max(4, m_viewer->width() - m_contextBar->width() - 4)),
                       std::clamp(qRound(at.y()), 4, std::max(4, m_viewer->height() - m_contextBar->height() - 4)));
    m_contextBar->show();
    m_contextBar->raise();
}

void MainWindow::updateMode(EditorMode mode)
{
    const bool sketch = mode == EditorMode::Sketch;
    m_modelBar->setVisible(!sketch);
    m_sketchBar->setVisible(sketch);
    m_constraintBar->setVisible(sketch);
    m_constraintMenu->setEnabled(sketch);
    m_bodiesMenu->setEnabled(!sketch);
    // Hidden actions must not fire through their shortcuts either.
    for (const auto& [kind, action] : m_toolActions)
        action->setEnabled(kind == ToolKind::Select || isSketchTool(kind) == sketch);
    for (const auto& [command, action] : m_constraintActions)
        action->setEnabled(sketch);
    m_newSketchAction->setEnabled(!sketch);
    m_closeSketchAction->setEnabled(sketch);
    m_clearSketchAction->setEnabled(sketch);
    updateEditActions();
}

void MainWindow::updateEditActions()
{
    m_undoAction->setEnabled(m_editor->canUndo());
    m_redoAction->setEnabled(m_editor->canRedo());
    m_deleteAction->setEnabled(!m_editor->selection().empty() || !m_editor->selectedBodies().empty());
    for (QAction* action : m_bodyActions)
        action->setEnabled(m_editor->selectedBodies().size() >= 2);
    m_editSketchAction->setEnabled(m_editor->canEditSketch());
    m_constructionAction->setEnabled(m_editor->canToggleConstruction());
    m_faceSketchAction->setEnabled(m_editor->canSketchOnFace());
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
