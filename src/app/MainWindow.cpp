#include "app/MainWindow.h"

#include "app/Icons.h"
#include "app/Language.h"
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

    buildUi();


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
    syncUi();

    connect(m_viewer, &OcctViewerWidget::glReady, this, [this](const QString& info) {
        m_glLabel->setText(info);
        // Defer until the first frame has sized the view, otherwise FitAll sees a 0x0 viewport.
        QTimer::singleShot(0, this, [this] {
            m_viewer->setProjection(V3d_XposYnegZpos, false);
            m_viewReady = true; // from now on, turning the screen re-frames the model
        });

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
        QMessageBox::critical(this, tr("OpenGL error"), reason);
    });
    connect(m_viewer, &OcctViewerWidget::penInput, this, &MainWindow::onPenInput);
}

void MainWindow::buildUi()
{
    // Everything here is rebuilt when the language changes (see changeEvent()).
    m_uiRoot = new QObject(this); // parent of the window-level actions, deleted with them
    buildActions();
    buildContextBar();

    // Tablet mode: the keys a keyboard would give, as big buttons along the left edge.
    m_quickBar = new QToolBar(tr("Quick"), this);
    m_quickBar->setObjectName(QStringLiteral("quickBar"));
    m_quickBar->setMovable(false);
    m_quickBar->setOrientation(Qt::Vertical);
    m_quickBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_quickBar->setIconSize(QSize(32, 32));
    for (QAction* action : {m_undoAction, m_redoAction, m_deleteAction, m_cancelAction, m_confirmAction, m_fitAction})
        m_quickBar->addAction(action);
    addToolBar(Qt::LeftToolBarArea, m_quickBar);
    m_timelineBar = new QToolBar(tr("History"), this);
    m_timelineBar->setObjectName(QStringLiteral("timeline"));
    m_timelineBar->setMovable(false);
    m_timelineBar->setIconSize(QSize(18, 18));
    addToolBar(Qt::BottomToolBarArea, m_timelineBar);
}

void MainWindow::syncUi()
{
    // Freshly built bars and menus take the current state.
    m_themeActions[m_theme]->setChecked(true);
    applyTheme(); // icons and style sheet
    if (QAction* action = m_toolActions[m_editor->tool()])
        action->setChecked(true);
    updateMode(m_editor->mode());
    updateTimeline();
    updateTitle();
    applyTabletMode();
    updateEditActions();
}

void MainWindow::rebuildUi()
{
    m_rebuildPending = false;
    // Toolbars and the context bar don't own the actions they show; the menu bar, its
    // menus and m_uiRoot do. Bars first, then their actions.
    delete m_contextBar;
    m_contextBar = nullptr;
    m_contextButtons.clear();
    for (QToolBar* bar : {m_modelBar, m_sketchBar, m_viewBar, m_constraintBar, m_timelineBar, m_quickBar}) {
        removeToolBar(bar);
        delete bar;
    }
    m_modelBar = m_sketchBar = m_viewBar = m_constraintBar = m_timelineBar = m_quickBar = nullptr;
    delete m_uiRoot;
    setMenuBar(new QMenuBar(this)); // deletes the old one, its menus and their actions
    m_toolActions.clear();
    m_constraintActions.clear();
    m_bodyActions.clear();
    m_themeActions.clear();
    m_icons.clear();

    buildUi();
    syncUi();
    m_editor->retranslate(); // hint, status and timeline names
    updateLayout();
}

void MainWindow::changeEvent(QEvent* event)
{
    // Installing / removing translators sends a few of these; rebuild once.
    if (event->type() == QEvent::LanguageChange && m_uiRoot && !m_rebuildPending) {
        m_rebuildPending = true;
        QTimer::singleShot(0, this, &MainWindow::rebuildUi);
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::buildActions()
{
    // --- File menu -------------------------------------------------------------------
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("New"), QKeySequence::New, this, &MainWindow::newFile);
    file->addAction(tr("Open…"), QKeySequence::Open, this, &MainWindow::openFile);
    file->addSeparator();
    file->addAction(tr("Save"), QKeySequence::Save, this, &MainWindow::save);
    file->addAction(tr("Save As…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this,
                    &MainWindow::saveAs);
    file->addSeparator();
    m_exportStepAction = file->addAction(tr("Export STEP…"), this, [this] { exportBodies(true); });
    m_exportStlAction = file->addAction(tr("Export STL…"), this, [this] { exportBodies(false); });
    file->addSeparator();
    file->addAction(tr("Quit"), QKeySequence::Quit, this, &QWidget::close);

    // --- Edit menu -------------------------------------------------------------------
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    m_undoAction = edit->addAction(tr("Undo"), m_editor, &SketchEditor::undo);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = edit->addAction(tr("Redo"), m_editor, &SketchEditor::redo);
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    edit->addSeparator();
    m_deleteAction = edit->addAction(tr("Delete Selected"), m_editor, &SketchEditor::deleteSelection);
    m_deleteAction->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    m_clearSketchAction = edit->addAction(tr("Clear Sketch"), m_editor, &SketchEditor::clearActiveSketch);
    edit->addSeparator();
    m_cancelAction = edit->addAction(tr("Cancel / Deselect"), m_editor, &SketchEditor::cancel);
    m_cancelAction->setShortcut(QKeySequence(Qt::Key_Escape));
    m_confirmAction = edit->addAction(tr("Finish / Edit Dimension"), m_editor, &SketchEditor::confirm);
    m_confirmAction->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    iconize(m_undoAction, Icon::Undo);
    iconize(m_redoAction, Icon::Redo);
    iconize(m_deleteAction, Icon::Delete);
    iconize(m_cancelAction, Icon::Cancel);
    iconize(m_confirmAction, Icon::Done);

    // --- Tool bars: one for the model, one for the open sketch, one for the view ------
    auto makeBar = [this](const QString& name) {
        QToolBar* bar = addToolBar(name);
        bar->setMovable(false);
        bar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        bar->setIconSize(QSize(24, 24));
        return bar;
    };
    m_modelBar = makeBar(tr("Model"));
    m_sketchBar = makeBar(tr("Sketch"));
    QToolBar* bar = makeBar(tr("View"));
    m_viewBar = bar;

    // Which mode the window is in, at the start of its bar.
    auto badge = [](QToolBar* toolbar, const QString& text, const char* mode) {
        auto* label = new QLabel(text, toolbar);
        label->setObjectName(QStringLiteral("modeBadge"));
        label->setProperty("mode", QString::fromLatin1(mode)); // colored by the style sheet
        toolbar->addWidget(label);
    };
    badge(m_modelBar, tr("MODEL"), "model");
    badge(m_sketchBar, tr("SKETCH"), "sketch");

    auto* tools = new QActionGroup(m_uiRoot);
    auto makeTool = [&](ToolKind kind, const QString& name, const QKeySequence& key, Icon icon) {
        auto* action = new QAction(name, m_uiRoot);
        iconize(action, icon);
        action->setCheckable(true);
        action->setShortcut(key);
        action->setToolTip(QStringLiteral("%1 (%2)").arg(name, key.toString(QKeySequence::NativeText)));
        tools->addAction(action);
        connect(action, &QAction::triggered, this, [this, kind] { m_editor->setTool(kind); });
        m_toolActions[kind] = action;
        return action;
    };
    QAction* select = makeTool(ToolKind::Select, tr("Select"), QKeySequence(Qt::Key_S), Icon::Select);

    // Model: start / reopen sketches, pull them into bodies, round edges.
    m_modelBar->addAction(select);
    m_modelBar->addSeparator();
    m_newSketchAction = new QAction(tr("New Sketch"), m_uiRoot);
    m_newSketchAction->setShortcut(QKeySequence(Qt::Key_K));
    iconize(m_newSketchAction, Icon::NewSketch);
    m_newSketchAction->setToolTip(tr("New Sketch (K): on the selected flat face, otherwise on the ground; "
                                     "the arrow next to it picks the plane"));
    connect(m_newSketchAction, &QAction::triggered, this, [this] {
        if (m_editor->canSketchOnFace())
            m_editor->createSketchOnFace();
        else
            m_editor->createSketch(SketchEditor::basePlane(SketchPlane::XY));
    });
    auto* planes = new QMenu(m_modelBar);
    m_faceSketchAction = planes->addAction(tr("On the Selected Face"), m_editor,
                                           &SketchEditor::createSketchOnFace);
    for (const auto& [plane, name] : {std::pair(SketchPlane::XY, tr("Ground (XY)")),
                                      std::pair(SketchPlane::XZ, tr("Front (XZ)")),
                                      std::pair(SketchPlane::YZ, tr("Right (YZ)"))}) {
        planes->addAction(name, this, [this, plane] { m_editor->createSketch(SketchEditor::basePlane(plane)); });
    }
    auto* newSketch = new QToolButton(m_modelBar);
    m_newSketchButton = newSketch;
    newSketch->setDefaultAction(m_newSketchAction);
    newSketch->setMenu(planes);
    newSketch->setPopupMode(QToolButton::MenuButtonPopup);
    newSketch->setFocusPolicy(Qt::NoFocus);
    newSketch->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_modelBar->addWidget(newSketch);
    m_editSketchAction = m_modelBar->addAction(tr("Edit Sketch"), m_editor, &SketchEditor::editSketch);
    m_editSketchAction->setToolTip(tr("Opens the sketch whose region is selected"));
    iconize(m_editSketchAction, Icon::EditSketch);
    m_modelBar->addSeparator();
    m_modelBar->addAction(
        makeTool(ToolKind::PushPull, tr("Extrude"), QKeySequence(Qt::Key_W), Icon::Extrude));
    m_modelBar->addAction(
        makeTool(ToolKind::Fillet, tr("Fillet"), QKeySequence(Qt::Key_G), Icon::Fillet));
    m_modelBar->addAction(
        makeTool(ToolKind::Chamfer, tr("Chamfer"), QKeySequence(Qt::Key_B), Icon::Chamfer));
    m_modelBar->addAction(
        makeTool(ToolKind::Shell, tr("Shell"), QKeySequence(Qt::Key_J), Icon::Shell));

    // What extruding does to the bodies it meets.
    m_modelBar->addSeparator();
    // Hidden in compact layouts through its toolbar action (the widget itself can't be).
    m_operationLabel = m_modelBar->addWidget(new QLabel(tr(" Operation: "), m_modelBar));
    auto* operation = new QComboBox(m_modelBar);
    operation->setFocusPolicy(Qt::NoFocus);
    operation->setToolTip(tr("Automatic: pushing into a body cuts, pulling out of it or next to it joins,\n"
                             "and away from bodies a new body is made"));
    operation->addItem(tr("Automatic"), static_cast<int>(BodyOperation::Auto));
    operation->addItem(tr("New Body"), static_cast<int>(BodyOperation::NewBody));
    operation->addItem(tr("Join"), static_cast<int>(BodyOperation::Join));
    operation->addItem(tr("Cut"), static_cast<int>(BodyOperation::Cut));
    operation->setCurrentIndex(operation->findData(static_cast<int>(m_editor->bodyOperation())));
    connect(operation, &QComboBox::currentIndexChanged, this, [this, operation](int index) {
        m_editor->setBodyOperation(static_cast<BodyOperation>(operation->itemData(index).toInt()));
    });
    // Also chosen under the value field while extruding.
    connect(m_editor, &SketchEditor::bodyOperationChanged, operation, [operation](BodyOperation op) {
        const QSignalBlocker block(operation);
        operation->setCurrentIndex(operation->findData(static_cast<int>(op)));
    });
    m_modelBar->addWidget(operation);

    // Sketch: drawing tools, then the way back to the model.
    m_sketchBar->addAction(select);
    m_sketchBar->addSeparator();
    m_sketchBar->addAction(makeTool(ToolKind::Line, tr("Line"), QKeySequence(Qt::Key_L), Icon::Line));
    m_sketchBar->addAction(
        makeTool(ToolKind::Rectangle, tr("Rectangle"), QKeySequence(Qt::Key_R), Icon::Rectangle));
    m_sketchBar->addAction(makeTool(ToolKind::Circle, tr("Circle"), QKeySequence(Qt::Key_C), Icon::Circle));
    m_sketchBar->addAction(makeTool(ToolKind::Arc, tr("Arc"), QKeySequence(Qt::Key_A), Icon::Arc));
    m_sketchBar->addAction(makeTool(ToolKind::Trim, tr("Trim"), QKeySequence(Qt::Key_X), Icon::Trim));
    m_sketchBar->addAction(
        makeTool(ToolKind::Offset, tr("Offset"), QKeySequence(Qt::Key_U), Icon::Offset));
    m_constructionAction = m_sketchBar->addAction(tr("Construction"), m_editor,
                                                  &SketchEditor::toggleConstruction);
    m_constructionAction->setShortcut(QKeySequence(Qt::Key_Q));
    m_constructionAction->setToolTip(tr("Construction (Q): the selected curves become reference geometry "
                                        "(dashed, never bounding a region), or back to regular"));
    iconize(m_constructionAction, Icon::Construction);
    m_sketchBar->addSeparator();
    m_closeSketchAction = m_sketchBar->addAction(tr("Finish Sketch"), m_editor, &SketchEditor::closeSketch);
    m_closeSketchAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    m_closeSketchAction->setToolTip(tr("Back to the model (Ctrl+Enter)"));
    iconize(m_closeSketchAction, Icon::CloseSketch);
    if (QWidget* button = m_sketchBar->widgetForAction(m_closeSketchAction))
        button->setObjectName(QStringLiteral("closeSketch")); // the accent button of the sketch bar

    bar->addSeparator();
    QAction* fit = bar->addAction(tr("Fit"), m_viewer, &OcctViewerWidget::fitAll);
    m_fitAction = fit;
    fit->setShortcut(QKeySequence(Qt::Key_F));
    fit->setToolTip(tr("Fit the view (F)"));
    iconize(fit, Icon::Fit);

    // --- Views: digits = faces, Ctrl+digit = corners, Shift+digit = edges --------------
    QMenu* views = menuBar()->addMenu(tr("&View"));
    views->addAction(fit);

    // Pen and touch: without a keyboard, and with a hand resting on the screen.
    m_tabletAction = views->addAction(tr("Tablet Mode"), this, [this](bool on) {
        QSettings().setValue(QStringLiteral("tabletMode"), on);
        applyTabletMode();
    });
    m_tabletAction->setCheckable(true);
    m_tabletAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    m_tabletAction->setToolTip(tr("Bigger buttons and pick areas, an Undo / Delete / Done bar on the side "
                                  "and a numeric keypad for values"));
    m_tabletAction->setChecked(QSettings().value(QStringLiteral("tabletMode"), false).toBool());
    QAction* palm = views->addAction(tr("Palm Rejection"), this, [this](bool on) {
        QSettings().setValue(QStringLiteral("palmRejection"), on);
        m_viewer->setPalmRejection(on);
    });
    palm->setCheckable(true);
    palm->setToolTip(tr("Touch is ignored while the pen is near the screen, and for wide contacts"));
    palm->setChecked(QSettings().value(QStringLiteral("palmRejection"), true).toBool());
    m_viewer->setPalmRejection(palm->isChecked());
    QAction* usedRegions = views->addAction(tr("Show Extruded Regions"), this, [this](bool on) {
        QSettings().setValue(QStringLiteral("showUsedRegions"), on);
        m_editor->setShowUsedRegions(on);
    });
    usedRegions->setCheckable(true);
    usedRegions->setToolTip(tr("Sketch regions already extruded are hidden, so the faces under them can be "
                               "picked; show them to extrude one again"));
    usedRegions->setChecked(QSettings().value(QStringLiteral("showUsedRegions"), false).toBool());
    m_editor->setShowUsedRegions(usedRegions->isChecked());
    views->addSeparator();

    // Not translated, so it can be found whatever language is showing.
    QMenu* languages = views->addMenu(QStringLiteral("Język / Language"));
    auto* languageGroup = new QActionGroup(m_uiRoot);
    for (const auto& [code, name] : {std::pair(QStringLiteral("system"), tr("System")),
                                     std::pair(QStringLiteral("pl"), QStringLiteral("Polski")),
                                     std::pair(QStringLiteral("en"), QStringLiteral("English"))}) {
        QAction* action = languages->addAction(name, this, [code] {
            saveLanguage(code);
            applyLanguage(code); // every widget gets a LanguageChange: see changeEvent()
        });
        action->setCheckable(true);
        action->setChecked(code == savedLanguage());
        languageGroup->addAction(action);
    }

    QMenu* themes = views->addMenu(tr("Theme"));
    auto* themeGroup = new QActionGroup(m_uiRoot);
    for (const auto& [mode, name] : {std::pair(ThemeMode::System, tr("System")),
                                     std::pair(ThemeMode::Light, tr("Light")),
                                     std::pair(ThemeMode::Dark, tr("Dark"))}) {
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

    views->addSection(tr("Faces"));
    const std::pair<const char*, V3d_TypeOfOrientation> faces[] = {
        {QT_TR_NOOP("Top"), V3d_Zpos},   {QT_TR_NOOP("Front"), V3d_Yneg}, {QT_TR_NOOP("Right"), V3d_Xpos},
        {QT_TR_NOOP("Back"), V3d_Ypos},  {QT_TR_NOOP("Left"), V3d_Xneg},  {QT_TR_NOOP("Bottom"), V3d_Zneg},
    };
    std::vector<QAction*> faceActions;
    for (int i = 0; i < 6; ++i)
        faceActions.push_back(addView(tr(faces[i].first), faces[i].second, QKeySequence(digits[i])));

    // Top ring first, then the bottom one, both going front-right -> back-right -> back-left -> front-left.
    views->addSection(tr("Corners"));
    const std::pair<const char*, V3d_TypeOfOrientation> corners[] = {
        {QT_TR_NOOP("Top-front-right"), V3d_XposYnegZpos},    {QT_TR_NOOP("Top-back-right"), V3d_XposYposZpos},
        {QT_TR_NOOP("Top-back-left"), V3d_XnegYposZpos},      {QT_TR_NOOP("Top-front-left"), V3d_XnegYnegZpos},
        {QT_TR_NOOP("Bottom-front-right"), V3d_XposYnegZneg}, {QT_TR_NOOP("Bottom-back-right"), V3d_XposYposZneg},
        {QT_TR_NOOP("Bottom-back-left"), V3d_XnegYposZneg},   {QT_TR_NOOP("Bottom-front-left"), V3d_XnegYnegZneg},
    };
    std::vector<QAction*> cornerActions;
    for (int i = 0; i < 8; ++i) {
        cornerActions.push_back(addView(tr(corners[i].first), corners[i].second,
                                        QKeySequence(Qt::CTRL | digits[i])));
    }

    // Twelve edges but ten digits: the two least used bottom edges go to Ctrl+Shift+9 / 0.
    views->addSection(tr("Edges"));
    const std::pair<const char*, V3d_TypeOfOrientation> edges[] = {
        {QT_TR_NOOP("Top-front"), V3d_YnegZpos},    {QT_TR_NOOP("Top-right"), V3d_XposZpos},
        {QT_TR_NOOP("Top-back"), V3d_YposZpos},     {QT_TR_NOOP("Top-left"), V3d_XnegZpos},
        {QT_TR_NOOP("Front-right"), V3d_XposYneg},  {QT_TR_NOOP("Back-right"), V3d_XposYpos},
        {QT_TR_NOOP("Back-left"), V3d_XnegYpos},    {QT_TR_NOOP("Front-left"), V3d_XnegYneg},
        {QT_TR_NOOP("Bottom-front"), V3d_YnegZneg}, {QT_TR_NOOP("Bottom-back"), V3d_YposZneg},
    };
    for (int i = 0; i < 10; ++i)
        addView(tr(edges[i].first), edges[i].second, QKeySequence(Qt::SHIFT | digits[i]));
    addView(tr("Bottom-right"), V3d_XposZneg, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_9));
    addView(tr("Bottom-left"), V3d_XnegZneg, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_0));

    // The toolbar keeps its quick buttons, sharing the menu's actions (and shortcuts).
    QAction* iso = bar->addAction(tr("Isometric"));
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
    QToolBar* constraints = addToolBar(tr("Constraints"));
    m_constraintBar = constraints;
    constraints->setMovable(false);
    constraints->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    constraints->setIconSize(QSize(20, 20));
    // --- Bodies: picked through their faces ----------------------------------------------
    QMenu* bodies = menuBar()->addMenu(tr("&Bodies"));
    m_bodiesMenu = bodies;
    auto addBoolean = [&](BooleanKind kind, const QString& name, const QString& help) {
        QAction* action = bodies->addAction(name, this, [this, kind] { m_editor->combineBodies(kind); });
        action->setToolTip(help);
        m_bodyActions.push_back(action);
    };
    addBoolean(BooleanKind::Union, tr("Union"), tr("The selected bodies into one"));
    addBoolean(BooleanKind::Subtract, tr("Cut"),
               tr("Subtracts the other selected bodies from the first"));
    addBoolean(BooleanKind::Intersect, tr("Intersect"),
               tr("Keeps only what the selected bodies share"));
    bodies->setToolTipsVisible(true);

    QMenu* constraintMenu = menuBar()->addMenu(tr("&Constraints"));
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
    addConstraint(ConstraintCommand::Dimension, tr("Dimension"), Qt::Key_D,
                  tr("A line or 2 points: distance · a point and a line: distance to the line · "
                     "2 parallel lines: gap · 2 other lines: angle · circle: diameter · arc: radius\n"
                     "With nothing selected: a tool, tap what to dimension"));
    {
        // Dimensions the selection, or else becomes a tool: checked like the drawing tools.
        QAction* dimension = m_constraintActions[ConstraintCommand::Dimension];
        disconnect(dimension, &QAction::triggered, this, nullptr);
        dimension->setCheckable(true);
        tools->addAction(dimension);
        m_toolActions[ToolKind::Dimension] = dimension;
        connect(dimension, &QAction::triggered, this, [this] {
            m_editor->dimension();
            // Applied to the selection: the tool stays what it was.
            if (QAction* current = m_toolActions[m_editor->tool()])
                current->setChecked(true);
        });
    }
    constraints->addSeparator();
    constraintMenu->addSeparator();
    addConstraint(ConstraintCommand::Horizontal, tr("Horizontal"), Qt::Key_H,
                  tr("Lines or 2 points"));
    addConstraint(ConstraintCommand::Vertical, tr("Vertical"), Qt::Key_V,
                  tr("Lines or 2 points"));
    addConstraint(ConstraintCommand::Parallel, tr("Parallel"), Qt::Key_P, tr("2+ lines"));
    addConstraint(ConstraintCommand::Perpendicular, tr("Perpendicular"), Qt::Key_N,
                  tr("2 lines"));
    addConstraint(ConstraintCommand::Equal, tr("Equal"), Qt::Key_E,
                  tr("2+ lines (length) or 2+ circles/arcs (radius)"));
    addConstraint(ConstraintCommand::Tangent, tr("Tangent"), Qt::Key_T,
                  tr("A line and a circle/arc, or 2 circles/arcs"));
    addConstraint(ConstraintCommand::Concentric, tr("Concentric"), Qt::Key_O,
                  tr("2+ circles/arcs"));
    addConstraint(ConstraintCommand::Coincident, tr("Coincident"), Qt::Key_I,
                  tr("2 points (merges them) or a point and a curve (point on the curve)"));
    addConstraint(ConstraintCommand::Midpoint, tr("Midpoint"), Qt::Key_M,
                  tr("A point and a line"));
    addConstraint(ConstraintCommand::Fix, tr("Fix"), Qt::Key_Z,
                  tr("Points or curves: pins them where they are"));
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
QToolButton#valueChoice { border: 1px solid @border; border-radius: 6px; padding: 2px 8px; }
QToolButton#valueChoice:checked { border-color: @accent; }
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
    m_editor->extrudeRegions({*boss}, 25.0, BodyOperation::Join); // Auto would drill it: it starts in the plate
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
    auto* title = new QLabel(tr("History"), m_timelineBar);
    title->setObjectName(QStringLiteral("timelineTitle"));
    m_timelineBar->addWidget(title);
    const std::vector<TimelineItem> items = m_editor->timeline();
    if (items.empty()) {
        auto* empty = new QLabel(tr("empty: create a sketch and extrude it"), m_timelineBar);
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
                         : item.sketch         ? tr("Double-click: edit the sketch")
                         : editable            ? tr("Double-click: change the value")
                                               : QString());
        chip->setCheckable(true);
        chip->setChecked(i == m_timelinePick);
        chip->setProperty("timelineIndex", i);
        chip->installEventFilter(this); // double taps
        chip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        chip->setFocusPolicy(Qt::NoFocus);
        const Icon icon = item.kind == Feature::Kind::Sketch  ? Icon::EditSketch
                        : item.kind == Feature::Kind::Extrude ? Icon::Extrude
                        : item.kind == Feature::Kind::Fillet  ? Icon::Fillet
                        : item.kind == Feature::Kind::Chamfer ? Icon::Chamfer
                        : item.kind == Feature::Kind::Shell   ? Icon::Shell
                        : item.kind == Feature::Kind::Delete  ? Icon::Delete
                                                              : Icon::Coincident;
        chip->setIcon(makeIcon(icon, palette().color(QPalette::WindowText), QColor(m_accent)));
        connect(chip, &QToolButton::clicked, this, [this, i] {
            m_timelinePick = i;
            for (QToolButton* other : m_timelineBar->findChildren<QToolButton*>(QStringLiteral("timelineChip")))
                other->setChecked(other->property("timelineIndex").toInt() == i);
        });
        auto* menu = new QMenu(chip);
        menu->addAction(tr("Edit"), this, [this, i] { m_editor->activateTimelineItem(i); })
            ->setEnabled(editable);
        menu->addAction(tr("Delete"), this, [this, i] {
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

void MainWindow::applyTabletMode()
{
    const bool tablet = m_tabletAction->isChecked();
    m_quickBar->setVisible(tablet);
    m_editor->setTabletMode(tablet);
    // Short labels on the side bar: they sit under big icons.
    m_cancelAction->setIconText(tr("Cancel"));
    m_confirmAction->setIconText(tr("Done"));
    m_deleteAction->setIconText(tr("Delete"));
    updateLayout();
}

void MainWindow::updateLayout()
{
    // Narrow (portrait, a tablet turned on its side): icons only, so the bars still fit.
    const bool compact = width() < 1100;
    const bool tablet = m_tabletAction && m_tabletAction->isChecked();
    const int icon = tablet ? 32 : 24;
    for (QToolBar* bar : {m_modelBar, m_sketchBar, m_viewBar}) {
        bar->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon);
        bar->setIconSize(QSize(icon, icon));
    }
    m_newSketchButton->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon);
    m_newSketchButton->setIconSize(QSize(icon, icon));
    m_operationLabel->setVisible(!compact);
    m_constraintBar->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    m_constraintBar->setIconSize(QSize(tablet ? 26 : 20, tablet ? 26 : 20));
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (!m_modelBar)
        return;
    updateLayout();
    // Turned between portrait and landscape: frame the model again for the new shape.
    const bool portrait = height() > width();
    if (m_viewReady && m_portrait && *m_portrait != portrait)
        QTimer::singleShot(0, m_viewer, &OcctViewerWidget::refit);
    m_portrait = portrait;
}

void MainWindow::updateTitle()
{
    const QString name = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName();
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
        this, tr("Unsaved Changes"), tr("Save the changes to the project?"),
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
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), lastDirectory(),
                                                      tr("Shapr Linux projects (*.shapr)"));
    if (!path.isEmpty())
        openPath(path);
}

void MainWindow::openPath(const QString& path)
{
    QString error;
    auto project = ::loadProject(path, &error);
    if (!project) {
        QMessageBox::warning(this, tr("Can't Open"), error);
        return;
    }
    m_editor->loadProject(std::move(*project));
    m_path = path;
    rememberDirectory(path);
    updateTitle();
    m_viewer->fitAll();
    showMessage(tr("Opened %1").arg(QFileInfo(path).fileName()));
}

bool MainWindow::save()
{
    if (m_path.isEmpty())
        return saveAs();
    const QString error = saveProject(m_path, m_editor->project());
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Can't Save"), error);
        return false;
    }
    m_editor->markSaved();
    showMessage(tr("Saved %1").arg(QFileInfo(m_path).fileName()));
    return true;
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project"),
                                                m_path.isEmpty() ? lastDirectory() : m_path,
                                                tr("Shapr Linux projects (*.shapr)"));
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
    QString path = QFileDialog::getSaveFileName(this, step ? tr("Export STEP")
                                                           : tr("Export STL"),
                                                suggested, filter);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += suffix;
    rememberDirectory(path);
    const std::string error = step ? exportStep(m_editor->bodies(), path.toStdString())
                                   : exportStl(m_editor->bodies(), path.toStdString());
    if (!error.empty())
        QMessageBox::warning(this, tr("Export Failed"), QString::fromStdString(error));
    else
        showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()));
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
    m_ctxFillet = add(tr("Fillet"), Icon::Fillet, [this] { m_editor->setTool(ToolKind::Fillet); });
    m_ctxChamfer = add(tr("Chamfer"), Icon::Chamfer, [this] { m_editor->setTool(ToolKind::Chamfer); });
    m_ctxSketch = add(tr("Sketch on Face"), Icon::NewSketch, [this] { m_editor->createSketchOnFace(); });
    m_ctxPull = add(tr("Extrude"), Icon::Extrude, [this] { m_editor->setTool(ToolKind::PushPull); });
    m_ctxShell = add(tr("Shell"), Icon::Shell, [this] { m_editor->setTool(ToolKind::Shell); });
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
            button->setEnabled(command == ConstraintCommand::Dimension || m_editor->canApply(command));
    }
}

void MainWindow::onPenInput(const PenSample& sample)
{
    m_inputLabel->setText(tr("Pen%1: pressure %2%3")
                              .arg(sample.eraser ? tr(" [eraser]") : QString())
                              .arg(sample.pressure, 0, 'f', 2)
                              .arg(sample.barrelButton ? tr(" [button]") : QString()));
}
