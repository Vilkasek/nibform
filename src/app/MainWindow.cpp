#include "app/MainWindow.h"

#include "app/Icons.h"
#include "app/Language.h"
#include "app/RadialMenu.h"
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
#include <QLocale>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <Standard_Version.hxx>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
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
    setWindowTitle(QStringLiteral("Nibform"));

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

    // Unsaved work goes to a recovery file every minute (NIBFORM_AUTOSAVE_SECONDS for tests).
    m_recovery = std::make_unique<RecoverySession>(defaultRecoveryDir());
    const int autosaveSeconds = qEnvironmentVariableIntValue("NIBFORM_AUTOSAVE_SECONDS");
    m_autosaveTimer.start((autosaveSeconds > 0 ? autosaveSeconds : 60) * 1000);
    connect(&m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autosave);

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

        // Dev aid: NIBFORM_DEMO=1 builds a small model to look at (with NIBFORM_SCREENSHOT, too).
        if (qEnvironmentVariableIsSet("NIBFORM_DEMO"))
            QTimer::singleShot(0, this, &MainWindow::loadDemo);

        // Dev aid: NIBFORM_SCREENSHOT=/path.png renders one frame, saves it and quits.
        const QString shotPath = qEnvironmentVariable("NIBFORM_SCREENSHOT");
        if (!shotPath.isEmpty()) {
            QTimer::singleShot(1500, this, [this, shotPath] {
                grab().save(shotPath);
                m_recovery->clear(); // not a crash
                QApplication::quit();
            });
        }
    });
    connect(m_viewer, &OcctViewerWidget::glFailed, this, [this](const QString& reason) {
        QMessageBox::critical(this, tr("OpenGL error"), reason);
    });
    connect(m_viewer, &OcctViewerWidget::penInput, this, &MainWindow::onPenInput);
    connect(m_viewer, &OcctViewerWidget::radialMenuRequested, this, &MainWindow::showRadialMenu);
    connect(m_viewer, &OcctViewerWidget::radialMenuTrack, this, [this](const QPointF& pos, bool lifted) {
        if (m_radialMenu)
            m_radialMenu->track(m_viewer->mapTo(this, pos), lifted);
    });
    connect(m_viewer, &OcctViewerWidget::fingerTap, this, &MainWindow::onFingerTap);
    // The ring belongs to the mode it was opened in.
    connect(m_editor, &SketchEditor::modeChanged, this, [this] {
        if (m_radialMenu)
            m_radialMenu->close();
    });
}

void MainWindow::showRadialMenu(const QPointF& pos)
{
    if (m_radialMenu)
        m_radialMenu->close();
    std::vector<QAction*> actions;
    auto toolAction = [this](ToolKind kind) { return m_toolActions[kind]; };
    if (m_editor->mode() == EditorMode::Sketch) {
        actions = {toolAction(ToolKind::Select), toolAction(ToolKind::Freehand), toolAction(ToolKind::Line),
                   toolAction(ToolKind::Rectangle), toolAction(ToolKind::Circle), toolAction(ToolKind::Arc),
                   toolAction(ToolKind::Trim), toolAction(ToolKind::Dimension), m_undoAction, m_closeSketchAction};
    } else {
        actions = {toolAction(ToolKind::Select),  m_newSketchAction,          m_editSketchAction,
                   toolAction(ToolKind::PushPull), toolAction(ToolKind::Revolve), toolAction(ToolKind::Hole),
                   toolAction(ToolKind::Fillet),   toolAction(ToolKind::Shell),   m_undoAction,
                   m_fitAction};
    }
    // Icons as the bars draw them, except the one made for the orange Finish button.
    const bool dark = resolvesToDark(m_theme);
    const ThemeStyle style = themeStyle(dark);
    std::vector<RadialMenu::Item> items;
    for (QAction* action : actions) {
        if (!action)
            continue;
        items.push_back({action, action == m_closeSketchAction ? makeIcon(Icon::CloseSketch, style.ink, style.accent)
                                                                : action->icon()});
    }
    const double scale = m_tabletAction && m_tabletAction->isChecked() ? 1.3 : 1.0;
    const RadialMenu::Colors colors{themePalette(dark).color(QPalette::Window), style.ink, style.accent};
    m_radialMenu = new RadialMenu(std::move(items), m_viewer->mapTo(this, pos), scale, colors, this);
}

void MainWindow::onFingerTap(int fingers)
{
    QAction* action = fingers == 2 ? m_undoAction : fingers == 3 ? m_redoAction : nullptr;
    if (!action)
        return;
    if (!action->isEnabled()) {
        showMessage(fingers == 2 ? tr("Nothing to undo") : tr("Nothing to redo"));
        return;
    }
    action->trigger();
    showMessage(fingers == 2 ? tr("Undone (two-finger tap)") : tr("Redone (three-finger tap)"));
}

void MainWindow::buildUi()
{
    // Everything here is rebuilt when the language changes (see changeEvent()).
    m_uiRoot = new QObject(this); // parent of the window-level actions, deleted with them
    buildActions();
    buildContextBar();

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("About Nibform"), this, &MainWindow::showAbout);
    help->addAction(tr("About Qt"), qApp, &QApplication::aboutQt);

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
    m_groupButtons.clear();
    m_imageItems.clear();

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
    QMenu* recent = file->addMenu(tr("Open Recent"));
    connect(recent, &QMenu::aboutToShow, this, [this, recent] { fillRecentMenu(recent); });
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
        action->setToolTip(key.isEmpty() ? name
                                         : QStringLiteral("%1 (%2)").arg(name, key.toString(QKeySequence::NativeText)));
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
    // Related model tools share a button too (the arrow lists the others).
    auto modelGroup = [&](const std::vector<QAction*>& actions) {
        auto* menu = new QMenu(m_modelBar);
        for (QAction* action : actions)
            menu->addAction(action);
        auto* button = new QToolButton(m_modelBar);
        button->setDefaultAction(actions.front());
        button->setMenu(menu);
        button->setPopupMode(QToolButton::MenuButtonPopup);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        for (QAction* action : actions)
            connect(action, &QAction::triggered, button, [button, action] { button->setDefaultAction(action); });
        m_modelBar->addWidget(button);
        m_groupButtons.push_back(button);
    };
    auto tip = [this](ToolKind kind, const QString& text) { m_toolActions[kind]->setToolTip(text); };
    m_modelBar->addAction(
        makeTool(ToolKind::PushPull, tr("Extrude"), QKeySequence(Qt::Key_W), Icon::Extrude));
    modelGroup({makeTool(ToolKind::Revolve, tr("Revolve"), QKeySequence(Qt::Key_R), Icon::Revolve),
                makeTool(ToolKind::Sweep, tr("Sweep"), QKeySequence(Qt::SHIFT | Qt::Key_W), Icon::Sweep),
                makeTool(ToolKind::Loft, tr("Loft"), QKeySequence(Qt::SHIFT | Qt::Key_F), Icon::Loft)});
    tip(ToolKind::Revolve, tr("Revolve (R): tap sketch regions, then the axis (a sketch line, an edge or an axis)"));
    tip(ToolKind::Sweep, tr("Sweep (Shift+W): tap the profile region, then a curve of the path sketch"));
    tip(ToolKind::Loft, tr("Loft (Shift+F): tap profile regions on different planes in order, then Enter"));
    m_modelBar->addAction(makeTool(ToolKind::Hole, tr("Hole"), QKeySequence(Qt::Key_H), Icon::Hole));
    tip(ToolKind::Hole, tr("Hole (H): tap a flat face where it goes (sketch points and round edges' centers "
                           "snap); through, blind, counterbored or countersunk"));
    m_modelBar->addAction(
        makeTool(ToolKind::Fillet, tr("Fillet"), QKeySequence(Qt::Key_G), Icon::Fillet));
    m_modelBar->addAction(
        makeTool(ToolKind::Chamfer, tr("Chamfer"), QKeySequence(Qt::Key_B), Icon::Chamfer));
    m_modelBar->addAction(
        makeTool(ToolKind::Shell, tr("Shell"), QKeySequence(Qt::Key_J), Icon::Shell));
    m_modelBar->addSeparator();
    modelGroup({makeTool(ToolKind::BodyMirror, tr("Mirror"), QKeySequence(Qt::Key_M), Icon::BodyMirror),
                makeTool(ToolKind::BodyLinearPattern, tr("Linear Pattern"), QKeySequence(Qt::SHIFT | Qt::Key_L),
                         Icon::LinearPattern),
                makeTool(ToolKind::BodyCircularPattern, tr("Circular Pattern"), QKeySequence(Qt::SHIFT | Qt::Key_O),
                         Icon::CircularPattern)});
    tip(ToolKind::BodyMirror, tr("Mirror (M): tap bodies, Enter, then the mirror plane (a flat face or a plane)"));
    tip(ToolKind::BodyLinearPattern,
        tr("Linear Pattern (Shift+L): tap bodies, then the direction (an edge, axis or sketch line). "
           "To repeat a feature (a hole, a boss), use Pattern in its history menu"));
    tip(ToolKind::BodyCircularPattern,
        tr("Circular Pattern (Shift+O): tap bodies, then the axis (a round or straight edge, an axis). "
           "To repeat a feature, use Pattern in its history menu"));
    modelGroup({makeTool(ToolKind::OffsetPlane, tr("Offset Plane"), QKeySequence(), Icon::Plane),
                makeTool(ToolKind::AnglePlane, tr("Plane at Angle"), QKeySequence(), Icon::Plane),
                makeTool(ToolKind::ThreePointPlane, tr("Plane Through 3 Points"), QKeySequence(), Icon::Plane),
                makeTool(ToolKind::Axis, tr("Axis"), QKeySequence(), Icon::Axis)});
    tip(ToolKind::OffsetPlane, tr("Offset Plane: a construction plane parallel to a flat face or a plane; "
                                  "select it to start a sketch on it, or mirror about it"));
    tip(ToolKind::AnglePlane, tr("Plane at Angle: a plane turned about an edge or axis"));
    tip(ToolKind::ThreePointPlane, tr("Plane Through 3 Points: through three corners of bodies"));
    tip(ToolKind::Axis, tr("Axis: along a straight edge, through a round one's center, or through two corners"));

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

    // Sketch: drawing tools, then the way back to the model. Related tools share a button
    // whose arrow lists the others; it shows whichever was used last. All of them are in
    // the Sketch menu too.
    QMenu* sketchMenu = menuBar()->addMenu(tr("S&ketch"));
    m_sketchMenu = sketchMenu;
    auto tool = [&](ToolKind kind, const QString& name, const QKeySequence& key, Icon icon) {
        QAction* action = makeTool(kind, name, key, icon);
        sketchMenu->addAction(action);
        return action;
    };
    auto group = [&](const std::vector<QAction*>& actions) {
        auto* menu = new QMenu(m_sketchBar);
        for (QAction* action : actions)
            menu->addAction(action);
        auto* button = new QToolButton(m_sketchBar);
        button->setDefaultAction(actions.front());
        button->setMenu(menu);
        button->setPopupMode(QToolButton::MenuButtonPopup);
        button->setFocusPolicy(Qt::NoFocus);
        for (QAction* action : actions)
            connect(action, &QAction::triggered, button, [button, action] { button->setDefaultAction(action); });
        m_sketchBar->addWidget(button);
        m_groupButtons.push_back(button);
    };
    m_sketchBar->addAction(select);
    m_sketchBar->addSeparator();
    m_sketchBar->addAction(tool(ToolKind::Line, tr("Line"), QKeySequence(Qt::Key_L), Icon::Line));
    m_sketchBar->addAction(tool(ToolKind::Freehand, tr("Freehand"), QKeySequence(Qt::Key_P), Icon::Freehand));
    m_toolActions[ToolKind::Freehand]->setToolTip(
        tr("Freehand (P): draw roughly and get the clean shape (line, corners, rectangle, circle, arc); "
           "scribble over something to delete it"));
    group({tool(ToolKind::Rectangle, tr("Rectangle"), QKeySequence(Qt::Key_R), Icon::Rectangle),
           tool(ToolKind::Polygon, tr("Polygon"), QKeySequence(Qt::SHIFT | Qt::Key_P), Icon::Polygon),
           tool(ToolKind::Slot, tr("Slot"), QKeySequence(Qt::SHIFT | Qt::Key_S), Icon::Slot)});
    group({tool(ToolKind::Circle, tr("Circle"), QKeySequence(Qt::Key_C), Icon::Circle),
           tool(ToolKind::Ellipse, tr("Ellipse"), QKeySequence(Qt::SHIFT | Qt::Key_E), Icon::Ellipse)});
    group({tool(ToolKind::Arc, tr("Arc"), QKeySequence(Qt::Key_A), Icon::Arc),
           tool(ToolKind::Spline, tr("Spline"), QKeySequence(Qt::SHIFT | Qt::Key_C), Icon::Spline)});
    sketchMenu->addSeparator();
    m_sketchBar->addSeparator();
    m_sketchBar->addAction(tool(ToolKind::Trim, tr("Trim"), QKeySequence(Qt::Key_X), Icon::Trim));
    m_sketchBar->addAction(tool(ToolKind::Offset, tr("Offset"), QKeySequence(Qt::Key_U), Icon::Offset));
    group({tool(ToolKind::SketchFillet, tr("Fillet"), QKeySequence(Qt::Key_G), Icon::SketchFillet),
           tool(ToolKind::SketchChamfer, tr("Chamfer"), QKeySequence(Qt::Key_B), Icon::SketchChamfer)});
    group({tool(ToolKind::Mirror, tr("Mirror"), QKeySequence(Qt::SHIFT | Qt::Key_M), Icon::Mirror),
           tool(ToolKind::LinearPattern, tr("Linear Pattern"), QKeySequence(Qt::SHIFT | Qt::Key_L), Icon::LinearPattern),
           tool(ToolKind::CircularPattern, tr("Circular Pattern"), QKeySequence(Qt::SHIFT | Qt::Key_O),
                Icon::CircularPattern)});
    m_toolActions[ToolKind::Mirror]->setToolTip(
        tr("Mirror (Shift+M): select curves, then tap the line to mirror them about; the copy stays symmetric"));
    m_toolActions[ToolKind::LinearPattern]->setToolTip(
        tr("Linear Pattern (Shift+L): select curves, then pick the step between copies and their count"));
    m_toolActions[ToolKind::CircularPattern]->setToolTip(
        tr("Circular Pattern (Shift+O): select curves, then pick the center and the count (a full turn)"));
    m_sketchBar->addAction(tool(ToolKind::Project, tr("Project"), QKeySequence(Qt::SHIFT | Qt::Key_K), Icon::Project));
    m_toolActions[ToolKind::Project]->setToolTip(
        tr("Project (Shift+K): tap body edges or faces to copy them into the sketch, pinned in place"));
    m_constructionAction = m_sketchBar->addAction(tr("Construction"), m_editor,
                                                  &SketchEditor::toggleConstruction);
    sketchMenu->addAction(m_constructionAction);
    m_constructionAction->setShortcut(QKeySequence(Qt::Key_Q));
    m_constructionAction->setToolTip(tr("Construction (Q): the selected curves become reference geometry "
                                        "(dashed, never bounding a region), or back to regular"));
    iconize(m_constructionAction, Icon::Construction);

    // A picture on the sketch plane to trace over.
    sketchMenu->addSeparator();
    QMenu* image = sketchMenu->addMenu(tr("Reference Image"));
    m_imageMenu = image;
    QAction* insertImage = image->addAction(tr("Insert Image…"), this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Insert Image"), lastDirectory(),
            tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;All files (*)"));
        if (path.isEmpty())
            return;
        rememberDirectory(path);
        m_editor->insertImage(path);
    });
    insertImage->setToolTip(tr("Puts a picture on the sketch plane; then tap two points on it a known distance "
                               "apart to scale it"));
    image->addAction(tool(ToolKind::Image, tr("Move / Scale Image"), QKeySequence(), Icon::Image));
    QMenu* opacity = image->addMenu(tr("Opacity"));
    for (int percent : {25, 50, 75, 100}) {
        opacity->addAction(QStringLiteral("%1 %").arg(percent), this,
                           [this, percent] { m_editor->setImageOpacity(percent / 100.0); });
    }
    m_removeImageAction = image->addAction(tr("Remove Image"), m_editor, &SketchEditor::removeImage);
    m_imageItems = {m_toolActions[ToolKind::Image], opacity->menuAction(), m_removeImageAction};
    {
        auto* button = new QToolButton(m_sketchBar);
        auto* show = new QAction(tr("Image"), button);
        iconize(show, Icon::Image);
        show->setToolTip(tr("Reference image: insert a picture to trace over, scale it by two points"));
        show->setMenu(image);
        button->setDefaultAction(show);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setFocusPolicy(Qt::NoFocus);
        m_sketchBar->addWidget(button);
        m_groupButtons.push_back(button);
    }
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
    auto addView = [this, views](const QString& name, V3d_TypeOfOrientation orientation, const QKeySequence& key) {
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
    bodies->addSeparator();
    for (ToolKind kind : {ToolKind::Revolve, ToolKind::Sweep, ToolKind::Loft, ToolKind::Hole, ToolKind::BodyMirror,
                          ToolKind::BodyLinearPattern, ToolKind::BodyCircularPattern, ToolKind::OffsetPlane,
                          ToolKind::AnglePlane, ToolKind::ThreePointPlane, ToolKind::Axis})
        bodies->addAction(m_toolActions[kind]);
    bodies->setToolTipsVisible(true);

    QMenu* constraintMenu = menuBar()->addMenu(tr("&Constraints"));
    m_constraintMenu = constraintMenu;
    auto addConstraint = [&](ConstraintCommand command, const QString& name, const QKeySequence& key,
                             const QString& help) {
        QAction* action = constraints->addAction(name);
        constraintMenu->addAction(action);
        action->setShortcut(key);
        action->setToolTip(QStringLiteral("%1 (%2)\n%3").arg(name, key.toString(QKeySequence::NativeText), help));
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
    addConstraint(ConstraintCommand::HorizontalDimension, tr("Horizontal Dimension"), QKeySequence(Qt::SHIFT | Qt::Key_H),
                  tr("A line or 2 points: the distance along X"));
    addConstraint(ConstraintCommand::VerticalDimension, tr("Vertical Dimension"), QKeySequence(Qt::SHIFT | Qt::Key_V),
                  tr("A line or 2 points: the distance along Y"));
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
    addConstraint(ConstraintCommand::Symmetric, tr("Symmetric"), Qt::Key_Y,
                  tr("2 points and a line: the points mirror each other about the line"));
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
    const ThemeStyle style = themeStyle(dark);
    setStyleSheet(style.sheet);

    // Icons follow the text color; the accent marks what each tool adds or changes.
    const QColor& ink = style.ink;
    const QColor& accent = style.accent;
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

    // NIBFORM_DEMO=sketch: then open a new sketch above it, with a rectangle drawn.
    if (qEnvironmentVariable("NIBFORM_DEMO") == QLatin1String("sketch")) {
        m_editor->createSketch(gp_Ax3(gp_Pnt(0, 0, 37), gp::DZ(), gp::DX()));
        m_editor->commit([](Sketch& s) {
            const PointId a = s.addPoint({35, 10}), b = s.addPoint({55, 10});
            const PointId c = s.addPoint({55, 30}), d = s.addPoint({35, 30});
            s.addLine(a, b);
            s.addLine(b, c);
            s.addLine(c, d);
            s.addLine(d, a);
        });
        // NIBFORM_DEMO_IMAGE=picture.png: with a reference image on that sketch.
        const QString image = qEnvironmentVariable("NIBFORM_DEMO_IMAGE");
        if (!image.isEmpty())
            m_editor->insertImage(image);
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
                        : item.kind == Feature::Kind::Revolve ? Icon::Revolve
                        : item.kind == Feature::Kind::Sweep   ? Icon::Sweep
                        : item.kind == Feature::Kind::Loft    ? Icon::Loft
                        : item.kind == Feature::Kind::Hole    ? Icon::Hole
                        : item.kind == Feature::Kind::Mirror  ? Icon::BodyMirror
                        : item.kind == Feature::Kind::Pattern ? Icon::CircularPattern
                        : item.kind == Feature::Kind::Datum   ? Icon::Plane
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
        if (m_editor->canPattern(i)) {
            menu->addAction(tr("Linear Pattern…"), this, [this, i] { m_editor->patternFeature(i, false); });
            menu->addAction(tr("Circular Pattern…"), this, [this, i] { m_editor->patternFeature(i, true); });
        }
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
    for (QToolButton* button : m_groupButtons) {
        button->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(icon, icon));
    }
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
    setWindowTitle(QStringLiteral("%1%2 – Nibform").arg(name, m_editor->isModified() ? QStringLiteral("*") : QString()));
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
    m_recovery->clear();
    m_autosavedChanges = -1;
    updateTitle();
}

void MainWindow::openFile()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), lastDirectory(),
                                                      tr("Nibform projects (*.nibform)"));
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
    m_recovery->clear();
    m_autosavedChanges = -1;
    rememberDirectory(path);
    addRecentFile(path);
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
    m_recovery->clear();
    addRecentFile(m_path);
    showMessage(tr("Saved %1").arg(QFileInfo(m_path).fileName()));
    return true;
}

bool MainWindow::saveAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project"),
                                                m_path.isEmpty() ? lastDirectory() : m_path,
                                                tr("Nibform projects (*.nibform)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QStringLiteral(".nibform")))
        path += QStringLiteral(".nibform");
    m_path = path;
    rememberDirectory(path);
    return save();
}

void MainWindow::autosave()
{
    if (!m_editor->isModified()) {
        m_recovery->clear();
        return;
    }
    if (m_editor->changeCount() == m_autosavedChanges)
        return;
    const QString error = m_recovery->write(m_editor->project(), m_path);
    if (!error.isEmpty()) {
        showMessage(tr("Autosave failed: %1").arg(error));
        return;
    }
    m_autosavedChanges = m_editor->changeCount();
}

bool MainWindow::offerRecovery()
{
    // Newest first; one project per window, so the rest wait for the next start.
    for (RecoveredWork& work : findRecoveredWork(defaultRecoveryDir())) {
        const QString name = QFileInfo(work.sourcePath).fileName();
        const QString time = QLocale().toString(work.saved, QLocale::ShortFormat);
        const QString question =
            name.isEmpty() ? tr("Nibform closed unexpectedly. Restore the unsaved project autosaved at %1?").arg(time)
                           : tr("Nibform closed unexpectedly. Restore the unsaved changes to %1, autosaved at %2?")
                                 .arg(name, time);
        QMessageBox box(QMessageBox::Question, tr("Restore Unsaved Work"), question, QMessageBox::NoButton, this);
        QPushButton* restore = box.addButton(tr("Restore"), QMessageBox::AcceptRole);
        QPushButton* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.setDefaultButton(restore);
        box.exec();
        if (box.clickedButton() == discard) {
            discardRecovery(work.file);
            continue;
        }
        if (box.clickedButton() != restore)
            return false; // kept for the next start
        m_editor->loadProject(std::move(work.project));
        m_editor->markUnsaved();
        m_path = work.sourcePath;
        // Ours now: written under this session before the old file goes.
        m_autosavedChanges = -1;
        autosave();
        discardRecovery(work.file);
        updateTitle();
        m_viewer->fitAll();
        showMessage(name.isEmpty() ? tr("Restored the unsaved project") : tr("Restored %1").arg(name));
        return true;
    }
    return false;
}

void MainWindow::addRecentFile(const QString& path)
{
    QSettings settings;
    QStringList files = settings.value(QStringLiteral("recentFiles")).toStringList();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    files.removeAll(absolute);
    files.prepend(absolute);
    settings.setValue(QStringLiteral("recentFiles"), files.mid(0, 10));
}

void MainWindow::fillRecentMenu(QMenu* menu)
{
    menu->clear();
    const QStringList files = QSettings().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString& path : files) {
        if (!QFileInfo::exists(path))
            continue; // moved or deleted; dropped the next time the list changes
        QAction* action = menu->addAction(QFileInfo(path).fileName(), this, [this, path] {
            if (maybeSave())
                openPath(path);
        });
        action->setToolTip(path);
        action->setStatusTip(path);
    }
    if (menu->isEmpty()) {
        menu->addAction(tr("No recent projects"))->setEnabled(false);
        return;
    }
    menu->setToolTipsVisible(true);
    menu->addSeparator();
    menu->addAction(tr("Clear List"), this, [] { QSettings().remove(QStringLiteral("recentFiles")); });
}

void MainWindow::showAbout()
{
    const QString website = QStringLiteral("https://vilkasek.github.io/nibform/");
    const QString source = QStringLiteral("https://github.com/Vilkasek/nibform");
    QMessageBox::about(
        this, tr("About Nibform"),
        QStringLiteral("<h3>Nibform %1</h3><p>%2</p><p>%3</p><p><a href=\"%4\">%4</a><br><a href=\"%5\">%5</a></p>"
                       "<p><small>Qt %6 · OpenCASCADE %7</small></p>")
            .arg(QApplication::applicationVersion(), tr("Pen-first parametric CAD for Linux."),
                 tr("Free software under the GNU General Public License, version 3 or later. "
                    "It comes with no warranty."),
                 website, source, QString::fromLatin1(qVersion()), QStringLiteral(OCC_VERSION_COMPLETE)));
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
    if (maybeSave()) {
        m_recovery->clear(); // saved or discarded on purpose
        event->accept();
    } else {
        event->ignore();
    }
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
    case ConstraintCommand::Symmetric: make = glyph(ConstraintKind::Symmetric); break;
    case ConstraintCommand::HorizontalDimension:
        make = [](const QColor& ink, const QColor& accent) { return makeIcon(Icon::HorizontalDimension, ink, accent); };
        break;
    case ConstraintCommand::VerticalDimension:
        make = [](const QColor& ink, const QColor& accent) { return makeIcon(Icon::VerticalDimension, ink, accent); };
        break;
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
    m_sketchMenu->setEnabled(sketch);
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
    for (QAction* action : m_imageItems)
        action->setEnabled(m_editor->hasImage());
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
